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
#include "modparms.h"
#include "pcmcia.h"

#ifdef USE_LIBHD

static hd_data_t *hd_data = NULL;
static char *auto2_loaded_module = NULL;
static char *auto2_loaded_module_args = NULL;
#if 0
static driver_info_t *x11_driver = NULL;
#endif
static char *pcmcia_params = NULL;
static int is_vaio = 0;

static void auto2_check_cdrom_update(char *dev);
static char *auto2_device_name(hd_t *hd);
static hd_t *add_hd_entry(hd_t **hd, hd_t *new_hd);
static int auto2_cdrom_dev(hd_t **);
static int auto2_net_dev(hd_t **);
static int auto2_driver_is_active(driver_info_t *di);
static int auto2_activate_devices(unsigned base_class, unsigned last_idx);
static void auto2_chk_frame_buffer(void);
#if 0
static void auto2_chk_x11i(void);
#endif
static int auto2_find_floppy(void);
static void auto2_find_mouse(void);
static int auto2_has_i2o(void);
#if 0
static int auto2_get_probe_env(hd_data_t *hd_data);
#endif
static void auto2_progress(char *pos, char *msg);
static int auto2_ask_for_modules(int prompt, int mod_type);

char *auto2_device_name(hd_t *hd)
{
  static char *s;

  if(hd->dev_name) return hd->dev_name;

  s = hd_device_name(hd_data, hd->vend, hd->dev);

  return s ? s : "?";
}

int auto2_init_settings()
{
  disp_set_display(1);

#if 0	// #####
  char *s;

  color_ig = get_rc_int(rc_color_screen);

  language_ig = getlangidbyname(get_rc_str(rc_language));

  s = get_rc_str(rc_keymap);
  if(!*s) s = get_rc_str(rc_language);
  strcpy(keymap_tg, getkeymapbyname(s));
#endif

  return 0;
}


/*
 * mount a detected suse-cdrom at mountpoint_tg and run inst_check_instsys()
 *
 * return 0 on success
 */
int auto2_mount_cdrom(char *device)
{
  int rc;

  bootmode_ig = BOOTMODE_CD;

  rc = mount(device, mountpoint_tg, "iso9660", MS_MGC_VAL | MS_RDONLY, 0);
  if(!rc) {
    if((rc = inst_check_instsys())) {
      fprintf(stderr, "%s is not a SuSE Installation CD.\n", device);
      umount(mountpoint_tg);
    } else {
      /*
       * skip "/dev/"; instead, cdrom_tg should hold the *full* device name
       */
      strcpy(cdrom_tg, device + sizeof "/dev/" - 1);
    }
  }
  else {
    fprintf(stderr, "%s does'nt have an ISO9660 file syetem.\n", device);
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

  if((guru_ig & 4)) hd_data->debug=-1 & ~HD_DEB_DRIVER_INFO;

  with_usb = hd_probe_feature(hd_data, pr_usb);
  hd_clear_probe_feature(hd_data, pr_usb);
  hd_scan(hd_data);
  if(hd_data->progress) {
    printf("\r%64s\r", "");
    fflush(stdout);
  }

  if((usb_mod = auto2_usb_module())) {
    i = 0;
    if(*usb_mod) {
      if(
        (i = mod_load_module("usbcore", NULL)) ||
        (i = mod_load_module(usb_mod, NULL))   ||
        (i = mod_load_module("input", NULL))   ||
        (i = mod_load_module("hid", NULL))
      );
      mod_load_module("keybdev", NULL);
      mod_load_module("mousedev", NULL);
    }
    k = mount (0, "/proc/bus/usb", "usbdevfs", 0, 0);
    if(!i) sleep(4);
    if(with_usb) {
      hd_clear_probe_feature(hd_data, pr_all);
      hd_set_probe_feature(hd_data, pr_usb);
      hd_scan(hd_data);
    }
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
        if(di->kbd.XkbRules) strcpy(xkbrules_tg, di->kbd.XkbRules);
        if(di->kbd.XkbModel) strcpy(xkbmodel_tg, di->kbd.XkbModel);
        if(di->kbd.XkbLayout) strcpy(xkblayout_tg, di->kbd.XkbLayout);
	/* UNTESTED !!! */
        if(di->kbd.keymap) strcpy(keymap_tg, di->kbd.keymap);
      }
      di = hd_free_driver_info(di);
    }
  }

  if(auto2_has_i2o()) {
    i = 0;
    if(
      (i = mod_load_module("i2o_pci", NULL))    ||
      (i = mod_load_module("i2o_core", NULL))   ||
      (i = mod_load_module("i2o_config", NULL)) ||
      (i = mod_load_module("i2o_block", NULL))
    );
    if(!i) {
      mpar_save_modparams("i2o_pci", NULL);
      mpar_save_modparams("i2o_core", NULL);
      mpar_save_modparams("i2o_config", NULL);
      mpar_save_modparams("i2o_block", NULL);

      hd_clear_probe_feature(hd_data, pr_all);
      hd_set_probe_feature(hd_data, pr_i2o);
      hd_scan(hd_data);
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

#if 0
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
    cdrom_drives++;
    add_hd_entry(hd0, hd);
    if(
      hd->unix_dev_name &&
      hd->detail &&
      hd->detail->type == hd_detail_cdrom
    ) {
      ci = hd->detail->cdrom.data;

      if(*cdrom_tg) {
        i = 0;
      }
      else {
        if(ci->iso9660.ok && ci->iso9660.volume && strstr(ci->iso9660.volume, "SU") == ci->iso9660.volume) {
          fprintf(stderr, "Found SuSE CD in %s.\n", hd->unix_dev_name);
          found_suse_cd_ig = TRUE;
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

  for(hd = hd_list(hd_data, hw_network, 1, *hd0); hd; hd = hd->next) {
    add_hd_entry(hd0, hd);
    if(hd->unix_dev_name && strcmp(hd->unix_dev_name, "lo")) {

      /* net_stop() - just in case */
      net_stop();

      fprintf(stderr, "Trying to activate %s\n", hd->unix_dev_name);

      strcpy(netdevice_tg, hd->unix_dev_name);

      if(net_activate()) {
        deb_msg("net_activate() failed");
        return 1;
      }
      else
        fprintf(stderr, "%s activated\n", hd->unix_dev_name);

      net_is_configured_im = TRUE;

      fprintf(stderr, "Starting portmap.\n");
      system("portmap");

      fprintf(stderr, "OK, going to mount %s:%s ...\n", inet_ntoa(nfs_server_rg), server_dir_tg);

      if(net_mount_nfs(inet_ntoa(nfs_server_rg), server_dir_tg)) {
        deb_msg("NFS mount failed.");
        return 1;
      }

      deb_msg("NFS mount ok.");

      bootmode_ig = BOOTMODE_NET;
      if(auto2_loaded_module && strlen(auto2_loaded_module) < sizeof net_tg)
        strcpy(net_tg, auto2_loaded_module);

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
  char mod_cmd[100];
  driver_info_t *di, *di0;
  hd_t *hd;

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
        if(!di->module.modprobe) {
          sprintf(mod_cmd, "insmod %s%s%s",
            di->module.name,
            di->module.mod_args ? " " : "",
            di->module.mod_args ? di->module.mod_args : ""
          );
          fprintf(stderr, "Found a \"%s\"\n", auto2_device_name(hd));
          fprintf(stderr, "Going to load module \"%s\" to activate it...\n", di->module.name);
          system(mod_cmd);
          if(hd_module_is_active(hd_data, di->module.name)) {
            if(auto2_loaded_module) {
              free(auto2_loaded_module); auto2_loaded_module = NULL;
            }
            if(auto2_loaded_module_args) {
              free(auto2_loaded_module_args); auto2_loaded_module_args = NULL;
            }
            auto2_loaded_module = strdup(di->module.name);
            if(di->module.mod_args)
              auto2_loaded_module_args = strdup(di->module.mod_args);

            if(base_class == bc_storage) {
              fprintf(stderr, "added %s to initrd\n", di->module.name);
              mpar_save_modparams(auto2_loaded_module, auto2_loaded_module_args);
            }

            last_idx = hd->idx;
            fprintf(stderr, "Ok, that seems to have worked. :-)\n");
            break;
          }
          else {
            fprintf(stderr, "Oops, that didn't work.\n");
          }
        }
      }

      /* some module was loaded...; in demo mode activate all disks */
      if(
        !(
          ((action_ig & ACT_LOAD_DISK) && base_class == bc_storage) ||
          ((action_ig & ACT_LOAD_NET) && base_class == bc_network)
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
  int i, j;

#if 0
  auto2_chk_x11i();
#endif

  auto2_chk_frame_buffer();

  deb_msg("Beginning hardware probing...");
  printf("Starting hardware detection...\n");

  auto2_scan_hardware(NULL);

  printf("\r%64s\r", "");
  fflush(stdout);
  deb_msg("Hardware probing finished.");

#if WITH_PCMCIA
  if(hd_has_pcmcia(hd_data)) {
    deb_msg("Going to load PCMCIA support...");

    if(!util_check_exist("modules/pcmcia_core.o")) {
      char s[200], *t;

      util_manual_mode();
      disp_cursor_off();
      disp_set_display(1);
      util_print_banner();

      sprintf(s, txt_get(TXT_FOUND_PCMCIA), "i82365");
      t = index(s, '\n');
      if(t) {
        *t = 0;
        strcat(t, "\n\n");
      }
      else {
        *s = 0;
      }
      strcat(s, txt_get(TXT_ENTER_MODDISK));

      j = dia_okcancel(s, YES) == YES ? 1 : 0;

      if(j) {
        mod_force_moddisk_im = TRUE;
        mod_free_modules();
        mod_get_ram_modules(MOD_TYPE_OTHER);
      }

      ask_for_moddisk = FALSE;

      printf("\033c"); fflush(stdout);
      disp_clear_screen();

      auto2_ig = TRUE;
    }

    if(
      (i = mod_load_module("pcmcia_core", NULL)) ||
      (i = mod_load_module("i82365", pcmcia_params))   ||
      (i = mod_load_module("ds", NULL))
    );

    if(!i) {
      deb_msg("PCMCIA modules loaded - starting card manager.");
      pcmcia_chip_ig = 2;	/* i82365 */
      i = system("cardmgr -v -m /modules");
      if(i)
        deb_msg("Oops: card manager didn't start.");
      else {
        pcmcia_core_loaded_im = TRUE;
        deb_msg("card manager ok.");
      }
      /* wait for cards to be activated... */
      sleep(is_vaio ? 10 : 2);
      /* check for cdrom & net devs */
      hd_list(hd_data, hw_cdrom, 0, NULL);
      hd_list(hd_data, hw_network, 0, NULL);
    }
    else {
      deb_msg("Error loading PCMCIA modules.");
    }
  }
#endif

  if(!auto2_find_floppy()) {
    deb_msg("There seems to be no floppy disk.");
  }
  else {
    file_read_info();
  }

  auto2_find_mouse();

  deb_int(valid_net_config_ig);

  i = auto2_find_install_medium();

#ifdef __i386__
  {
    int net_cfg = (valid_net_config_ig & 0x2b) == 0x2b;

    /* ok, found something */
    if(i) return i;

    /* no CD, but CD drive and no network config */
    if(cdrom_drives && !net_cfg) return i;

    if(auto2_ask_for_modules(1, net_cfg ? MOD_TYPE_NET : MOD_TYPE_SCSI) == 0) return FALSE;

    i = auto2_find_install_medium();

    if(i || !net_cfg) return i;

    if(auto2_ask_for_modules(0, net_cfg ? MOD_TYPE_SCSI : MOD_TYPE_NET) == 0) return FALSE;

    i = auto2_find_install_medium();
  }
#endif

  return i;
}


/*
 * Look for a SuSE CD ot NFS source.
 */
int auto2_find_install_medium()
{
  int i;
  unsigned last_idx;
  hd_t *hd_devs = NULL;

  if(bootmode_ig == BOOTMODE_CD) {
    *cdrom_tg = 0;

    deb_msg("Looking for a SuSE CD...");
    if(!(i = auto2_cdrom_dev(&hd_devs))) {
      if((action_ig & ACT_LOAD_DISK)) auto2_activate_devices(bc_storage, 0);
      if((action_ig & ACT_LOAD_NET)) auto2_activate_devices(bc_network, 0);
      if(!*driver_update_dir) util_chk_driver_update(mountpoint_tg);
      return TRUE;
    }

    for(last_idx = 0;;) {
      /* i == 1 -> try to activate another storage device */
      if(i == 1) {
        deb_msg("Ok, that didn't work; see if we can activate another storage device...");
      }

      if(!(last_idx = auto2_activate_devices(bc_storage, last_idx))) {
        fprintf(stderr, "No further storage devices found; giving up.\n");
        break;
      }

      fprintf(stderr, "Looking for a SuSE CD again...\n");
      if(!(i = auto2_cdrom_dev(&hd_devs))) {
        if((action_ig & ACT_LOAD_DISK)) auto2_activate_devices(bc_storage, 0);
        if((action_ig & ACT_LOAD_NET)) auto2_activate_devices(bc_network, 0);
        if(!*driver_update_dir) util_chk_driver_update(mountpoint_tg);
        return TRUE;
      }
    }

    if(*cdrom_tg) {
      if(!*driver_update_dir) util_chk_driver_update(mountpoint_tg);
      return TRUE;
    }
  }

  if((valid_net_config_ig & 0x2b) != 0x2b) return FALSE;

  if(auto2_loaded_module) {
    free(auto2_loaded_module); auto2_loaded_module = NULL;
  }
  if(auto2_loaded_module_args) {
    free(auto2_loaded_module_args); auto2_loaded_module_args = NULL;
  }

  deb_msg("Well, maybe there is a NFS/FTP server...");

  broadcast_rg.s_addr = ipaddr_rg.s_addr | ~netmask_rg.s_addr;

  fprintf(stderr, "host ip:   %s\n", inet_ntoa(ipaddr_rg));
  fprintf(stderr, "netmask:   %s\n", inet_ntoa(netmask_rg));
  fprintf(stderr, "broadcast: %s\n", inet_ntoa(broadcast_rg));
  fprintf(stderr, "gateway:   %s\n", inet_ntoa(gateway_rg));
  fprintf(stderr, "server ip: %s\n", inet_ntoa(nfs_server_rg));
  if((valid_net_config_ig & 0x10))
    fprintf(stderr, "name srv:  %s\n", inet_ntoa(nameserver_rg));

  if(!auto2_net_dev(&hd_devs)) {
    if((action_ig & ACT_LOAD_NET)) auto2_activate_devices(bc_network, 0);
    if(!*driver_update_dir) util_chk_driver_update(mountpoint_tg);
    return TRUE;
  }

  for(last_idx = 0;;) {
    deb_msg("Ok, that didn't work; see if we can activate another network device...");

    if(!(last_idx = auto2_activate_devices(bc_network, last_idx))) {
      fprintf(stderr, "No further network cards found; giving up.\n");
      break;
    }

    fprintf(stderr, "Looking for a NFS/FTP server again...\n");
    if(!auto2_net_dev(&hd_devs)) {
      if((action_ig & ACT_LOAD_NET)) auto2_activate_devices(bc_network, 0);
      if(!*driver_update_dir) util_chk_driver_update(mountpoint_tg);
      return TRUE;
    }
  }

  bootmode_ig = BOOTMODE_CD;		/* reset value */

  return FALSE;
}


/*
 * Read *all* "expert=" entries from the kernel command line.
 *
 * Note: can't use getenv() as there might be multiple "expert=" entries.
 */
void auto2_chk_expert()
{
  FILE *f;
  char buf[256], *s, *t;
  int i = 0, j;

  if((f = fopen("/proc/cmdline", "r"))) {
    if(fread(buf, 1, sizeof buf, f)) {
      t = buf;
      while((s = strsep(&t, " "))) {
        if(sscanf(s, "expert=%i", &j) == 1) i |= j;
      }
    }
    fclose(f);
  }

  if((i & 0x01)) text_mode_ig = 1;
  if((i & 0x02)) yast2_update_ig = 1;
  if((i & 0x04)) yast2_serial_ig = 1;
  if((i & 0x08)) auto2_ig = 1;
  guru_ig = i >> 4;
}


/*
 * Read "vga=" entry from the kernel command line.
 */
void auto2_chk_frame_buffer()
{
  FILE *f;
  char buf[256], *s, *t;
  int j;
  int fb_mode = -1;

  if((f = fopen("/proc/cmdline", "r"))) {
    if(fread(buf, 1, sizeof buf, f)) {
      t = buf;
      while((s = strsep(&t, " "))) {
        if(sscanf(s, "vga=%i", &j) == 1) fb_mode = j;
      }
    }
    fclose(f);
  }

  if(fb_mode > 0x10) frame_buffer_mode_ig = fb_mode;
}


#if 0
/*
 * Read "x11i=" entry from the kernel command line.
 */
void auto2_chk_x11i()
{
  FILE *f;
  char buf[256], *s, *t;
  char x11i[64];

  *x11i = 0;

  if((f = fopen("/proc/cmdline", "r"))) {
    if(fread(buf, 1, sizeof buf, f)) {
      t = buf;
      while((s = strsep(&t, " "))) {
        if(sscanf(s, "x11i=%60s", x11i) == 1) {
          x11i_tg = strdup(x11i);
          break;
        }
      }
    }
    fclose(f);
  }
}
#endif


/*
 * Scans the hardware list for a mouse.
 */
void auto2_find_mouse()
{
  driver_info_t *di;
  hd_t *hd;

  if(!hd_data) return;

  mouse_type_xf86_ig = mouse_type_gpm_ig = mouse_dev_ig = NULL;

  for(di = NULL, hd = hd_data->hd; hd; hd = hd->next) {
    if(
      hd->base_class == bc_mouse &&             /* is a mouse */
      hd->unix_dev_name &&                      /* and has a device name */
      (di = hd_driver_info(hd_data, hd)) &&     /* and has driver info... */
      di->any.type == di_mouse &&               /* which is actually *mouse* info... */
      (di->mouse.xf86 || di->mouse.gpm)         /* it's supported */
    ) {
      mouse_type_xf86_ig = strdup(di->mouse.xf86);
      mouse_type_gpm_ig = strdup(di->mouse.gpm);
      mouse_dev_ig = strdup(hd->unix_dev_name);
    }

    di = hd_free_driver_info(di);

    if(mouse_dev_ig) break;
  }
}


/*
 * Scans the hardware list for a floppy and puts the result in
 * has_floppy_ig.
 */
int auto2_find_floppy()
{
  hd_t *hd;

  if(!hd_data) return has_floppy_ig;

  for(hd = hd_data->hd; hd; hd = hd->next) {
    if(
      hd->base_class == bc_storage_device &&	/* is a storage device... */
      hd->sub_class == sc_sdev_floppy &&	/* a floppy actually... */
      hd->unix_dev_name &&			/* and has a device name */
      !strcmp(hd->unix_dev_name, "/dev/fd0") &&	/* it's the 1st floppy */
      hd->detail &&
      hd->detail->type == hd_detail_floppy	/* floppy can be read */
    ) {
      return has_floppy_ig = hd->detail->floppy.data ? TRUE : FALSE;
    }
  }

  return has_floppy_ig = FALSE;
}

#if 0
/*
 * Scans the hardware list for a keybord and puts the result in
 * has_kbd_ig.
 */
int auto2_find_kbd()
{
  hd_t *hd;

  for(hd = hd_data->hd; hd; hd = hd->next) {
    if(hd->base_class == bc_keyboard) {
      return has_kbd_ig = TRUE;
    }
  }

  return has_kbd_ig = FALSE;
}
#endif

int auto2_has_i2o()
{
  hd_t *hd;

  if(hd_data) {
    for(hd = hd_data->hd; hd; hd = hd->next) {
      if(hd->base_class == bc_i2o) {
        return TRUE;
      }
    }
  }

  return FALSE;
}

#if 0
int auto2_get_probe_env(hd_data_t *hd_data)
{
  char *s, *t, *env = getenv("probe");
  int j, k;

  if(env) s = env = strdup(env);
  if(!env) return 0;

  while((t = strsep(&s, ","))) {
    if(*t == '+') {
      k = 1;
    }
    else if(*t == '-') {
      k = 0;
    }
    else {
      deb_msg("unknown flag");
      free(env);
      return -1;
    }

    t++;

    if((j = hd_probe_feature_by_name(t))) {
      if(k)
        hd_set_probe_feature(hd_data, j);
      else
        hd_clear_probe_feature(hd_data, j);
    }
    else {
      fprintf(stderr, "unknown flag");
      free(env);
      return -2;
    }
  }

  free(env);

  return 0;
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

#if 0
/*
 * Assumes xf86_ver to be either "3" or "4" (or empty).
 */
char *auto2_xserver(char **version, char **busid)
{
  static char display[16];
  static char xf86_ver[2];
  static char id[32];
  char c, *x11i = x11i_tg;
  static driver_info_t *di0 = NULL;
  driver_info_t *di;
  hd_t *hd;

  di0 = hd_free_driver_info(di0);
  x11_driver = NULL;

  *display = *xf86_ver = *id = c = 0;
  *version = xf86_ver;
  *busid = id;

  if(x11i) {
    if(*x11i == '3' || *x11i == '4') {
      c = *x11i;
    }
    else {
      if(*x11i >= 'A' && *x11i <= 'Z') {
        c = '3';
      }
      if(*x11i >= 'a' && *x11i <= 'z') {
        c = '4';
      }
      if(c) {
        strncpy(display, x11i, sizeof display - 1);
        display[sizeof display - 1] = 0;
      }
    }
  }

#ifdef __PPC__
  /* temporary hack due to XF4 problems */
  if(!c) c = '3';
#endif

  if(c) { xf86_ver[0] = c; xf86_ver[1] = 0; }

  if(!hd_data) return NULL;

  hd = hd_get_device_by_idx(hd_data, hd_display_adapter(hd_data));
  if(hd && hd->bus == bus_pci)
    sprintf(id, "%d:%d:%d", hd->slot >> 8, hd->slot & 0xff, hd->func);

  if(*display) return display;

  di0 = hd_driver_info(hd_data, hd);
  for(di = di0; di; di = di->next) {
    if(di->any.type == di_x11 && di->x11.server && di->x11.xf86_ver && !di->x11.x3d) {
      if(c == 0 || c == di->x11.xf86_ver[0]) {
        xf86_ver[0] = di->x11.xf86_ver[0];
        xf86_ver[1] = 0;
        strncpy(display, di->x11.server, sizeof display - 1);
        display[sizeof display - 1] = 0;
        x11_driver = di;
        break;
      }
    }
  }


  if(*display) return display;

  if(c == 0) c = '3';	/* default to XF 3, if nothing else is known  */

  xf86_ver[0] = c;
  xf86_ver[1] = 0;
  strcpy(display, c == '3' ? "FBDev" : "fbdev");

  return display;
}
#endif

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
	sprintf (console_parms_tg, "%s,%d%s%d", &console[5],
		 hd->res->baud.speed,
		 hd->res->baud.parity ? "p" : "n",
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


int auto2_ask_for_modules(int prompt, int mod_type)
{
  int do_something = 0;

  if(
    !ask_for_moddisk ||
    !util_check_exist("/etc/need_modules_disk")
  ) return do_something;

  util_manual_mode();
  disp_cursor_off();
  disp_set_display(1);
  util_print_banner();

  if(prompt) {
    prompt = dia_okcancel(txt_get(TXT_ENTER_MODDISK), YES) == YES ? 1 : 0;
  }
  else {
    prompt = 1;
  }

  if(prompt) {
    mod_force_moddisk_im = TRUE;
    mod_free_modules();
    mod_get_ram_modules(mod_type);
    do_something = 1;
  }

  printf("\033c"); fflush(stdout);
  disp_clear_screen();

  auto2_ig = TRUE;

  return do_something;
}


#endif	/* USE_LIBHD */
