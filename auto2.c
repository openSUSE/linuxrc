#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <ctype.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <hd.h>

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


#ifdef USE_LIBHD

static hd_data_t *hd_data = NULL;
static char *pcmcia_params = NULL;
static int is_vaio = 0;
static int need_modules = 0;

static void auto2_check_cdrom_update(char *dev);
static hd_t *add_hd_entry(hd_t **hd, hd_t *new_hd);
static int auto2_cdrom_dev(hd_t **);
static int auto2_net_dev(hd_t **);
static int driver_is_active(hd_t *hd);
static int activate_driver(hd_t *hd, slist_t **mod_list);
static int auto2_activate_devices(unsigned base_class, unsigned last_idx);
static void auto2_chk_frame_buffer(void);
static int auto2_find_floppy(void);
static int load_usb_storage(hd_data_t *hd_data);
static void auto2_progress(char *pos, char *msg);
#ifdef __i386__
static int auto2_ask_for_modules(int prompt, char *type);
#endif
static void load_storage_mods();

/*
 * mount a detected suse-cdrom at mountpoint_tg and run inst_check_instsys()
 *
 * return 0 on success
 */
int auto2_mount_cdrom(char *device)
{
  int rc;

  set_instmode(inst_cdrom);

  if(config.cdid && strstr(config.cdid, "-DVD-")) set_instmode(inst_dvd);

  rc = mount(device, mountpoint_tg, "iso9660", MS_MGC_VAL | MS_RDONLY, 0);
  if(!rc) {
    if((rc = inst_check_instsys())) {
      fprintf(stderr, "%s is not a %s Installation CD.\n", device, config.product);
      umount(mountpoint_tg);
    } else {
      /* skip "/dev/" !!! */
      str_copy(&config.cdrom, device + sizeof "/dev/" - 1);
    }
  }
  else {
    fprintf(stderr, "%s does'nt have an ISO9660 file syetem.\n", device);
  }

  if(rc) set_instmode(inst_cdrom);

  return rc;
}

/* 
 * mmj@suse.de - mount a harddisk partition at mountpoint_tg and run 
 * inst_check_instsys(), by trying all known partition types.
 * 
 * return 0 on success
 */

int auto2_mount_harddisk(char *device)
{
  int rc;

  set_instmode(inst_hd);

  if(!(rc = util_mount_ro(device, mountpoint_tg))) {
    if((rc = inst_check_instsys())) {
      fprintf(stderr, "%s is not a %s installation media.\n", device, config.product);
      umount(mountpoint_tg);
    }
  }
  else {
    fprintf(stderr, "%s does not have a mountable file system.\n", device);
  }

  return rc;
}

void auto2_check_cdrom_update(char *dev)
{
  int i;

  i = mount(dev, mountpoint_tg, "iso9660", MS_MGC_VAL | MS_RDONLY, 0);
  if(!i) {
    util_chk_driver_update(mountpoint_tg);
    umount(mountpoint_tg);
  }
}

/*
 * probe for installed hardware
 *
 * if log_file != NULL -> write hardware info to this file
 */
void auto2_scan_hardware(char *log_file)
{
  FILE *f = NULL;
  hd_t *hd, *hd_sys, *hd_usb, *hd_fw;
  driver_info_t *di;
  int j, ju, k, with_usb;
  sys_info_t *st;
  slist_t *usb_modules = NULL;
  int storage_loaded = 0;

  if(hd_data) {
    hd_free_hd_data(hd_data);
    free(hd_data);
  }
  hd_data = calloc(1, sizeof *hd_data);

  if(!config.hwdetect) return;

  hd_set_probe_feature(hd_data, log_file ? pr_default : pr_lxrc);
  hd_clear_probe_feature(hd_data, pr_parallel);
  if(!log_file) hd_data->progress = auto2_progress;

  with_usb = hd_probe_feature(hd_data, pr_usb);
  hd_clear_probe_feature(hd_data, pr_usb);
  hd_scan(hd_data);
  if(hd_data->progress) {
    printf("\r%64s\r", "");
    fflush(stdout);
  }

  hd_usb = hd_list(hd_data, hw_usb_ctrl, 0, NULL);

  if(hd_usb) {
    load_storage_mods();
    storage_loaded = 1;

    printf("Activating usb devices...");
    hd_data->progress = NULL;

    config.module.delay = 1;

    for(hd = hd_usb; hd; hd = hd->next) activate_driver(hd, &usb_modules);
    hd_usb = hd_free_hd_list(hd_usb);

    fflush(stdout);

    mod_insmod("input", NULL);
    mod_insmod("hid", NULL);
    mod_insmod("keybdev", NULL);

    config.module.delay = 0;

    k = mount("usbdevfs", "/proc/bus/usb", "usbdevfs", 0, 0);
    if(config.usbwait > 0) sleep(config.usbwait);

    if(with_usb) {
      hd_free_hd_list(hd_list(hd_data, hw_usb, 1, NULL));
      if(load_usb_storage(hd_data)) {
        mod_insmod("usb-storage", NULL);
        if(config.usbwait > 0) sleep(config.usbwait);
        hd_free_hd_list(hd_list(hd_data, hw_usb, 1, NULL));
      }
    }
    printf(" done\n"); fflush(stdout);
    if(!log_file) hd_data->progress = auto2_progress;
  }

  hd_fw = hd_list(hd_data, hw_ieee1394_ctrl, 0, NULL);

  if(hd_fw) {
    if(!storage_loaded) load_storage_mods();

    printf("Activating ieee1394 devices...");
    fflush(stdout);

    config.module.delay = 3;

    for(hd = hd_fw; hd; hd = hd->next) activate_driver(hd, NULL);
    hd_usb = hd_free_hd_list(hd_fw);

    mod_insmod("sbp2", NULL);

    config.module.delay = 0;

    if(config.usbwait > 0) sleep(config.usbwait);

    printf(" done\n");
    fflush(stdout);
  }

  /* look for keyboards & mice */
  has_kbd_ig = FALSE;

  j = ju = 0;
  for(hd = hd_data->hd; hd; hd = hd->next) {
    if(hd->base_class.id == bc_mouse && hd->bus.id == bus_usb) j++;
    if(hd->base_class.id == bc_keyboard) {
      has_kbd_ig = TRUE;
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

  hd_sys = hd_list(hd_data, hw_sys, 0, NULL);

  if(
    hd_sys &&
    hd_sys->detail && hd_sys->detail->type == hd_detail_sys &&
    (st = hd_sys->detail->sys.data)
  ) {
    if(st->model && strstr(st->model, "PCG-") == st->model) {
      /* is a Sony Vaio */
      fprintf(stderr, "is a Vaio\n");
      is_vaio = 1;
      pcmcia_params = "irq_list=9,10,11,15";
      slist_append(&config.module.initrd, usb_modules);
      usb_modules = NULL;
    }
  }

  /* usb keyboard ? */
  if(ju) {
    slist_append(&config.module.initrd, usb_modules);
    usb_modules = NULL;
    slist_append_str(&config.module.initrd, "input");
    slist_append_str(&config.module.initrd, "hid");
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

  if(log_file && (f = fopen(log_file, "w+"))) {

    if((hd_data->debug & HD_DEB_SHOW_LOG) && hd_data->log) {
      fprintf(f,
        "============ start debug info ============\n%s=========== end debug info ============\n",
        hd_data->log
      );
    }

    for(hd = hd_data->hd; hd; hd = hd->next) hd_dump_entry(hd_data, hd, f);
    fclose(f);
  }

  hd_data->progress = NULL;
}


hd_t *add_hd_entry(hd_t **hd, hd_t *new_hd)
{
  while(*hd) hd = &(*hd)->next;

  *hd = calloc(sizeof **hd, 1);
  **hd = *new_hd;
  (*hd)->next = NULL;

  return *hd;
}


/*
 * Look for a SuSE CD and mount it.
 *
 * Returns:
 *    0: OK, CD was mounted
 *    1: no CD found
 *   >1: CD found, but contiune the search
 *
 */
int auto2_cdrom_dev(hd_t **hd0)
{
  int i = 1;
  hd_t *hd;
  cdrom_info_t *ci;
  char buf[256];

  if(config.cdromdev) {
    sprintf(buf, "/dev/%s", config.cdromdev);
    i = auto2_mount_cdrom(buf) ? 1 : 0;
    if(i && !*driver_update_dir) {
      auto2_check_cdrom_update(buf);
    }
    return i;
  }

  for(hd = hd_list(hd_data, hw_cdrom, 1, *hd0); hd; hd = hd->next) {
    fprintf(stderr, "Checking CD: %s\n", hd->unix_dev_name);
    
    cdrom_drives++;
    add_hd_entry(hd0, hd);
    if(
      hd->unix_dev_name &&
      hd->detail &&
      hd->detail->type == hd_detail_cdrom
    ) {
      ci = hd->detail->cdrom.data;

      if(config.cdrom) {
        i = 0;
      }
      else {
        if(ci->iso9660.ok && ci->iso9660.volume) {
          fprintf(stderr, "Found CD in %s\n", hd->unix_dev_name);
          str_copy(&config.cdid, ci->iso9660.volume);
          if(ci->iso9660.application) str_copy(&config.cdid, ci->iso9660.application);
          /* CD found -> try to mount it */
          i = auto2_mount_cdrom(hd->unix_dev_name) ? 2 : 0;
        }
      }

      if(i && !*driver_update_dir && ci->iso9660.ok) {
        auto2_check_cdrom_update(hd->unix_dev_name);
      }

      if(yast2_update_ig && !*driver_update_dir && i == 0) i = 3;

      if(i == 0) break;
    }
  }

  hd = hd_free_hd_list(hd);

  return i;
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
  hd_t *hd;
  int i;

  if(!(net_config_mask() || config.insttype == inst_net)) return 1;

  for(hd = hd_list(hd_data, hw_network, 1, *hd0); hd; hd = hd->next) {
    add_hd_entry(hd0, hd);
    if(hd->unix_dev_name && strcmp(hd->unix_dev_name, "lo")) {

      if(config.net.device_given && strcmp(netdevice_tg, hd->unix_dev_name)) continue;

      /* net_stop() - just in case */
      net_stop();

      fprintf(stderr, "Trying to activate %s\n", hd->unix_dev_name);

      strcpy(netdevice_tg, hd->unix_dev_name);

      net_setup_localhost();

      config.net.configured = nc_static;

      /* do bootp of there's some indication that a net install is intended
       * but some data are still missing
       */
      if(
        (net_config_mask() || config.insttype == inst_net) &&
        (net_config_mask() & 0x2b) != 0x2b
      ) {
        printf("Sending %s request to %s...\n", config.net.use_dhcp ? "DHCP" : "BOOTP", netdevice_tg);
        fflush(stdout);
        fprintf(stderr, "Sending %s request to %s... ", config.net.use_dhcp ? "DHCP" : "BOOTP", netdevice_tg);
        config.net.use_dhcp ? net_dhcp() : net_bootp();
        if(
          !config.serverdir || !*config.serverdir ||
          !config.net.hostname.ok ||
          !config.net.netmask.ok ||
          !config.net.broadcast.ok ||
          !config.net.gateway.ok
        ) {
          fprintf(stderr, "no/incomplete answer.\n");
          config.net.configured = nc_none;
          return 1;
        }
        fprintf(stderr, "ok.\n");


        config.net.configured = config.net.use_dhcp ? nc_dhcp : nc_bootp;

        if(net_check_address2(&config.net.server, 1)) {
          fprintf(stderr, "invalid server address: %s\n", config.net.server.name);
          config.net.configured = nc_none;
          return 1;
        }
      }

      if(net_activate()) {
        fprintf(stderr, "net activation failed\n");
        config.net.configured = nc_none;
        return 1;
      }
      else {
        fprintf(stderr, "%s activated\n", hd->unix_dev_name);
      }

      net_is_configured_im = TRUE;

      net_ask_password(); /* in case we have ssh or vnc in auto mode */

      switch(config.instmode) {
        case inst_smb:
          fprintf(stderr, "OK, going to mount //%s/%s ...\n", config.net.server.name, config.serverdir);

          i = net_mount_smb(mountpoint_tg,
            &config.net.server, config.serverdir,
            config.net.user, config.net.password, config.net.workgroup
          );
          
          if(i) {
            fprintf(stderr, "SMB mount failed\n");
            return 1;
          }

          fprintf(stderr, "SMB mount ok\n");
          break;

        case inst_nfs:
          fprintf(stderr, "Starting portmap.\n");
          system("portmap");

          fprintf(stderr, "OK, going to mount %s:%s ...\n", inet_ntoa(config.net.server.ip), config.serverdir ?: "");

          if(net_mount_nfs(mountpoint_tg, &config.net.server, config.serverdir)) {
            fprintf(stderr, "NFS mount failed\n");
            return 1;
          }

          fprintf(stderr, "NFS mount ok\n");
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
          fprintf(stderr, "insupported inst mode: %s\n", get_instmode_name(config.instmode));
          return 1;
      }

      return inst_check_instsys();
    }
  }

  return 1;
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
int activate_driver(hd_t *hd, slist_t **mod_list)
{
  driver_info_t *di;
  str_list_t *sl1, *sl2;
  slist_t *slm;
  int i, ok = 0;

  if(!hd || driver_is_active(hd)) return 1;

  if(hd->is.notready) return 1;

  for(di = hd->driver_info; di; di = di->next) {
    if(di->module.type == di_module && !di->module.modprobe) {
      for(
        sl1 = di->module.names, sl2 = di->module.mod_args;
        sl1 && sl2;
        sl1 = sl1->next, sl2 = sl2->next
      ) {
        mod_insmod(sl1->str, sl2->str);
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

  return ok;
}


/*
 * Activate storage/network devices.
 * Returns 0 or the index of the last controller we activated.
 */
int auto2_activate_devices(unsigned base_class, unsigned last_idx)
{
  hd_t *hd;
  int ok;

  for(hd = hd_data->hd; hd; hd = hd->next) {
    if(hd->idx > last_idx) break;
  }

  last_idx = 0;		/* re-use */

  if(!hd) return 0;	/* no further entries */

  for(; hd; hd = hd->next) {
    if(hd->base_class.id == base_class && !driver_is_active(hd)) {
      if((ok = activate_driver(hd, NULL))) {
        last_idx = hd->idx;
        fprintf(stderr, "Ok, that seems to have worked. :-)\n");
      }
      else {
        need_modules = 1;
        fprintf(stderr, "Oops, that didn't work.\n");
      }

      /* some module was loaded...; in demo mode activate all disks */
      if(
        !(
          (config.activate_storage && base_class == bc_storage) ||
          (config.activate_network && base_class == bc_network)
        ) &&
        ok
      ) break;
    }
  }

  return last_idx;
}


/*
 * Checks if autoinstall is possible.
 *
 * First, try to find a CDROM; then go for a network device.
 *
 * called from linuxrc.c
 *
 */
int auto2_init()
{
  int i, j;
#ifdef __i386__
  int net_cfg;
#endif
  hd_t *hd;

  auto2_chk_frame_buffer();

  fprintf(stderr, "Beginning hardware probing...\n");
  fflush(stderr);
  printf("Starting hardware detection...\n");
  fflush(stdout);

  auto2_scan_hardware(NULL);

  printf("\r%64s\r", "");
  fflush(stdout);
  fprintf(stderr, "Hardware probing finished.\n");
  fflush(stderr);

  if(!auto2_find_floppy()) {
    fprintf(stderr, "There seems to be no floppy disk.\n");
  }

  if(!config.hwcheck) file_read_info();

  util_debugwait("got info file");

  if(!config.test) {
    if(mod_is_loaded("usb-storage")) {
      for(hd = hd_list(hd_data, hw_cdrom, 0, NULL); hd; hd = hd->next) {
        if(hd->hotplug == hp_usb) {
          config.module.keep_usb_storage = 1;
        }
      }
    }
    if(!config.module.keep_usb_storage) {
      mod_unload_module("usb-storage");
    }
  }

#if WITH_PCMCIA
  if(
    !config.nopcmcia &&
    hd_has_pcmcia(hd_data) &&
    !mod_pcmcia_ok()
  ) {
    fprintf(stderr, "Going to load PCMCIA support...\n");
    printf("Activating PCMCIA devices...");
    fflush(stdout);

    if(!util_check_exist("/modules/pcmcia_core.o")) {
      char buf[256], *t;

      util_disp_init();

      sprintf(buf, txt_get(TXT_FOUND_PCMCIA), "i82365");
      t = strchr(buf, '\n');
      if(t) {
        *t = 0;
        strcat(t, "\n\n");
      }
      else {
        *buf = 0;
      }

      mod_disk_text(buf + strlen(buf), config.module.pcmcia_type);

      j = dia_okcancel(buf, YES) == YES ? 1 : 0;

      if(j) mod_add_disk(0, config.module.pcmcia_type);

      util_disp_done();
    }

    if(
      (i = mod_insmod("pcmcia_core", NULL)) ||
      (i = mod_insmod("i82365", pcmcia_params))   ||
      (i = mod_insmod("ds", NULL))
    );

    if(!i) {
      fprintf(stderr, "PCMCIA modules loaded - starting card manager.\n");
      pcmcia_chip_ig = 2;	/* i82365 */
      i = system("cardmgr -v -m /modules >&2");
      if(i)
        fprintf(stderr, "Oops: card manager didn't start.\n");
      else {
        fprintf(stderr, "card manager ok.\n");
      }
      /* wait for cards to be activated... */
      sleep(is_vaio ? 10 : 2);
      /* check for cdrom & net devs */
      // What's this???????
      hd_list(hd_data, hw_cdrom, 0, NULL);
      hd_list(hd_data, hw_network, 0, NULL);
    }
    else {
      fprintf(stderr, "Error loading PCMCIA modules.\n");
      i = -1;
    }
    printf("\r%s%s", "Activating PCMCIA devices...", i ? " failed\n" : " done\n");
    fflush(stdout);
  }
#endif

  util_debugwait("starting search for inst-sys");

  i = auto2_find_install_medium();

  if(!i && config.hwcheck) i = 1;

#ifdef __i386__
  /* ok, found something */
  if(i) return i;

  net_cfg = net_config_mask() || config.insttype == inst_net;

  /* no CD, but CD drive and no network install */
  if(cdrom_drives && config.insttype != inst_net) return i;

  if(need_modules) {
    if(config.insttype == inst_net) {
      if(auto2_ask_for_modules(1, "network") == 0) return FALSE;
    }
    else {
      if(auto2_ask_for_modules(1, "ide/raid/scsi") == 0) return FALSE;
    }
  }

  i = auto2_find_install_medium();
#endif

  return i;
}

static void auto2_ask_net_if_vnc()
{
    if (config.vnc || config.usessh) {
      int win_old;

      auto2_activate_devices(bc_network, 0);
      if(!(win_old = config.win)) util_disp_init();
      if (net_config())
	config.vnc = config.usessh = 0;
      if(!win_old) util_disp_done();
    }

}

/*
 * mmj@suse.de - first check if we're running automated harddisk install 
 * 
 * Look for a SuSE CD or NFS source.
 */
int auto2_find_install_medium()
{
  int i;
  unsigned last_idx;
  hd_t *hd_devs = NULL;
  char buf[256];

  if(config.instmode == inst_hd) {
    if(!config.partition) return FALSE;

    sprintf(buf, "/dev/%s", config.partition);
    if(!(i = auto2_mount_harddisk(buf))) {
	auto2_ask_net_if_vnc();
    	return TRUE;
    }
  }
    
  if(config.instmode == inst_cdrom || !config.instmode) {
    set_instmode(inst_cdrom);

    str_copy(&config.cdrom, NULL);

    util_debugwait("CD?");

    need_modules = 0;
  
    fprintf(stderr, "Looking for a %s CD...\n", config.product);
    if(!(i = auto2_cdrom_dev(&hd_devs))) {
      if(config.activate_storage) auto2_activate_devices(bc_storage, 0);
      if(config.activate_network) auto2_activate_devices(bc_network, 0);
      if(!*driver_update_dir) util_chk_driver_update(mountpoint_tg);
      auto2_ask_net_if_vnc();
      return TRUE;
    }

    for(need_modules = 0, last_idx = 0;;) {
      /* i == 1 -> try to activate another storage device */
      if(i == 1) {
        fprintf(stderr, "Ok, that didn't work; see if we can activate another storage device...\n");
      }

      if(!(last_idx = auto2_activate_devices(bc_storage, last_idx))) {
        fprintf(stderr, "No further storage devices found; giving up.\n");
        break;
      }

      fprintf(stderr, "Looking for a %s CD again...\n", config.product);
      if(!(i = auto2_cdrom_dev(&hd_devs))) {
        if(config.activate_storage) auto2_activate_devices(bc_storage, 0);
        if(config.activate_network) auto2_activate_devices(bc_network, 0);
        if(!*driver_update_dir) util_chk_driver_update(mountpoint_tg);
        auto2_ask_net_if_vnc();
        return TRUE;
      }
    }

    if(config.cdrom) {
      if(!*driver_update_dir) util_chk_driver_update(mountpoint_tg);
      auto2_ask_net_if_vnc();
      return TRUE;
    }
  }

  if(net_config_mask() || config.insttype == inst_net) {

    util_debugwait("Net?");

    need_modules = 0;

    s_addr2inet(
      &config.net.broadcast,
      config.net.hostname.ip.s_addr | ~config.net.netmask.ip.s_addr
    );

    fprintf(stderr, "hostname:   %s\n", inet2print(&config.net.hostname));
    fprintf(stderr, "netmask:    %s\n", inet2print(&config.net.netmask));
    fprintf(stderr, "broadcast:  %s\n", inet2print(&config.net.broadcast));
    fprintf(stderr, "gateway:    %s\n", inet2print(&config.net.gateway));
    fprintf(stderr, "server:     %s\n", inet2print(&config.net.server));
    fprintf(stderr, "nameserver: %s\n", inet2print(&config.net.nameserver));

    fprintf(stderr, "Looking for a network server...\n");
    if(!auto2_net_dev(&hd_devs)) {
      if(config.activate_storage) auto2_activate_devices(bc_storage, 0);
      if(config.activate_network) auto2_activate_devices(bc_network, 0);
      if(!*driver_update_dir) util_chk_driver_update(mountpoint_tg);
      return TRUE;
    }

    for(need_modules = 0, last_idx = 0;;) {
      fprintf(stderr, "Ok, that didn't work; see if we can activate another network device...\n");

      if(!(last_idx = auto2_activate_devices(bc_network, last_idx))) {
        fprintf(stderr, "No further network cards found; giving up.\n");
        break;
      }

      fprintf(stderr, "Looking for a network server again...\n");
      if(!auto2_net_dev(&hd_devs)) {
        if(config.activate_storage) auto2_activate_devices(bc_storage, 0);
        if(config.activate_network) auto2_activate_devices(bc_network, 0);
        if(!*driver_update_dir) util_chk_driver_update(mountpoint_tg);
        return TRUE;
      }
    }
  }

  util_debugwait("Nothing found");

  return FALSE;
}


/*
 * Read "vga=" entry from the kernel command line.
 */
void auto2_chk_frame_buffer()
{
  file_t *f0, *f;
  int fb_mode = -1;

  f0 = file_read_cmdline();
  for(f = f0; f; f = f->next) {
    if(strcmp(f->key_str, "vga")) {
      if(strcmp(f->value, "normal")) {
        fb_mode = 0;
      }
      else if(f->is.numeric) {
        fb_mode = f->nvalue;
      }
    }
  }
  file_free_file(f0);
  
  if(fb_mode > 0x10) frame_buffer_mode_ig = fb_mode;
}


/*
 * Scans the hardware list for a floppy and puts the result into
 * config.floppies & config.floppy_dev[].
 *
 * Returns number of floppies (with medium inserted!) found.
 */
int auto2_find_floppy()
{
  hd_t *hd, *hd0;
  hd_res_t *res;
  int i, small_floppy = 0;
  char *s, buf[256];

  if(config.floppydev) {
    config.floppy = 0;
    sprintf(buf, "/dev/%s", config.floppydev);
    str_copy(&config.floppy_dev[0], buf);
    return config.floppies = 1;
  }

  if(!hd_data) return config.floppies;

  config.floppy = config.floppies = 0;
  for(
    i = 0;
    i < sizeof config.floppy_dev / sizeof *config.floppy_dev && config.floppy_dev[i];
    i++
  ) {
    free(config.floppy_dev[i]);
    config.floppy_dev[i] = NULL;
  }

  hd0 = hd_list(hd_data, hw_floppy, 1, NULL);

  for(hd = hd0; hd; hd = hd->next) {
    if(
      config.floppies < sizeof config.floppy_dev / sizeof *config.floppy_dev &&
      hd->unix_dev_name &&			/* and has a device name */
      !hd->is.notready				/* medium inserted */
    ) {
      config.floppy_dev[config.floppies++] = strdup(hd->unix_dev_name);
      fprintf(stderr, "floppy: %s\n", hd->unix_dev_name);
      for(res = hd->res; res; res = res->next) {
        if(
          !small_floppy &&
          res->any.type == res_size &&
          res->size.unit == size_unit_sectors &&
          res->size.val1 >= 0 && res->size.val1 <= 2880 * 2
        ) small_floppy = config.floppies - 1;
      }
    }
  }

  hd_free_hd_list(hd0);

  /* try 'real' floppy first */
  if(small_floppy) {
    s = config.floppy_dev[0];
    config.floppy_dev[0] = config.floppy_dev[small_floppy];
    config.floppy_dev[small_floppy] = s;
  }

  return config.floppies;
}


/*
 * Return != 0 if we should load the usb-storage module.
 */
int load_usb_storage(hd_data_t *hd_data)
{
  hd_t *hd, *hd_floppy, *hd_cdrom, *hd_usb;
  int usb_floppies = 0, usb_cdroms = 0, usb_other = 0;

  hd_floppy = hd_list(hd_data, hw_floppy, 0, NULL);
  for(hd = hd_floppy; hd; hd = hd->next) {
    if(hd->bus.id == bus_usb && !hd->is.zip) usb_floppies++;
  }
  hd_free_hd_list(hd_floppy);

  hd_cdrom = hd_list(hd_data, hw_cdrom, 0, NULL);
  for(hd = hd_cdrom; hd; hd = hd->next) {
    if(hd->bus.id == bus_usb) usb_cdroms++;
  }
  hd_free_hd_list(hd_cdrom);

  hd_usb = hd_list(hd_data, hw_usb, 0, NULL);
  for(hd = hd_usb; hd; hd = hd->next) {
    if(hd->hw_class == hw_unknown) usb_other++;
  }
  hd_free_hd_list(hd_usb);

  return usb_floppies || usb_cdroms || usb_other;
}


int auto2_pcmcia()
{
  if(!hd_data) return 0;

  return hd_has_pcmcia(hd_data);
}

char *auto2_disk_list(int *boot_disk)
{
  static char buf[256];
  char bdev[64];
  hd_t *hd;
  int matches;
  unsigned boot_idx;

  *boot_disk = 0;
  *buf = 0;
  *bdev = 0;
  if(!hd_data) return buf;

  boot_idx = hd_boot_disk(hd_data, &matches);
  if(boot_idx && matches == 1) {
    hd = hd_get_device_by_idx(hd_data, boot_idx);
    if(hd && hd->unix_dev_name) {
      *boot_disk = boot_idx;
      strcpy(buf, hd->unix_dev_name);
      strcpy(bdev, hd->unix_dev_name);
    }
  }

  for(hd = hd_list(hd_data, hw_disk, 1, NULL); hd; hd = hd->next) {
    if(hd->unix_dev_name && strcmp(bdev, hd->unix_dev_name)) {
      if(*buf) strcat(buf, " ");
      strcat(buf, hd->unix_dev_name);
    }
  }

  hd = hd_free_hd_list(hd);

  return buf;
}


#if defined(__sparc__) || defined(__PPC__)

/* We can only probe on SPARC for serial console */

extern void hd_scan_kbd(hd_data_t *hd_data);
char *auto2_serial_console (void)
{
  char console[32];
  char buf[256];
  hd_data_t *hd_data2;
  hd_t *hd;

  if(hd_data == NULL)
    {
// FIXME: use hd_list( ,hw_keyboard, ) instead!!!
      hd_data2 = calloc(1, sizeof (hd_data_t));
      hd_set_probe_feature(hd_data2, pr_kbd);
#ifdef __PPC__
      hd_set_probe_feature(hd_data2, pr_serial);
#endif
      hd_scan_kbd(hd_data2);
    }
  else
    hd_data2 = hd_data;

  if (hd_data2 == NULL)
    return NULL;

  *console = 0;

  for(hd = hd_data2->hd; hd; hd = hd->next)
    if(hd->base_class.id == bc_keyboard && hd->bus.id == bus_serial &&
       hd->sub_class.id == sc_keyboard_console &&
       hd->res->baud.type && hd->res->baud.type == res_baud)
      {
	strcpy (console, hd->unix_dev_name);
	/* Create a string like: ttyS0,38400n8 */
#if defined(__sparc__)
	sprintf (buf, "%s,%d%c%d", &console[5],
		 hd->res->baud.speed,
		 hd->res->baud.parity,
		 hd->res->baud.bits);
#else
	sprintf (buf, "%s,%d", &console[5], hd->res->baud.speed);
#endif
	str_copy(&config.serial, buf);
      }

  if (hd_data == NULL)
    {
      hd_free_hd_data (hd_data2);
      free (hd_data2);
    }

  if (*console)
    return config.serial;
  else
    return NULL;
}
#endif

void auto2_progress(char *pos, char *msg)
{
  printf("\r%64s\r", "");
  printf("> %s: %s", pos, msg);
  fflush(stdout);
}

#if 0
void auto2_print_x11_opts(FILE *f)
{
  str_list_t *sl;

  if(!x11_driver) return;

  for(sl = x11_driver->x11.extensions; sl; sl = sl->next) {
    if(*sl->str) fprintf(f, "XF86Ext:   Load\t\t\"%s\"\n", sl->str);
  }

  for(sl = x11_driver->x11.options; sl; sl = sl->next) {
    if(*sl->str) fprintf(f, "XF86Raw:   Option\t\"%s\"\n", sl->str);
  }

  for(sl = x11_driver->x11.raw; sl; sl = sl->next) {
    if(*sl->str) fprintf(f, "XF86Raw:   %s\n", sl->str);
  }
}
#endif


#ifdef __i386__
int auto2_ask_for_modules(int prompt, char *type)
{
  int do_something = 0;
  char buf[256];
  int mtype = mod_get_type(type);

  if(mod_check_modules(type)) return do_something;

  util_disp_init();

  *buf = 0;
  mod_disk_text(buf, mtype);

  do_something = prompt ? dia_okcancel(buf, YES) == YES ? 1 : 0 : 1;

  if(do_something) mod_add_disk(0, mtype);

  util_disp_done();

  return do_something;
}
#endif


void load_storage_mods()
{
  if(!config.scsi_before_usb) return;

  config.activate_storage = 1;
  auto2_activate_devices(bc_storage, 0);
}

#endif	/* USE_LIBHD */
