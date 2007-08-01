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
#include "ftp.h"
#include "install.h"
#include "auto2.h"
#include "settings.h"
#include "url.h"

static void auto2_user_netconfig(void);
static int driver_is_active(hd_t *hd);
static void load_drivers(hd_data_t *hd_data, hd_hw_item_t hw_item);
static void auto2_progress(char *pos, char *msg);
static void auto2_read_repo_files(url_t *url);


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

  util_splash_bar(40, SPLASH_40);

  if(config.update.ask && !config.update.shown) {
    if(!(win_old = config.win)) util_disp_init();
    if(config.update.name_list) {
      dia_show_lines2("Driver Updates added", config.update.name_list, 64);
    }
    while(!inst_update_cd());
    if(!win_old) util_disp_done();
  }

  ok = auto2_find_repo();

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
  int ju, k, err;
  slist_t *usb_modules = NULL, *sl;
  int storage_loaded = 0, max_wait;
  hd_data_t *hd_data;
  hd_hw_item_t hw_items[] = {
    hw_storage_ctrl, hw_network_ctrl, hw_hotplug_ctrl, hw_sys, 0
  };
  url_t *url;

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

    mod_modprobe("input", NULL);
    mod_modprobe("usbhid", NULL);
    mod_modprobe("keybdev", NULL);

    config.module.delay -= 1;

    k = mount("usbfs", "/proc/bus/usb", "usbfs", 0, 0);
    if(config.usbwait > 0) sleep(config.usbwait);

    mod_modprobe("usb-storage", NULL);

    max_wait = 50;
    do {
      sleep(1);
    } while(max_wait-- && util_process_running("usb-stor-scan"));

    if(config.usbwait > 0) sleep(config.usbwait);
    // hd_list(hd_data, hw_usb, 1, NULL);

    printf(" ok\n");
    fflush(stdout);
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
    err = url_read_file(url, NULL, NULL, "/info", NULL, URL_FLAG_PROGRESS);
    url_umount(url);
    url_free(url);
    if(!err) {
      fprintf(stderr, "parsing info file: %s\n", sl->key);
      file_read_info_file("file:/info", kf_cfg);
    }
  }

  /* set default repository */
  if(!config.url.install) config.url.install = url_set("cd:/");
  if(!config.url.instsys) config.url.instsys = url_set(config.rescue ? config.rescueimage : config.rootimage);
  // if(!config.url.instsys2) config.url.instsys2 = url_set(config.rootimage2);
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
  unsigned dud_count;
  char *file_name;

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
    static int as3d = 0;
  
    if(!as3d) {
      as3d = 1;
      if(net_activate_s390_devs()) return 0;
    }
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

  /* if instsys is not a relative url, load it here */
  if(!err && !config.url.instsys->mount) {
    err = url_find_instsys(config.url.instsys, config.mountpoint.instsys);
  }

  if(err) {
    fprintf(stderr, "no %s repository found\n", config.product);
    return 0;
  }

  /* get some files for lazy yast */
  auto2_read_repo_files(config.url.install);

  /* check for driver updates */

  dud_count = config.update.count;

  /* first, look for 'driverupdate' archive */
  err = url_read_file(
    config.url.install,
    NULL,
    "driverupdate",
    file_name = strdup(new_download()),
    txt_get(TXT_LOADING_UPDATE),
    URL_FLAG_UNZIP /* + URL_FLAG_PROGRESS */
  );

  if(!err) err = util_mount_ro(file_name, config.mountpoint.update);

  if(!err) util_chk_driver_update(config.mountpoint.update, get_instmode_name(config.url.install->scheme));

  util_umount(config.mountpoint.update);
  free(file_name);

  if(!err) util_do_driver_updates();

  /* then, look for unpacked version */
  if(config.url.install->mount) {
    util_chk_driver_update(config.url.install->mount, get_instmode_name(config.url.install->scheme));
    util_do_driver_updates();
  }

  if(dud_count == config.update.count) {
    fprintf(stderr, "No new driver updates found.\n");
    // printf("No new driver updates found.\n");
  }

  return 1;
}


/*
 * Let user enter network config data.
 */
void auto2_user_netconfig()
{
  int win_old;
  slist_t *sl;

  if(!config.net.do_setup) return;

  if((net_config_mask() & 3) == 3) {	/* we have ip & netmask */
    config.net.configured = nc_static;
    /* looks a bit weird, but we need it here for net_activate_ns() */
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
            printf("%s %s", j++ ? "," : "  loading", sl1->str);
            fflush(stdout);
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
  if(j) printf("\n");

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

  for(hd = hd_list(hd_data, hw_item, 0, NULL); hd; hd = hd->next) {
    hd_add_driver_data(hd_data, hd);
    i = 0;
    if((di = hd->driver_info)) {
      for(di = hd->driver_info; di; di = di->next) {
        if(
          di->any.type == di_module &&
          di->module.names &&
          di->module.names->str
        ) {
          if(!i) printf("%s\n", hd->model);
          if(hd->driver_module) {
            active = !mod_cmp(hd->driver_module, di->module.names->str);
          }
          else {
            active = di->module.active || hd_module_is_active(hd_data, di->module.names->str);
          }
          printf("%s %s%s",
            i++ ? "," : "  drivers:",
            di->module.names->str,
            active ? "*" : ""
          );
        }
      }
      if(i) {
        printf("\n");
        fflush(stdout);
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
    { "/part.info", "/part.info" },
    { "/control.xml", "/control.xml" }
  };

  // url_read_file(url, NULL, "/media.1/installfiles", file_list = strdup(new_download()), NULL, URL_FLAG_PROGRESS);

  if((f0 = file_read_file(file_list, kf_none))) {
    for(f = f0; f; f = f->next) {
      strprintf(&dst, "/%s/%s", f->value, *f->key_str == '/' ? f->key_str + 1 : f->key_str);
      url_read_file(url, NULL, f->key_str, dst, NULL, 0 /* + URL_FLAG_PROGRESS */);
    }
  }
  else {
    for(i = 0; i < sizeof default_list / sizeof *default_list; i++) {
      url_read_file(url, NULL, default_list[i][0], default_list[i][1], NULL, 0 /* + URL_FLAG_PROGRESS */);
    }
  }

  file_free_file(f0);

  str_copy(&dst, NULL);
  str_copy(&file_list, NULL);
}


