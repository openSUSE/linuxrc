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

static int auto2_mount_disk(char *dev);
static int auto2_find_install_disk(void);
static void auto2_user_netconfig(void);
static int auto2_net_dev(hd_t **);
static int auto2_net_dev1(hd_t *hd, hd_t *hd_card);
static int driver_is_active(hd_t *hd);
static void auto2_progress(char *pos, char *msg);
static void get_zen_config(void);
static void load_drivers(hd_data_t *hd_data, hd_hw_item_t hw_item);


/*
 * Probe for installed hardware.
 *
 * return:
 *   - must set config.url.install
 */
void auto2_scan_hardware()
{
  hd_t *hd, *hd_sys, *hd_usb, *hd_fw, *hd_pcmcia, *hd_pcmcia2;
  driver_info_t *di;
  int i, ju, k;
  slist_t *usb_modules = NULL;
  int storage_loaded = 0, max_wait;
  hd_data_t *hd_data;
  hd_hw_item_t hw_items[] = {
    hw_storage_ctrl, hw_network_ctrl, hw_hotplug_ctrl, hw_sys, 0
  };

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

  if(config.info.add_cmdline) {
    file_read_info_file("cmdline", kf_cmd);
  }

  if(config.info.file) {
    fprintf(stderr, "Looking for info file: %s\n", config.info.file);
    printf("Reading info file:\n  %s ...", config.info.file);
    fflush(stdout);
    i = get_url(config.info.file, "/download/info");
    printf("%s\n", i ? " failed" : " ok");
    if(!i) {
      fprintf(stderr, "Parsing info file: %s\n", config.info.file);
      file_read_info_file("file:/download/info", kf_cfg);
    }
    else {
      fprintf(stderr, "Info file not found: %s\n", config.info.file);
    }
  }

  hd_free_hd_data(hd_data);
  free(hd_data);

  update_device_list(1);

  if(!config.url.install) config.url.install = url_set("cd:/");
}


/*
 * Look for install source on block device 'dev' and mount it.
 *
 * return:
 *   0: ok
 *   1: error
 */
int auto2_mount_disk(char *dev)
{
  int err = 0;

  if(do_mount_disk(dev, 1)) return 1;

  if((err = inst_check_instsys())) {
    fprintf(stderr, "disk: %s::%s is not an installation source\n", dev, config.serverdir);
  }
  else {
    fprintf(stderr, "disk: using %s::%s\n", dev, config.serverdir);
  }

  if(err) {
    fprintf(stderr, "disk: %s: no install data found\n", dev);
    inst_umount();
  }

  util_do_driver_updates();

  return err;
}


/*
 * Look for a block device with install source and mount it.
 *
 * return:
 *   0: ok, device was mounted
 *   1: no device found
 */
int auto2_find_install_disk()
{
  int err = 1;
  hd_data_t *hd_data;
  hd_t *hd;
  hd_hw_item_t hw_item = hw_block;

  url_mount(config.url.install, "/mnt");

  getchar(); lxrc_end(); exit(0);

  hd_data = calloc(1, sizeof *hd_data);

  switch(config.url.install->scheme) {
    case inst_cdrom:
      hw_item = hw_cdrom;
      break;

    case inst_floppy:
      hw_item = hw_floppy;
      break;

    default:
      break;
  }

  for(hd = hd_list(hd_data, hw_item, 1, NULL); hd; hd = hd->next) {
    if(
      (
        config.url.install->scheme == inst_hd &&
        (					/* hd means: */
          hd_is_hw_class(hd, hw_floppy) ||	/*  - not a floppy */
          hd_is_hw_class(hd, hw_cdrom) ||	/*  - not a cdrom */
          hd->child_ids				/*  - has no partitions */
        )
      ) ||
      !hd->unix_dev_name
    ) continue;

    if(
      config.url.install->device &&
      strcmp(hd->unix_dev_name, long_dev(config.url.install->device)) &&
      !search_str_list(hd->unix_dev_names, long_dev(config.url.install->device))
    ) continue;

    fprintf(stderr, "disk: trying to mount: %s\n", hd->unix_dev_name);

    if(!(err = auto2_mount_disk(hd->unix_dev_name))) {
      str_copy(&config.url.install->used_device, short_dev(hd->unix_dev_name));
      break;
    }
  }

  hd_free_hd_data(hd_data);
  free(hd_data);

  return err;
}


/*
 * Look for a NFS/FTP server as install source.
 *
 * Returns:
 *   0: OK
 *   1: oops
 */
int auto2_net_dev(hd_t **hd0)
{
  hd_t *hd, *hd_card;
  hd_data_t *hd_data;
  int err = 1;
  hd_hw_item_t hw_items[] = {
    hw_network_ctrl, hw_network
  };

#if defined(__s390__) || defined(__s390x__)
  static int as3d = 0;
  
  if(!as3d) {
    as3d = 1;
    if(net_activate_s390_devs()) return 1;
  }
#endif

  if(!(net_config_mask() || config.insttype == inst_net)) return 1;

  hd_data = calloc(1, sizeof *hd_data);

  hd_list2(hd_data, hw_items, 1);

  for(hd = hd_list(hd_data, hw_network, 0, NULL); hd; hd = hd->next) {
    // set wlan tag for interface
    hd_card = hd_get_device_by_idx(hd_data, hd->attached_to);
    if(hd_card && hd_card->is.wlan) hd->is.wlan = 1;

    if(!auto2_net_dev1(hd, hd_card)) {
      err = 0;
      break;
    }
  }

  hd_free_hd_data(hd_data);
  free(hd_data);

  return err;
}


/*
 * Try to get inst-sys from network device.
 *
 * Return:
 *   0: ok
 *   1: failed
 */
int auto2_net_dev1(hd_t *hd, hd_t *hd_card)
{
  int i /*, link */;
  char *device, *hwaddr = NULL;
  hd_res_t *res;

  if(!hd || !(device = hd->unix_dev_name)) return 1;

  if(!strncmp(device, "lo", sizeof "lo" - 1)) return 1;
  if(!strncmp(device, "sit", sizeof "sit" - 1)) return 1;

  for(res = hd->res; res; res = res->next) {
    if(res->any.type == res_hwaddr) {
      hwaddr = res->hwaddr.addr;
      break;
    }
  }

  if(
    config.net.device_given &&
    !match_netdevice(device, hwaddr, config.net.device)
  ) return 1;

  /* net_stop() - just in case */
  if(!getenv("PXEBOOT")) net_stop();

  fprintf(stderr, "Trying to activate %s\n", device);

#if 0
  // not useful - too many false negatives

  link = 1;

  if(!config.net.device_given) {
    for(res = hd->res; res; res = res->next) {
      if(res->any.type == res_link) {
        link = res->link.state;
        break;
      }
    }
  }

  if(!link) {
    fprintf(stderr, "no link - skipping %s\n", device);
    return 1;
  }
#endif

  str_copy(&config.net.device, device);
  str_copy(&config.net.hwaddr, hwaddr);
  if(hd_card) str_copy(&config.net.cardname, hd_card->model);

  net_setup_localhost();

  config.net.configured = nc_static;

  if(hd->is.wlan && wlan_setup()) return 1;

  /* do bootp if there's some indication that a net install is intended
   * but some data are still missing
   */
  if(
    (net_config_mask() || config.insttype == inst_net) &&
    (net_config_mask() & 0x2b) != 0x2b
  ) {
    printf("Sending %s request to %s...\n", config.net.use_dhcp ? "DHCP" : "BOOTP", config.net.device);
    fflush(stdout);
    fprintf(stderr, "Sending %s request to %s... ", config.net.use_dhcp ? "DHCP" : "BOOTP", config.net.device);
    config.net.use_dhcp ? net_dhcp() : net_bootp();
    if(
      !config.serverdir || !*config.serverdir ||
      !config.net.hostname.ok ||
      !config.net.netmask.ok ||
      !config.net.broadcast.ok
    ) {
      fprintf(stderr, "no/incomplete answer.\n");
      config.net.configured = nc_none;
      return 1;
    }
    fprintf(stderr, "ok.\n");

    config.net.configured = config.net.use_dhcp ? nc_dhcp : nc_bootp;
  }

  if(net_activate_ns()) {
    fprintf(stderr, "net activation failed\n");
    config.net.configured = nc_none;
    return 1;
  }
  else {
    fprintf(stderr, "%s activated\n", device);
  }

  if(net_check_address2(&config.net.server, 1)) {
    fprintf(stderr, "invalid server address: %s\n", config.net.server.name);
    config.net.configured = nc_none;
    return 1;
  }

  while(config.instmode == inst_slp) {
    extern int slp_get_install(void);
    if(slp_get_install()) {
      fprintf(stderr, "SLP failed\n");
      return 1;
    }
  }

  net_ask_password(); /* in case we have ssh or vnc in auto mode */

  switch(config.instmode) {
    case inst_smb:
      if(do_mount_smb()) return 1;
      break;

    case inst_nfs:
      if(do_mount_nfs()) return 1;
      break;

    case inst_ftp:
    case inst_tftp:
    case inst_http:
      i = net_open(NULL);
      if(i < 0) {
        util_print_net_error();
        return 1;
      }
      break;

    default:
      fprintf(stderr, "unsupported inst mode: %s\n", get_instmode_name(config.instmode));
      return 1;
  }

  return inst_check_instsys();
}


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
 * Returns 1 if it worked, else 0.
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


/*
 * Checks if autoinstall is possible.
 *
 * called from linuxrc.c
 *
 */
int auto2_init()
{
  int i, win_old;

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

  if(config.zen) get_zen_config();

  util_debugwait("starting search for inst-sys");

  i = auto2_find_install_medium();

  util_splash_bar(50, SPLASH_50);

  return i;
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
 * Look for install medium.
 *
 * return:
 *   0: not found
 *   1: ok
 */
int auto2_find_install_medium()
{
  hd_t *hd_devs = NULL;

  if(!config.url.install || !config.url.install->scheme) return 0;

  /* no need to mount anything */
  if(config.url.install->scheme == inst_exec) {
    auto2_user_netconfig();

    return 1;
  }

  /* local disk */
  if(
    config.url.install->is.mountable &&
    !config.url.install->is.network
  ) {
    fprintf(stderr, "looking for a %s disk\n", config.product);
    if(!auto2_find_install_disk()) {
      auto2_user_netconfig();

      return 1;
    }

    fprintf(stderr, "no %s disk found\n", config.product);

    return 0;
  }

  if(net_config_mask() || config.insttype == inst_net) {

    util_debugwait("Net?");

    if((config.net.do_setup & DS_SETUP)) auto2_user_netconfig();

    s_addr2inet(
      &config.net.broadcast,
      config.net.hostname.ip.s_addr | ~config.net.netmask.ip.s_addr
    );

    fprintf(stderr, "hostname:   %s\n", inet2print(&config.net.hostname));
    fprintf(stderr, "netmask:    %s\n", inet2print(&config.net.netmask));
    fprintf(stderr, "broadcast:  %s\n", inet2print(&config.net.broadcast));
    fprintf(stderr, "gateway:    %s\n", inet2print(&config.net.gateway));
    fprintf(stderr, "server:     %s\n", inet2print(&config.net.server));
    fprintf(stderr, "nameserver: %s\n", inet2print(&config.net.nameserver[0]));

    fprintf(stderr, "Looking for a network server...\n");
    if(!auto2_net_dev(&hd_devs)) return TRUE;

  }

  util_debugwait("Nothing found");

  return FALSE;
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


void auto2_progress(char *pos, char *msg)
{
  printf("\r%64s\r", "");
  printf("> %s: %s", pos, msg);
  fflush(stdout);
}


void load_network_mods()
{
  hd_data_t *hd_data;

  hd_data = calloc(1, sizeof *hd_data);
  hd_clear_probe_feature(hd_data, pr_parallel);
  hd_list(hd_data, hw_network_ctrl, 1, NULL);

  config.activate_network = 1;
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


void get_zen_config()
{
  static char *zen_mp = "/mounts/zen";
  char *cfg = NULL;
  slist_t *sl, sl0 = { };
  int cfg_ok = 0;

  if(!config.zenconfig) return;

  strprintf(&cfg, "%s/%s", zen_mp, config.zenconfig);

  printf("Looking for ZENworks config");
  fflush(stdout);

  sl = config.cdroms;

  if(config.cdromdev) {
    sl0.key = config.cdromdev;
    sl0.next = sl;
    sl = &sl0;
  }

  if(config.insttype == inst_hd && config.partition) {
    sl0.key = config.partition;
    sl0.next = NULL;
    sl = &sl0;
  }

  for(; !cfg_ok && sl; sl = sl->next) {
    if(util_mount_ro(long_dev(sl->key), zen_mp)) continue;
    if(util_check_exist(cfg) == 'r') {
      char *argv[3];
      unlink("/settings.txt");
      argv[1] = cfg;
      argv[2] = "/";
      util_cp_main(3, argv);
      cfg_ok = 1;
      fprintf(stderr, "copied %s:%s\n", sl->key, config.zenconfig);
      if(config.zen == 2) {
        file_read_info_file("file:/settings.txt", kf_cfg);
        fprintf(stderr, "read /settings.txt\n");
      }
    }
    util_umount(zen_mp);
  }
  
  printf(cfg_ok ? " - got it\n" : " - not found\n");

  free(cfg);
}


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


