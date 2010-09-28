#define _GNU_SOURCE	/* getline */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <ctype.h>
#include <string.h>
#include <dirent.h>
#include <sys/mount.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/utsname.h>

#include "global.h"
#include "linuxrc.h"
#include "text.h"
#include "util.h"
#include "dialog.h"
#include "window.h"
#include "net.h"
#include "display.h"
#include "rootimage.h"
#include "module.h"
#include "keyboard.h"
#include "file.h"
#include "info.h"
#include "install.h"
#include "auto2.h"
#include "settings.h"
#include "url.h"
#include "checkmd5.h"

static int driver_is_active(hd_t *hd);
static void auto2_progress(char *pos, char *msg);
static void auto2_read_repo_files(url_t *url);
static char *auto2_splash_name(void);
static void auto2_kexec(url_t *url);

static int test_and_add_dud(url_t *url);


/*
 * Initializes hardware and looks for repository/inst-sys.
 *
 * If ok, the instsys (and repo, if possible) are mounted.
 *
 * return:
 *   0: not found
 *   1: ok
 */
int auto2_init()
{
  int ok, win_old;

  auto2_scan_hardware();

  if(config.sig_failed) return 0;

  util_splash_bar(40, SPLASH_40);

  win_old = config.win;

  if(config.update.ask && !config.update.shown) {
    if(!config.win) util_disp_init();
    if(config.update.name_list) {
      dia_show_lines2("Driver Updates added", config.update.name_list, 64);
    }
    while(!inst_update_cd());
  }

  if(config.mediacheck) {
    if(!config.win) util_disp_init();
    md5_verify();  
  }

  if(config.win && !win_old) util_disp_done();

  ok = auto2_find_repo();

  if(config.debug) fprintf(stderr, "ZyppRepoURL: %s\n", url_print(config.url.install, 4));

  LXRC_WAIT

  util_splash_bar(50, SPLASH_50);

  return ok;
}


/*
 * Initializes hardware.
 *
 * return:
 *   - must set config.url.install & config.url.instsys
 */
void auto2_scan_hardware()
{
  hd_t *hd, *hd_sys, *hd_usb, *hd_fw, *hd_pcmcia, *hd_pcmcia2;
  driver_info_t *di;
  int ju, err;
  slist_t *usb_modules = NULL, *sl, **names;
  int storage_loaded = 0, max_wait;
  hd_data_t *hd_data;
  hd_hw_item_t hw_items[] = {
    hw_storage_ctrl, hw_network_ctrl, hw_hotplug_ctrl, hw_sys, 0
  };
  url_t *url;
  unsigned dud_count;

  hd_data = calloc(1, sizeof *hd_data);

#if !defined(__s390__) && !defined(__s390x__)
  if(config.debug) hd_data->progress = auto2_progress;
#endif

  fprintf(stderr, "Starting hardware detection...\n");
  printf("Starting hardware detection...");
  if(hd_data->progress) printf("\n");
  fflush(stdout);

  hd_list2(hd_data, hw_items, 1);

  if(hd_data->progress) {
    printf("\r%64s\r", "");
  }
  else {
    printf(" ok\n");
  }
  fflush(stdout);
  fprintf(stderr, "Hardware detection finished.\n");

  util_splash_bar(20, SPLASH_20);

  printf("(If a driver is not working for you, try booting with brokenmodules=driver_name.)\n\n");
  fflush(stdout);

  if(config.scsi_before_usb) {
    load_drivers(hd_data, hw_storage_ctrl);
    storage_loaded = 1;
  }

  if((hd_pcmcia = hd_list(hd_data, hw_pcmcia_ctrl, 0, NULL)) && !config.test) {
    printf("Activating pcmcia devices...");
    fflush(stdout);

    hd_data->progress = NULL;

    config.module.delay += 2;

    for(hd = hd_pcmcia; hd; hd = hd->next) activate_driver(hd_data, hd, NULL, 0);
    hd_pcmcia = hd_free_hd_list(hd_pcmcia);

    config.module.delay -= 2;

    if(config.usbwait > 0) sleep(config.usbwait);

    pcmcia_socket_startup();

    sleep(2);

    hd_pcmcia2 = hd_list(hd_data, hw_pcmcia, 1, NULL);
    if(hd_pcmcia2) config.has_pcmcia = 1;
    for(hd = hd_pcmcia2; hd; hd = hd->next) activate_driver(hd_data, hd, NULL, 0);
    hd_pcmcia2 = hd_free_hd_list(hd_pcmcia2);

    printf(" ok\n");
    fflush(stdout);
  }

  if((hd_usb = hd_list(hd_data, hw_usb_ctrl, 0, NULL)) && !config.test) {
    printf("Activating usb devices...");
    fflush(stdout);

    hd_data->progress = NULL;

    config.module.delay += 1;

    /* ehci needs to be loaded first */
    for(hd = hd_usb; hd; hd = hd->next) {
      if(
        hd->base_class.id == bc_serial &&
        hd->sub_class.id == sc_ser_usb &&
        hd->prog_if.id == pif_usb_ehci
      ) {
        mod_modprobe("ehci-hcd", NULL);
        break;
      }
    }

    for(hd = hd_usb; hd; hd = hd->next) activate_driver(hd_data, hd, &usb_modules, 0);
    hd_usb = hd_free_hd_list(hd_usb);

    /* might be useful anyway */
    mod_modprobe("input", NULL);
    mod_modprobe("usbhid", NULL);
    mod_modprobe("keybdev", NULL);
    mod_modprobe("usb-storage", NULL);

    max_wait = 50;
    do {
      sleep(1);
    } while(max_wait-- && util_process_running("usb-stor-scan"));

    sleep(config.usbwait + 1);

    hd_list(hd_data, hw_usb, 1, NULL);

    printf(" ok\n");
    fflush(stdout);

    load_drivers(hd_data, hw_usb);

    config.module.delay -= 1;
  }

  if((hd_fw = hd_list(hd_data, hw_ieee1394_ctrl, 0, NULL)) && !config.test) {
    printf("Activating ieee1394 devices...");
    fflush(stdout);

    hd_data->progress = NULL;

    config.module.delay += 3;

    for(hd = hd_fw; hd; hd = hd->next) activate_driver(hd_data, hd, NULL, 0);
    hd_fw = hd_free_hd_list(hd_fw);

    mod_modprobe("sbp2", NULL);

    config.module.delay -= 3;

    if(config.usbwait > 0) sleep(config.usbwait);

    printf(" ok\n");
    fflush(stdout);
  }

  util_splash_bar(30, SPLASH_30);

  /* look for keyboard and remember if it's usb */
  for(ju = 0, hd = hd_data->hd; hd; hd = hd->next) {
    if(hd->base_class.id == bc_keyboard) {
      if(hd->bus.id == bus_usb) ju++;
      di = hd->driver_info;
      if(di && di->any.type == di_kbd) {
        if(di->kbd.XkbModel) strcpy(xkbmodel_tg, di->kbd.XkbModel);
        if(di->kbd.keymap) {
          str_copy(&config.keymap, di->kbd.keymap);
        }
      }
    }
  }

#if !defined(__s390__) && !defined(__s390x__)
  if(config.debug) hd_data->progress = auto2_progress;
#endif

  hd_sys = hd_list(hd_data, hw_sys, 0, NULL);

  activate_driver(hd_data, hd_sys, NULL, 0);

  /* usb keyboard -> load usb */
  if(ju) {
    slist_append(&config.module.initrd, usb_modules);
    usb_modules = NULL;
    slist_append_str(&config.module.initrd, "input");
    slist_append_str(&config.module.initrd, "usbhid");
    slist_append_str(&config.module.initrd, "keybdev");
  }

#ifdef __PPC__
  switch(hd_mac_color(hd_data)) {
    case 0x01:
      disp_vgacolors_rm.bg = COL_BLUE;
      yast2_color_ig = 0x5a4add;
      break;
    case 0x04:
      disp_vgacolors_rm.bg = COL_GREEN;
      yast2_color_ig = 0x32cd32;
      break;
    case 0x05:
      disp_vgacolors_rm.bg = COL_YELLOW;
      yast2_color_ig = 0xff7f50;
      break;
    case 0x07:
      disp_vgacolors_rm.bg = COL_BLACK;
      yast2_color_ig = 0x000000;
      break;
    case 0xff:
      disp_vgacolors_rm.bg = COL_WHITE;
      yast2_color_ig = 0x7f7f7f;
      break;
  }
#endif

  if(!storage_loaded) load_drivers(hd_data, hw_storage_ctrl);
  load_drivers(hd_data, hw_network_ctrl);

  hd_free_hd_data(hd_data);
  free(hd_data);

  update_device_list(1);

  /* read command line parameters (2nd time) */
  if(config.info.add_cmdline) {
    file_read_info_file("cmdline", kf_cmd);
  }

  /* load & parse info files */
  for(sl = config.info.file; sl; sl = sl->next) {
    fprintf(stderr, "info file: %s\n", sl->key);
    printf("Reading info file: %s\n", sl->key);
    fflush(stdout);
    url = url_set(sl->key);
#if defined(__s390__) || defined(__s390x__)
    if(url->is.network && !config.net.configured) {
        net_activate_s390_devs();
    }
#endif
    err = url_read_file_anywhere(url, NULL, NULL, "/download/info", NULL, URL_FLAG_PROGRESS + URL_FLAG_NOSHA1);
    url_umount(url);
    url_free(url);
    if(!err) {
      fprintf(stderr, "parsing info file: %s\n", sl->key);
      file_read_info_file("file:/download/info", kf_cfg);
    }
  }

  /* load & run driverupdates */
  if(config.update.urls) {
    dud_count = config.update.count;
    /* point at list end */
    for(names = &config.update.name_list; *names; names = &(*names)->next);

    for(sl = config.update.urls; sl && !config.sig_failed; sl = sl->next) {
      fprintf(stderr, "dud url: %s\n", sl->key);

      url = url_set(sl->key);

      fprintf(url->quiet ? stderr : stdout, "Reading driver update: %s\n", sl->key);
      fflush(url->quiet ? stderr : stdout);

      if(url->is.mountable) {
        err = url_mount(url, config.mountpoint.update, test_and_add_dud);
      }
      else {
        char *file_name = strdup(new_download());
        char *path1 = url->path ?: "", *path2 = NULL;

        strprintf(&path2, "%s%sdriverupdate", path1, path1[0] == 0 || path1[strlen(path1) - 1] == '/' ? "" : "/");

        err = url_read_file_anywhere(
          url, NULL, NULL, file_name, NULL,
          URL_FLAG_UNZIP + URL_FLAG_NOSHA1 + URL_FLAG_PROGRESS + (config.secure ? URL_FLAG_CHECK_SIG : 0)
        );

        if(err && !config.sig_failed) {
          str_copy(&url->path, path2);
          err = url_read_file_anywhere(
            url, NULL, NULL, file_name, NULL,
            URL_FLAG_UNZIP + URL_FLAG_NOSHA1 + URL_FLAG_PROGRESS + (config.secure ? URL_FLAG_CHECK_SIG : 0)
          );
        }
        fprintf(stderr, "err2 = %d\n", err);
        LXRC_WAIT
        if(!err) err = util_mount_ro(file_name, config.mountpoint.update, url->file_list) ? 1 : 0;

        if(err) unlink(file_name);
        free(file_name);

        free(path2);

        if(!err) {
          test_and_add_dud(url);
          LXRC_WAIT
          util_umount(config.mountpoint.update);
        }
      }

      LXRC_WAIT

      url_umount(url);
      url_free(url);
    }
    util_do_driver_updates();

    if(dud_count == config.update.count) {
      fprintf(stderr, "No new driver updates found.\n");
    }
    else {
      if(*names) {
        if(config.win && config.manual) {
          dia_show_lines2(txt_get(TXT_DUD_ADDED), *names, 64);
        }
        else {
          printf("%s:\n", txt_get(TXT_DUD_ADDED));
          for(sl = *names; sl; sl = sl->next) {
            printf("  %s\n", sl->key);
          }
        }
      }
      else {
        if(config.win && config.manual) {
          dia_message(txt_get(TXT_DUD_OK), MSGTYPE_INFO);
        }
        else {
          printf("%s\n", txt_get(TXT_DUD_OK));
        }
      }
    }  
  }

  /* set default repository */
  if(!config.url.install) config.url.install = url_set("cd:/");
  if(!config.url.instsys) {
    config.url.instsys = url_set(config.url.instsys_default ?: config.rescue ? config.rescueimage : config.rootimage);
  }
}


/*
 * 0: failed, 1: ok, 2: ok but continue search
 */
int test_and_add_dud(url_t *url)
{
  char *buf = NULL, *s;
  int i, is_dud;

  if(config.debug) fprintf(stderr, "test_and_add_dud: all = %u\n", url->search_all);

  is_dud = util_chk_driver_update(config.mountpoint.update, get_instmode_name(url->scheme));

  LXRC_WAIT;

  if(!is_dud && (url->is.file || !url->is.mountable)) {
    is_dud = 1;

    s = url_print(url, 1);

    printf("%s: adding to %s system\n", s, config.rescue ? "rescue" : "installation");

    strprintf(&buf, "%s/dud_%04u", config.download.base, config.update.ext_count++);

    fprintf(stderr, "%s -> %s: converting dud to squashfs\n", s, buf);
    strprintf(&buf, "mksquashfs %s %s -noappend -no-progress >%s", config.mountpoint.update, buf, config.debug ? "&2" : "/dev/null");
    i = system(buf);
    if(i) fprintf(stderr, "mount: mksquashfs failed\n");

    LXRC_WAIT
  }
  str_copy(&buf, NULL);

  return (url->search_all || !is_dud) && !url->is.network ? 2 : 1;
}


/*
 * Look for a block device/network server with install source (and mount it,
 * if possible).
 *
 * Handles driverupdates in repository, too.
 *
 * return:
 *   0: not found
 *   1: ok
 */
int auto2_find_repo()
{
  int err;

  config.sig_failed = 0;
  config.sha1_failed = 0;

  if(!config.url.install || !config.url.install->scheme) return 0;

  /* no need to mount anything */
  if(config.url.install->scheme == inst_exec) {
    auto2_user_netconfig();

    return 1;
  }

  /*
   * - do some s390 preparations
   * - ineractive network setup, if asked for
   */
  if(config.url.install->is.network) {
#if defined(__s390__) || defined(__s390x__)
    if(!config.net.configured && net_activate_s390_devs()) return 0;
#endif

    if((config.net.do_setup & DS_SETUP)) auto2_user_netconfig();

    s_addr2inet(
      &config.net.broadcast,
      config.net.hostname.ip.s_addr | ~config.net.netmask.ip.s_addr
    );
  }
  else {
    auto2_user_netconfig();
  }

  /* now go and look for repo */
  err = url_find_repo(config.url.install, config.mountpoint.instdata);

  if(!err && config.kexec) {
    auto2_kexec(config.url.install);
    fprintf(stderr, "kexec failed\n");
    return 0;
  }

  /* if instsys is not a relative url, load it here */
  if(!err && !config.url.instsys->mount) {
    err = url_find_instsys(config.url.instsys, config.mountpoint.instsys);
    if(err) url_umount(config.url.install);
  }

  /* get some files for lazy yast */
  if(!err) auto2_read_repo_files(config.url.install);

#if 0
  if(err && (config.sig_failed || config.sha1_failed)) {
    url_umount(config.url.instsys);
    url_umount(config.url.install);
  }
#endif

  if(err) {
    fprintf(stderr, "no %s repository found\n", config.product);
    return 0;
  }

  auto2_driverupdate(config.url.install);

  return config.sig_failed ? 0: 1;
}


/*
 * Let user enter network config data.
 */
extern int net_is_ptp_im;

void auto2_user_netconfig()
{
  int win_old;
  slist_t *sl;

  if(!config.net.do_setup) return;

  if((net_config_mask() & 3) == 3) {	/* we have ip & netmask */
    config.net.configured = nc_static;
    /* looks a bit weird, but we need it here for net_activate_ns() */
    if(!config.net.device) str_copy(&config.net.device, config.netdevice);
    if(!config.net.device) {
      util_update_netdevice_list(NULL, 1);
      if(config.net.devices) str_copy(&config.net.device, config.net.devices->key);
    }
    if(net_activate_ns()) {
      fprintf(stderr, "net activation failed\n");
      config.net.configured = nc_none;
    }
  }

  if(config.net.configured == nc_none || config.net.do_setup) {
    if(config.net.all_ifs && (config.net.setup & NS_DHCP)) {
      util_update_netdevice_list(NULL, 1);

      config.net.configured = nc_none;

      for(sl = config.net.devices; sl && config.net.configured == nc_none; sl = sl->next) {
        str_copy(&config.net.device, sl->key);

        printf(
          "Sending %s request to %s...\n",
          config.net.use_dhcp ? "DHCP" : "BOOTP", config.net.device
        );
        fflush(stdout);
        fprintf(stderr,
          "Sending %s request to %s... ",
          config.net.use_dhcp ? "DHCP" : "BOOTP", config.net.device
        );
        config.net.use_dhcp ? net_dhcp() : net_bootp();
        if(
          !config.net.hostname.ok ||
          !config.net.netmask.ok ||
          !config.net.broadcast.ok
        ) {
          fprintf(stderr, "no/incomplete answer.\n");
        }
        else {
          config.net.configured = config.net.use_dhcp ? nc_dhcp : nc_bootp;

          if(net_activate_ns()) {
            fprintf(stderr, "%s: net activation failed\n", config.net.device);
            config.net.configured = nc_none;
          }
          else {
            fprintf(stderr, "%s: ok\n", config.net.device);
          }
        }
      }
    }
    else {
      if(!(win_old = config.win)) util_disp_init();
      net_config();
      if(!win_old) util_disp_done();
    }
  }

  if(config.net.configured == nc_none) {
    config.vnc = config.usessh = 0;
  }
  else {
    net_is_ptp_im = FALSE;
    if(strstr(config.net.device, "plip") == config.net.device) net_is_ptp_im = TRUE;
    if(strstr(config.net.device, "iucv") == config.net.device) net_is_ptp_im = TRUE;
    if(strstr(config.net.device, "ctc") == config.net.device) net_is_ptp_im = TRUE;
  }
}


/*
 * Check if any driver for that device is active.
 *
 * return:
 *   0: no
 *   1: yes (or no driver needed)
 */
int driver_is_active(hd_t *hd)
{
  driver_info_t *di;

  if(!hd || !(di = hd->driver_info)) return 1;

  for(; di; di = di->next) {
    if(di->any.type == di_module && di->module.active) return 1;
  }

  return 0;
}


/*
 * Activate device driver.
 *
 * return:
 *   0: failed
 *   1: ok
 */
int activate_driver(hd_data_t *hd_data, hd_t *hd, slist_t **mod_list, int show_modules)
{
  driver_info_t *di;
  str_list_t *sl1, *sl2;
  slist_t *slm;
  int i, j, ok = 0;
  char buf[256];
  FILE *msg = config.win ? stderr : stdout;

  if(!hd || driver_is_active(hd)) return 1;

  if(hd->is.notready) return 1;

  if(
    !config.nopcmcia &&
    hd->hotplug == hp_pcmcia &&
    hd->hotplug_slot &&
    util_check_exist("/sbin/pcmcia-socket-startup")
  ) {
    sprintf(buf, "%s %d\n", "/sbin/pcmcia-socket-startup", hd->hotplug_slot - 1);
    fprintf(stderr, "pcmcia socket startup for: %s (socket %d)\n", hd->sysfs_id, hd->hotplug_slot - 1);

    system(buf);
  }

  for(j = 0, di = hd->driver_info; di; di = di->next) {
    if(di->module.type == di_module) {
      for(
        sl1 = di->module.names, sl2 = di->module.mod_args;
        sl1 && sl2;
        sl1 = sl1->next, sl2 = sl2->next
      ) {
        if(!hd_module_is_active(hd_data, sl1->str)) {
          if(show_modules && !slist_getentry(config.module.broken, sl1->str)) {
            fprintf(msg, "%s %s", j++ ? "," : "  loading", sl1->str);
            fflush(msg);
          }
          di->module.modprobe ? mod_modprobe(sl1->str, sl2->str) : mod_insmod(sl1->str, sl2->str);
        }
        if(mod_list) {
          slm = slist_add(mod_list, slist_new());
          str_copy(&slm->key, sl1->str);
        }
      }

      /* all modules should be loaded now */
      for(i = 1, sl1 = di->module.names; sl1; sl1 = sl1->next) {
        i &= hd_module_is_active(hd_data, sl1->str);
      }

      if(i) {
        ok = 1;
        break;
      }
    }
  }
  if(j) fprintf(msg, "\n");

  return ok;
}


void load_network_mods()
{
  hd_data_t *hd_data;

  hd_data = calloc(1, sizeof *hd_data);
  hd_clear_probe_feature(hd_data, pr_parallel);
  hd_list(hd_data, hw_network_ctrl, 1, NULL);

  load_drivers(hd_data, hw_network_ctrl);

  hd_free_hd_data(hd_data);
  free(hd_data);
}


void load_drivers(hd_data_t *hd_data, hd_hw_item_t hw_item)
{
  hd_t *hd;
  driver_info_t *di;
  int i, active;
  char *mods;
  FILE *msg = config.win ? stderr : stdout;

  for(hd = hd_list(hd_data, hw_item, 0, NULL); hd; hd = hd->next) {
    hd_add_driver_data(hd_data, hd);
    i = 0;
    if(
      (di = hd->driver_info) &&
      !(hd_is_hw_class(hd, hw_usb) && hd_is_hw_class(hd, hw_hub))
    ) {
      for(di = hd->driver_info; di; di = di->next) {
        if(
          di->any.type == di_module &&
          di->module.names &&
          di->module.names->str
        ) {
          if(!i) fprintf(msg, "%s\n", hd->model);
          if(hd->driver_module) {
            active =
              !mod_cmp(hd->driver_module, di->module.names->str) ||
              (
                di->module.names->next &&
                !mod_cmp(hd->driver_module, di->module.names->next->str)
              );
          }
          else {
            active = di->module.active ||
              (
                hd_module_is_active(hd_data, di->module.names->str) &&
                (
                  !di->module.names->next ||
                  hd_module_is_active(hd_data, di->module.names->next->str)
                )
              );
          }
          mods = hd_join("+", di->module.names);
          fprintf(msg, "%s %s%s",
            i++ ? "," : "  drivers:",
            mods,
            active ? "*" : ""
          );
          free(mods);
        }
      }
      if(i) {
        fprintf(msg, "\n");
        fflush(msg);
      }
    }
    activate_driver(hd_data, hd, NULL, 1);
  }
}


/*
 * Default progress indicator for hardware probing.
 */
void auto2_progress(char *pos, char *msg)
{
  printf("\r%64s\r", "");
  printf("> %s: %s", pos, msg);
  fflush(stdout);
}


char *auto2_serial_console()
{
  static char *console = NULL;
  hd_data_t *hd_data;
  hd_t *hd;

  hd_data = calloc(1, sizeof *hd_data);

  hd = hd_list(hd_data, hw_keyboard, 1, NULL);

  for(; hd; hd = hd->next) {
    if(
      hd->base_class.id == bc_keyboard &&
      hd->sub_class.id == sc_keyboard_console
    ) {
      if(hd->unix_dev_name) {
        strprintf(&console, "%s", short_dev(hd->unix_dev_name));

        if(
          hd->res &&
          hd->res->baud.type == res_baud &&
          hd->res->baud.speed
        ) {
          strprintf(&console, "%s,%u", console, hd->res->baud.speed);
          if(hd->res->baud.parity && hd->res->baud.bits) {
            strprintf(&console, "%s%c%u", console, hd->res->baud.parity, hd->res->baud.bits);
          }
        }
      }
      else {
        strprintf(&console, "console");
      }

      break;
    }
  }

  hd_free_hd_data(hd_data);
  free(hd_data);

  return console;
}


/*
 * Do special pcmcia startup things.
 */
void pcmcia_socket_startup()
{
  char buf[256];
  struct dirent *de;
  DIR *d;
  FILE *f;
  int i, socket;

  if(!util_check_exist("/sbin/pcmcia-socket-startup")) return;

  if((d = opendir("/sys/class/pcmcia_socket"))) {
    while((de = readdir(d))) {
      if(sscanf(de->d_name, "pcmcia_socket%d", &socket) == 1) {
        sprintf(buf, "%s/%s/card_type", "/sys/class/pcmcia_socket", de->d_name);
        i = 0;
        if((f = fopen(buf, "r"))) {
          i = fgetc(f) == EOF ? 0 : 1;
          fclose(f);
        }

        if(!i) continue;

        sprintf(buf, "%s %d\n", "/sbin/pcmcia-socket-startup", socket);
        fprintf(stderr, "pcmcia socket startup for: socket %d\n", socket);

        system(buf);
      }
    }
    closedir(d);
  }
}


/*
 * Get various files from repositrory for yast's convenience.
 */
void auto2_read_repo_files(url_t *url)
{
  int i;
  file_t *f0, *f;
  char *dst = NULL, *file_list = NULL;
  static char *default_list[][2] = {
    { "/media.1/info.txt", "/info.txt" },
    { "/license.tar.gz", "/license.tar.gz" },
    { "/part.info", "/part.info" },
    { "/control.xml", "/control.xml" }
  };

  // url_read_file(url, NULL, "/media.1/installfiles", file_list = strdup(new_download()), NULL, URL_FLAG_PROGRESS);

  if((f0 = file_read_file(file_list, kf_none))) {
    for(f = f0; f && !config.sha1_failed; f = f->next) {
      strprintf(&dst, "/%s/%s", f->value, *f->key_str == '/' ? f->key_str + 1 : f->key_str);
      url_read_file(url, NULL, f->key_str, dst, NULL, 0 /* + URL_FLAG_PROGRESS */);
    }
  }
  else {
    for(i = 0; i < sizeof default_list / sizeof *default_list && !config.sha1_failed; i++) {
      url_read_file(url, NULL, default_list[i][0], default_list[i][1], NULL, 0 /* + URL_FLAG_PROGRESS */);
    }
  }

  file_free_file(f0);

  str_copy(&dst, NULL);
  str_copy(&file_list, NULL);
}


/*
 * Return splash file name (or NULL if not appropriate).
 */
char *auto2_splash_name()
{
  unsigned width, height;
  char *splash = NULL, *s;
  FILE *f;

  if(!config.kexec_initrd) return NULL;

  f = fopen("/sys/devices/platform/vesafb.0/graphics/fb0/virtual_size", "r");
  if(f) {
    if(fscanf(f, "%u,%u", &width, &height) == 2) {
      str_copy(&splash, config.kexec_initrd);
      s = strrchr(splash, '/');
      if(s) {
        *s = 0;
        strprintf(&splash, "%s/%04u%04u.spl", splash, width, height);
      }
      else {
        str_copy(&splash, NULL);
      }
    }

    fclose(f);
  }

  return splash;
}


/*
 * Download new kernel & initrd and run kexec.
 *
 * Does not return if successful.
 */
void auto2_kexec(url_t *url)
{
  char *kernel, *initrd, *buf = NULL, *cmdline = NULL, *splash = NULL, *splash_name = NULL;
  FILE *f;
  int err = 0;
  unsigned vga_mode = 0;

  if(!config.kexec_kernel || !config.kexec_initrd) {
    fprintf(stderr, "no kernel and initrd for kexec specified\n");
    return;
  }

  splash_name = auto2_splash_name();
  if(config.debug) fprintf(stderr, "splash = %s\n", splash_name);

#if defined(__i386__) || defined(__x86_64__)
  if(config.vga) {
    vga_mode = config.vga_mode;
    if(config.debug) fprintf(stderr, "vga = 0x%04x\n", vga_mode);
  }
#endif

  kernel = strdup(new_download());
  initrd = strdup(new_download());
  splash = strdup(new_download());

  err = url_read_file(url, NULL, config.kexec_kernel, kernel, NULL, URL_FLAG_PROGRESS);
  if(!err) err = url_read_file(url, NULL, config.kexec_initrd, initrd, NULL, URL_FLAG_PROGRESS);

  if(!err && splash_name) {
    err = url_read_file(url, NULL, splash_name, splash, NULL, URL_FLAG_PROGRESS);
    if(!err) {
      strprintf(&buf, "cat %s >>%s", splash, initrd);
      system(buf);
    }
    else {
      err = 0;
    }
  }

#if 0
  if(!err && config.secure && (config.sig_failed || config.sha1_failed)) {
    if(!(win = config.win)) util_disp_init();
    i = dia_okcancel(txt_get(TXT_INSECURE_REPO), NO);
    if(!win) util_disp_done();
    if(i == YES) {
      config.secure = 0;
    }
    else {
      err = 1;
      url_umount(config.url.instsys);
      url_umount(config.url.install);
    }
  }
#endif

  if(!err) {
    cmdline = calloc(1024, 1);
    if((f = fopen("/proc/cmdline", "r"))) {
      if(!fread(cmdline, 1, 1023, f)) *cmdline = 0;
      fclose(f);
    }

    sync();

    strprintf(&buf, "kexec -l %s --initrd=%s --append='%s kexec=0'", kernel, initrd, cmdline);
    fprintf(stderr, "%s\n", buf);

    if(!config.test) {
      system(buf);
      util_umount_all();
      sync();
      system("kexec -e");
    }
  }

  free(kernel);
  free(initrd);
  free(splash);
  free(splash_name);
  free(buf);
  free(cmdline);
}


/*
 * Check for driver updates.
 */
void auto2_driverupdate(url_t *url)
{
  unsigned dud_count;
  char *file_name;
  slist_t **names;
  int err = 0;
  window_t win;

  dud_count = config.update.count;

  /* point at list end */
  for(names = &config.update.name_list; *names; names = &(*names)->next);

  if(config.win) dia_info(&win, txt_get(TXT_DUD_READ), MSGTYPE_INFO);

  /* first, look for 'driverupdate' archive */
  err = url_read_file(
    url,
    NULL,
    "driverupdate",
    file_name = strdup(new_download()),
    txt_get(TXT_LOADING_UPDATE),
    URL_FLAG_UNZIP + URL_FLAG_NOSHA1 + URL_FLAG_KEEP_MOUNTED + (config.secure ? URL_FLAG_CHECK_SIG : 0)
  );

  if(!err) err = util_mount_ro(file_name, config.mountpoint.update, NULL);

  if(!err) util_chk_driver_update(config.mountpoint.update, get_instmode_name(url->scheme));

  util_umount(config.mountpoint.update);

  unlink(file_name);

  free(file_name);

  if(!err) util_do_driver_updates();

  /* then, look for unpacked version */
  if(url->mount) {
    util_chk_driver_update(url->mount, get_instmode_name(url->scheme));
    util_do_driver_updates();
  }

  if(config.win) win_close(&win);

  if(dud_count == config.update.count) {
    fprintf(stderr, "No new driver updates found.\n");
  }
  else {
    if(*names) {
      if(config.win && config.manual) dia_show_lines2(txt_get(TXT_DUD_ADDED), *names, 64);
    }
    else {
      if(config.win && config.manual) dia_message(txt_get(TXT_DUD_OK), MSGTYPE_INFO);
    }
  }  
}


int auto2_add_extension(char *extension)
{
  int err = 0;
  char *argv[3] = { };
  char *s, *cmd = NULL;
  slist_t *sl;

  fprintf(stderr, "instsys add extension: %s\n", extension);

  str_copy(&config.mountpoint.instdata, new_mountpoint());
  str_copy(&config.mountpoint.instsys, new_mountpoint());

  if(config.url.install) {
    config.url.install->mount = config.url.install->tmp_mount = 0;
  }

  if(config.url.instsys) {
    config.url.instsys->mount = config.url.instsys->tmp_mount = 0;
  }

  if(!config.url.instsys) {
    fprintf(stderr, "no instsys\n");
    err = 1;
  }

  if(config.url.instsys->scheme == inst_rel && !config.url.install) {
    fprintf(stderr, "no repo\n");
    err = 2;
  }

  if(err) return err;

  s = url_instsys_base(config.url.instsys->path);
  if(!s) return 3;

  strprintf(&config.url.instsys->path, "%s/%s", s, extension);

  if(config.url.instsys->scheme == inst_rel) {
    err = url_find_repo(config.url.install, config.mountpoint.instdata);
  }

  /* if instsys is not a relative url, load it here */
  if(!err && !config.url.instsys->mount) {
    err = url_find_instsys(config.url.instsys, config.mountpoint.instsys);
  }

  sync();

  url_umount(config.url.install);
  if(err) url_umount(config.url.instsys);

  if(config.mountpoint.instdata) rmdir(config.mountpoint.instdata);
  if(config.mountpoint.instsys) rmdir(config.mountpoint.instsys);

  if(!err) {
    for(sl = config.url.instsys_list; sl; sl = sl->next) {
      fprintf(stderr, "integrating %s (%s)\n", sl->key, sl->value);
      if(!config.test) {
        argv[1] = sl->value;
        argv[2] = "/";
        util_lndir_main(3, argv);
        if(util_check_exist2(sl->value, ".init") == 'r') {
          strprintf(&cmd, "%s/.init %s", sl->value, sl->value);
          system(cmd);
          str_copy(&cmd, NULL);
        }
      }
    }
  }

  return err;
}


int auto2_remove_extension(char *extension)
{
  int err = 0;
  char *s, *prefix, *path = NULL, *lbuf = NULL, *cmd = NULL;
  size_t lbuf_size = 0;
  FILE *f, *w;
  slist_t *sl0 = NULL, *sl;

  fprintf(stderr, "instsys remove extension: %s\n", extension);

  s = url_instsys_base(config.url.instsys->path);
  if(!s) return 3;

  strprintf(&path, "%s/%s", s, extension);

  url_build_instsys_list(path, 0);

  if((f = fopen("/etc/instsys.parts", "r"))) {
    if((w = fopen("/etc/instsys.parts.tmp", "w"))) {
      while(getline(&lbuf, &lbuf_size, f) > 0) {
        sl0 = slist_split(' ', lbuf);
        prefix = "";
        if(*sl0->key != '#') {
          if(slist_getentry(config.url.instsys_list, sl0->key)) {
            prefix = "# ";
            for(sl = sl0->next; sl; sl = sl->next) {
              if(util_check_exist2(sl->key, ".done") == 'r') {
                strprintf(&cmd, "%s/.done %s", sl->key, sl->key);
                system(cmd);
                str_copy(&cmd, NULL);
              }
              util_umount(sl->key);
            }
          }
        }
        fprintf(w, "%s%s", prefix, lbuf);
      }
      fclose(w);
      rename("/etc/instsys.parts.tmp", "/etc/instsys.parts");
    }
    fclose(f);
  }

  slist_free(sl0);
  free(path);

  return err;
}


