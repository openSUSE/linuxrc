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
#include <sys/mount.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/swap.h>
#include <sys/socket.h>
#include <sys/reboot.h>
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


#define YAST2_COMMAND   "/usr/lib/YaST2/bin/YaST2.start"
#define YAST1_COMMAND   "/sbin/YaST"
#define SETUP_COMMAND   "/sbin/inst_setup"

static char  inst_rootimage_tm [MAX_FILENAME];
static char *inst_demo_sys_tm = "/suse/images/cd-demo";

static int   inst_mount_harddisk      (void);
static int   inst_try_cdrom           (char *device_tv);
static int   inst_mount_cdrom         (int show_err);
static int   inst_mount_nfs           (void);
static int   inst_start_rescue        (void);
static int   inst_prepare             (void);
static void  inst_yast_done           (void);
static int   inst_execute_yast        (void);
static int   inst_check_floppy        (void);
static int   inst_commit_install      (void);
static int inst_choose_netsource(void);
static int inst_choose_netsource_cb(dia_item_t di);
static int   inst_choose_source       (void);
static int   inst_choose_source_cb    (dia_item_t di);
static int   inst_menu_cb             (dia_item_t di);
static int   inst_umount              (void);
static int   inst_mount_smb           (void);
static int   inst_do_ftp              (void);
static int   inst_get_ftpsetup        (void);
static int   inst_do_http             (void);
static int   inst_do_tftp             (void);
static int   inst_choose_yast_version (void);
static int   inst_update_cd           (void);
static void  inst_swapoff             (void);

#ifdef OBSOLETE_YAST_LIVECD
/* 'Live' entry in yast.inf */
static int yast_live_cd = 0;
#endif

static dia_item_t di_inst_menu_last = di_none;
static dia_item_t di_inst_choose_source_last = di_none;
static dia_item_t di_inst_choose_netsource_last = di_netsource_nfs;

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


int inst_start_demo (void)
    {
    int    rc_ii;
    char   filename_ti [MAX_FILENAME];
    FILE  *file_pri;
    char   line_ti [MAX_X];
    int    test_ii = FALSE;

    if (!auto2_ig)
        {
        if (config.demo)
            if (!info_eide_cd_exists ())
                {
#if 0
                rc_ii = mod_auto (MOD_TYPE_SCSI);
                if (rc_ii || !info_scsi_cd_exists ())
                    (void) mod_auto (MOD_TYPE_OTHER);
#endif
                }

        test_ii = FALSE;

        if (test_ii)
            rc_ii = inst_mount_nfs ();
        else
            {
            if (!config.demo)
                (void) dia_message (txt_get (TXT_INSERT_LIVECD), MSGTYPE_INFO);

            rc_ii = inst_mount_cdrom (1);
            }

        if (rc_ii)
            return (rc_ii);
        }
    else
        {
        if(config.ask_language || config.ask_keytable) {
          int win_old;
          if(!(win_old = config.win)) util_disp_init();
          if(config.ask_language) set_choose_language();
          util_print_banner();
          if(config.ask_keytable) set_choose_keytable(1);
          if(!win_old) util_disp_done();
        }
        }

    sprintf (filename_ti, "%s/%s", mountpoint_tg, inst_demo_sys_tm);
    if (!util_check_exist (filename_ti))
        {
        util_disp_init();
        dia_message (txt_get (TXT_RI_NOT_FOUND), MSGTYPE_ERROR);
        inst_umount ();
        return (-1);
        }

    rc_ii = root_load_rootimage (filename_ti);
    inst_umount ();

    if (rc_ii)
        return (rc_ii);

    if (util_mount_rw(RAMDISK_2, mountpoint_tg)) return (-1);

    file_write_install_inf (mountpoint_tg);

    sprintf (filename_ti, "%s/%s", mountpoint_tg, "etc/fstab");
    file_pri = fopen (filename_ti, "a");
    // TODO:SMB???
    if (config.instmode == inst_nfs && !*livesrc_tg)
        {
        sprintf(line_ti,
          "%s:%s /S.u.S.E. nfs ro,nolock 0 0\n",
          inet_ntoa(config.net.server.ip),
          config.serverdir ?: ""
        );
        }
    else
        {
        sprintf(line_ti,
          "/dev/%s /S.u.S.E. %s ro 0 0\n",
          *livesrc_tg ? livesrc_tg : config.cdrom,
          *livesrc_tg ? "auto" : "iso9660"
        );
        }

    fprintf (file_pri, line_ti);
    fclose (file_pri);
    inst_umount ();
    return (0);
    }


int inst_menu()
{
  dia_item_t di;
  dia_item_t items[] = {
    di_inst_install,
    di_inst_demo,
    di_inst_system,
    di_inst_rescue,
    di_inst_eject,
    di_inst_update,
    di_none
  };

  items[config.demo ? 0 : 1] = di_skip;
  if(!yast2_update_ig) items[5] = di_skip;

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

    case di_inst_eject:
      util_eject_cdrom(config.cdrom);
      error = 1;
      break;

    case di_inst_update:
      inst_update_cd();
      error = 1;
      break;

    default:
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

  if(!config.fullnetsetup) {
    // yast doesn't support it :-((
    items[0] = items[1] = items[4] = di_skip;
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

#if 0
    case di_netsource_smb:
      config.net.smb_available = config.test || util_check_exist("/bin/smbmount");
      if(!config.net.smb_available) {
        sprintf(buf, "%s\n\n%s", txt_get(TXT_SMBDISK), txt_get(TXT_MODDISK2));
        dia_okcancel(buf, YES);
      }
      config.net.smb_available = config.test || util_check_exist("/bin/smbmount");
      error = config.net.smb_available ? inst_mount_smb() : 1;
      break;
#endif

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
        dia_message(tmp, MSGTYPE_INFO);
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
  }

  dia_info(&win, txt_get(TXT_TRY_CD_MOUNT));

  rc = 1;

  if(config.cdrom) rc = inst_try_cdrom(config.cdrom);

  if(rc) {
    for(sl = config.cdroms; sl; sl = sl->next) {
      if(!(rc = inst_try_cdrom(sl->key))) {
        str_copy(&config.cdrom, sl->key);
        break;
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

  set_instmode(inst_hd);

  do {
    if(!auto_ig) {
      rc = inst_choose_partition(&config.partition, 0, txt_get(TXT_CHOOSE_PARTITION), txt_get(TXT_ENTER_PARTITION));
      if(rc) return -1;
    }

    if(config.partition) {
      sprintf(buf, "/dev/%s", config.partition);
      rc = util_mount_ro(buf, config.mountpoint.extra);
    }
    else {
      rc = -1;
    }

    if(rc) {
      dia_message (txt_get(TXT_ERROR_HD_MOUNT), MSGTYPE_ERROR);
    }
    else {
      if(!auto_ig) {
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

  if(config.win && !auto_ig) {
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

  if(config.win && !auto_ig) {
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

      sprintf(filename, "%s%s", config.mountpoint.instdata, config.installdir);
      if(config.rescue || force_ri_ig || !util_is_dir(filename)) {
        sprintf(filename, "%s%s",
          config.mountpoint.instdata,
          config.rescue ? config.rescueimage : config.rootimage
        );
      }

#if 0
      deb_int(config.rescue);
      deb_int(force_ri_ig);
      deb_int(util_is_mountable(filename));
      deb_int(util_is_dir(filename));
#endif

      if(
        config.rescue ||
        force_ri_ig ||
        !(util_is_mountable(filename) || util_is_dir(filename))
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
  int rc;
  char buf[256];

  if(config.manual) {
    if((rc = inst_choose_source())) return rc;
  }
  else {
    fprintf(stderr, "going for automatic install\n");
  }

  // deb_str(inst_rootimage_tm);

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
    sprintf(buf, "%s%s", config.mountpoint.instdata, config.installdir);
    str_copy(&config.instsys, buf);
  }

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


/*
 * Do some basic preparations before we can run programs from the
 * installation system. More is done later in SETUP_COMMAND.
 *
 * Note: the instsys must already be mounted at this point.
 *
 */
int inst_prepare()
{
  char *links[] = { "/bin", "/lib", "/sbin", "/usr" };
  char link_source[MAX_FILENAME];
  int i = 0, rc = 0;

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
    for(i = 0; i < sizeof links / sizeof *links; i++) {
      if(!util_check_exist(links[i])) {
	unlink(links[i]);
	sprintf(link_source, "%s%s", config.instsys, links[i]);
	symlink(link_source, links[i]);
      }
    }

  setenv("PATH", "/lbin:/bin:/sbin:/usr/bin:/usr/sbin:/usr/lib/YaST2/bin", TRUE);

  if(serial_ig) {
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
  }

//  if(!config.use_ramdisk) rc = inst_init_cache();

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

  if(!config.initrd_has_ldso && !config.test) {
    unlink("/bin");
    rename("/.bin", "/bin");
  }
}


int inst_execute_yast()
{
  int i, rc;
  char cmd[256];

  rc = inst_prepare();
  if(rc) return rc;

  if(inst_choose_yast_version()) {
    inst_yast_done();
    return -1;
  }

  if(!config.test) {
    lxrc_set_modprobe("/sbin/modprobe");
    if(util_check_exist("/sbin/update")) system("/sbin/update");
  }

  util_free_mem();
  if(config.addswap) ask_for_swap(
    (config.memory.min_yast - config.memory.min_free) << 10,
    "Not enough memory for YaST."
  );

  i = 0;

  util_free_mem();
  if(config.addswap && config.memory.current < config.memory.min_yast) {
    if(!config.textmode) {
      int win_old;
      if(!(win_old = config.win)) util_disp_init();
      if(dia_okcancel(
        "You don't have enough free memory for a graphical installation.\n\n"
        "The text mode frontend of YaST2 will be used instead.",
        YES
      ) != YES) i = 1;
      if(!win_old) util_disp_done();

      if(!i) {
        config.textmode = 1;
        file_write_install_inf("");
      }
    }
  }

  if(i) {
    inst_yast_done();
    return -1;
  }

  /* start shells only _after_ the swap dialog */
  if(!config.test) {
    util_start_shell("/dev/tty2", "/bin/bash", 1);
    if(config.memory.current >= config.memory.min_yast) {
      util_start_shell("/dev/tty5", "/bin/bash", 1);
      util_start_shell("/dev/tty6", "/bin/bash", 1);
    }
  }

  disp_set_color(COL_WHITE, COL_BLACK);
  if(config.win) util_disp_done();

  if(config.splash && config.textmode) system("echo 0 >/proc/splash");

  if(config.test) {
    rc = system("/bin/bash 2>&1");
  }
  else {
    sprintf(cmd, "%s%s", config.instsys, SETUP_COMMAND);

    if(util_check_exist(cmd)) {
      sprintf(cmd + strlen(cmd), " yast%d", yast_version_ig == 2 ? 2 : 1);
      fprintf(stderr, "starting yast%d\n", yast_version_ig == 2 ? 2 : 1);
    }
    else {
      sprintf(cmd, "%s", yast_version_ig == 2 ? YAST2_COMMAND : YAST1_COMMAND);
      fprintf(stderr, "starting \"%s\"\n", cmd);
    }

    rc = system(cmd);
  }

  if(rc) {
    if(rc == -1) {
      rc = errno;
    }
    else if(WIFEXITED(rc)) {
      rc = WEXITSTATUS(rc);
    }
  }

  if(config.splash && config.textmode) system("echo 1 >/proc/splash");

  fprintf(stderr, "yast exit code is %d\n", rc);

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

  yast_version_ig = 0;

  if(rc || config.aborted) {
    config.rescue = 0;
    util_manual_mode();
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
      util_manual_mode();
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

  i = dia_message(txt_get(TXT_INSERT_DISK), MSGTYPE_INFO);
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
  window_t win;

  if(reboot_ig) {

    if(config.rebootmsg) {
      disp_clear_screen();
      util_disp_init();
      dia_message(txt_get(TXT_DO_REBOOT), MSGTYPE_INFO);
    }

    if(config.test) {
      fprintf(stderr, "*** reboot ***\n");
    }
    else {
      reboot(RB_AUTOBOOT);
    }
    err = -1;
  }
  else {
#ifdef OBSOLETE_YAST_LIVECD
    if(yast_live_cd) {
      util_disp_init();
      dia_message(txt_get(TXT_INSERT_LIVECD), MSGTYPE_INFO);
    }
    else
#endif
    {
      if(auto_ig) {
        util_disp_init();
        dia_info(&win, txt_get(TXT_INSTALL_SUCCESS));
        sleep(2);
        win_close(&win);
      }
      else {

#if 0 /* ifndef __PPC__ */
        if(!auto2_ig) {
          util_disp_init();
          dia_message(txt_get(TXT_INSTALL_SUCCESS), MSGTYPE_INFO);
        }
#endif

      }
    }
  }

  return err;
}


#if 0
static int inst_init_cache (void)
    {
    char     *files_ati [] = {
                             "/lib/libc.so.6",
                             "/lib/libc.so.5",
                             "/lib/ld.so",
                             "/bin/bash",
                             "/sbin/YaST"
                             };
    int32_t   size_li;
    long      allsize_li;
    int       dummy_ii;
    char      buffer_ti [10240];
    window_t  status_ri;
    int       i_ii;
    int       fd_ii;
    int       percent_ii;
    int       old_percent_ii;
    int       read_ii;


    if (memory_ig < MEM_LIMIT_CACHE_LIBS)
        return (0);

    dia_status_on (&status_ri, txt_get (TXT_PREPARE_INST));
    allsize_li = 0;
    for (i_ii = 0; i_ii < sizeof (files_ati) / sizeof (files_ati [0]); i_ii++)
        if (!util_fileinfo (files_ati [i_ii], &size_li, &dummy_ii))
            allsize_li += size_li;

    if (allsize_li)
        {
        size_li = 0;
        old_percent_ii = 0;

        for (i_ii = 0; i_ii < sizeof (files_ati) / sizeof (files_ati [0]); i_ii++)
            {
            fd_ii = open (files_ati [i_ii], O_RDONLY);
            while ((read_ii = read (fd_ii, buffer_ti, sizeof (buffer_ti))) > 0)
                {
                size_li += (long) read_ii;
                percent_ii = (int) ((size_li * 100) / allsize_li);
                if (percent_ii != old_percent_ii)
                    {
                    dia_status (&status_ri, percent_ii);
                    old_percent_ii = percent_ii;
                    }
                }
            close (fd_ii);
            }
        }
    win_close (&status_ri);
    return (0);
    }
#endif


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
    if((rc = inst_get_ftpsetup())) return rc;

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


int inst_get_ftpsetup()
{
  int rc;
  char *s, tmp[256];
  unsigned u;

  rc = dia_yesno(txt_get(TXT_ANONYM_FTP), NO);
  if(rc == ESCAPE) return -1;

  if(rc == NO) {
    str_copy(&config.net.user, NULL);
    str_copy(&config.net.password, NULL);
  }
  else {
    if((rc = dia_input2(txt_get(TXT_ENTER_FTPUSER), &config.net.user, 20, 0))) return rc;
    if((rc = dia_input2(txt_get(TXT_ENTER_FTPPASSWD), &config.net.password, 20, 1))) return rc;
  }

  rc = dia_yesno(txt_get(TXT_WANT_FTPPROXY), NO);
  if(rc == ESCAPE) return -1;

  if(rc == YES) {
    if((rc = net_get_address(txt_get(TXT_ENTER_FTPPROXY), &config.net.proxy, 1))) return rc;

    *tmp = 0;
    if(config.net.proxyport) sprintf(tmp, "%u", config.net.proxyport);

    do {
      rc = dia_input(txt_get(TXT_ENTER_FTPPORT), tmp, 6, 6, 0);
      if(rc) return rc;
      u = strtoul(tmp, &s, 0);
      if(*s) {
        rc = -1;
        dia_message (txt_get (TXT_INVALID_INPUT), MSGTYPE_ERROR);
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
//    if((rc = inst_get_ftpsetup())) return rc;

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


int inst_do_tftp()
{
  int rc;
  window_t win;
  char buf[256];

  set_instmode(inst_tftp);

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


int inst_choose_yast_version()
{
  int yast1, yast2;
  char yast1_file[MAX_FILENAME], yast2_file[MAX_FILENAME];
  static int last_item = 0;
  char *items[] = {
    txt_get(TXT_YAST1),
    txt_get(TXT_YAST2),
    NULL
  };

  *yast1_file = *yast2_file = 0;
  if(config.instsys) {
    strcpy(yast1_file, config.instsys);
    strcpy(yast2_file, config.instsys);
  }
  strcat(yast1_file, YAST1_COMMAND);
  strcat(yast2_file, YAST2_COMMAND);

  yast1 = util_check_exist(yast1_file);
  yast2 = util_check_exist(yast2_file);

  if(!yast_version_ig && auto_ig) yast_version_ig = 1;

  if(yast_version_ig == 1 && yast1) return 0;

  if(yast_version_ig == 2 && yast2) return 0;

  if(yast1 && !yast2) {
    yast_version_ig = 1;
    return 0;
  }

  if(!yast1 && yast2) {
    yast_version_ig = 2;
    return 0;
  }

  if(auto2_ig) {
    util_manual_mode();
    util_disp_init();
  }

  yast_version_ig = dia_list(txt_get(TXT_CHOOSE_YAST), 30, NULL, items, last_item, align_center);
  if(yast_version_ig) last_item = 0;

  return yast_version_ig ? 0 : -1;
}


int inst_update_cd()
{
  int  i, cdroms = 0;
  char *mp = "/tmp/drvupdate";
  hd_data_t *hd_data;
  hd_t *hd0, *hd;
  window_t win;

  *driver_update_dir = 0;

  mkdir(mp, 0755);

  dia_message("Please insert the Driver Update CD-ROM", MSGTYPE_INFO);
  dia_info(&win, "Trying to mount the CD-ROM...");	// TXT_TRY_CD_MOUNT

  hd_data = calloc(1, sizeof *hd_data);
  hd0 = hd_list(hd_data, hw_cdrom, 1, NULL);

  for(hd = hd0; hd; hd = hd->next) {
    if(hd->unix_dev_name) {
      cdrom_drives++;
      i = util_mount_ro(hd->unix_dev_name, mp);
      if(!i) {
        cdroms++;
        deb_msg("Update CD mounted");
        util_chk_driver_update(mp);
        umount(mp);
      }
      else {
        deb_msg("Update CD mount failed");
      }
    }
    if(*driver_update_dir) break;
  }

  hd_free_hd_list(hd0);
  hd_free_hd_data(hd_data);
  free(hd_data);

  win_close(&win);

  if(!*driver_update_dir) {
    dia_message(cdroms ? "Driver Update failed" : "Could not mount the CD-ROM", MSGTYPE_ERROR);
  }
  else {
    dia_message("Driver Update ok", MSGTYPE_INFO);
  }

  return 0;
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

