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
static int auto2_driver_is_active(driver_info_t *di);
static int auto2_activate_devices(unsigned base_class, unsigned last_idx);
static void auto2_chk_frame_buffer(void);
static int auto2_find_floppy(void);
static int auto2_load_usb_storage(void);
static void auto2_progress(char *pos, char *msg);
static int auto2_ask_for_modules(int prompt, char *type);


/*
 * mount a detected suse-cdrom at mountpoint_tg and run inst_check_instsys()
 *
 * return 0 on success
 */
int auto2_mount_cdrom(char *device)
{
  int rc;

  set_instmode(inst_cdrom);

  if(config.susecd && strstr(config.susecd, "-DVD-")) set_instmode(inst_dvd);

  rc = mount(device, mountpoint_tg, "iso9660", MS_MGC_VAL | MS_RDONLY, 0);
  if(!rc) {
    if((rc = inst_check_instsys())) {
      fprintf(stderr, "%s is not a SuSE Installation CD.\n", device);
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
      fprintf(stderr, "%s is not a SuSE installation media.\n", device);
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
  hd_t *hd, *hd_sys;
  driver_info_t *di;
  char *usb_mod;
  static char usb_mods[128];
  int i, j, ju, k, with_usb;
  sys_info_t *st;

  if(hd_data) {
    hd_free_hd_data(hd_data);
    free(hd_data);
  }
  hd_data = calloc(1, sizeof *hd_data);
  hd_set_probe_feature(hd_data, log_file ? pr_default : pr_lxrc);
  hd_clear_probe_feature(hd_data, pr_parallel);
  if(!log_file) hd_data->progress = auto2_progress;

#if 0
  if(auto2_get_probe_env(hd_data)) {
    /* reset flags on error */
    hd_set_probe_feature(hd_data, log_file ? pr_default : pr_lxrc);
    hd_clear_probe_feature(hd_data, pr_parallel);
  }
#endif

  with_usb = hd_probe_feature(hd_data, pr_usb);
  hd_clear_probe_feature(hd_data, pr_usb);
  hd_scan(hd_data);
  if(hd_data->progress) {
    printf("\r%64s\r", "");
    fflush(stdout);
  }

  if((usb_mod = auto2_usb_module())) {
    printf("Activating usb devices...");
    hd_data->progress = NULL;
    fflush(stdout);
    i = 0;
    if(*usb_mod) {
      if(
        (i = mod_insmod("usbcore", NULL)) ||
        (i = mod_insmod(usb_mod, NULL))   ||
        (i = mod_insmod("input", NULL))   ||
        (i = mod_insmod("hid", NULL))
      );
      mod_insmod("keybdev", NULL);
    }
    k = mount("usbdevfs", "/proc/bus/usb", "usbdevfs", 0, 0);
    if(!i && config.usbwait > 0) sleep(config.usbwait);
    if(with_usb) {
      hd_clear_probe_feature(hd_data, pr_all);
      hd_set_probe_feature(hd_data, pr_usb);
      hd_set_probe_feature(hd_data, pr_scsi);
      hd_set_probe_feature(hd_data, pr_int);
      hd_scan(hd_data);
      if(auto2_load_usb_storage()) {
        mod_insmod("usb-storage", NULL);
        if(config.usbwait > 0) sleep(1);
        hd_clear_probe_feature(hd_data, pr_all);
        hd_set_probe_feature(hd_data, pr_usb);
        hd_set_probe_feature(hd_data, pr_scsi);
        hd_set_probe_feature(hd_data, pr_int);
        hd_scan(hd_data);
      }
    }
    printf(" done\n"); fflush(stdout);
    if(!log_file) hd_data->progress = auto2_progress;
  }

  /* look for keyboards & mice */
  has_kbd_ig = FALSE;

  j = ju = 0;
  for(hd = hd_data->hd; hd; hd = hd->next) {
    if(hd->base_class == bc_mouse && hd->bus == bus_usb) j++;
    if(hd->base_class == bc_keyboard) {
      has_kbd_ig = TRUE;
      if(hd->bus == bus_usb) ju++;
      di = hd_driver_info(hd_data, hd);
      if(di && di->any.type == di_kbd) {
//        if(di->kbd.XkbRules) strcpy(xkbrules_tg, di->kbd.XkbRules);
        if(di->kbd.XkbModel) strcpy(xkbmodel_tg, di->kbd.XkbModel);
//        if(di->kbd.XkbLayout) strcpy(xkblayout_tg, di->kbd.XkbLayout);
	/* UNTESTED !!! */
        if(di->kbd.keymap) {
          str_copy(&config.keymap, di->kbd.keymap);
        }
      }
      di = hd_free_driver_info(di);
    }
  }

#if 0
  if(auto2_has_i2o()) {
    i = 0;
    if(
      (i = mod_load_module("i2o_pci", NULL))    ||
      (i = mod_load_module("i2o_core", NULL))   ||
      (i = mod_load_module("i2o_config", NULL)) ||
      (i = mod_load_module("i2o_block", NULL))
    );
    if(!i) {
      hd_clear_probe_feature(hd_data, pr_all);
      hd_set_probe_feature(hd_data, pr_i2o);
      hd_scan(hd_data);
    }
  }
#endif

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
      if(usb_mod && *usb_mod) {
        sprintf(usb_mods, "usbcore %s", usb_mod);
        usb_mods_ig = usb_mods;
      }
    }
  }

  /* usb keyboard or mouse ? */
  if((j || ju) && usb_mod && *usb_mod) {
    sprintf(usb_mods, "usbcore %s input hid keybdev mousedev", usb_mod);
    usb_mods_ig = usb_mods;
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
        if(ci->iso9660.ok && ci->iso9660.volume && strstr(ci->iso9660.volume, "SU") == ci->iso9660.volume) {
          fprintf(stderr, "Found SuSE CD in %s\n", hd->unix_dev_name);
          str_copy(&config.susecd, ci->iso9660.volume);
          if(ci->iso9660.application) str_copy(&config.susecd, ci->iso9660.application);
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

      /* net_stop() - just in case */
      net_stop();

      fprintf(stderr, "Trying to activate %s\n", hd->unix_dev_name);

      strcpy(netdevice_tg, hd->unix_dev_name);

      net_setup_localhost();

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
          return 1;
        }
        fprintf(stderr, "ok.\n");

        if(net_check_address2(&config.net.server, 1)) {
          fprintf(stderr, "invalid server address: %s\n", config.net.server.name);
          return 1;
        }
      }

      if(net_activate()) {
        deb_msg("net_activate() failed");
        return 1;
      }
      else {
        fprintf(stderr, "%s activated\n", hd->unix_dev_name);
      }

      net_is_configured_im = TRUE;

      switch(config.instmode) {
        case inst_smb:
          fprintf(stderr, "OK, going to mount //%s/%s ...\n", config.net.server.name, config.serverdir);

          i = net_mount_smb(mountpoint_tg,
            &config.net.server, config.serverdir,
            config.net.user, config.net.password, config.net.workgroup
          );
          
          if(i) {
            deb_msg("SMB mount failed.");
            return 1;
          }

          deb_msg("SMB mount ok.");
          break;

        case inst_nfs:
          fprintf(stderr, "Starting portmap.\n");
          system("portmap");

          fprintf(stderr, "OK, going to mount %s:%s ...\n", inet_ntoa(config.net.server.ip), config.serverdir ?: "");

          if(net_mount_nfs(mountpoint_tg, &config.net.server, config.serverdir)) {
            deb_msg("NFS mount failed.");
            return 1;
          }

          deb_msg("NFS mount ok.");
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

      return 0;
    }
  }

  return 1;
}

int auto2_driver_is_active(driver_info_t *di)
{
  for(; di; di = di->next) {
    if(di->any.type == di_module && di->module.active) return 1;
  }
  return 0;
}


/*
 * Activate storage/network devices.
 * Returns 0 or the index of the last controller we activated.
 */
int auto2_activate_devices(unsigned base_class, unsigned last_idx)
{
  driver_info_t *di, *di0;
  str_list_t *sl1, *sl2;
  hd_t *hd;
  int i;

  for(hd = hd_data->hd; hd; hd = hd->next) {
    if(hd->idx > last_idx) break;
  }

  last_idx = 0;		/* re-use */

  if(!hd) return 0;	/* no further entries */

  for(di0 = NULL; hd; hd = hd->next) {
    if(
      hd->base_class == base_class &&
      (di0 = hd_driver_info(hd_data, hd)) &&
      !auto2_driver_is_active(di0)
    ) {
      for(di = di0; di; di = di->next) {
        if(di->module.type == di_module && !di->module.modprobe) {
          // fprintf(stderr, "Found a \"%s\"\n", auto2_device_name(hd));
          for(
            sl1 = di->module.names, sl2 = di->module.mod_args;
            sl1 && sl2;
            sl1 = sl1->next, sl2 = sl2->next
          ) {
            mod_insmod(sl1->str, sl2->str);
          }

          /* all modules should be loaded now */
          for(i = 1, sl1 = di->module.names; sl1; sl1 = sl1->next) {
            i &= hd_module_is_active(hd_data, sl1->str);
          }

          if(i) {
            last_idx = hd->idx;
            fprintf(stderr, "Ok, that seems to have worked. :-)\n");
            break;
          }
          else {
            need_modules = 1;
            fprintf(stderr, "Oops, that didn't work.\n");
          }
        }
      }

      /* some module was loaded...; in demo mode activate all disks */
      if(
        !(
          (config.activate_storage && base_class == bc_storage) ||
          (config.activate_network && base_class == bc_network)
        ) &&
        di
      ) break;
    }

    di0 = hd_free_driver_info(di0);
  }

  di0 = hd_free_driver_info(di0);

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
  int i, j, net_cfg;

#if 0
  auto2_chk_x11i();
#endif

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

  file_read_info();

  util_debugwait("got info file");

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
      if(auto2_ask_for_modules(1, "scsi") == 0) return FALSE;
    }
  }

  i = auto2_find_install_medium();
#endif

  return i;
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
    if(!(i = auto2_mount_harddisk(buf))) return TRUE;
  }
    
  if(config.instmode == inst_cdrom || !config.instmode) {
    set_instmode(inst_cdrom);

    str_copy(&config.cdrom, NULL);

    util_debugwait("CD?");

    need_modules = 0;

    fprintf(stderr, "Looking for a SuSE CD...\n");
    if(!(i = auto2_cdrom_dev(&hd_devs))) {
      if(config.activate_storage) auto2_activate_devices(bc_storage, 0);
      if(config.activate_network) auto2_activate_devices(bc_network, 0);
      if(!*driver_update_dir) util_chk_driver_update(mountpoint_tg);
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

      fprintf(stderr, "Looking for a SuSE CD again...\n");
      if(!(i = auto2_cdrom_dev(&hd_devs))) {
        if(config.activate_storage) auto2_activate_devices(bc_storage, 0);
        if(config.activate_network) auto2_activate_devices(bc_network, 0);
        if(!*driver_update_dir) util_chk_driver_update(mountpoint_tg);
        return TRUE;
      }
    }

    if(config.cdrom) {
      if(!*driver_update_dir) util_chk_driver_update(mountpoint_tg);
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
  hd_t *hd;
  hd_res_t *res;
  int i, small_floppy = 0;
  char *s;

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

  for(hd = hd_data->hd; hd; hd = hd->next) {
    if(
      config.floppies < sizeof config.floppy_dev / sizeof *config.floppy_dev &&
      hd->base_class == bc_storage_device &&	/* is a storage device... */
      hd->sub_class == sc_sdev_floppy &&	/* a floppy actually... */
      hd->unix_dev_name &&			/* and has a device name */
      !hd->is.notready				/* medium inserted */
    ) {
      config.floppy_dev[config.floppies++] = strdup(hd->unix_dev_name);
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
int auto2_load_usb_storage()
{
  hd_t *hd;
  int nonusb_floppy_drives = 0;
  int usb_floppy_drives = 0;

  if(!hd_data) return 0;

  for(hd = hd_data->hd; hd; hd = hd->next) {
    if(
      hd->base_class == bc_storage_device &&
      hd->sub_class == sc_sdev_floppy
    ) {
      if(hd->bus == bus_usb) {
        if(
          !(
            (
              (hd->vend_name && !strcasecmp(hd->vend_name, "iomega")) ||
              (hd->sub_vend_name && !strcasecmp(hd->sub_vend_name, "iomega"))
            ) &&
            (
              (hd->dev_name && strstr(hd->dev_name, "ZIP")) ||
              (hd->sub_dev_name && strstr(hd->sub_dev_name, "Zip"))
            )
          )
        ) {
          usb_floppy_drives++;
        }
      }
      else {
        nonusb_floppy_drives++;
      }
    }
  }

  return usb_floppy_drives != 0;
}


#if 0
int auto2_has_i2o()
{
  hd_t *hd;
  driver_info_t *di = NULL;
  int i2o_needed = -1;

  if(hd_data) {
    for(hd = hd_data->hd; hd; hd = hd->next) {
      if(hd->base_class == bc_i2o) {
        di = hd_driver_info(hd_data, hd);

        /* don't use i2o if we have an alternative driver */
        if(di && di->any.type == di_module) {
          i2o_needed = 0;
        } else {
          if(i2o_needed == -1) i2o_needed = 1;
        }

        di = hd_free_driver_info(di);
      }
    }
  }

  return i2o_needed > 0 ? TRUE : FALSE;
}
#endif

int auto2_pcmcia()
{
  if(!hd_data) return 0;

  return hd_has_pcmcia(hd_data);
}

int auto2_full_libhd()
{
  hd_data_t *hd_data_tmp;
  int i;

  if(hd_data) {
    return hd_bus_name(hd_data, bus_none) ? 1 : 0;
  }

  hd_data_tmp = calloc(1, sizeof *hd_data_tmp);
  /* init data structures  */
  hd_scan(hd_data_tmp);
  i = hd_bus_name(hd_data_tmp, bus_none) ? 1 : 0;
  hd_free_hd_data(hd_data_tmp);

  return i;
}

char *auto2_usb_module()
{
  int no_usb_mods = 0;

  if(hd_data) {
    usb_ig = hd_usb_support(hd_data);
    if(hd_cpu_arch(hd_data) == arch_ppc) no_usb_mods = 1;
  }

  if(usb_ig && no_usb_mods) return "";

  return usb_ig == 2 ? "usb-ohci" : usb_ig == 1 ? "usb-uhci" : NULL;
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
  static char console[32];
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

  console[0] = 0;

  for(hd = hd_data2->hd; hd; hd = hd->next)
    if(hd->base_class == bc_keyboard && hd->bus == bus_serial &&
       hd->sub_class == sc_keyboard_console &&
       hd->res->baud.type && hd->res->baud.type == res_baud)
      {
	strcpy (console, hd->unix_dev_name);
	/* Create a string like: ttyS0,38400n8 */
#if defined(__sparc__)
	sprintf (console_parms_tg, "%s,%d%c%d", &console[5],
		 hd->res->baud.speed,
		 hd->res->baud.parity,
		 hd->res->baud.bits);
#else
	sprintf (console_parms_tg, "%s,%d", &console[5], hd->res->baud.speed);
#endif
      }

  if (hd_data == NULL)
    {
      hd_free_hd_data (hd_data2);
      free (hd_data2);
    }

  if (strlen (console) > 0)
    return console;
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


#endif	/* USE_LIBHD */
