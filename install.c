/*
 *
 * install.c           Handling of installation
 *
 * Copyright (c) 1996-2002  Hubert Mantel, SuSE Linux AG  (mantel@suse.de)
 *
 */

#include "dietlibc.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <ctype.h>
#include <string.h>
#include <signal.h>
#include <netdb.h>
#include <errno.h>
#include <dirent.h>
#include <sys/mount.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/swap.h>
#include <sys/socket.h>
#include <sys/reboot.h>
#include <sys/vfs.h>
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
#include "settings.h"
#include "auto2.h"
#include "fstype.h"


static char  inst_rootimage_tm [MAX_FILENAME];

static int   inst_mount_harddisk      (void);
static int   inst_try_cdrom           (char *device_tv);
static int   inst_mount_cdrom         (int show_err);
static int   inst_mount_nfs           (void);
static int   inst_start_rescue        (void);
// static int   inst_prepare             (void);
static int   add_instsys              (void);
static void  inst_yast_done           (void);
static int   inst_execute_yast        (void);
static int   inst_check_floppy        (void);
static int   inst_commit_install      (void);
static int   inst_choose_netsource    (void);
static int   inst_choose_netsource_cb (dia_item_t di);
static int   inst_choose_source       (void);
static int   inst_choose_source_cb    (dia_item_t di);
static int   inst_menu_cb             (dia_item_t di);
static int   inst_mount_smb           (void);
static int   inst_do_ftp              (void);
static int   inst_do_http             (void);
static int   inst_get_proxysetup      (void);
static int   inst_do_tftp             (void);
// static int   inst_update_cd           (void);
static int choose_dud(char **dev);
static int dud_probe_cdrom(char **dev);
static int dud_probe_floppy(char **dev);
static void  inst_swapoff             (void);
static void get_file(char *src, char *dst);
static void eval_find_config(void);
static int eval_configure(void);
static void live_show_state(void);
static void save_1st_content_file(char *dir, char *loc);

static dia_item_t di_inst_menu_last = di_none;
static dia_item_t di_inst_choose_source_last = di_none;
static dia_item_t di_inst_choose_netsource_last = di_netsource_nfs;

#if 0
int inst_auto_install()
{
  int rc;

  if(config.manual) return -1;

  switch(config.instmode) {
    case inst_cdrom:
//  case BOOTMODE_CDWITHNET:
      rc = inst_mount_cdrom(1);
      break;

    case inst_smb:
      rc = inst_mount_smb();
      break;

    case inst_nfs:
      rc = inst_mount_nfs();
      break;

    case inst_hd:
      rc = inst_mount_harddisk();
      break;

    case inst_ftp:
      rc = inst_do_ftp();
      break;

    case inst_http:
      rc = inst_do_http();
      break;

    case inst_tftp:
      rc = inst_do_tftp();
      break;

    default:
      rc = -1;
      break;
  }

  if(!rc) rc = inst_check_instsys();

  if(rc) {
    inst_umount();
    return -1;
  }

  config.rescue = 0;

  return inst_start_install();
}
#endif


int inst_start_demo()
{
  int rc, win_old;
  char buf[256];
  FILE *f;

  if(config.manual) {
    if(config.instmode == inst_nfs) {
      rc = inst_mount_nfs();
    }
    else {
      dia_message(txt_get(TXT_INSERT_LIVECD), MSGTYPE_INFOENTER);
      rc = inst_mount_cdrom(1);
    }

    if(rc) return rc;
  }
  else {
    if(config.ask_language || config.ask_keytable) {
      if(!(win_old = config.win)) util_disp_init();
      if(config.ask_language) set_choose_language();
      util_print_banner();
      if(config.ask_keytable) set_choose_keytable(1);
      if(!win_old) util_disp_done();
    }
  }

  sprintf(buf, "%s/%s", mountpoint_tg, config.live.image);

  if(!util_check_exist(buf)) {
    util_disp_init();
    dia_message(txt_get(TXT_RI_NOT_FOUND), MSGTYPE_ERROR);
    inst_umount();
    return -1;
  }

  config.inst_ramdisk = load_image(buf, config.instmode);

  inst_umount();	// what for???, cf. inst_start_rescue()

  if(config.inst_ramdisk < 0) return -1;

  root_set_root(config.ramdisk[config.inst_ramdisk].dev);

  eval_find_config();
  if(eval_configure()) return -1;

  rc = ramdisk_mount(config.inst_ramdisk, config.mountpoint.live);
  if(!rc) mount(0, config.mountpoint.live, 0, MS_MGC_VAL | MS_REMOUNT, 0);

  file_write_install_inf(config.mountpoint.live);
  file_write_live_config(config.mountpoint.live);

  sprintf(buf, "%s/%s", config.mountpoint.live, "etc/fstab");
  f = fopen(buf, "a");

  if(config.instmode == inst_nfs && !*livesrc_tg) {
    sprintf(buf, "%s:%s /S.u.S.E. nfs ro,nolock 0 0\n",
      inet_ntoa(config.net.server.ip),
      config.serverdir ?: ""
    );
  }
  else {
    sprintf(buf, "/dev/%s /S.u.S.E. %s ro 0 0\n",
      *livesrc_tg ? livesrc_tg : config.cdrom,
      *livesrc_tg ? "auto" : "iso9660"
    );
  }

  fprintf(f, buf);
  fclose(f);

  util_umount(config.mountpoint.live);

  return 0;
}


int inst_menu()
{
  dia_item_t di;
  dia_item_t items[] = {
    di_inst_install,
    di_inst_demo,
    di_inst_system,
    di_inst_rescue,
    di_none
  };

  items[config.demo ? 0 : 1] = di_skip;

  di = dia_menu2(txt_get(TXT_MENU_START), 40, inst_menu_cb, items, di_inst_menu_last);

  return di == di_none ? 1 : 0;
}


/*
 * return values:
 * -1    : abort (aka ESC)
 *  0    : ok
 *  other: stay in menu
 */
int inst_menu_cb(dia_item_t di)
{
  int error = 0;
  int rc = 1;

  di_inst_menu_last = di;

  switch(di) {
    case di_inst_install:
      config.rescue = 0;
      error = inst_start_install();
     /*
      * Fall through to the main menu if we return from a failed installation
      * attempt.
      */
      if(config.redraw_menu) rc = -1;
      break;

    case di_inst_demo:
      error = inst_start_demo();
      if(config.redraw_menu) rc = -1;
      break;

    case di_inst_system:
      error = root_boot_system();
      break;

    case di_inst_rescue:
      config.rescue = 1;
      error = inst_start_rescue();
      break;

    default:
      break;
  }

  config.redraw_menu = 0;

  if(!error) rc = 0;

  return rc;
}


int inst_choose_netsource()
{
  dia_item_t di;
  dia_item_t items[] = {
    di_netsource_ftp,
    di_netsource_http,
    di_netsource_nfs,
    di_netsource_smb,
    di_netsource_tftp,
    di_none
  };

  inst_umount();

  config.net.smb_available = config.test || util_check_exist("/bin/smbmount");

  if(!config.net.smb_available) items[3] = di_skip;

  di = dia_menu2(txt_get(TXT_CHOOSE_NETSOURCE), 33, inst_choose_netsource_cb, items, di_inst_choose_netsource_last);

  return di == di_none ? -1 : 0;
}


/*
 * return values:
 * -1    : abort (aka ESC)
 *  0    : ok
 *  other: stay in menu
 */
int inst_choose_netsource_cb(dia_item_t di)
{
  int error = FALSE;

  di_inst_choose_netsource_last = di;

  switch(di) {
    case di_netsource_nfs:
      error = inst_mount_nfs();
      break;

    case di_netsource_smb:
      error = inst_mount_smb();
      break;

    case di_netsource_ftp:
      error = inst_do_ftp();
      break;

    case di_netsource_http:
      error = inst_do_http();
      break;

    case di_netsource_tftp:
      error = inst_do_tftp();
      break;

    default:
      break;
  }

  if(!error) {
    error = inst_check_instsys();
    if(error) dia_message(txt_get(TXT_RI_NOT_FOUND), MSGTYPE_ERROR);
  }

  if(error) inst_umount();

  return error ? 1 : 0;
}


int inst_choose_source()
{
  dia_item_t di;
  dia_item_t items[] = {
    di_source_cdrom,
    di_source_net,
    di_source_hd,
    di_source_floppy,
    di_none
  };

  inst_umount();

  if(!config.rescue) items[3] = di_skip;

  di = dia_menu2(txt_get(TXT_CHOOSE_SOURCE), 33, inst_choose_source_cb, items, di_inst_choose_source_last);

  return di == di_none ? -1 : 0;
}


/*
 * return values:
 * -1    : abort (aka ESC)
 *  0    : ok
 *  other: stay in menu
 */
int inst_choose_source_cb(dia_item_t di)
{
  int error = FALSE;
  char tmp[200];

  di_inst_choose_source_last = di;

  switch(di) {
    case di_source_cdrom:
      error = inst_mount_cdrom(0);
      if(error) {
        sprintf(tmp, txt_get(TXT_INSERT_CD), 1);
        dia_message(tmp, MSGTYPE_INFOENTER);
        error = inst_mount_cdrom(1);
      }
      break;

    case di_source_net:
      error = inst_choose_netsource();
      break;

    case di_source_hd:
      error = inst_mount_harddisk();
      break;

    case di_source_floppy:
      error = inst_check_floppy();
      break;

    default:
      break;
  }

  if(!error && di != di_source_cdrom) {
    error = inst_check_instsys();
    if(error) dia_message(txt_get(TXT_RI_NOT_FOUND), MSGTYPE_ERROR);
  }

  if(error) inst_umount();

  return error ? 1 : 0;
}


int inst_try_cdrom(char *dev)
{
  char buf[64];
  int rc;

  sprintf(buf, "/dev/%s", dev);
  rc = util_mount_ro(buf, config.mountpoint.instdata);

  if(rc) return 1;

  if((rc = inst_check_instsys())) {
    inst_umount();
    return 2;
  }

  return rc;
}


int inst_mount_cdrom(int show_err)
{
  int rc;
  slist_t *sl;
  window_t win;

  if(config.instmode_extra == inst_cdwithnet) {
    rc = net_config();
  }
  else {
    set_instmode(inst_cdrom);

    if(config.net.do_setup && (rc = net_config())) return rc;
  }

  dia_info(&win, txt_get(TXT_TRY_CD_MOUNT));

  rc = 1;

  if(config.cdrom) rc = inst_try_cdrom(config.cdrom);

  if(rc) {
    if(config.cdromdev) rc = inst_try_cdrom(config.cdromdev);
    if(rc) {
      for(sl = config.cdroms; sl; sl = sl->next) {
        if(!(rc = inst_try_cdrom(sl->key))) {
          str_copy(&config.cdrom, sl->key);
          break;
        }
      }
    }
  }

  win_close(&win);

  if(rc) {
    if(show_err) {
      dia_message(txt_get(rc == 2 ? TXT_RI_NOT_FOUND : TXT_ERROR_CD_MOUNT), MSGTYPE_ERROR);
     }
  }

  return rc;
}


int inst_choose_partition(char **partition, int swap, char *txt_menu, char *txt_input)
{
  int i, item_cnt, rc;
  char **items, **values;
  char *type;
  slist_t *sl;
  char buf[256];
  int found = 0;
  static int last_item = 0;
  char *s, *tmp = NULL;

  util_update_disk_list(NULL, 1);
  util_update_swap_list();

  for(i = 0, sl = config.partitions; sl; sl = sl->next) i++;

  /* just max values, actual lists might be shorter */
  items = calloc(i + 2, sizeof *items);
  values = calloc(i + 2, sizeof *values);

  for(item_cnt = 0, sl = config.partitions; sl; sl = sl->next) {
    if(
      sl->key &&
      !slist_getentry(config.swaps, sl->key)		/* don't show active swaps */
    ) {
      sprintf(buf, "/dev/%s", sl->key);
      type = fstype(buf);
      if(type && (!strcmp(type, "swap") ^ !swap)) {
        if(*partition && !strcmp(sl->key, *partition)) found = 1;
        sprintf(buf, "%-12s : %s", sl->key, type);
        values[item_cnt] = strdup(sl->key);
        items[item_cnt++] = strdup(buf);
      }
    }
  }

  if(*partition && !found && item_cnt) {
    sprintf(buf, "/dev/%s", *partition);
    type = fstype(buf);
    sprintf(buf, "%-12s : %s", *partition, type ?: "");
    values[item_cnt] = strdup(*partition);
    items[item_cnt++] = strdup(buf);
  }

  rc = 1;
  if(item_cnt) {
    i = dia_list(txt_menu, 32, NULL, items, last_item, align_left);
    if(i > 0) {
      last_item = i;
      str_copy(partition, values[i - 1]);
      rc = 0;
    }
  }
  else {
    str_copy(&tmp, *partition);
    rc = dia_input2(txt_input, &tmp, 30, 0);
    if(!rc) {
      s = tmp;
      if(tmp && strstr(tmp, "/dev/") == tmp) s = tmp  + sizeof "/dev/" - 1;
      str_copy(partition, s);
      str_copy(&tmp, NULL);
    }
  }

  for(i = 0; i < item_cnt; i++) { free(items[i]); free(values[i]); }
  free(items);
  free(values);

  return rc;
}


int inst_mount_harddisk()
{
  int rc = 0;
  char buf[256];
  char *module;

  set_instmode(inst_hd);

  if(config.net.do_setup && (rc = net_config())) return rc;

  do {
    rc = inst_choose_partition(&config.partition, 0, txt_get(TXT_CHOOSE_PARTITION), txt_get(TXT_ENTER_PARTITION));
    if(rc) return -1;

    if(config.partition) {
      sprintf(buf, "/dev/%s", config.partition);
      util_fstype(buf, &module);
      if(module) mod_modprobe(module, NULL);
      rc = util_mount_ro(buf, config.mountpoint.extra);
    }
    else {
      rc = -1;
    }

    if(rc) {
      dia_message (txt_get(TXT_ERROR_HD_MOUNT), MSGTYPE_ERROR);
    }
    else {
      rc = dia_input2(txt_get(TXT_ENTER_HD_DIR), &config.serverdir, 30, 0);
      util_truncate_dir(config.serverdir);
      sprintf(buf, "%s/%s", config.mountpoint.extra, config.serverdir);
      if(!rc) rc = util_mount_ro(buf, config.mountpoint.instdata);
      if(rc) {
        fprintf(stderr, "doing umount\n");
        inst_umount();
        return rc;
      }
    }
  } while(rc);

  config.extramount = 1;

  return 0;
}


int inst_mount_nfs()
{
  int rc;
  window_t win;
  char text[256];

  set_instmode(inst_nfs);

  if((rc = net_config())) return rc;

  if(config.win) {	/* ###### check really needed? */
    sprintf(text, txt_get(TXT_INPUT_NETSERVER), get_instmode_name_up(config.instmode));
    if((rc = net_get_address(text, &config.net.server, 1))) return rc;
    if((rc = dia_input2(txt_get(TXT_INPUT_DIR), &config.serverdir, 30, 0))) return rc;
  }
  util_truncate_dir(config.serverdir);

  sprintf(text, txt_get(TXT_TRY_NFS_MOUNT), config.net.server.name, config.serverdir);

  dia_info(&win, text);

  system("portmap");

  rc = net_mount_nfs(mountpoint_tg, &config.net.server, config.serverdir);

  win_close(&win);

  return rc;
}


int inst_mount_smb()
{
  int rc;
  window_t win;
  char msg[256];

  set_instmode(inst_smb);

  if((rc = net_config())) return rc;

  if(config.win) {	/* ###### check really needed? */
    sprintf(msg, txt_get(TXT_INPUT_NETSERVER), get_instmode_name_up(config.instmode));
    if((rc = net_get_address(msg, &config.net.server, 1))) return rc;
    if((rc = dia_input2(txt_get(TXT_SMB_ENTER_SHARE), &config.serverdir, 30, 0))) return rc;
  }
  util_truncate_dir(config.serverdir);

  rc = dia_yesno(txt_get(TXT_SMB_GUEST_LOGIN), YES);

  if(rc == ESCAPE) {
    return -1;
  }
  else {
    if(rc == YES) {
      str_copy(&config.net.user, NULL);
      str_copy(&config.net.password, NULL);
      str_copy(&config.net.workgroup, NULL);
    }
    else {
      if((rc = dia_input2(txt_get(TXT_SMB_ENTER_USER), &config.net.user, 20, 0))) return rc;
      if((rc = dia_input2(txt_get(TXT_SMB_ENTER_PASSWORD), &config.net.password, 20, 1))) return rc;
      if((rc = dia_input2(txt_get(TXT_SMB_ENTER_WORKGROUP), &config.net.workgroup, 20, 0))) return rc;
    }
  }

  sprintf(msg, txt_get(TXT_SMB_TRYING_MOUNT), config.net.server.name, config.serverdir);

  dia_info(&win, msg);

  rc = net_mount_smb(mountpoint_tg,
    &config.net.server, config.serverdir,
    config.net.user, config.net.password, config.net.workgroup
  );

  win_close(&win);

  if(rc) return -1;

  return 0;
}


int inst_check_instsys()
{
  char filename[MAX_FILENAME];

  switch(config.instmode) {
    case inst_floppy:
      config.use_ramdisk = 1;
      config.instdata_mounted = 0;

      strcpy(inst_rootimage_tm, config.floppies ? config.floppy_dev[config.floppy] : "/dev/fd0");
      break;

    case inst_hd:
    case inst_cdrom:
    case inst_nfs:
    case inst_smb:
      config.use_ramdisk = 0;
      config.instdata_mounted = 1;

      save_1st_content_file(config.mountpoint.instdata, get_instmode_name(config.instmode));
      util_chk_driver_update(config.mountpoint.instdata, get_instmode_name(config.instmode));
      util_do_driver_updates();

      sprintf(filename, "%s%s", config.mountpoint.instdata, config.installdir);
      if(config.rescue || !util_is_dir(filename)) {
        sprintf(filename, "%s%s",
          config.mountpoint.instdata,
          config.demo ? config.live.image : config.rescue ? config.rescueimage : config.rootimage
        );
      }

#if 0
      deb_int(config.rescue);
      deb_int(force_ri_ig);
      deb_int(util_is_mountable(filename));
      deb_int(util_is_dir(filename));
#endif

      if(
        (config.rescue || force_ri_ig || !util_is_mountable(filename)) &&
        !util_is_dir(filename)
      ) {
        config.use_ramdisk = 1;
      }
      strcpy(inst_rootimage_tm, filename);
      
      // TODO: handle demo image!

      // deb_int(config.use_ramdisk);

      break;

    case inst_ftp:
    case inst_http:
    case inst_tftp:
      config.use_ramdisk = 1;
      config.instdata_mounted = 0;

      sprintf(inst_rootimage_tm, "%s%s",
        config.serverdir ?: "",
        config.rescue ? config.rescueimage : config.rootimage
      );
      break;

    default:
      break;
  }

  if(
    config.instmode != inst_ftp &&
    config.instmode != inst_http &&
    config.instmode != inst_tftp &&
    config.use_ramdisk &&
    !util_check_exist(inst_rootimage_tm)
  ) {
    return -1;
  }

  return 0;
}


int inst_start_install()
{
  int i, rc, update_rd;
  char *buf = NULL;

  if(config.manual) {
    if((rc = inst_choose_source())) return rc;
  }
  else {
    fprintf(stderr, "going for automatic install\n");
  }

  str_copy(&config.instsys, NULL);

  if(config.use_ramdisk) {
    config.inst_ramdisk = load_image(inst_rootimage_tm, config.instmode);
    // maybe: inst_umount(); ???
    if(config.inst_ramdisk < 0) return -1;

    if(config.rescue) {
      root_set_root(config.ramdisk[config.inst_ramdisk].dev);

      return 0;
    }

    rc = ramdisk_mount(config.inst_ramdisk, config.mountpoint.instsys);
    if(rc) return rc;
    str_copy(&config.instsys, config.mountpoint.instsys);
  }
  else if(!util_is_dir(inst_rootimage_tm)) {
    rc = util_mount_ro(inst_rootimage_tm, config.mountpoint.instsys);
    if(rc) return rc;
    str_copy(&config.instsys, config.mountpoint.instsys);
  }
  else {
    strprintf(&buf, "%s%s", config.mountpoint.instdata, config.installdir);
    str_copy(&config.instsys, buf);
  }

  /* load some extra files, if they exist */
  get_file("/content", "/content");
  get_file("/media.1/info.txt", "/info.txt");
  get_file("/part.info", "/part.info");
  get_file("/control.xml", "/control.xml");

  /* look for driver update image; load and apply it */
  i = 1;
  if(
    config.instmode == inst_ftp ||
    config.instmode == inst_http ||
    config.instmode == inst_tftp
  ) {
    strprintf(&buf, "%s/driverupdate", config.serverdir ?: "");
  }
  else {
    strprintf(&buf, "%s/driverupdate", config.mountpoint.instdata);
    /* look for it in advance */
    i = util_check_exist(buf);
  }

  if(i) {
    config.noerrors = 1;
    update_rd = load_image(buf, config.instmode);
    config.noerrors = 0;

    if(update_rd >= 0) {
      i = ramdisk_mount(update_rd, config.mountpoint.update);
      if(!i) util_chk_driver_update(config.mountpoint.update, get_instmode_name(config.instmode));
      ramdisk_free(update_rd);
      if(!i) util_do_driver_updates();
    }
  }

  free(buf);

  util_splash_bar(60);

  return inst_execute_yast();
}


/* we might as well just use inst_start_install() instead... */
int inst_start_rescue()
{
  int rc;

  if((rc = inst_choose_source())) return rc;

  config.inst_ramdisk = load_image(inst_rootimage_tm, config.instmode);

  inst_umount();	// what for???

  if(config.inst_ramdisk >= 0) {
    root_set_root(config.ramdisk[config.inst_ramdisk].dev);
  }

  util_debugwait("rescue system loaded");

  return config.inst_ramdisk < 0 ? -1 : 0;
}


#if 0
/*
 * Do some basic preparations before we can run programs from the
 * installation system. More must be done later in config.setupcmd.
 *
 * Note: the instsys must already be mounted at this point.
 *
 */
int inst_prepare()
{
  char *links[] = { "/bin", "/lib", "/lib64", "/sbin", "/usr" };
  char link_source[MAX_FILENAME], buf[256];
  int i = 0, rc = 0;
  struct dirent *de;
  DIR *d;

  mod_free_modules();
  if(!config.initrd_has_ldso && !config.test) {
    rename("/bin", "/.bin");
  }

  setenv("INSTSYS", config.instsys, TRUE);

  if(config.term) setenv("TERM", config.term, TRUE);

  util_free_mem();
  if(config.memory.current - config.memory.free_swap < config.memory.min_modules) {
    putenv("REMOVE_MODULES=1");
  }
  else {
    unsetenv("REMOVE_MODULES");
  }

  if(!config.initrd_has_ldso && !config.test)
    for(i = 0; (unsigned) i < sizeof links / sizeof *links; i++) {
      if(!util_check_exist(links[i])) {
	unlink(links[i]);
	sprintf(link_source, "%s%s", config.instsys, links[i]);
	symlink(link_source, links[i]);
      }
    }

  setenv("PATH", "/lbin:/bin:/sbin:/usr/bin:/usr/sbin:/usr/lib/YaST2/bin", TRUE);

  if(config.serial) {
    setenv("TERM", "vt100", TRUE);
    setenv("ESCDELAY", "1100", TRUE);
  }
  else {
    setenv("TERM", "linux", TRUE);
    setenv("ESCDELAY", "10", TRUE);
  }

  setenv("YAST_DEBUG", "/debug/yast.debug", TRUE);

  file_write_install_inf("");
  if(!config.test) {
    // file_write_mtab();
    system("rm /etc/mtab 2>/dev/null; cat /proc/mounts >/etc/mtab");

    /*
     * In these cases, part of the "/suse" tree is in the installation
     * system. We add some symlinks needed by YaST2 to make it available
     * below config.mountpoint.instdata.
     */
    if(
      config.instmode == inst_ftp ||
      config.instmode == inst_tftp ||
      config.instmode == inst_http
    ) {
      if((d = opendir(config.mountpoint.instsys))) {
        while((de = readdir(d))) {
          if(
            strstr(de->d_name, ".S.u.S.E") == de->d_name ||
            !strcmp(de->d_name, config.product_dir)
          ) {
            sprintf(buf, "%s/%s", config.mountpoint.instdata, de->d_name);
            unlink(buf);
            if(!util_check_exist(buf)) {
              sprintf(link_source, "%s/%s", config.mountpoint.instsys, de->d_name);
              symlink(link_source, buf);
            }
          }
        }
        closedir(d);
      }
    }
  }

  return rc;
}
#endif


int add_instsys()
{
  char link_source[MAX_FILENAME], buf[256];
  int rc = 0;
  struct dirent *de;
  DIR *d;
  char *argv[3] = { };

  mod_free_modules();

  util_free_mem();
  if(config.memory.current - config.memory.free_swap < config.memory.min_modules) {
    putenv("REMOVE_MODULES=1");
  }
  else {
    unsetenv("REMOVE_MODULES");
  }

  setenv("INSTSYS", config.instsys, 1);

  setenv("TERM", config.term ?: config.serial ? "vt100" : "linux", 1);

  setenv("ESCDELAY", config.serial ? "1100" : "10", 1);

  setenv("YAST_DEBUG", "/debug/yast.debug", 1);

  file_write_install_inf("");
  if(!config.test) {
    // file_write_mtab();
    system("rm /etc/mtab 2>/dev/null; cat /proc/mounts >/etc/mtab");

    /*
     * In these cases, part of the "/suse" tree is in the installation
     * system. We add some symlinks needed by YaST2 to make it available
     * below config.mountpoint.instdata.
     */
    if(
      config.instmode == inst_ftp ||
      config.instmode == inst_tftp ||
      config.instmode == inst_http
    ) {
      if((d = opendir(config.mountpoint.instsys))) {
        while((de = readdir(d))) {
          if(
            strstr(de->d_name, ".S.u.S.E") == de->d_name ||
            !strcmp(de->d_name, config.product_dir)
          ) {
            sprintf(buf, "%s/%s", config.mountpoint.instdata, de->d_name);
            unlink(buf);
            if(!util_check_exist(buf)) {
              sprintf(link_source, "%s/%s", config.mountpoint.instsys, de->d_name);
              symlink(link_source, buf);
            }
          }
        }
        closedir(d);
      }
    }
  }

  argv[1] = config.instsys;
  argv[2] = "/";
  util_lndir_main(3, argv);

  return rc;
}


void inst_yast_done()
{
  int count;

  lxrc_set_modprobe("/etc/nothing");

  lxrc_killall(0);
  inst_umount();

  util_debugwait("going to umount inst-sys");

  /* wait a bit */
  for(count = 5; inst_umount() == EBUSY && count--;) sleep(1);

  util_debugwait("inst-sys umount done");

  ramdisk_free(config.inst_ramdisk);
  config.inst_ramdisk = -1;

  find_shell();
}


int inst_execute_yast()
{
  int i, rc;
  char cmd[256];

//  rc = inst_prepare();
  rc = add_instsys();
  if(rc) return rc;

  if(!config.test) {
    lxrc_set_modprobe("/sbin/modprobe");
    if(util_check_exist("/sbin/update")) system("/sbin/update");
  }

  i = 0;
  util_free_mem();
  if(config.addswap) {
    i = ask_for_swap(
      (config.memory.min_yast_text - config.memory.min_free) << 10,
      txt_get(TXT_LOW_MEMORY2)
    );
  }

  if(i == -1) {
    inst_yast_done();
    return -1;
  }

  util_free_mem();
  if(config.addswap && config.memory.current < config.memory.min_yast) {
    config.textmode = 1;
    file_write_install_inf("");
  }

  if(i) {
    inst_yast_done();
    return -1;
  }

  if (!config.test && config.usessh && config.net.sshpassword) {
    FILE *passwd;

    /* symlink to ro medium, but we need to overwrite them. */  
    unlink("/etc/passwd");
    unlink("/etc/shadow");
    sprintf(cmd, "cp %s/etc/shadow /etc", config.instsys);system(cmd);
    sprintf(cmd, "cp %s/etc/passwd /etc", config.instsys);system(cmd);
    sprintf(cmd, "%s/etc/pam.d", config.instsys);symlink(cmd,"/etc/pam.d");

 
    passwd = popen("/usr/sbin/chpasswd","w");
    if (passwd) {
      fprintf(passwd,"root:%s\n",config.net.sshpassword);
      pclose(passwd);
    }
  }

  /* start shells only _after_ the swap dialog */
  if(!config.test && !config.noshell) {
    util_start_shell("/dev/tty2", "/bin/bash", 1);
    if(config.memory.current >= config.memory.min_yast) {
      util_start_shell("/dev/tty5", "/bin/bash", 1);
      util_start_shell("/dev/tty6", "/bin/bash", 1);
    }
  }

  disp_set_color(COL_WHITE, COL_BLACK);
  if(config.win) util_disp_done();

  if(config.splash && config.textmode) system("echo 0 >/proc/splash");

  fprintf(stderr, "starting %s\n", config.setupcmd);

  kbd_end();
  util_notty();

  if(config.test) {
    rc = system("/bin/bash 2>&1");
  }
  else {
    rc = system(config.setupcmd);
  }

  if(rc) {
    if(rc == -1) {
      rc = errno;
    }
    else if(WIFEXITED(rc)) {
      rc = WEXITSTATUS(rc);
    }
  }

  freopen(config.console, "r", stdin);
  freopen(config.console, "a", stdout);
  freopen(config.stderr_name, "a", stderr);
  kbd_init(0);
  util_notty();

  if(config.splash && config.textmode) system("echo 1 >/proc/splash");

  fprintf(stderr, "install program exit code is %d\n", rc);

  /* Redraw erverything and go back to the main menu. */
  config.redraw_menu = 1;

  fprintf(stderr, "sync...");
  sync();
  fprintf(stderr, " ok\n");

  util_debugwait("going to read yast.inf");

  i = file_read_yast_inf();
  if(!rc) rc = i;

  disp_cursor_off();
  kbd_reset();

  if(rc || config.aborted) {
    config.rescue = 0;
    config.manual |= 1;
  }

  if(config.manual) util_disp_init();

  if(rc && config.win) {
    dia_message(txt_get(TXT_ERROR_INSTALL), MSGTYPE_ERROR);
  }

  if(!config.test) {
    /* never trust yast */
    mount(0, "/", 0, MS_MGC_VAL | MS_REMOUNT, 0);
  }

  /* turn off swap */
  inst_swapoff();

  inst_yast_done();

  if(config.aborted) {
    config.aborted = 0;
    rc = -1;
  }

  if(!rc) {
    rc = inst_commit_install();
    if(rc) {
      config.rescue = 0;
      config.manual |= 1;
      util_disp_init();
    }
  }

  return rc;
}


/*
 * Look for a usable (aka with medium) floppy drive.
 *
 * return: 0: ok, < 0: failed
 */
// ####### We should make sure the floppy has the rescue system on it!
int inst_check_floppy()
{
  int i, fd = -1;
  char *s;

  set_instmode(inst_floppy);

  i = dia_message(txt_get(TXT_INSERT_DISK), MSGTYPE_INFOENTER);
  if(i) return i;

  for(i = -1; i < config.floppies; i++) {
    /* try last working floppy first */
    if(i == config.floppy) continue;
    s = config.floppy_dev[i == -1 ? config.floppy : i];
    if(!s) continue;
    fd = open(s, O_RDONLY);
    if(fd < 0) continue;
    config.floppy = i == -1 ? config.floppy : i;
    break;
  }

  if(fd < 0)
    dia_message(txt_get(TXT_ERROR_READ_DISK), MSGTYPE_ERROR);
  else
    close(fd);

  return fd < 0 ? fd : 0;
}


int inst_commit_install()
{
  int err = 0;

  if(reboot_ig == 2) {
    reboot(RB_POWER_OFF);
  }
  else if(reboot_ig) {

    if(config.rebootmsg) {
      disp_clear_screen();
      util_disp_init();
      dia_message(txt_get(TXT_DO_REBOOT), MSGTYPE_INFO);
    }

    if(config.test) {
      fprintf(stderr, "*** reboot ***\n");
    }
    else {
#if	defined(__s390__)
      reboot(RB_POWER_OFF);
#else
      reboot(RB_AUTOBOOT);
#endif
    }
    err = -1;
  }

  return err;
}


int inst_umount()
{
  int i = 0, j;

  j = util_umount(config.mountpoint.instsys);
  if(j == EBUSY) i = EBUSY;

  j = util_umount(config.mountpoint.instdata);
  if(j == EBUSY) i = EBUSY;

  if(config.extramount) {
    j = util_umount(config.mountpoint.extra);
    if(j == EBUSY) i = EBUSY;
    config.extramount = 0;
  }

  return i;
}


int inst_do_ftp()
{
  int rc;
  window_t win;
  char buf[256];

  set_instmode(inst_ftp);

  if((rc = net_config())) return rc;

  do {
    sprintf(buf, txt_get(TXT_INPUT_NETSERVER), get_instmode_name_up(config.instmode));
    if((rc = net_get_address(buf, &config.net.server, 1))) return rc;
    if((rc = inst_get_proxysetup())) return rc;

    sprintf(buf, txt_get(TXT_TRY_REACH_SERVER), get_instmode_name_up(config.instmode));
    dia_info(&win, buf);
    rc = net_open(NULL);
    win_close(&win);

    if(rc < 0) {
      util_print_net_error();
    }
    else {
      rc = 0;
    }
  }
  while(rc);

  if(dia_input2(txt_get(TXT_INPUT_DIR), &config.serverdir, 30, 0)) return -1;
  util_truncate_dir(config.serverdir);

  return 0;
}


int inst_do_http()
{
  int rc;
  window_t win;
  char buf[256];

  set_instmode(inst_http);

  if((rc = net_config())) return rc;

  do {
    sprintf(buf, txt_get(TXT_INPUT_NETSERVER), get_instmode_name_up(config.instmode));
    if((rc = net_get_address(buf, &config.net.server, 1))) return rc;
    if((rc = inst_get_proxysetup())) return rc;

    sprintf(buf, txt_get(TXT_TRY_REACH_SERVER), get_instmode_name_up(config.instmode));
    dia_info(&win, buf);
    rc = net_open(NULL);
    win_close(&win);

    if(rc < 0) {
      util_print_net_error();
    }
    else {
      rc = 0;
    }
  }
  while(rc);

  if(dia_input2(txt_get(TXT_INPUT_DIR), &config.serverdir, 30, 0)) return -1;
  util_truncate_dir(config.serverdir);

  return 0;
}


int inst_get_proxysetup()
{
  int rc;
  char *s, tmp[256], buf[256];
  unsigned u;

  if(config.instmode == inst_ftp) {
    if(config.instmode == inst_ftp) {
      strcpy(buf, txt_get(TXT_ANONYM_FTP));
    }
    else {
      sprintf(buf,
        "Do you need a username and password to access the %s server?",
        get_instmode_name_up(config.instmode)
      );
    }
    rc = dia_yesno(buf, NO);
    if(rc == ESCAPE) return -1;

    if(rc == NO) {
      str_copy(&config.net.user, NULL);
      str_copy(&config.net.password, NULL);
    }
    else {
      sprintf(buf, txt_get(TXT_ENTER_USER), get_instmode_name_up(config.instmode));
      if((rc = dia_input2(buf, &config.net.user, 20, 0))) return rc;
      sprintf(buf, txt_get(TXT_ENTER_PASSWORD), get_instmode_name_up(config.instmode));
      if((rc = dia_input2(buf, &config.net.password, 20, 1))) return rc;
    }
  }

  sprintf(buf, txt_get(TXT_WANT_PROXY), get_instmode_name_up(config.net.proxyproto));
  rc = dia_yesno(buf, NO);
  if(rc == ESCAPE) return -1;

  if(rc == YES) {
    sprintf(buf, txt_get(TXT_ENTER_PROXY), get_instmode_name_up(config.net.proxyproto));
    if((rc = net_get_address(buf, &config.net.proxy, 1))) return rc;

    *tmp = 0;
    if(config.net.proxyport) sprintf(tmp, "%u", config.net.proxyport);

    do {
      sprintf(buf, txt_get(TXT_ENTER_PROXYPORT), get_instmode_name_up(config.net.proxyproto));
      rc = dia_input(buf, tmp, 6, 6, 0);
      if(rc) return rc;
      u = strtoul(tmp, &s, 0);
      if(*s) {
        rc = -1;
        dia_message(txt_get(TXT_INVALID_INPUT), MSGTYPE_ERROR);
      }
    }
    while(rc);

    config.net.proxyport = u;
  }
  else {
    name2inet(&config.net.proxy, "");
    config.net.proxyport = 0;
  }

  return 0;
}


int inst_do_tftp()
{
  int rc;
  window_t win;
  char buf[256];

  set_instmode(inst_tftp);

  config.net.proxyport = 0;

  if((rc = net_config())) return rc;

  do {
    sprintf(buf, txt_get(TXT_INPUT_NETSERVER), get_instmode_name_up(config.instmode));
    if((rc = net_get_address(buf, &config.net.server, 1))) return rc;

    sprintf(buf, txt_get(TXT_TRY_REACH_SERVER), get_instmode_name_up(config.instmode));
    dia_info(&win, buf);
    rc = net_open(NULL);
    win_close(&win);

    if(rc < 0) {
      util_print_net_error();
    }
    else {
      rc = 0;
    }
  }
  while(rc);

  if(dia_input2(txt_get(TXT_INPUT_DIR), &config.serverdir, 30, 0)) return -1;
  util_truncate_dir(config.serverdir);

  return 0;
}


/*
 * Ask for and apply driver update.
 *
 * return values:
 *  0    : ok
 *  1    : abort
 */
int inst_update_cd()
{
  int  i;
  char *dev;
  unsigned old_count;
  slist_t **names;
  window_t win;

  config.update.shown = 1;

  while(choose_dud(&dev));

  if(!dev) return 1;

  /* ok, mount it */
  i = util_mount_ro(long_dev(dev), config.mountpoint.update);

  if(i) {
    dia_message("Failed to access Driver Update medium.", MSGTYPE_ERROR);
    return 0;
  }

  old_count = config.update.count;

  /* point at list end */
  for(names = &config.update.name_list; *names; names = &(*names)->next);

  dia_info(&win, "Reading Driver Update...");

  util_chk_driver_update(config.mountpoint.update, dev);

  util_umount(config.mountpoint.update);

  util_do_driver_updates();

  win_close(&win);

  if(old_count == config.update.count) {
    dia_message("No new Driver Updates found", MSGTYPE_INFO);
  }
  else {
    if(*names) {
      dia_show_lines2("Driver Updates added", *names, 64);
    }
    else {
      dia_message("Driver Update ok", MSGTYPE_INFO);
    }
  }

  return 0;
}


/*
 * Let user enter a device for driver updates
 * (*dev = NULL if she changed her mind).
 *
 * return values:
 *  0    : ok
 *  other: call choose_dud() again
 */
int choose_dud(char **dev)
{
  int i, j, item_cnt, last_item, restart = 0, probe;
  char *s, *s1, *buf = NULL, **items, **values;
  slist_t *sl;

  *dev = NULL;

  for(i = 4 /* max 4 floppies */, sl = config.cdroms; sl; sl = sl->next) i++;

  /* just max values, actual lists might be shorter */
  items = calloc(i + 5, sizeof *items);
  values = calloc(i + 5, sizeof *values);

  item_cnt = 0;

  if(config.floppies && config.floppy_probed) {
    for(i = 0; i < config.floppies; i++) {
      s = short_dev(config.floppy_dev[i]);
      strprintf(&buf, "%s (floppy)", s);
      values[item_cnt] = strdup(s);
      items[item_cnt++] = strdup(buf);
    }
  }
  else {
    values[item_cnt] = strdup(" floppy");
    items[item_cnt++] = strdup("floppy");
  }

  if(config.cdroms) {
    for(sl = config.cdroms; sl; sl = sl->next) {
      if(sl->key) {
        strprintf(&buf, "%s (cdrom)", sl->key);
        values[item_cnt] = strdup(sl->key);
        items[item_cnt++] = strdup(buf);
      }
    }
  }
  else {
    values[item_cnt] = strdup(" cdrom");
    items[item_cnt++] = strdup("cdrom");
  }

  last_item = 0;

  if(config.update.dev) {
    for(i = 0; i < item_cnt; i++) {
      if(values[i] && !strcmp(values[i], config.update.dev)) {
        last_item = i + 1;
        break;
      }
    }

    if(!last_item) {
      values[item_cnt] = strdup(config.update.dev);
      items[item_cnt++] = strdup(config.update.dev);
      last_item = item_cnt;
    }
  }

  values[item_cnt] = NULL;
  items[item_cnt++] = strdup("other");

  i = dia_list("Please choose the Driver Update medium.", 30, NULL, items, last_item, align_left);
  if(i > 0) {
    s = values[i - 1];
    if(s) {
      probe = 0;
      if(!strcmp(s, " floppy")) {
        probe = 1;
      }
      else if(!strcmp(s, " cdrom")) {
        probe = 2;
      }

      if(probe) {
        j = probe == 1 ? dud_probe_floppy(&s1) : dud_probe_cdrom(&s1);
        if(j) {
          str_copy(&config.update.dev, s1);
          *dev = config.update.dev;
        }
        if(j != 1) restart = 1;
      }
      else {
        str_copy(&config.update.dev, values[i - 1]);
        *dev = config.update.dev;
      }
    }
    else {
      str_copy(&buf, NULL);
      i = dia_input2("Please enter the Driver Update device.", &buf, 30, 0);
      if(!i) {
        if(util_check_exist(long_dev(buf)) == 'b') {
          str_copy(&config.update.dev, short_dev(buf));
          *dev = config.update.dev;
        }
        else {
          dia_message("Invalid device name.", MSGTYPE_ERROR);
        }
      }
    }
  }

  for(i = 0; i < item_cnt; i++) { free(items[i]); free(values[i]); }
  free(items);
  free(values);

  free(buf);

  // fprintf(stderr, "dud dev = %s\n", *dev);

  return restart;
}


/*
 * Look for cdrom drives. Returns number of devices found (0, 1, many).
 */
int dud_probe_cdrom(char **dev)
{
  window_t win;
  int cnt = 0;

  if(dev) *dev = NULL;

  dia_info(&win, "Looking for cdrom drives...");

  util_update_cdrom_list();

  if(!config.cdroms) {
    load_storage_mods();
    util_update_cdrom_list();		/* probably not needed */
  }

  if(!config.cdroms) {
    win_close(&win);
    dia_message("No cdrom drives found.", MSGTYPE_ERROR);
  }
  else {
    win_close(&win);
    if(dev)  *dev = config.cdroms->key;
    cnt = config.cdroms->next ? 2 : 1;
  }
 
  return cnt;
}


/*
 * Look for floppy disks. Returns number of disks found.
 */
int dud_probe_floppy(char **dev)
{
  window_t win;
  int cnt = 0;

  if(dev) *dev = NULL;

  dia_info(&win, "Looking for floppy disks...");

  if(!auto2_find_floppy()) {
    win_close(&win);
    dia_message("No floppy disk found.", MSGTYPE_ERROR);
  }
  else {
    win_close(&win);
    if(dev) *dev = short_dev(config.floppy_dev[0]);
    cnt = config.floppies;
  }

  return cnt;
}


void inst_swapoff()
{
  slist_t *sl;
  char buf[64];

  util_update_swap_list();

  if(config.test) return;

  for(sl = config.swaps; sl; sl = sl->next) {
    sprintf(buf, "/dev/%s", sl->key);
    fprintf(stderr, "swapoff %s\n", buf);
    swapoff(buf);
  }
}


void get_file(char *src, char *dst)
{
  char buf[0x1000];
  char fname[256];
  int i, j, fd_in, fd_out;

  if(
    config.instmode == inst_ftp ||
    config.instmode == inst_http ||
    config.instmode == inst_tftp
  ) {
    sprintf(fname, "%s%s", config.serverdir ?: "", src);
    fd_in = net_open(fname);
    if(fd_in >= 0) {
      fd_out = open("/xxx.tmp", O_WRONLY | O_CREAT | O_TRUNC, 0644);
      if(fd_out >= 0) {
        do {
          i = net_read(fd_in, buf, sizeof buf);
          j = 0;
          if(i > 0) j = write(fd_out, buf, i);
        }  
        while(i > 0 && j > 0);
        close(fd_out);
        if(!(i < 0 || j < 0)) {
          unlink(dst);
          rename("/xxx.tmp", dst);
        }
      }
      net_close(fd_in);
    }
  }
  else if(
    config.instmode == inst_hd ||
    config.instmode == inst_cdrom ||
    config.instmode == inst_nfs ||
    config.instmode == inst_smb
  ) {
    /* copy content file */
    sprintf(fname, "%s%s", config.mountpoint.instdata, src);
    if(util_check_exist(fname)) {
      char *argv[3];

      unlink(dst);
      argv[1] = fname;
      argv[2] = "/";
      util_cp_main(3, argv);
    }
  }
}


void eval_find_config()
{
  hd_data_t *hd_data;
  hd_t *hd, *hd1;
  char *type, buf[256], buf2[256], *module;
  struct statfs statfs_buf;
  unsigned u0, u1;
  int cfg_found = 0, len = 0;
  slist_t *sl;

  if(config.live.nodisk) return;

  printf(config.live.newconfig ? "Checking partitions...\n" : "Looking for LiveEval config...\n");

  hd_data = calloc(1, sizeof *hd_data);

  hd = hd_list(hd_data, hw_partition, 1, NULL);

  for(hd1 = hd; hd1; hd1 = hd1->next) {
    if(
      !hd1->unix_dev_name ||
      strncmp(hd1->unix_dev_name, "/dev/", sizeof "/dev/" - 1)
    ) continue;
    type = util_fstype(hd1->unix_dev_name, &module);
    if(!type || !strcmp(type, "ntfs")) continue;
    if(len) printf("\r%*s\r", len, "");
    len = printf("%s: %s", hd1->unix_dev_name, type);
    fflush(stdout);

    if(!strcmp(type, "swap")) {
      slist_append_str(&config.live.swaps, hd1->unix_dev_name + sizeof "/dev/" - 1);
    }
    else {
      mod_modprobe(module, NULL);
      if(!util_mount_ro(hd1->unix_dev_name, config.mountpoint.live)) {
        sprintf(buf, "%s/%s", config.mountpoint.live, config.live.cfg);
        if(util_check_exist(buf)) {
          if(!config.partition) {
            str_copy(&config.partition, hd1->unix_dev_name + sizeof "/dev/" - 1);
          }
          if(!config.live.newconfig) cfg_found = 1;
        }
        if(!statfs(config.mountpoint.live, &statfs_buf)) {
          u0 = ((uint64_t) statfs_buf.f_bsize * statfs_buf.f_blocks) >> 20;
          u1 = ((uint64_t) statfs_buf.f_bsize * statfs_buf.f_bfree) >> 20;
          if(u1 >= 256) {
            sprintf(buf2,
              "%s, %u%cB (%u%cB free)",
              type,
              u0 >= 10240 ? u0 >> 10 : u0,
              u0 >= 10240 ? 'G' : 'M',
              u1 >= 10240 ? u1 >> 10 : u1,
              u1 >= 10240 ? 'G' : 'M'
            );
            sl = slist_append_str(&config.live.partitions, hd1->unix_dev_name + sizeof "/dev/" - 1);
            str_copy(&sl->value, buf2);
          }
        }
        util_umount(config.mountpoint.live);
      }
    }
    if(cfg_found) break;
  }

  if(!cfg_found) config.live.newconfig = 1;

  if(len) printf("\r%*s\r", len, "");

  hd_free_hd_data(hd_data);
  free(hd_data);
}


int eval_configure()
{
  int win_old = 1, cnt, i, j, width, pwidth;
  slist_t *sl;
  char **items, *s, buf[256];

  if(config.live.autopart && !config.partition) {
    if(config.live.partitions) {
      str_copy(&config.partition, config.live.partitions->key);
    }
    else {
      config.live.nodisk = 1;
    }
  }

  if(config.live.autoswap && !config.live.nodisk && !config.live.useswap) {
    if(config.live.swaps) {
      slist_append_str(&config.live.useswap, config.live.swaps->key);
    }
    else {
      config.live.swapfile = 1;
    }
  }

  live_show_state();

  for(cnt = 0, pwidth = 4, sl = config.live.partitions; sl; sl = sl->next) {
    j = strlen(sl->key);
    if(j > pwidth) pwidth = j;
    cnt++;
  }

  if(!config.live.nodisk && !config.partition) {
    if(!cnt) {
      if(!(win_old = config.win)) util_disp_init();
      dia_message("Sorry, no partition found, your config data will not be saved.", MSGTYPE_INFO);
      if(!win_old) util_disp_done();
      config.live.nodisk = 1;
      config.live.newconfig = 1;
      str_copy(&config.partition, NULL);
      return 0;
    }
    else {
      if(!(win_old = config.win)) util_disp_init();

      items = calloc(cnt + 2, sizeof *items);

      s = "don't save config data";
      str_copy(&items[0], s);
      width = strlen(s) + 1;

      for(i = 1, sl = config.live.partitions; sl; sl = sl->next) {
        j = sprintf(buf, "%-*s: %s", pwidth, sl->key, sl->value);
        if(j > width) width = j;
        str_copy(&items[i++], buf);
      }

      j = dia_list("Where do you want config data to be saved?", width + 1, NULL, items, 2, align_left);

      for(i = 0; i < cnt + 1; i++) { free(items[i]); }
      free(items);

      if(j >= 2) {
        for(i = 2, sl = config.live.partitions; sl; sl = sl->next, i++) {
          if(i == j) {
            str_copy(&config.partition, sl->key);
          }
        }
      }
      else {
        config.live.newconfig = 1;
        str_copy(&config.partition, NULL);
      }
    }
  }

  if(
    config.live.newconfig &&
    !config.live.nodisk &&
    !config.live.useswap &&
    (config.live.swaps || config.partition)
  ) {
    if(!config.win && !(win_old = config.win)) util_disp_init();

    for(cnt = 0, sl = config.live.swaps; sl; sl = sl->next) cnt++;

    items = calloc(cnt + 3, sizeof *items);

    s = "no swap space";
    str_copy(&items[0], s);
    width = strlen(s) + 1;

    s = "create swapfile";
    str_copy(&items[1], s);
    i = strlen(s) + 1;
    if(i > width) width = i;

    for(i = 2, sl = config.live.swaps; sl; sl = sl->next) {
      j = sprintf(buf, "%s", sl->key);
      if(j > width) width = j;
      str_copy(&items[i++], buf);
    }

    j = dia_list(
      "Do you want to use swap space?",
      width + 1, NULL, items,
      config.partition ? cnt ? 3 : 2 : 1,
      align_left
    );

    for(i = 0; i < cnt + 2; i++) { free(items[i]); }
    free(items);

    if(j >= 3) {
      for(i = 3, sl = config.live.swaps; sl; sl = sl->next, i++) {
        if(i == j) {
          slist_append_str(&config.live.useswap, sl->key);
        }
      }
    }
    else if(j == 2) {
      config.live.swapfile = 1;
    }
  }

  if(!config.partition) config.live.nodisk = 1;

  if(!win_old) util_disp_done();

  live_show_state();

  return 0;
}


void live_show_state()
{
  slist_t *sl;

  if(!config.debug) return;

  printf("\n");

  printf(
    "newconfig = %u, nodisk = %u, swapfile = %u, autopart = %u, autoswap = %u\n",
    config.live.newconfig, config.live.nodisk, config.live.swapfile,
    config.live.autopart, config.live.autoswap
  );

  if(config.partition) {
    printf("partition = %s\n", config.partition);
  }

  for(sl = config.live.swaps; sl; sl = sl->next) {
    printf("swap found: %s\n", sl->key);
  }

  for(sl = config.live.useswap; sl; sl = sl->next) {
    printf("swap to use: %s\n", sl->key);
  }

  for(sl = config.live.partitions; sl; sl = sl->next) {
    printf("%s: %s\n", sl->key, sl->value);
  }

  getchar();
}


/*
 * get content file from first medium we see
 */
void save_1st_content_file(char *dir, char *loc)
{
  char *buf = NULL, *argv[3];

  if(dir && loc && !util_check_exist("/tmp/content")) {
    strprintf(&buf, "%s/content", dir);
    if(util_check_exist(buf) == 'r') {
      fprintf(stderr, "1st content file: %s:%s\n", loc, dir);
      argv[1] = buf;
      argv[2] = "/tmp";
      util_cp_main(3, argv);
    }
    free(buf);
  }
}


