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

static void auto2_user_netconfig(void);
static hd_t *add_hd_entry(hd_t **hd, hd_t *new_hd);
static int auto2_harddisk_dev(hd_t **);
static int auto2_cdrom_dev(hd_t **);
static int hwaddr_cmp(char *str, char *addr);
static int auto2_net_dev(hd_t **);
static int auto2_net_dev1(hd_t *hd);
static int driver_is_active(hd_t *hd);
static int auto2_activate_devices(hd_hw_item_t hw_class, unsigned last_idx);
static void auto2_chk_frame_buffer(void);
static void auto2_progress(char *pos, char *msg);
static int auto2_ask_for_modules(int prompt, char *type);
static void get_zen_config(void);

/*
 * mount a detected suse-cdrom at config.mountpoint.instdata and run inst_check_instsys()
 *
 * return 0 on success
 */
int auto2_mount_cdrom(char *device)
{
  int rc;

  set_instmode(inst_cdrom);

  if(config.cdid && strstr(config.cdid, "-DVD-")) set_instmode(inst_dvd);

  rc = mount(device, config.mountpoint.instdata, "iso9660", MS_MGC_VAL | MS_RDONLY, 0);
  if(!rc) {
    if((rc = inst_check_instsys())) {
      fprintf(stderr, "%s is not a %s Installation CD.\n", device, config.product);
      umount(config.mountpoint.instdata);
    } else {
      /* skip "/dev/" !!! */
      str_copy(&config.cdrom, device + sizeof "/dev/" - 1);
    }
  }
  else {
    fprintf(stderr, "%s does'nt have an ISO9660 file system.\n", device);
  }

  if(rc) set_instmode(inst_cdrom);

  return rc;
}


int auto2_mount_harddisk(char *dev)
{
  int rc = 0;
  char buf[256];
  char *module, *type;

  set_instmode(inst_hd);

  /* load fs module if necessary */

  type = util_fstype(dev, &module);
  if(module) mod_modprobe(module, NULL);

  if(!type || !strcmp(type, "swap")) rc = -1;

  if(!rc) {
    rc = util_mount_ro(dev, config.mountpoint.extra);
  }

  if(!rc) {
    config.extramount = 1;
    util_truncate_dir(config.serverdir);
    sprintf(buf, "%s/%s", config.mountpoint.extra, config.serverdir);
    rc = util_check_exist(buf) ? util_mount_ro(buf, config.mountpoint.instdata) : -1;
  }

  if(!rc) {
    if((rc = inst_check_instsys())) {
      fprintf(stderr, "%s::%s is not an installation source\n", dev, config.serverdir);
    }
    else {
      fprintf(stderr, "using %s::%s\n", dev, config.serverdir);
    }
  }

  if(rc) {
    fprintf(stderr, "nothing found on %s\n", dev);
    inst_umount();
  }

  util_do_driver_updates();

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

  util_splash_bar(20);

  hd_usb = hd_list(hd_data, hw_usb_ctrl, 0, NULL);

  if(hd_usb) {
    if(config.scsi_before_usb) load_storage_mods();
    storage_loaded = 1;

    printf("Activating usb devices...");
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

    for(hd = hd_usb; hd; hd = hd->next) activate_driver(hd_data, hd, &usb_modules);
    hd_usb = hd_free_hd_list(hd_usb);

    fflush(stdout);

    mod_modprobe("input", NULL);
    mod_modprobe("usbhid", NULL);
    mod_modprobe("keybdev", NULL);

    config.module.delay -= 1;

    k = mount("usbfs", "/proc/bus/usb", "usbfs", 0, 0);
    if(config.usbwait > 0) sleep(config.usbwait);

    if(with_usb) {
      mod_modprobe("usb-storage", NULL);
      if(config.usbwait > 0) sleep(config.usbwait);
      hd_free_hd_list(hd_list(hd_data, hw_usb, 1, NULL));
    }
    printf(" done\n"); fflush(stdout);
    if(!log_file) hd_data->progress = auto2_progress;
  }

  hd_fw = hd_list(hd_data, hw_ieee1394_ctrl, 0, NULL);

  if(hd_fw) {
    if(!storage_loaded && config.scsi_before_usb) load_storage_mods();

    printf("Activating ieee1394 devices...");
    fflush(stdout);

    config.module.delay += 3;

    for(hd = hd_fw; hd; hd = hd->next) activate_driver(hd_data, hd, NULL);
    hd_usb = hd_free_hd_list(hd_fw);

    mod_modprobe("sbp2", NULL);

    config.module.delay -= 3;

    if(config.usbwait > 0) sleep(config.usbwait);

    printf(" done\n");
    fflush(stdout);
  }

  util_splash_bar(30);

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

  hd_sys = hd_list(hd_data, hw_sys, 1, NULL);

  activate_driver(hd_data, hd_sys, NULL);

  if(
    hd_sys &&
    hd_sys->detail && hd_sys->detail->type == hd_detail_sys &&
    (st = hd_sys->detail->sys.data)
  ) {
    if(st->model && strstr(st->model, "PCG-") == st->model) {
      /* is a Sony Vaio */
      fprintf(stderr, "is a Vaio\n");
      is_vaio = 1;
      if(!config.kernel_pcmcia) pcmcia_params = "irq_list=9,10,11,15";
      slist_append(&config.module.initrd, usb_modules);
      usb_modules = NULL;
    }
  }

  /* usb keyboard ? */
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
 * Look for a SuSE HD and mount it.
 *
 * Returns:
 *    0: OK, HD was mounted
 *    1: no HD found
 *   >1: HD found, but contiune the search
 *
 */
int auto2_harddisk_dev(hd_t **hd0)
{
  int i = 1;
  hd_t *hd;

  for(hd = hd_list(hd_data, hw_partition, 1, *hd0); hd; hd = hd->next) {
    add_hd_entry(hd0, hd);

    if(
      !hd->unix_dev_name ||
      strncmp(hd->unix_dev_name, "/dev/", sizeof "/dev/" - 1)
    ) continue;

    if(
      config.partition &&
      strcmp(config.partition, hd->unix_dev_name + sizeof "/dev/" - 1)
    ) continue;

    fprintf(stderr, "Checking partition: %s\n", hd->unix_dev_name);

    i = auto2_mount_harddisk(hd->unix_dev_name) ? config.partition ? 1 : 2 : 0;

    if(i == 0) {
      str_copy(&config.partition, hd->unix_dev_name + sizeof "/dev/" - 1);
    }

    if(i == 0) break;
  }

  hd = hd_free_hd_list(hd);

  return i;
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

      if(config.update.ask && i == 0) i = 3;

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

  if(!(net_config_mask() || config.insttype == inst_net)) return 1;

  for(hd = hd_list(hd_data, hw_network, 1, *hd0); hd; hd = hd->next) {
    add_hd_entry(hd0, hd);

    if(!auto2_net_dev1(hd)) return 0;
  }

  return 1;
}


/*
 * 0: match, 1: no match
 */
int hwaddr_cmp(char *str, char *addr)
{
  int alen, slen;
  int wc = 0;

  alen = strlen(addr);

  if(*str == '*') {
    str++;
    wc = 1;
  }

  slen = strlen(str);

  if(slen > alen) return 1;

  if(wc) addr += alen - slen;

  return strcasecmp(addr, str);
}


/*
 * Try to get inst-sys from network device.
 *
 * Return:
 *   0: ok
 *   1: failed
 */
int auto2_net_dev1(hd_t *hd)
{
  int i /*, link */;
  char buf[256];
  char *device, *hwaddr = NULL;
  hd_res_t *res;

  if(!hd || !(device = hd->unix_dev_name)) return 1;

  if(!strncmp(device, "lo", sizeof "lo" - 1)) return 1;

  for(res = hd->res; res; res = res->next) {
    if(res->any.type == res_hwaddr) {
      hwaddr = res->hwaddr.addr;
      break;
    }
  }

  if(
    config.net.device_given &&
    strcmp(config.net.device, device) &&
    hwaddr_cmp(config.net.device, hwaddr)
  ) return 1;

  /* net_stop() - just in case */
  net_stop();

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

  net_setup_localhost();

  config.net.configured = nc_static;

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

    if(net_check_address2(&config.net.server, 1)) {
      fprintf(stderr, "invalid server address: %s\n", config.net.server.name);
      config.net.configured = nc_none;
      return 1;
    }
  }

  if(net_activate_ns()) {
    fprintf(stderr, "net activation failed\n");
    config.net.configured = nc_none;
    return 1;
  }
  else {
    fprintf(stderr, "%s activated\n", device);
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
      fprintf(stderr, "OK, going to mount //%s/%s ...\n", config.net.server.name, config.net.share);

      mkdir(config.mountpoint.extra, 0755);

      i = net_mount_smb(config.mountpoint.extra,
        &config.net.server, config.net.share,
        config.net.user, config.net.password, config.net.workgroup
      );

      if(!i) {
        config.extramount = 1;
        util_truncate_dir(config.serverdir);
        sprintf(buf, "%s/%s", config.mountpoint.extra, config.serverdir);
        i = util_check_exist(buf) ? util_mount_ro(buf, config.mountpoint.instdata) : -1;
      }
      
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

      if(net_mount_nfs(config.mountpoint.instdata, &config.net.server, config.serverdir)) {
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
int activate_driver(hd_data_t *hd_data, hd_t *hd, slist_t **mod_list)
{
  driver_info_t *di;
  str_list_t *sl1, *sl2;
  slist_t *slm;
  int i, ok = 0;

  if(!hd || driver_is_active(hd)) return 1;

  if(hd->is.notready) return 1;

  /* skip pcmcia devices; card manager handles those */
  if(ID_TAG(hd->vendor.id) == TAG_PCMCIA) return 0;

  for(di = hd->driver_info; di; di = di->next) {
    if(di->module.type == di_module) {
      for(
        sl1 = di->module.names, sl2 = di->module.mod_args;
        sl1 && sl2;
        sl1 = sl1->next, sl2 = sl2->next
      ) {
        di->module.modprobe ? mod_modprobe(sl1->str, sl2->str) : mod_insmod(sl1->str, sl2->str);
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
int auto2_activate_devices(hd_hw_item_t hw_class, unsigned last_idx)
{
  hd_t *hd;
  int ok;

  for(hd = hd_data->hd; hd; hd = hd->next) {
    if(hd->idx > last_idx) break;
  }

  last_idx = 0;		/* re-use */

  if(!hd) return 0;	/* no further entries */

  for(; hd; hd = hd->next) {
    if(hd_is_hw_class(hd, hw_class) && !driver_is_active(hd)) {
      if((ok = activate_driver(hd_data, hd, NULL))) {
        last_idx = hd->idx;
        fprintf(stderr, "Ok, that seems to have worked. :-)\n");
      }
      else {
#ifdef __i386__
        need_modules = 1;
#endif
        fprintf(stderr, "Oops, that didn't work.\n");
      }

      /* some module was loaded...; in demo mode activate all disks */
      if(
        !(
          (config.activate_storage && hw_class == hw_storage_ctrl) ||
          (config.activate_network && hw_class == hw_network_ctrl)
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
  int i, win_old;
#if 0	/* def __i386__ */
  int net_cfg;
#endif
  hd_t *hd;
  char buf[256];
#if WITH_PCMCIA
//  int j;
  hd_hw_item_t hw_items[] = { hw_cdrom, hw_network, 0 };
#endif

  auto2_chk_frame_buffer();

  fprintf(stderr, "Beginning hardware probing...\n");
  fflush(stderr);
  printf("Starting hardware detection...\n");
  fflush(stdout);

  auto2_scan_hardware(NULL);

  printf("\r%64s\r", "");
  fflush(stdout);

  if(config.idescsi) {
    strcpy(buf, "ignore=");
    for(i = 0, hd = hd_list(hd_data, hw_cdrom, 0, NULL); hd; hd = hd->next) {
      if(
        hd->bus.id == bus_ide &&
        (config.idescsi > 1 || hd->is.cdr || hd->is.cdrw || hd->is.dvdr) &&
        hd->unix_dev_name &&
        !strncmp(hd->unix_dev_name, "/dev/hd", sizeof "/dev/hd" - 1)
      ) {
        sprintf(buf + strlen(buf), "%s%s", i ? "," : "", hd->unix_dev_name + 5);
        i = 1;
      }
    }

    if(i) {
      mod_unload_module("ide-cd");
      mod_modprobe("ide-cd", buf);
      mod_modprobe("ide-scsi", NULL);
      hd_list(hd_data, hw_cdrom, 1, NULL);
    }
  }

  fprintf(stderr, "Hardware probing finished.\n");
  fflush(stderr);

  if(!auto2_find_floppy()) {
    fprintf(stderr, "There seems to be no floppy disk.\n");
  }

  if(!config.hwcheck) {
    file_read_info();
    util_debugwait("got info file");
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

#if 0
    if(!util_check_exist("/modules/pcmcia_core" MODULE_SUFFIX)) {
      char buf[256], *t;

      util_disp_init();

      sprintf(buf, txt_get(TXT_FOUND_PCMCIA), pcmcia_driver(2));
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
#endif

    config.module.delay += 1;

    i = mod_modprobe(pcmcia_driver(2), pcmcia_params);
    if(i) fprintf(stderr, "Error %d loading PCMCIA modules.\n", i);
    i = 0;

    if(!i) {
      fprintf(stderr, "PCMCIA modules loaded - starting card manager.\n");
      pcmcia_chip_ig = 2;
      i = system("cardmgr -d -v -m /modules -n \"\" >&2");
      if(i)
        fprintf(stderr, "Oops: card manager didn't start.\n");
      else {
        fprintf(stderr, "card manager ok.\n");
      }
      /* wait for cards to be activated... */
      sleep(config.usbwait > 0 ? config.usbwait : is_vaio ? 10 : 6);
      /* check for cdrom & net devs */
      hd_list2(hd_data, hw_items, 1);
    }
    else {
      fprintf(stderr, "Error loading PCMCIA modules.\n");
      i = -1;
    }
    printf("\r%s%s", "Activating PCMCIA devices...", i ? " failed\n" : " done\n");
    fflush(stdout);
  }

  config.module.delay -= 1;

#endif

  util_splash_bar(40);

  if(config.update.ask && !config.update.shown) {
    auto2_activate_devices(hw_storage_ctrl, 0);
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

  util_splash_bar(50);

  if(!i && config.hwcheck) i = 1;

  return i;
}


/*
 * Let user enter network config data.
 */
void auto2_user_netconfig()
{
  int win_old;

  if(!config.net.do_setup) return;

  auto2_activate_devices(hw_network_ctrl, 0);

  if((net_config_mask() & 3) == 3) {	/* we have ip & netmask */
    config.net.configured = nc_static;
    if(net_activate_ns()) {
      fprintf(stderr, "net activation failed\n");
      config.net.configured = nc_none;
    }
  }

  if(config.net.configured == nc_none || config.net.do_setup) {
    if(!(win_old = config.win)) util_disp_init();
    net_config();
    if(!win_old) util_disp_done();
  }

  if(config.net.configured == nc_none) {
    config.vnc = config.usessh = 0;
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

  if(config.instmode == inst_cdrom || !config.instmode) {
    set_instmode(inst_cdrom);

    str_copy(&config.cdrom, NULL);

    util_debugwait("CD?");

    do {
      need_modules = 0;

      if(config.activate_storage) auto2_activate_devices(hw_storage_ctrl, 0);
    }
    while(need_modules && auto2_ask_for_modules(1, "ide/raid/scsi"));

    do {
      need_modules = 0;

      if(config.activate_network) auto2_activate_devices(hw_network_ctrl, 0);
    }
    while(need_modules && auto2_ask_for_modules(1, "network"));

    fprintf(stderr, "Looking for a %s CD...\n", config.product);
    if(!(i = auto2_cdrom_dev(&hd_devs))) {
      auto2_user_netconfig();
      return TRUE;
    }

    do {
      need_modules = 0;

      for(last_idx = 0;;) {
        /* i == 1 -> try to activate another storage device */
        if(i == 1) {
          fprintf(stderr, "Ok, that didn't work; see if we can activate another storage device...\n");
        }

        if(!(last_idx = auto2_activate_devices(hw_storage_ctrl, last_idx))) {
          fprintf(stderr, "No further storage devices found; giving up.\n");
          break;
        }

        fprintf(stderr, "Looking for a %s CD again...\n", config.product);
        if(!(i = auto2_cdrom_dev(&hd_devs))) {
          auto2_user_netconfig();
          return TRUE;
        }
      }
    }
    while(need_modules && auto2_ask_for_modules(1, "ide/raid/scsi"));

    if(config.cdrom) {
      auto2_user_netconfig();
      return TRUE;
    }
  }

  if(config.instmode == inst_hd) {

    util_debugwait("HD?");

    do {
      need_modules = 0;
  
      if(config.activate_storage) auto2_activate_devices(hw_storage_ctrl, 0);
    }
    while(need_modules && auto2_ask_for_modules(1, "ide/raid/scsi"));

    do {
      need_modules = 0;

      if(config.activate_network) auto2_activate_devices(hw_network_ctrl, 0);
    }
    while(need_modules && auto2_ask_for_modules(1, "network"));

    fprintf(stderr, "Looking for a %s hard disk...\n", config.product);
    if(!(i = auto2_harddisk_dev(&hd_devs))) {
      auto2_user_netconfig();
      return TRUE;
    }

    do {
      need_modules = 0;

      for(last_idx = 0;;) {
        /* i == 1 -> try to activate another storage device */
        if(i == 1) {
          fprintf(stderr, "Ok, that didn't work; see if we can activate another storage device...\n");
        }

        if(!(last_idx = auto2_activate_devices(hw_storage_ctrl, last_idx))) {
          fprintf(stderr, "No further storage devices found; giving up.\n");
          break;
        }

        fprintf(stderr, "Looking for a %s hard disk again...\n", config.product);
        if(!(i = auto2_harddisk_dev(&hd_devs))) {
          auto2_user_netconfig();
          return TRUE;
        }
      }
    }
    while(need_modules && auto2_ask_for_modules(1, "ide/raid/scsi"));

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

    do {
      need_modules = 0;

      if(config.activate_network) auto2_activate_devices(hw_network_ctrl, 0);
    }
    while(need_modules && auto2_ask_for_modules(1, "network"));

    if(config.activate_storage) auto2_activate_devices(hw_storage_ctrl, 0);

    fprintf(stderr, "Looking for a network server...\n");
    if(!auto2_net_dev(&hd_devs)) return TRUE;

    do {
      need_modules = 0;

      for(last_idx = 0;;) {
        fprintf(stderr, "Ok, that didn't work; see if we can activate another network device...\n");

        if(!(last_idx = auto2_activate_devices(hw_network_ctrl, last_idx))) {
          fprintf(stderr, "No further network cards found; giving up.\n");
          break;
        }

        fprintf(stderr, "Looking for a network server again...\n");
        if(!auto2_net_dev(&hd_devs)) return TRUE;
      }
    }
    while(need_modules && auto2_ask_for_modules(1, "network"));

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

  f0 = file_read_cmdline(kf_cmd);
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

  config.floppy_probed = 1;

  if(config.floppydev) {
    config.floppy = 0;
    sprintf(buf, "/dev/%s", config.floppydev);
    str_copy(&config.floppy_dev[0], buf);
    return config.floppies = 1;
  }

  if(!hd_data) {
    hd_data = calloc(1, sizeof *hd_data);
    hd_set_probe_feature(hd_data, pr_lxrc);
    hd_clear_probe_feature(hd_data, pr_parallel);
    hd_scan(hd_data);
  }

  config.floppy = config.floppies = 0;
  for(
    i = 0;
    (unsigned) i < sizeof config.floppy_dev / sizeof *config.floppy_dev && config.floppy_dev[i];
    i++
  ) {
    free(config.floppy_dev[i]);
    config.floppy_dev[i] = NULL;
  }

  hd0 = hd_list(hd_data, hw_floppy, 1, NULL);

  for(hd = hd0; hd; hd = hd->next) {
    if(
      (unsigned) config.floppies < sizeof config.floppy_dev / sizeof *config.floppy_dev &&
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


/*
 * Prompt for module floppy; does some real things only on i386 (other
 * archs don't have module floppies).
 */
int auto2_ask_for_modules(int prompt, char *type)
{
  int do_something = 0;

#ifdef __i386__
  char buf[256];
  int mtype = mod_get_type(type);

  if(!config.module.disks) return do_something;

  if(mod_check_modules(type)) return do_something;

  util_disp_init();

  *buf = 0;
  mod_disk_text(buf, mtype);

  do_something = prompt ? dia_okcancel(buf, YES) == YES ? 1 : 0 : 1;

  if(do_something) mod_add_disk(0, mtype);

  util_disp_done();
#endif

  return do_something;
}


void load_storage_mods()
{
  if(!hd_data) {
    hd_data = calloc(1, sizeof *hd_data);
    hd_set_probe_feature(hd_data, pr_lxrc);
    hd_clear_probe_feature(hd_data, pr_parallel);
    hd_scan(hd_data);
  }

  config.activate_storage = 1;
  auto2_activate_devices(hw_storage_ctrl, 0);
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


#endif	/* USE_LIBHD */

