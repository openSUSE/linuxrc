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

#ifdef USE_LIBHD

static hd_data_t *hd_data = NULL;
static char *auto2_loaded_module = NULL;
static char *auto2_loaded_module_args = NULL;

static char *auto2_device_name(hd_t *hd);
static int auto2_cdrom_dev(void);
static int auto2_net_dev(void);
static int auto2_driver_is_active(driver_info_t *di);
static int auto2_activate_devices(unsigned base_class, int last_idx);
static void auto2_chk_frame_buffer(void);
static int auto2_find_floppy(void);
static int auto2_get_probe_env(hd_data_t *hd_data);
static void auto2_progress(char *pos, char *msg);

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
      fprintf(stderr, "Defective SuSE CD in %s\n", device);
      umount(mountpoint_tg);
    } else {
      /*
       * skip "/dev/"; instead, cdrom_tg should hold the *full* device name
       */
      strcpy(cdrom_tg, device + sizeof "/dev/" - 1);
    }
  }

  return rc;
}


/*
 * probe for installed hardware
 *
 * if log_file != NULL -> write hardware info to this file
 */
void auto2_scan_hardware(char *log_file)
{
  FILE *f = NULL;
  hd_t *hd;
  char *usb_mod;
  static char usb_mods[128];
  int i, j, ju, k, with_usb;

  if(hd_data) {
    hd_free_hd_data(hd_data);
    free(hd_data);
  }
  hd_data = calloc(1, sizeof *hd_data);
  hd_set_probe_feature(hd_data, log_file ? pr_default : pr_lxrc);
  if(!log_file) hd_data->progress = auto2_progress;

  if(auto2_get_probe_env(hd_data)) {
    /* reset flags on error */
    hd_set_probe_feature(hd_data, log_file ? pr_default : pr_lxrc);
  }

  if((guru_ig & 4)) hd_data->debug=-1 & ~HD_DEB_DRIVER_INFO;

  with_usb = hd_probe_feature(hd_data, pr_usb);
  hd_clear_probe_feature(hd_data, pr_usb);
  hd_scan(hd_data);

  if((usb_mod = auto2_usb_module())) {
    if(
      (i = mod_load_module("usbcore", NULL)) ||
      (i = mod_load_module(usb_mod, NULL))   ||
      (i = mod_load_module("input", NULL))   ||
      (i = mod_load_module("hid", NULL))
    );
    mod_load_module("keybdev", NULL);
    mod_load_module("mousedev", NULL);
    k = mount (0, "/proc/bus/usb", "usbdevfs", 0, 0);
    if(!i) sleep(3);
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
    if(hd->base_class == bc_keyboard) {
      has_kbd_ig = TRUE;
      j++;
      if(hd->bus == bus_usb) ju++;
    }
  }

  /* usb keyboard only ? */
  if((j && j == ju) && usb_mod) {
    sprintf(usb_mods, "usbcore %s input hid keybdev mousedev", usb_mod);
    usb_mods_ig = usb_mods;
  }

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


/*
 * Look for a SuSE CD and mount it.
 *
 * Returns:
 *   0: OK, CD was mounted
 *   1: no CD found
 *   2: CD found, but mount failed
 *
 * ##### FIXME: check only *new* devices (e.g. check every device only *once*)
 */
int auto2_cdrom_dev()
{
  int i = 1;
  hd_t *hd;
  cdrom_info_t *ci;

  for(hd = hd_cd_list(hd_data, 2); hd; hd = hd->next) {
    if(
      hd->unix_dev_name &&
      hd->detail &&
      hd->detail->type == hd_detail_cdrom
    ) {
      ci = hd->detail->cdrom.data;
      if(ci->volume && strstr(ci->volume, "SU") == ci->volume) {
        fprintf(stderr, "Found SuSE CD in %s.\n", hd->unix_dev_name);
        found_suse_cd_ig = TRUE;
        break;
      }
    }
  }

  /* CD found -> try to mount it */
  if(hd) i = auto2_mount_cdrom(hd->unix_dev_name) ? 2 : 0;

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
int auto2_net_dev()
{
  hd_t *hd;

  for(hd = hd_net_list(hd_data, 2); hd; hd = hd->next) {
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
int auto2_activate_devices(unsigned base_class, int last_idx)
{
  char mod_cmd[100];
  driver_info_t *di, *di0;
  hd_t *hd;

  for(hd = hd_data->hd; hd; hd = hd->next) {
    if(hd->idx > last_idx) break;
  }

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

            mpar_save_modparams(auto2_loaded_module, auto2_loaded_module_args);

            fprintf(stderr, "Ok, that seems to have worked. :-)\n");
            break;
          }
          else {
            fprintf(stderr, "Oops, that didn't work.\n");
          }
        }
      }

      /* some module was loaded... */
      if(di) break;
    }

    di0 = hd_free_driver_info(di0);
  }

  di0 = hd_free_driver_info(di0);

  return hd ? hd->idx : 0;
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
  int i;
  unsigned last_idx;

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

    if(
      (i = mod_load_module("pcmcia_core", NULL)) ||
      (i = mod_load_module("i82365", NULL))   ||
      (i = mod_load_module("ds", NULL))
    );

    if(!i) {
      deb_msg("PCMCIA modules loaded - starting card manager.");
      pcmcia_chip_ig = 2;	/* i82365 */
      i = system("cardmgr -v -m /modules");
      if(i)
        deb_msg("Oops: card manager didn't start.");
      else
        deb_msg("card manager ok.");
    }
    else {
      deb_msg("Error loading PCMCIA modules.");
    }
  }
#endif

  if(!auto2_find_floppy()) {
    deb_msg("There seems to be no floppy drive.");
  }
  else {
    file_read_info();
  }

  auto2_find_mouse();

  deb_int(valid_net_config_ig);

  if(bootmode_ig == BOOTMODE_CD) {
    deb_msg("Looking for a SuSE CD...");
    if(!(i = auto2_cdrom_dev())) return TRUE;

    for(last_idx = 0;;) {
      if(i == 2) {		/* CD found, but mount failed */
        deb_msg("So you don't have an intact SuSE CD.");
        break;
      }

      /* i == 1 -> try to activate another storage device */
      deb_msg("Ok, that didn't work; see if we can activate another storage device...");

      if(!(last_idx = auto2_activate_devices(bc_storage, last_idx))) {
        fprintf(stderr, "No further storage devices found; giving up.\n");
        break;
      }

      fprintf(stderr, "Looking for a SuSE CD again...\n");
      if(!(i = auto2_cdrom_dev())) return TRUE;
    }
  }

  if((valid_net_config_ig & 0x2f) != 0x2f) return FALSE;
  
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

  if(!auto2_net_dev()) return TRUE;

  for(last_idx = 0;;) {
    deb_msg("Ok, that didn't work; see if we can activate another network device...");

    if(!(last_idx = auto2_activate_devices(bc_network, last_idx))) {
      fprintf(stderr, "No further network cards found; giving up.\n");
      break;
    }

    fprintf(stderr, "Looking for a NFS/FTP server again...\n");
    if(!auto2_net_dev()) return TRUE;
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
  char *env = getenv("vga");
  int i;

  if(!env) return;

  if(sscanf(env, "%i", &i) == 1) frame_buffer_mode_ig = i;
}


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
      hd->base_class == bc_mouse &&		/* is a mouse */
      hd->unix_dev_name	&&			/* and has a device name */
      (di = hd_driver_info(hd_data, hd)) &&	/* and has driver info... */
      di->any.type == di_mouse &&		/* which is actually *mouse* info... */
      (di->mouse.xf86 || di->mouse.gpm)		/* it's supported */
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
  if(hd_data) usb_ig = hd_usb_support(hd_data);

  return usb_ig == 2 ? "usb-ohci" : usb_ig == 1 ? "usb-uhci" : NULL;
}

char *auto2_xserver()
{
  static char display[16];
  driver_info_t *di;

  if(!hd_data) return NULL;

  *display = 0;
  di = hd_driver_info(hd_data, hd_get_device_by_idx(hd_data, hd_display_adapter(hd_data)));
  if(di && di->any.type == di_x11 && di->x11.server) {
    strncpy(display, di->x11.server, sizeof display - 1);
    display[sizeof display - 1] = 0;
  }
  hd_free_driver_info(di);

  return display;
}


void auto2_progress(char *pos, char *msg)
{
  printf("\r%64s\r", "");
  printf("> %s: %s", pos, msg);
  fflush(stdout);
}

#endif	/* USE_LIBHD */
