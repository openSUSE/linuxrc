/*
 *
 * install.c           Handling of installation
 *
 * Copyright (c) 1996-2002  Hubert Mantel, SuSE Linux AG  (mantel@suse.de)
 *
 */

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
static int   add_instsys              (void);
static void  inst_yast_done           (void);
static int   inst_execute_yast        (void);
static int   inst_commit_install      (void);
static int   inst_choose_netsource    (void);
static int   inst_choose_netsource_cb (dia_item_t di);
#if defined(__s390__) || defined(__s390x__)
static int   inst_choose_display      (void);
static int   inst_choose_display_cb   (dia_item_t di);
#endif
static int   inst_choose_source       (void);
static int   inst_choose_source_cb    (dia_item_t di);
static int   inst_menu_cb             (dia_item_t di);
static int   inst_mount_smb           (void);
static int   inst_do_ftp              (void);
static int   inst_do_http             (void);
static int   inst_get_proxysetup      (void);
static int   inst_do_tftp             (void);
static int choose_dud(char **dev);
static void  inst_swapoff             (void);
static void get_file(char *src, char *dst);
static void eval_find_config(void);
static int eval_configure(void);
static void live_show_state(void);
static void read_install_files(void);

static dia_item_t di_inst_menu_last = di_none;
static dia_item_t di_inst_choose_source_last = di_none;
static dia_item_t di_inst_choose_netsource_last = di_none;
#if defined(__s390__) || defined(__s390x__)  
static dia_item_t di_inst_choose_display_last = di_none;
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

  sprintf(buf, "%s/%s", config.mountpoint.instdata, config.live.image);

  if(!util_check_exist(buf)) {
    util_disp_init();
    dia_message(txt_get(TXT_RI_NOT_FOUND), MSGTYPE_ERROR);
    inst_umount();
    return -1;
  }

  config.inst_ramdisk = load_image(buf, config.instmode, "Loading Live Image");

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

  /* hope this is correct... */
  config.net.do_setup = 0;

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
      * Back to main menu.
      */
      rc = -1;
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

  if(!(config.test || config.net.cifs.binary)) items[3] = di_skip;

  if(di_inst_choose_netsource_last == di_none) {
    switch(config.instmode) {
      case inst_ftp:
        di_inst_choose_netsource_last = di_netsource_ftp;
        break;

      case inst_http:
        di_inst_choose_netsource_last = di_netsource_http;
        break;

      case inst_smb:
        di_inst_choose_netsource_last = di_netsource_smb;
        break;

      case inst_tftp:
        di_inst_choose_netsource_last = di_netsource_tftp;
        break;

      default:
        di_inst_choose_netsource_last = di_netsource_nfs;
        break;
    }
  }

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

#if defined(__s390__) || defined(__s390x__)  
int inst_choose_display()
{
  dia_item_t di;
  dia_item_t items[] = {
    di_display_x11,
    di_display_vnc,
    di_display_ssh,
    di_none
  };

  di = dia_menu2(txt_get(TXT_CHOOSE_DISPLAY), 33, inst_choose_display_cb, items, di_inst_choose_display_last);

  return di == di_none ? -1 : 0;
}


/*
 * return values:
 * -1    : abort (aka ESC)
 *  0    : ok
 *  other: stay in menu
 */
int inst_choose_display_cb(dia_item_t di)
{
  int rc;
  di_inst_choose_display_last = di;

  switch(di) {
    case di_display_x11:
      if((rc = net_get_address(txt_get(TXT_XSERVER_IP), &config.net.displayip, 1)))
        return rc;
      break;

    case di_display_vnc:
      config.vnc=1;
      net_ask_password();
      break;

    case di_display_ssh:
      config.usessh=1;
      net_ask_password();
      break;

    default:
      break;
  }

  return 0;
}
#endif


int inst_choose_source()
{
  dia_item_t di;
  dia_item_t items[] = {
    di_source_cdrom,
    di_source_net,
    di_source_hd,
    di_none
  };

  inst_umount();

  if(di_inst_choose_source_last == di_none) {
    if(config.insttype == inst_net) di_inst_choose_source_last = di_source_net;
    if(config.instmode == inst_hd) di_inst_choose_source_last = di_source_hd;
  }

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
      str_copy(&config.serverdir, NULL);
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
  int rc, win_old = config.win;

  config.win = 0;

  rc = do_mount_disk(long_dev(dev), 0);

  config.win = win_old;

  if(rc) return 1;

  if(inst_check_instsys()) {
    inst_umount();
    return 2;
  }

  return 0;
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

  set_instmode(inst_hd);

  if(config.net.do_setup && (rc = net_config())) return rc;

  do {
    if((rc = inst_choose_partition(&config.partition, 0, txt_get(TXT_CHOOSE_PARTITION), txt_get(TXT_ENTER_PARTITION)))) return rc;
    if((rc = dia_input2(txt_get(TXT_ENTER_HD_DIR), &config.serverdir, 30, 0))) return rc;

    if(config.partition) {
      rc = do_mount_disk(long_dev(config.partition), 1);
    }
    else {
      rc = -1;
    }
  } while(rc);

  return 0;
}


int inst_mount_nfs()
{
  int rc;
  char text[256];

  set_instmode(inst_nfs);

  if((rc = net_config())) return rc;

  if(config.win) {	/* ###### check really needed? */
    sprintf(text, txt_get(TXT_INPUT_NETSERVER), get_instmode_name_up(config.instmode));
    if((rc = net_get_address(text, &config.net.server, 1))) return rc;
    if((rc = dia_input2(txt_get(TXT_INPUT_DIR), &config.serverdir, 30, 0))) return rc;
  }
  return do_mount_nfs();
}


int inst_mount_smb()
{
  int rc;
  char buf[256];

  set_instmode(inst_smb);

  if((rc = net_config())) return rc;

  if(config.win) {	/* ###### check really needed? */
    sprintf(buf, txt_get(TXT_INPUT_NETSERVER), get_instmode_name_up(config.instmode));
    if((rc = net_get_address(buf, &config.net.server, 1))) return rc;
    if((rc = dia_input2(txt_get(TXT_SMB_ENTER_SHARE), &config.net.share, 30, 0))) return rc;
    if((rc = dia_input2(txt_get(TXT_INPUT_DIR), &config.serverdir, 30, 0))) return rc;
  }

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

  return do_mount_smb();
}


int inst_check_instsys()
{
  char *buf = NULL;
  int update_rd, i;

  config.installfilesread = 0;

  switch(config.instmode) {
    case inst_hd:
    case inst_cdrom:
    case inst_nfs:
    case inst_smb:
      config.use_ramdisk = 0;
      config.instdata_mounted = 1;

      read_install_files();

      if(!config.installfilesread) {
        if(!util_check_exist("/" SP_FILE)) get_file("/" SP_FILE, "/" SP_FILE);
        if(!util_check_exist("/" TEXTS_FILE)) get_file("/media.1/" TEXTS_FILE, "/" TEXTS_FILE);
      }

      if(util_check_exist("/" TEXTS_FILE)) {
        config.cd1texts = file_parse_xmllike("/" TEXTS_FILE, "text");
      }

      util_chk_driver_update(config.mountpoint.instdata, get_instmode_name(config.instmode));

      strprintf(&buf, "%s/driverupdate", config.mountpoint.instdata);
      if(util_check_exist(buf) == 'r') {
        update_rd = load_image(buf, inst_file, txt_get(TXT_LOADING_UPDATE));

        if(update_rd >= 0) {
          i = ramdisk_mount(update_rd, config.mountpoint.update);
          if(!i) util_chk_driver_update(config.mountpoint.update, get_instmode_name(inst_file));
          ramdisk_free(update_rd);
        }
      }

      util_do_driver_updates();

      strprintf(&buf, "%s%s", config.mountpoint.instdata, config.installdir);
      if(config.rescue || !util_is_dir(buf)) {
        strprintf(&buf, "%s%s",
          config.mountpoint.instdata,
          config.demo ? config.live.image : config.rescue ? config.rescueimage : config.rootimage
        );
      }

      if(
        (config.rescue || force_ri_ig || !util_is_mountable(buf)) &&
        !util_is_dir(buf)
      ) {
        config.use_ramdisk = 1;
      }
      strcpy(inst_rootimage_tm, buf);
      
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

  free(buf);

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

  if(
    !config.rootimage2 &&
    current_language()->xfonts
  ) {
    str_copy(&config.rootimage2, ".fonts");
  }

  if(config.manual) {
    if((rc = inst_choose_source())) return rc;
#if defined(__s390__) || defined(__s390x__)
    if((rc = inst_choose_display())) return rc;
#endif
  }
  else {
    fprintf(stderr, "going for automatic install\n");
  }

  str_copy(&config.instsys, NULL);
  str_copy(&config.instsys2, NULL);

  if(config.instmode == inst_exec) {
    util_splash_bar(60, SPLASH_60);
    return inst_execute_yast();
  }

  if(config.use_ramdisk) {
    config.inst_ramdisk = load_image(inst_rootimage_tm, config.instmode, txt_get(config.rescue ? TXT_LOADING_RESCUE : TXT_LOADING_INSTSYS));
    // maybe: inst_umount(); ???
    if(config.inst_ramdisk < 0) return -1;

    if(config.rescue) {
      root_set_root(config.ramdisk[config.inst_ramdisk].dev);

      return 0;
    }

    rc = ramdisk_mount(config.inst_ramdisk, config.mountpoint.instsys);
    if(rc) return rc;
    str_copy(&config.instsys, config.mountpoint.instsys);

    if(config.rootimage2) {
      strprintf(&buf, "%s%s", inst_rootimage_tm, config.rootimage2);
      config.noerrors = 1;
      config.inst2_ramdisk = load_image(buf, config.instmode, txt_get(TXT_LOADING_FONTS));
      config.noerrors = 0;
      if(config.inst2_ramdisk >= 0) {
        if(!ramdisk_mount(config.inst2_ramdisk, config.mountpoint.instsys2)) {
          str_copy(&config.instsys2, config.mountpoint.instsys2);
        }
      }
    }
  }
  else if(!util_is_dir(inst_rootimage_tm)) {
    rc = util_mount_ro(inst_rootimage_tm, config.mountpoint.instsys);
    if(rc) return rc;
    str_copy(&config.instsys, config.mountpoint.instsys);

    if(config.rootimage2) {
      strprintf(&buf, "%s%s", inst_rootimage_tm, config.rootimage2);
      if(!util_mount_ro(buf, config.mountpoint.instsys2)) {
        str_copy(&config.instsys2, config.mountpoint.instsys2);
      }
      else {
        config.noerrors = 1;
        config.inst2_ramdisk = load_image(buf, config.instmode, txt_get(TXT_LOADING_FONTS));
        config.noerrors = 0;
        if(config.inst2_ramdisk >= 0) {
          if(!ramdisk_mount(config.inst2_ramdisk, config.mountpoint.instsys2)) {
            str_copy(&config.instsys2, config.mountpoint.instsys2);
          }
        }
      }
    }
  }
  else {
    strprintf(&buf, "%s%s", config.mountpoint.instdata, config.installdir);
    str_copy(&config.instsys, buf);
  }

  /* load some extra files, if they exist */

  if(!config.zen) {
    /* inly if we did not already read them in inst_check_instsys() */
    if(!config.installfilesread) {
      read_install_files();
      if(!config.installfilesread) {
        get_file("/content", "/content");
        get_file("/media.1/info.txt", "/info.txt");
        get_file("/media.1/license.zip", "/license.zip");
        get_file("/part.info", "/part.info");
        get_file("/control.xml", "/control.xml");
      }
    }
  }
  else if(
    config.zenconfig &&
    config.insttype != inst_hd &&
    config.insttype != inst_cdrom
  ) {
    strprintf(&buf, "/%s", config.zenconfig);
    get_file(buf, "/settings.txt");
    if(config.zen == 2) {
      file_read_info_file("file:/settings.txt", kf_cfg);
      fprintf(stderr, "read /settings.txt\n");
    }
  }

  /* remove it if not needed, might be a leftover from an earlier install attempt */
  if(config.insttype == inst_net) unlink("/" SP_FILE);

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
    update_rd = load_image(buf, config.instmode, txt_get(TXT_LOADING_UPDATE));
    config.noerrors = 0;

    if(update_rd >= 0) {
      i = ramdisk_mount(update_rd, config.mountpoint.update);
      if(!i) util_chk_driver_update(config.mountpoint.update, get_instmode_name(config.instmode));
      ramdisk_free(update_rd);
      if(!i) util_do_driver_updates();
    }
  }

  free(buf);

  util_splash_bar(60, SPLASH_60);

  return inst_execute_yast();
}


/* we might as well just use inst_start_install() instead... */
int inst_start_rescue()
{
  int rc;

  if((rc = inst_choose_source())) return rc;

  config.inst_ramdisk = load_image(inst_rootimage_tm, config.instmode, txt_get(TXT_LOADING_RESCUE));

  inst_umount();	// what for???

  if(config.inst_ramdisk >= 0) {
    root_set_root(config.ramdisk[config.inst_ramdisk].dev);
  }

  util_debugwait("rescue system loaded");

  return config.inst_ramdisk < 0 ? -1 : 0;
}


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

  setenv("TERM", config.term ?: config.serial ? "screen" : "linux", 1);

  setenv("ESCDELAY", config.serial ? "1100" : "10", 1);

  setenv("YAST_DEBUG", "/debug/yast.debug", 1);

  sprintf(buf, "file:%s/.instsys.config", config.mountpoint.instsys);
  file_read_info_file(buf, kf_cfg);

  file_write_install_inf("");

  if(
    config.instsys_complain &&
    config.initrd_id &&
    config.instsys_id &&
    strcmp(config.initrd_id, config.instsys_id)
  ) {
    int win;

    if(!(win = config.win)) util_disp_init();
    if(config.instsys_complain == 1) {
      dia_message(
        "Installation system does not match your boot medium.\n\n"
        "It may make your bugreports worthless.",
        MSGTYPE_ERROR
      );
    }
    else {
      dia_message(
        "Installation system does not match your boot medium.\n\n"
        "Sorry, this will not work.",
        MSGTYPE_ERROR
      );
      rc = 1;
    }
    if(!win) util_disp_done();
  }

  if(
    config.update_complain &&
    config.update.expected_name_list
  ) {
    int win;
    slist_t *sl;

    for(sl = config.update.expected_name_list; sl; sl = sl->next) {
      if(!slist_getentry(config.update.name_list, sl->key)) break;
    }

    if(sl) {
      if(!(win = config.win)) util_disp_init();

      sprintf(buf,
        "The following driver update has not been applied:\n\n%s\n\n"
        "You can continue, but things will not work as expected.\n"
        "If you don't want to see this message, boot with 'updatecomplain=0'.",
        sl->key
      );

      dia_message(buf, MSGTYPE_ERROR);

      if(!win) util_disp_done();
    }
  }

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

  if(config.instsys) {
    argv[1] = config.instsys;
    argv[2] = "/";
    util_lndir_main(3, argv);
  }

  if(config.instsys2) {
    argv[1] = config.instsys2;
    argv[2] = "/";
    util_lndir_main(3, argv);
  }

  return rc;
}


void inst_yast_done()
{
  int count;
  char *buf = NULL;

  lxrc_set_modprobe("/etc/nothing");

  lxrc_killall(0);

  for(count = 0; count < 8; count++) {
    strprintf(&buf, "/dev/loop%d", count);
    util_detach_loop(buf);
  }

  str_copy(&buf, NULL);

  inst_umount();

  util_debugwait("going to umount inst-sys");

  /* wait a bit */
  for(count = 5; inst_umount() == EBUSY && count--;) sleep(1);

  util_debugwait("inst-sys umount done");

  ramdisk_free(config.inst2_ramdisk);
  config.inst2_ramdisk = -1;

  ramdisk_free(config.inst_ramdisk);
  config.inst_ramdisk = -1;

  find_shell();
}


int inst_execute_yast()
{
  int i, rc;
  char *setupcmd = NULL;
  FILE *f;

  rc = add_instsys();
  if(rc) {
    inst_yast_done();
    return rc;
  }

  if(!config.test) {
    lxrc_set_modprobe("/sbin/modprobe");
    if(util_check_exist("/sbin/update")) system("/sbin/update");
  }

  i = 0;
  util_free_mem();
  if(config.addswap) {
    i = ask_for_swap(
      config.memory.min_yast_text - config.memory.min_free,
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

  if(!config.test && config.usessh && config.net.sshpassword) {
    if((f = popen("/usr/sbin/chpasswd", "w"))) {
      fprintf(f, "root:%s\n", config.net.sshpassword);
      pclose(f);
    }
  }

  /* start shells only _after_ the swap dialog */
  if(!config.test && !config.noshell) {
    util_start_shell("/dev/tty2", "/bin/bash", 3);
    if(config.memory.current >= config.memory.min_yast) {
      util_start_shell("/dev/tty5", "/bin/bash", 3);
      util_start_shell("/dev/tty6", "/bin/bash", 3);
    }
  }

  disp_set_color(COL_WHITE, COL_BLACK);
  if(config.win) util_disp_done();

  if(config.splash && config.textmode) system("echo 0 >/proc/splash");

  str_copy(&setupcmd, config.setupcmd);

  if(config.instmode == inst_exec && config.serverdir && *config.serverdir) {
    strprintf(&setupcmd, "setctsid `showconsole` %s", config.serverdir);
  }

  fprintf(stderr, "starting %s\n", setupcmd);

  kbd_end(1);
  util_notty();

  if(config.test) {
    rc = system("/bin/bash 2>&1");
  }
  else {
    if(config.zombies) {
      rc = system(setupcmd);
    }
    else {
      pid_t pid, inst_pid;

      inst_pid = fork();

      if(inst_pid) {
        // fprintf(stderr, "%d: inst_pid = %d\n", getpid(), inst_pid);

        while((pid = waitpid(-1, &rc, 0))) {
          // fprintf(stderr, "%d: chld(%d) = %d\n", getpid(), pid, rc);
          if(pid == inst_pid) {
            // fprintf(stderr, "%d: last chld\n", getpid());
            break;
          }
        }

        // fprintf(stderr, "%d: back from loop\n", getpid());
      }
      else {
        // fprintf(stderr, "%d: system()\n", getpid());
        rc = system(setupcmd);
        // fprintf(stderr, "%d: exit(%d)\n", getpid(), rc);
        exit(WEXITSTATUS(rc));
      }

      // fprintf(stderr, "%d: back, rc = %d\n", getpid(), rc);
    }
  }

  if(rc) {
    if(rc == -1) {
      rc = errno;
    }
    else if(WIFEXITED(rc)) {
      rc = WEXITSTATUS(rc);
    }
  }

  if(!config.listen) {
    freopen(config.console, "r", stdin);
    freopen(config.console, "a", stdout);
    freopen(config.stderr_name, "a", stderr);
  }
  else {
    dup2(1, 0);
    config.kbd_fd = 0;
  }
  kbd_init(0);
  util_notty();

  str_copy(&setupcmd, NULL);

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
#if	defined(__s390__) || defined(__s390x__)
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
  int i, update_rd;
  char *dev, *buf = NULL, *argv[3], *module;
  unsigned old_count;
  slist_t **names;
  window_t win;

  config.update.shown = 1;

  if(choose_dud(&dev)) return 1;

  if(!dev) return 0;

  util_fstype(long_dev(dev), &module);
  if(module) mod_modprobe(module, NULL);

  /* ok, mount it */
  i = util_mount_ro(long_dev(dev), config.mountpoint.update);

  if(i) {
    dia_message(txt_get(TXT_DUD_FAIL), MSGTYPE_ERROR);
    return 0;
  }

  old_count = config.update.count;

  /* point at list end */
  for(names = &config.update.name_list; *names; names = &(*names)->next);

  dia_info(&win, txt_get(TXT_DUD_READ));

  strprintf(&buf, "%s/%s", config.mountpoint.update, SP_FILE);

  if(util_check_exist(buf) == 'r' && !util_check_exist("/" SP_FILE)) {
    argv[1] = buf;
    argv[2] = "/";
    util_cp_main(3, argv);
  }

  util_chk_driver_update(config.mountpoint.update, dev);

  strprintf(&buf, "%s/driverupdate", config.mountpoint.update);
  if(util_check_exist(buf) == 'r') {
    update_rd = load_image(buf, inst_file, txt_get(TXT_LOADING_UPDATE));

    if(update_rd >= 0) {
      i = ramdisk_mount(update_rd, config.mountpoint.update);
      if(!i) util_chk_driver_update(config.mountpoint.update, get_instmode_name(inst_file));
      ramdisk_free(update_rd);
    }
  }

  util_umount(config.mountpoint.update);

  util_do_driver_updates();

  win_close(&win);

  if(old_count == config.update.count) {
    dia_message(txt_get(TXT_DUD_NOTFOUND), MSGTYPE_INFO);
  }
  else {
    if(*names) {
      dia_show_lines2(txt_get(TXT_DUD_ADDED), *names, 64);
    }
    else {
      dia_message(txt_get(TXT_DUD_OK), MSGTYPE_INFO);
    }
  }

  free(buf);

  return 0;
}


/*
 * Let user enter a device for driver updates
 * (*dev = NULL if she changed her mind).
 *
 * return values:
 *  0    : ok
 *  1    : abort
 */
int choose_dud(char **dev)
{
  int i, j, item_cnt, last_item, dev_len, item_width;
  int sort_cnt, err = 0;
  char *s, *s1, *s2, *s3, *buf = NULL, **items, **values;
  hd_data_t *hd_data;
  hd_t *hd, *hd1;
  window_t win;

  *dev = NULL;

  hd_data = calloc(1, sizeof *hd_data);

  if(config.manual < 2) {
    dia_info(&win, "Searching for storage devices...");
    hd_list(hd_data, hw_block, 1, NULL);
    win_close(&win);
  }

  for(i = 0, hd = hd_data->hd; hd; hd = hd->next) {
    if(!hd_is_hw_class(hd, hw_block)) continue;

    /* don't look at whole disk devs, if there are partitions */
    if(
      (hd1 = hd_get_device_by_idx(hd_data, hd->attached_to)) &&
      hd1->base_class.id == bc_storage_device
    ) {
      hd1->status.available = status_no;
    }

    i++;
  }

  /* just max values, actual lists might be shorter */
  items = calloc(i + 1+ 2, sizeof *items);
  values = calloc(i + 1 + 2, sizeof *values);

  item_cnt = 0;

  /* max device name length */
  for(dev_len = 0, hd = hd_data->hd; hd; hd = hd->next) {
    if(
      !hd_is_hw_class(hd, hw_block) ||
      hd->status.available == status_no ||
      !hd->unix_dev_name
    ) continue;

    j = strlen(hd->unix_dev_name);
    if(j > dev_len) dev_len = j;
  }
  dev_len = dev_len > 5 ? dev_len - 5 : 1;

  item_width = sizeof "other device" - 1;

  for(sort_cnt = 0; sort_cnt < 4; sort_cnt++) {
    for(hd = hd_data->hd; hd; hd = hd->next) {
      if(
        !hd_is_hw_class(hd, hw_block) ||
        hd->status.available == status_no ||
        !hd->unix_dev_name ||
        strncmp(hd->unix_dev_name, "/dev/", sizeof "/dev/" - 1)
      ) continue;

      j = 0;
      switch(sort_cnt) {
        case 0:
          if(hd_is_hw_class(hd, hw_floppy)) j = 1;
          break;

        case 1:
          if(hd_is_hw_class(hd, hw_cdrom)) j = 1;
          break;

        case 2:
          if(hd_is_hw_class(hd, hw_usb)) {
            j = 1;
          }
          else {
            hd1 = hd_get_device_by_idx(hd_data, hd->attached_to);
            if(hd1 && hd_is_hw_class(hd1, hw_usb)) j = 1;
          }
          break;

        default:
          j = 1;
          break;
      }

      if(!j) continue;

      hd->status.available = status_no;

      if(
        !(hd1 = hd_get_device_by_idx(hd_data, hd->attached_to)) ||
        hd1->base_class.id != bc_storage_device
      ) {
        hd1 = hd;
      }
      
      s1 = hd1->model;
      if(hd_is_hw_class(hd, hw_floppy)) s1 = "";

      s2 = "Disk";
      if(hd_is_hw_class(hd, hw_partition)) s2 = "Partition";
      if(hd_is_hw_class(hd, hw_floppy)) s2 = "Floppy";
      if(hd_is_hw_class(hd, hw_cdrom)) s2 = "CD-ROM";

      s3 = "";
      if(hd_is_hw_class(hd1, hw_usb)) s3 = "USB ";

      s = NULL;
      strprintf(&s, "%*s: %s%s%s%s",
        dev_len,
        short_dev(hd->unix_dev_name),
        s3,
        s2,
        *s1 ? ", " : "",
        s1
      );

      j = strlen(s);
      if(j > item_width) item_width = j;

      // fprintf(stderr, "<%s>\n", s);

      values[item_cnt] = strdup(short_dev(hd->unix_dev_name));
      items[item_cnt++] = s;
      s = NULL;
    }
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
  items[item_cnt++] = strdup("other device");

  if(item_width > 60) item_width = 60;

  if(item_cnt > 1) {
    i = dia_list(txt_get(TXT_DUD_SELECT), item_width + 2, NULL, items, last_item, align_left);
  }
  else {
    i = item_cnt;
  }

  if(i > 0) {
    s = values[i - 1];
    if(s) {
      str_copy(&config.update.dev, values[i - 1]);
      *dev = config.update.dev;
    }
    else {
      str_copy(&buf, NULL);
      i = dia_input2(txt_get(TXT_DUD_DEVICE), &buf, 30, 0);
      if(!i) {
        if(util_check_exist(long_dev(buf)) == 'b') {
          str_copy(&config.update.dev, short_dev(buf));
          *dev = config.update.dev;
        }
        else {
          dia_message(txt_get(TXT_DUD_INVALID_DEVICE), MSGTYPE_ERROR);
        }
      }
      else {
        err = 1;
      }
    }
  }
  else {
    err = 1;
  }

  for(i = 0; i < item_cnt; i++) { free(items[i]); free(values[i]); }
  free(items);
  free(values);

  free(buf);

  hd_free_hd_data(hd_data);

  free(hd_data);

  // fprintf(stderr, "dud dev = %s\n", *dev);

  return err;
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

  if(config.debug) fprintf(stderr, "get_file: src = %s, dst = %s\n", src, dst);

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
    /* simply copy file */
    sprintf(fname, "%s%s", config.mountpoint.instdata, src);
    if(util_check_exist(fname)) {
      char *argv[3];

      unlink(dst);
      argv[1] = fname;
      argv[2] = dst;
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
 * Mount nfs install source.
 *
 * If config.serverdir points to a file, mount one level higher and
 * loop-mount the file.
 */
int do_mount_nfs()
{
  int rc, file_type = 0;
  window_t win;
  char *buf = NULL, *serverdir = NULL, *file = NULL;

  str_copy(&config.serverpath, NULL);
  str_copy(&config.serverfile, NULL);

  util_truncate_dir(config.serverdir);

  strprintf(&buf,
    config.win ? txt_get(TXT_TRY_NFS_MOUNT) : "nfs: trying to mount %s:%s\n" ,
    config.net.server.name, config.serverdir ?: ""
  );

  if(config.win) {
    dia_info(&win, buf);
  }
  else {
    fprintf(stderr, "%s", buf);
  }

  fprintf(stderr, "Starting portmap.\n");
  system("portmap");

  rc = net_mount_nfs(config.mountpoint.instdata, &config.net.server, config.serverdir);

  if(config.debug) fprintf(stderr, "nfs: err #1 = %d\n", rc);

  if(rc == ENOTDIR && config.serverdir) {
    str_copy(&serverdir, config.serverdir);

    if((file = strrchr(serverdir, '/')) && file != serverdir && file[1]) {
      *file++ = 0;
      mkdir(config.mountpoint.extra, 0755);

      fprintf(stderr, "nfs: trying to mount %s:%s\n", config.net.server.name, serverdir);

      rc = net_mount_nfs(config.mountpoint.extra, &config.net.server, serverdir);

      if(config.debug) fprintf(stderr, "nfs: err #2 = %d\n", rc);
    }
  }

  if(file && !rc) {
    config.extramount = 1;

    strprintf(&buf, "%s/%s", config.mountpoint.extra, file);
    rc = util_mount_ro(buf, config.mountpoint.instdata);

    fprintf(stderr, "nfs: err #3 = %d\n", rc);

    if(rc) {
      fprintf(stderr, "nfs: %s: not found\n", file);
      inst_umount();
      rc = 2;
    }
    else {
      file_type = 1;
      str_copy(&config.serverpath, serverdir);
      str_copy(&config.serverfile, file);
    }
  }

  str_copy(&serverdir, NULL);

  if(config.debug) fprintf(stderr, "nfs: err #4 = %d\n", rc);

  /* rc = -1 --> error was shown in net_mount_nfs() */
  if(rc == -2) {
    fprintf(stderr, "network setup failed\n");
    if(config.win) dia_message("Network setup failed", MSGTYPE_ERROR);
  }
  else if(rc > 0) {
    strprintf(&buf, "nfs: mount failed: %s", strerror(rc));

    fprintf(stderr, "%s\n", buf);
    if(config.win) dia_message(buf, MSGTYPE_ERROR);
  }

  if(config.win) {
    win_close(&win);
  }

  if(!rc) {
    fprintf(stderr, "nfs: mount ok\n");
    config.sourcetype = file_type;
  }

  str_copy(&buf, NULL);

  return rc;
}


/*
 * Mount smb install source.
 *
 * If config.serverdir points to a file, loop-mount the file.
 */
int do_mount_smb()
{
  int rc, file_type = 0;
  window_t win;
  char *buf = NULL;

  util_truncate_dir(config.serverdir);

  strprintf(&buf,
    config.win ? txt_get(TXT_SMB_TRYING_MOUNT) : "smb: trying to mount //%s/%s\n" ,
    config.net.server.name, config.net.share
  );

  if(config.win) {
    dia_info(&win, buf);
  }
  else {
    fprintf(stderr, "%s", buf);
  }

  mkdir(config.mountpoint.extra, 0755);

  rc = net_mount_smb(config.mountpoint.extra,
    &config.net.server, config.net.share,
    config.net.user, config.net.password, config.net.workgroup
  );

  if(!rc) {
    config.extramount = 1;
    strprintf(&buf, "%s/%s", config.mountpoint.extra, config.serverdir);
    if((file_type = util_check_exist(buf))) {
      rc = util_mount_ro(buf, config.mountpoint.instdata);
    }
    else {
      fprintf(stderr, "smb: %s: not found\n", config.serverdir);
      rc = -1;
    }
  }

  switch(rc) {
    case 0:
      break;

    case -3:
      fprintf(stderr, "smb: network setup failed\n");
      if(config.win) dia_message("Network setup failed", MSGTYPE_ERROR);
      break;

    case -2:
      fprintf(stderr, "smb: smb/cifs not supported\n");
      if(config.win) dia_message("SMB/CIFS not supported", MSGTYPE_ERROR);
      break;

    default:	/* -1 */
      fprintf(stderr, "smb: mount failed\n");
      if(config.win) dia_message("SMB/CIFS mount failed", MSGTYPE_ERROR);
  }

  if(config.win) win_close(&win);

  if(rc) {
    inst_umount();
  }
  else {
    fprintf(stderr, "smb: mount ok\n");
  }

  if(!rc) config.sourcetype = file_type == 'r' ? 1 : 0;

  str_copy(&buf, NULL);

  return rc;
}


/*
 * Mount disk install source.
 *
 * If config.serverdir points to a file, loop-mount file.
 *
 * disk_type: 0 = cd, 1 = hd (only used for error message)
 */
int do_mount_disk(char *dev, int disk_type)
{
  int rc = 0, file_type = 0;
  char *buf = NULL, *module, *type;
  char *dir;

  util_truncate_dir(config.serverdir);

  dir = config.serverdir;
  if(!dir || !*dir || !strcmp(dir, "/")) dir = NULL;

  /* load fs module if necessary */
  type = util_fstype(dev, &module);
  if(module) mod_modprobe(module, NULL);

  if(!type || !strcmp(type, "swap")) rc = -1;

  mkdir(config.mountpoint.extra, 0755);

  if(!rc) {
    rc = util_mount_ro(dev, dir ? config.mountpoint.extra : config.mountpoint.instdata);
  }

  if(rc) {
    fprintf(stderr, "disk: %s: mount failed\n", dev);
    if(config.win) dia_message(txt_get(disk_type ? TXT_ERROR_HD_MOUNT : TXT_ERROR_CD_MOUNT), MSGTYPE_ERROR);
  }
  else {
    if(dir) {
      config.extramount = 1;
      strprintf(&buf, "%s/%s", config.mountpoint.extra, dir);
      file_type = util_check_exist(buf);
      rc = file_type ? util_mount_ro(buf, config.mountpoint.instdata) : -1;

      if(rc) {
        fprintf(stderr, "disk: %s: not found\n", dir);
        if(config.win) dia_message(txt_get(TXT_RI_NOT_FOUND), MSGTYPE_ERROR);
        inst_umount();
      }
    }

    if(!rc) {
      fprintf(stderr, "disk: %s: mount ok\n", dev);
    }
  }

  if(!rc) config.sourcetype = file_type == 'r' ? 1 : 0;

  str_copy(&buf, NULL);

  return rc;
}


void read_install_files()
{
  file_t *f0, *f;
  char *dst = NULL, *dst_dir, *s, *t;

  unlink("/" INSTALL_FILE_LIST);
  get_file("/media.1/" INSTALL_FILE_LIST, "/" INSTALL_FILE_LIST);

  if(!(f0 = file_read_file("/" INSTALL_FILE_LIST, kf_none))) return;

  config.installfilesread = 1;

  for(f = f0; f; f = f->next) {
    dst_dir = f->value;
    if(*dst_dir == '/') dst_dir++;
    s = strrchr(f->key_str, '/');
    s = s ? s + 1 : f->key_str;
    strprintf(&dst, "/%s/%s", dst_dir, s);

    for(s = dst_dir; (t = strchr(s, '/')); s = t + 1) {
      *t = 0;
      if(*dst_dir) mkdir(dst_dir, 0755);
      *t = '/';
    }
    mkdir(dst_dir, 0755);
    unlink(dst);
    get_file(f->key_str, dst);
  }

  file_free_file(f0);

  str_copy(&dst, NULL);
}

