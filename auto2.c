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
#include "util.h"
#include "dialog.h"
#include "window.h"
#include "net.h"
#include "display.h"
#include "module.h"
#include "keyboard.h"
#include "file.h"
#include "info.h"
#include "install.h"
#include "auto2.h"
#include "settings.h"
#include "url.h"
#include "checkmedia.h"

static int driver_is_active(hd_t *hd);
static void auto2_progress(char *pos, char *msg);
static void auto2_read_repo_file(url_t *url, char *src, char *dst);
static void auto2_read_repo_files(url_t *url);
static void auto2_read_repomd_files(url_t *url);

static int test_and_add_dud(url_t *url);
static void auto2_read_autoyast(url_t *url);


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
  int ok, win_old, install_unset = 0;
  char *device;
  slist_t *sl;

  auto2_scan_hardware();

  /* set default repository: try dvd drives */
  if(!config.url.install) {
    install_unset = 1;
    config.url.install = url_set(config.defaultrepo ? config.defaultrepo->key : "cd:/");
  }
  if(!config.url.instsys) {
    config.url.instsys = url_set(config.url.instsys_default ?: config.rescue ? config.rescueimage : config.rootimage);
  }

  if(config.sig_failed) return 0;

  util_splash_bar(40);

  win_old = config.win;

  if(config.update.ask && !config.update.shown) {
    if(!config.win) util_disp_init();
    if(config.update.name_list) {
      dia_show_lines2("Driver Updates added", config.update.name_list, 64);
    }
    while(!inst_update_cd());
  }

  if(config.systemboot) {
    if(!config.win) util_disp_init();
    util_boot_system();
    config.manual = 1;
    return 1;
  }

  if(config.mediacheck) {
    if(!config.win) util_disp_init();
    ok = check_media(NULL);
    if(!ok) return 0;
  }

  if(config.win && !win_old) util_disp_done();

  ok = auto2_find_repo();

  /* try again */
  if(install_unset && (sl = config.defaultrepo)) {
    while(!ok && (sl = sl->next)) {
      config.url.install = url_set(sl->key);
      ok = auto2_find_repo();
    }
  }

  LXRC_WAIT

  device = config.url.install->used.device ?: config.url.install->device;

  log_debug("find repo:\n");
  log_debug("  ok = %d\n", ok);
  log_debug("  is.network = %d\n", config.url.install->is.network);
  log_debug("  is.mountable = %d\n", config.url.install->is.mountable);
  log_debug("  device = %s\n", device ?: "");
  log_debug("  ZyppRepoURL: %s\n", url_print(config.url.install, 4));

  LXRC_WAIT

  util_splash_bar(50);

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

  log_info("Starting hardware detection...\n");
  util_splash_msg("Hardware detection");
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
  log_info("Hardware detection finished.\n");

  util_splash_bar(20);

  log_show("(If a driver is not working for you, try booting with brokenmodules=driver_name.)\n\n");

  if(config.scsi_before_usb) {
    load_drivers(hd_data, hw_storage_ctrl);
    storage_loaded = 1;
  }

  if((hd_pcmcia = hd_list(hd_data, hw_pcmcia_ctrl, 0, NULL)) && !config.test) {
    log_show("Activating pcmcia devices...");

    hd_data->progress = NULL;

    config.module.delay += 2;

    for(hd = hd_pcmcia; hd; hd = hd->next) activate_driver(hd_data, hd, NULL, 0);
    hd_pcmcia = hd_free_hd_list(hd_pcmcia);

    config.module.delay -= 2;

    if(config.usbwait > 0) sleep(config.usbwait);

    sleep(2);

    hd_pcmcia2 = hd_list(hd_data, hw_pcmcia, 1, NULL);
    for(hd = hd_pcmcia2; hd; hd = hd->next) activate_driver(hd_data, hd, NULL, 0);
    hd_pcmcia2 = hd_free_hd_list(hd_pcmcia2);

    log_show(" ok\n");
  }

  if((hd_usb = hd_list(hd_data, hw_usb_ctrl, 0, NULL)) && !config.test) {
    log_show("Activating usb devices...");

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

    log_show(" ok\n");

    load_drivers(hd_data, hw_usb);

    config.module.delay -= 1;
  }

  if((hd_fw = hd_list(hd_data, hw_ieee1394_ctrl, 0, NULL)) && !config.test) {
    log_show("Activating ieee1394 devices...");

    hd_data->progress = NULL;

    config.module.delay += 3;

    for(hd = hd_fw; hd; hd = hd->next) activate_driver(hd_data, hd, NULL, 0);
    hd_fw = hd_free_hd_list(hd_fw);

    mod_modprobe("sbp2", NULL);

    config.module.delay -= 3;

    if(config.usbwait > 0) sleep(config.usbwait);

    log_show(" ok\n");
  }

  util_splash_bar(30);

  /* look for keyboard and remember if it's usb */
  for(ju = 0, hd = hd_data->hd; hd; hd = hd->next) {
    if(hd->base_class.id == bc_keyboard) {
      if(hd->bus.id == bus_usb) ju++;
      di = hd->driver_info;
      if(di && di->any.type == di_kbd) {
        if(di->kbd.XkbModel) strncpy(xkbmodel_tg, di->kbd.XkbModel, sizeof xkbmodel_tg - 1);
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
      break;
    case 0x04:
      disp_vgacolors_rm.bg = COL_GREEN;
      break;
    case 0x05:
      disp_vgacolors_rm.bg = COL_YELLOW;
      break;
    case 0x07:
      disp_vgacolors_rm.bg = COL_BLACK;
      break;
    case 0xff:
      disp_vgacolors_rm.bg = COL_WHITE;
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
    log_show("Reading info file: %s\n", sl->key);
    url = url_set(sl->key);
    err = url_read_file_anywhere(url, NULL, NULL, "/download/info", NULL, URL_FLAG_PROGRESS + URL_FLAG_NODIGEST);
    url_umount(url);
    url_free(url);
    if(!err) {
      log_info("parsing info file: %s\n", sl->key);
      file_read_info_file("file:/download/info", kf_cfg);
      net_update_ifcfg(IFCFG_IFUP);
    }
    else {
      log_show("Failed to read info file.\n");
    }
  }

  /*
   * load ssh key
   */
  if(config.net.sshkey) {
    url = url_set(config.net.sshkey);
    log_show("Downloading SSH key: %s\n", config.net.sshkey);
    err = url_read_file_anywhere(url, NULL, NULL, "/download/authorized_keys", NULL, URL_FLAG_PROGRESS + URL_FLAG_NODIGEST);
    url_umount(url);
    url_free(url);
    if(!err) {
      log_info("activating SSH key\n");
      mkdir("/root/.ssh", 0755);
      rename("/download/authorized_keys", "/root/.ssh/authorized_keys");
    }
  }

  /*
   * load autoyast file; prefer autoyast option over autoyast2
   */
  auto2_read_autoyast(config.url.autoyast && config.autoyast_parse ? config.url.autoyast : config.url.autoyast2);

  /* load & run driverupdates */
  if(config.update.urls) {
    int should_have_updates = 0;

    dud_count = config.update.count;
    /* point at list end */
    for(names = &config.update.name_list; *names; names = &(*names)->next);

    for(sl = config.update.urls; sl && !config.sig_failed; sl = sl->next) {
      log_info("dud url: %s\n", sl->key);

      url = url_set(sl->key);

      log_show_maybe(!url->quiet, "Reading driver update: %s\n", sl->key);

      // for later...
      char *err_buf = NULL;
      strprintf(&err_buf, "Failed to load driver update:\n%s", url_print(url, 0));

      if(url->is.mountable) {
        #if defined(__s390__) || defined(__s390x__)
          if(url->is.network) net_activate_s390_devs();
        #endif
        err = url_mount(url, config.mountpoint.update, test_and_add_dud);
        if(!url->quiet) {
          if(err) {
            dia_message2(err_buf, MSGTYPE_ERROR);
          }
          else {
            should_have_updates = 1;
          }
        }
      }
      else {
        char *file_name = strdup(new_download());
        char *path1 = url->path ?: "", *path2 = NULL;

        strprintf(&path2, "%s%sdriverupdate", path1, path1[0] == 0 || path1[strlen(path1) - 1] == '/' ? "" : "/");

        err = url_read_file_anywhere(
          url, NULL, NULL, file_name, NULL,
          URL_FLAG_NODIGEST + URL_FLAG_PROGRESS + (config.secure ? URL_FLAG_CHECK_SIG : 0)
        );

        if(err && !config.sig_failed) {
          str_copy(&url->path, path2);
          err = url_read_file_anywhere(
            url, NULL, NULL, file_name, NULL,
            URL_FLAG_NODIGEST + URL_FLAG_PROGRESS + (config.secure ? URL_FLAG_CHECK_SIG : 0)
          );
        }
        log_info("err2 = %d\n", err);

        LXRC_WAIT

        if(!err) err = util_mount_ro(file_name, config.mountpoint.update, url->file_list) ? 1 : 0;

        if(err) unlink(file_name);
        free(file_name);

        free(path2);

        if(!err) {
          if(!url->quiet) should_have_updates = 1;
          test_and_add_dud(url);
          LXRC_WAIT
          util_umount(config.mountpoint.update);
        }
        else if(!url->quiet) {
          dia_message2(err_buf, MSGTYPE_ERROR);
        }
      }

      str_copy(&err_buf, NULL);

      LXRC_WAIT

      url_umount(url);
      url_free(url);
    }
    util_do_driver_updates();

    if(dud_count == config.update.count) {
      if(should_have_updates) {
        char *msg = "No applicable driver updates found.";
        log_info("%s\n", msg);
        dia_message2(msg, MSGTYPE_INFO);
      }
    }
    else {
      if(*names) {
        if(config.win && config.manual) {
          dia_show_lines2("Driver Updates added", *names, 64);
        }
        else {
          log_show("%s:\n", "Driver Updates added");
          for(sl = *names; sl; sl = sl->next) {
            log_show("  %s\n", sl->key);
          }
        }
      }
      else {
        if(config.win && config.manual) {
          dia_message("Driver Update ok", MSGTYPE_INFO);
        }
        else {
          log_show("%s\n", "Driver Update ok");
        }
      }
    }  
  }
}


/*
 * 0: failed, 1: ok, 2: ok but continue search
 */
int test_and_add_dud(url_t *url)
{
  char *buf = NULL, *s;
  int i, is_dud;

  log_debug("test_and_add_dud: all = %u\n", url->search_all);

  is_dud = util_chk_driver_update(config.mountpoint.update, url_scheme2name(url->scheme));

  LXRC_WAIT;

  if(!is_dud && (url->is.file || !url->is.mountable)) {
    is_dud = 1;

    // log as driver update
    config.update.count++;
    slist_append_str(&config.update.name_list, url->path);

    s = url_print(url, 1);

    log_show("%s: adding to %s system\n", s, config.rescue ? "rescue" : "installation");

    strprintf(&buf, "%s/dud_%04u", config.download.base, config.update.ext_count++);

    log_info("%s -> %s: converting dud to squashfs\n", s, buf);
    strprintf(&buf, "mksquashfs %s %s -noappend -no-progress", config.mountpoint.update, buf);
    i = lxrc_run(buf);
    if(i) log_info("mount: mksquashfs failed\n");

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
  config.digests.failed = 0;

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
    if(
      !config.net.configured &&
      net_config_needed(0) &&
      net_activate_s390_devs()
    ) return 0;
#endif

    if((config.net.do_setup & DS_SETUP)) auto2_user_netconfig();
  }
  else {
    auto2_user_netconfig();
  }

  /* now go and look for repo */
  err = url_find_repo(config.url.install, config.mountpoint.instdata);

  if(!err && config.kexec == 1) {
    auto2_kexec(config.url.install);
    log_info("kexec failed\n");
    return 0;
  }

  /* if instsys is not a relative url, load it here */
  if(!err && !config.url.instsys->mount) {
    err = url_find_instsys(config.url.instsys, config.mountpoint.instsys);
    if(err) url_umount(config.url.install);
  }

  /* get some files for lazy yast */
  if(!err) {
    if(config.repomd) auto2_read_repomd_files(config.url.install);
    auto2_read_repo_files(config.url.install);
  }

  if(err) {
    log_info("no %s repository found\n", config.product);
    return 0;
  }

  auto2_driverupdate(config.url.install);

  util_do_driver_updates();

  return config.sig_failed ? 0: 1;
}


/*
 * Let user enter network config data.
 */
void auto2_user_netconfig()
{
  if(!net_config_needed(0)) return;

  check_ptp(config.ifcfg.manual->device);
  
  if( ((net_config_mask() & 3) == 3) || (config.ifcfg.manual->ptp && ((net_config_mask() & 1) == 1)) ) {
    /* we have IP & netmask (or just IP for PTP devices) */
    config.net.configured = nc_static;
    /* looks a bit weird, but we need it here for net_static() */
    if(!config.net.device) str_copy(&config.net.device, config.ifcfg.manual->device);
    if(!config.net.device) {
      util_update_netdevice_list(NULL, 1);
      if(config.net.devices) str_copy(&config.net.device, config.net.devices->key);
    }
    if(net_static()) {
      log_info("net activation failed\n");
      config.net.configured = nc_none;
    }
  }

  if(config.net.configured == nc_none || config.net.do_setup) {
    int win_old, maybe_interactive;

    maybe_interactive = config.net.setup != NS_DHCP;
    if(!(win_old = config.win) && maybe_interactive) util_disp_init();
    net_config();
    if(!win_old && maybe_interactive) util_disp_done();
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

  if(!hd || driver_is_active(hd)) return 1;

  if(hd->is.notready) return 1;

  for(j = 0, di = hd->driver_info; di; di = di->next) {
    if(di->module.type == di_module) {
      for(
        sl1 = di->module.names, sl2 = di->module.mod_args;
        sl1 && sl2;
        sl1 = sl1->next, sl2 = sl2->next
      ) {
        if(!hd_module_is_active(hd_data, sl1->str)) {
          if(show_modules && !slist_getentry(config.module.broken, sl1->str)) {
            log_show_maybe(!config.win, "%s %s", j++ ? "," : "  loading", sl1->str);
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
  if(j) log_show_maybe(!config.win, "\n");

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
          if(!i) log_show_maybe(!config.win, "%s\n", hd->model);
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
          log_show_maybe(!config.win, "%s %s%s",
            i++ ? "," : "  drivers:",
            mods,
            active ? "*" : ""
          );
          free(mods);
        }
      }
      if(i) {
        log_show_maybe(!config.win, "\n");
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
 * Read a single file from repo directory.
 *
 * Be careful not to replace an existing file unless we successfully got
 * a new version.
 */
void auto2_read_repo_file(url_t *url, char *src, char *dst)
{
  char *tmp_file = NULL;

  str_copy(&tmp_file, new_download());
  if(
    !url_read_file(url, NULL, src, tmp_file, NULL, URL_FLAG_NODIGEST + URL_FLAG_OPTIONAL) &&
    util_check_exist(tmp_file)
  ) {
    rename(tmp_file, dst);
    log_info("mv %s -> %s\n", tmp_file, dst);
  }

  str_copy(&tmp_file, NULL);
}


/*
 * Get various files from repositrory for yast's convenience.
 */
void auto2_read_repo_files(url_t *url)
{
  int i;
  static char *default_list[][2] = {
    { "/control.xml", "/control.xml" },
    { "/license.tar.gz", "/license.tar.gz" },
    { "/media.1/info.txt", "/info.txt" },
    { "/part.info", "/part.info" },
    { "/README.BETA", "/README.BETA" }
  };

  for(i = 0; i < sizeof default_list / sizeof *default_list; i++) {
    auto2_read_repo_file(url, default_list[i][0], default_list[i][1]);
  }

  char *autoyast_file = NULL;

  if(config.url.autoyast) {
    if(
      config.url.autoyast->scheme == inst_rel &&
      config.autoyast_parse
    ) {
      log_show_maybe(!config.url.autoyast->quiet, "AutoYaST file in repo: %s\n", url_print(config.url.autoyast, 5));

      if(!config.url.autoyast->is.dir) {
        autoyast_file = config.url.autoyast->path;
      }
    }
  }
  else {
    autoyast_file = "/autoinst.xml";
  }

  if(autoyast_file) {
    auto2_read_repo_file(url, autoyast_file, "/tmp/autoinst.xml");

    if(util_check_exist("/tmp/autoinst.xml")) rename("/tmp/autoinst.xml", "/autoinst.xml");

    if(util_check_exist("/autoinst.xml")) {
      log_info("setting AutoYaST option to file:/autoinst.xml\n");
      url_free(config.url.autoyast);
      config.url.autoyast = url_set("file:/autoinst.xml");
      // parse for embedded linuxrc options in <info_file> element
      log_info("parsing AutoYaST file\n");
      file_read_info_file("file:/autoinst.xml", kf_cfg);
      net_update_ifcfg(IFCFG_IFUP);
    }
  }
}


/*
 * Get various files from repo-md repositrory for yast's convenience.
 *
 * Well, atm it's just license.tar.gz.
 */
void auto2_read_repomd_files(url_t *url)
{
  int i;
  static char *default_list[][2] = {
    { "license", "/license.tar.gz" },
    { "/README.BETA", "/README.BETA" }
  };

  for(i = 0; i < sizeof default_list / sizeof *default_list; i++) {
    // get real file name
    slist_t *sl = slist_getentry(config.repomd_data, default_list[i][0]);
    if(!sl) continue;

    auto2_read_repo_file(url, sl->value, default_list[i][1]);
  }
}


/*
 * Download new kernel & initrd and run kexec.
 *
 * Does not return if successful.
 */
void auto2_kexec(url_t *url)
{
  char *kernel, *initrd, *buf = NULL, *cmdline = NULL;
  FILE *f;
  int err = 0;
  unsigned vga_mode = 0;

  if(!config.kexec_kernel || !config.kexec_initrd) {
    log_info("no kernel and initrd for kexec specified\n");
    return;
  }

#if defined(__i386__) || defined(__x86_64__)
  if(config.vga) {
    vga_mode = config.vga_mode;
    log_debug("vga = 0x%04x\n", vga_mode);
  }
#endif

  kernel = strdup(new_download());
  initrd = strdup(new_download());

  err = url_read_file(url, NULL, config.kexec_kernel, kernel, NULL, URL_FLAG_PROGRESS);
  if(!err) err = url_read_file(url, NULL, config.kexec_initrd, initrd, NULL, URL_FLAG_PROGRESS);

  if(!err) {
    cmdline = calloc(1024, 1);
    if((f = fopen("/proc/cmdline", "r"))) {
      if(!fread(cmdline, 1, 1023, f)) *cmdline = 0;
      fclose(f);
    }

    sync();

    strprintf(&buf, "kexec -a -l %s --initrd=%s --append='%s kexec=0'", kernel, initrd, cmdline);

    if(!config.test) {
      lxrc_run(buf);
      LXRC_WAIT
      util_umount_all();
      sync();
      lxrc_run("kexec -e");
    }
  }

  free(kernel);
  free(initrd);
  free(buf);
  free(cmdline);
}


/*
 * Check for driver updates.
 *
 * Note: does not apply the updates, run util_do_driver_updates() afterwards.
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

  if(config.win) dia_info(&win, "Reading Driver Update...", MSGTYPE_INFO);

  /* first, look for 'driverupdate' archive */
  err = url_read_file(
    url,
    NULL,
    "driverupdate",
    file_name = strdup(new_download()),
    "Loading Driver Update",
    URL_FLAG_NODIGEST + URL_FLAG_KEEP_MOUNTED + (config.secure ? URL_FLAG_CHECK_SIG : 0)
  );

  if(!err) err = util_mount_ro(file_name, config.mountpoint.update, NULL);

  if(!err) util_chk_driver_update(config.mountpoint.update, url_scheme2name(url->scheme));

  util_umount(config.mountpoint.update);

  unlink(file_name);

  free(file_name);

  /* then, look for unpacked version */
  if(url->mount) {
    util_chk_driver_update(url->mount, url_scheme2name(url->scheme));
  }

  if(config.win) win_close(&win);

  if(dud_count == config.update.count) {
    if(!err) {
      char *msg = "No applicable driver updates found.";
      log_info("%s\n", msg);
      dia_message2(msg, MSGTYPE_INFO);
    }
  }
  else {
    if(*names) {
      if(config.win && config.manual) dia_show_lines2("Driver Updates added", *names, 64);
    }
    else {
      if(config.win && config.manual) dia_message("Driver Update ok", MSGTYPE_INFO);
    }
  }  
}


int auto2_add_extension(char *extension)
{
  int err = 0;
  char *argv[3] = { };
  char *s, *cmd = NULL;
  slist_t *sl;

  log_info("instsys add extension: %s\n", extension);

  if(config.test) {
    log_info("test mode - do nothing\n");
    return 0;
  }

  str_copy(&config.mountpoint.instdata, new_mountpoint());
  str_copy(&config.mountpoint.instsys, new_mountpoint());

  if(config.url.install) {
    config.url.install->mount = config.url.install->tmp_mount = 0;
  }

  if(config.url.instsys) {
    config.url.instsys->mount = config.url.instsys->tmp_mount = 0;
  }

  if(!config.url.instsys) {
    log_info("no instsys\n");
    err = 1;
  }

  if(config.url.instsys->scheme == inst_rel && !config.url.install) {
    log_info("no repo\n");
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

  /* retry a few times to umount in case the mountpoint is still busy */
  int umount_err = 0;
  int umount_try_count = 0;
  do {
    sync();
    umount_err = url_umount(config.url.install);
    if(err) umount_err |= url_umount(config.url.instsys);
    if(umount_err && umount_try_count++ < 5) {
      log_info("url_umount failed; going to re-try #%d\n", ++umount_try_count);
      sleep(1);
    }
    else {
      break;
    }
  }
  while(1);

  if(config.mountpoint.instdata) rmdir(config.mountpoint.instdata);
  if(config.mountpoint.instsys) rmdir(config.mountpoint.instsys);

  if(!err) {
    for(sl = config.url.instsys_list; sl; sl = sl->next) {
      log_info("integrating %s (%s)\n", sl->key, sl->value);
      if(!config.test) {
        argv[1] = sl->value;
        argv[2] = "/";
        util_lndir_main(3, argv);
        if(util_check_exist2(sl->value, ".init") == 'r') {
          strprintf(&cmd, "%s/.init %s", sl->value, sl->value);
          lxrc_run(cmd);
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

  log_info("instsys remove extension: %s\n", extension);

  if(config.test) {
    log_info("test mode - do nothing\n");
    return 0;
  }

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
                lxrc_run(cmd);
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


/*
 * Read autoyast file from url and parse it.
 *
 * If the autoyast url points to a directory, the function tries to mount
 * the directory to ensure it exists. If it is a non-mountable url (e.g.
 * http) it assumes a rules-based setup and that a file rules/rules.xml
 * exists below the specified directory. The rules file is only read, not
 * parsed.
 *
 * This is basically done to search for the correct medium.
 */
void auto2_read_autoyast(url_t *url)
{
  if(!url) return;

  // rel url is taken care of in auto2_read_repo_files()
  if(url->scheme == inst_rel) return;

  /*
   * If the AutoYaST url is a directory we have to verify its existence
   * somehow.
   *
   * That works for mountable url schemes.
   *
   * For the rest (http(s), (t)ftp), try to read rules/rules.xml.
   */
  if(url->is.dir) {
    /*
     * do nothing but
     *   - ensure network is set up
     *   - the correct disk device has been identified
     */
    if(url->is.mountable) {
      url_mount(url, NULL, NULL);
      url_umount(url);
    }
    else if(url->path) {
      char *old_path = NULL;
      str_copy(&old_path, url->path);

      int path_len = strlen(url->path);
      if(path_len && url->path[path_len - 1] == '/') url->path[path_len - 1] = 0;

      strprintf(&url->path, "%s/rules/rules.xml", url->path);

      log_show_maybe(!url->quiet, "Downloading AutoYaST rules file: %s\n", url->str);

      url_read_file_anywhere(url, NULL, NULL, "/download/rules.xml", NULL, URL_FLAG_PROGRESS + URL_FLAG_NODIGEST);

      str_copy(&url->path, old_path);
      free(old_path);
    }

    return;
  }

  log_show_maybe(!url->quiet, "Downloading AutoYaST file: %s\n", url->str);

  int err = url_read_file_anywhere(url, NULL, NULL, "/download/autoinst.xml", NULL, URL_FLAG_PROGRESS + URL_FLAG_NODIGEST);
  url_umount(url);

  if(!err) {
    if(!config.url.autoyast) {
      // this means 'autoyast2' has been used; point autoyast url to downloaded file
      config.url.autoyast = url_set("file:/download/autoinst.xml");
    }
    // parse for embedded linuxrc options in <info_file> element
    log_info("parsing AutoYaST file\n");
    file_read_info_file("file:/download/autoinst.xml", kf_cfg);
    net_update_ifcfg(IFCFG_IFUP);
  }
  else {
    log_show_maybe(!url->quiet, "Failed to download AutoYaST file.\n");
  }
}
