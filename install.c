/*
 *
 * install.c           Handling of installation
 *
 * Copyright (c) 1996-2008  Hubert Mantel, SuSE Linux AG  (mantel@suse.de)
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
#include "settings.h"
#include "auto2.h"
#include "url.h"
#include "checkmedia.h"

#ifndef MNT_DETACH
#define MNT_DETACH	(1 << 1)
#endif

// maximum number of entries you can get in a dialog
// - this is just more than you can reasonably expect -
#define MAX_DIALOG_ITEMS	64

// flags for inst_choose_partition()
#define ICP_SWAP	1
#define ICP_BLOCK_ALL	2

static int inst_do_cdrom(void);
static int inst_do_harddisk(void);
static int inst_do_mountable(instmode_t scheme);
static int inst_do_network(instmode_t scheme);
static int   add_instsys              (void);
static void  inst_yast_done           (void);
static int   inst_execute_yast        (void);
static int   inst_commit_install      (void);
static int   inst_choose_netsource    (void);
static int   inst_choose_netsource_cb (dia_item_t di);
static int   inst_choose_display      (void);
static int   inst_choose_display_cb   (dia_item_t di);
static int   inst_choose_source       (void);
static int   inst_choose_source_cb    (dia_item_t di);
static int   inst_menu_cb             (dia_item_t di);
static int choose_dud(char **dev);

static dia_item_t di_inst_menu_last = di_none;
static dia_item_t di_inst_choose_source_last = di_none;
static dia_item_t di_inst_choose_netsource_last = di_none;
static dia_item_t di_inst_choose_display_last = di_none;

static int ask_for_swap(int64_t size, char *msg);


/*
 * Menu: install, system start, rescue
 *
 * return values:
 *   0 : ok
 *   1 : abort
 */
int inst_menu()
{
  dia_item_t di;
  dia_item_t items[] = {
    di_inst_install,
    di_inst_update,
    di_inst_rescue,
    di_inst_system,
    di_inst_net_config,
    di_inst_check_media,
    di_none
  };

  /* hope this is correct... */
  /* ... apparently not: keep VNC & SSH settings (bnc #447433) */
  config.net.do_setup &= DS_VNC | DS_SSH;

  if(di_inst_menu_last == di_none) {
    di_inst_menu_last = config.rescue ? di_inst_rescue : config.upgrade ? di_inst_update : di_inst_install;
  }

  di = dia_menu2("Start Installation", 40, inst_menu_cb, items, di_inst_menu_last);

  return di == di_none ? 1 : 0;
}


/*
 * return values:
 *   -1    : abort (aka ESC)
 *    0    : ok
 *    other: stay in menu
 */
int inst_menu_cb(dia_item_t di)
{
  int err = 0;

  di_inst_menu_last = di;

  switch(di) {
    case di_inst_install:
      config.rescue = 0;
      config.upgrade = 0;
      err = inst_start_install();
      break;

    case di_inst_update:
      config.rescue = 0;
      config.upgrade = 1;
      err = inst_start_install();
      break;

    case di_inst_rescue:
      config.rescue = 1;
      err = inst_start_install();
      break;

    case di_inst_system:
      util_boot_system();
      err = 1;
      break;

    case di_inst_net_config:
      net_config();
      err = 1;
      break;

    case di_inst_check_media:
      check_media(NULL);
      err = 1;
      break;

    default:
      break;
  }

  config.redraw_menu = 0;

  return err ? 1 : 0;
}


/*
 * Menu: network protocol
 *
 * return values:
 *   0 : ok
 *   1 : error
 */
int inst_choose_netsource()
{
  int i, j;
  slist_t *sl;
  dia_item_t di;
  dia_item_t items[MAX_DIALOG_ITEMS] = {
    di_netsource_ftp,
    di_netsource_http,
    di_netsource_https,
    di_netsource_nfs,
    di_netsource_smb,
    di_netsource_tftp,
  };

 j = 6;        // first 6 items have aleady been filled in
  for(sl = config.extern_scheme, i = inst_extern; sl; sl = sl->next, i++) {
    if(sl->value && url_is_network(i)) {
      items[j] = dia_get_id(sl->key, sl->value);
      if(++j >= MAX_DIALOG_ITEMS - 1) break;
    }
  }

  items[j] = di_none;

  if(di_inst_choose_netsource_last == di_none && config.url.install) {
    switch(config.url.install->scheme) {
      case inst_ftp:
        di_inst_choose_netsource_last = di_netsource_ftp;
        break;

      case inst_http:
        di_inst_choose_netsource_last = di_netsource_http;
        break;

      case inst_https:
        di_inst_choose_netsource_last = di_netsource_https;
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

  di = dia_menu2("Choose the network protocol.", 33, inst_choose_netsource_cb, items, di_inst_choose_netsource_last);

  return di == di_none ? 1 : 0;
}


/*
 * return values:
 * -1    : abort (aka ESC)
 *  0    : ok
 *  other: stay in menu
 */
int inst_choose_netsource_cb(dia_item_t di)
{
  int err = 0;
  instmode_t scheme;
  char *s;

  di_inst_choose_netsource_last = di;

  switch(di) {
    case di_netsource_nfs:
      err = inst_do_network(inst_nfs);
      break;

    case di_netsource_smb:
      err = inst_do_network(inst_smb);
      break;

    case di_netsource_ftp:
      err = inst_do_network(inst_ftp);
      break;

    case di_netsource_http:
      err = inst_do_network(inst_http);
      break;

    case di_netsource_https:
      err = inst_do_network(inst_https);
      break;

    case di_netsource_tftp:
      err = inst_do_network(inst_tftp);
      break;

    default:
      s = dia_get_label(di);
      if(s) {
        scheme = url_scheme2id(s);
        err = inst_do_network(scheme);
      }
      else {
        err = 1;
      }
      break;
  }

  if(err) dia_message("No repository found.", MSGTYPE_ERROR);

  return err ? 1 : 0;
}


/*
 * Menu: installer UI variant
 *
 * return values:
 *   0 : ok
 *   1 : error
 */
int inst_choose_display()
{
  if(!config.manual && (config.net.displayip || config.vnc || config.usessh)) {
    net_ask_password();
    return 0;
  }
  else {
    dia_item_t di;
    dia_item_t items[] = {
      di_display_qt,
      di_display_console,
      di_display_vnc,
      di_display_ssh,
      di_display_x11,
      di_none
    };

    di = dia_menu2("Select the display type.", 33, inst_choose_display_cb, items, di_inst_choose_display_last);

    return di == di_none ? 1 : 0;
  }
}


/*
 * return values:
 *  0    : ok
 *  other: stay in menu
 */
int inst_choose_display_cb(dia_item_t di)
{
  int result = 0;

  di_inst_choose_display_last = di;

  switch(di) {
    case di_display_x11:
      dia_input2("Enter the name of the host running the X11 server.", &config.net.displayip, 40, 0);
      if(!config.net.displayip) result = 1;
      break;

    case di_display_vnc:
      config.vnc = 1;
      net_ask_password();
      if(!config.net.vncpassword) {
        config.vnc = 0;
        result = 1;
      }
      break;

    case di_display_ssh:
      config.usessh = 1;
      config.vnc = 0;
      net_ask_password();
      if(!(config.net.sshpassword || config.net.sshpassword_enc)) {
        config.usessh = 0;
        result = 1;
      }
      break;

    case di_display_console:
      config.textmode = 1;
      break;

    case di_display_qt:
      config.textmode = 0;
      break;

    default:
      break;
  }

  return result;
}


/*
 * Ask for repo location.
 *
 * return:
 *   0: ok
 *   1: abort
 */
int inst_choose_source()
{
  int i, j;
  slist_t *sl;
  dia_item_t di;
  dia_item_t items[MAX_DIALOG_ITEMS] = {
    di_source_cdrom,
    di_source_net,
    di_source_hd
  };

  j = 3;	// first 3 items have aleady been filled in
  for(sl = config.extern_scheme, i = inst_extern; sl; sl = sl->next, i++) {
    if(sl->value && url_is_mountable(i) && !url_is_network(i)) {
      items[j] = dia_get_id(sl->key, sl->value);
      if(++j >= MAX_DIALOG_ITEMS - 1) break;
    }
  }

  items[j] = di_none;

  if(di_inst_choose_source_last == di_none) {
    di_inst_choose_source_last = di_source_cdrom;
    if(config.url.install) {
      if(config.url.install->is.network) {
        di_inst_choose_source_last = di_source_net;
      }
      else if(!config.url.install->is.cdrom) {
        di_inst_choose_source_last = di_source_hd;
      }
    }
  }

  di = dia_menu2("Choose the source medium.", 33, inst_choose_source_cb, items, di_inst_choose_source_last);

  return di == di_none ? 1 : 0;
}


/*
 * Repo location menu.
 *
 * return values:
 * -1    : abort (aka ESC)
 *  0    : ok
 *  other: stay in menu
 */
int inst_choose_source_cb(dia_item_t di)
{
  int err = 0, rc = 0;
  instmode_t scheme;
  char *s;

  di_inst_choose_source_last = di;

  switch(di) {
    case di_source_cdrom:
      err = inst_do_cdrom();
      break;

    case di_source_net:
      rc = inst_choose_netsource();
      break;

    case di_source_hd:
      err = inst_do_harddisk();
      break;

    default:
      s = dia_get_label(di);
      if(s) {
        scheme = url_scheme2id(s);
        err = inst_do_mountable(scheme);
      }
      else {
        err = 1;
      }
      break;
  }

  if(err) {
    dia_message("No repository found.", MSGTYPE_ERROR);
    rc = 1;
  }

  return rc;
}


/*
 * build a partition list
 *
 * flags:
 *   0:             list all partitions with file systems on it
 *   ICP_BLOCK_ALL: list all block devices + partitions (empty or used)
 *   ICP_SWAP:      present selection for suitable swap partitions
 */
int inst_choose_partition(char **partition, int flags, char *txt_menu, char *txt_input)
{
  int i, j, rc, item_cnt, item_cnt1, item_cnt2, item_cnt3;
  char **items, **values;
  char **items1, **values1;
  char **items2, **values2;
  char **items3, **values3;
  char *type;
  slist_t *sl;
  // buf[] must be able to hold a command (of fixed known length) + filename[256],
  // so make a bit larger than 256
  char buf[512], *dev;
  int found = 0, item_mk_part = 0, item_mk_file = 0;
  char *s, *tmp = NULL;
  static char *last_part = NULL;
  int last_item, last_found, last_item1 = 0, last_item2 = 0, last_item3 = 0;
  char *module;
  slist_t *device_list = NULL;

  util_update_disk_list(NULL, 1);
  util_update_swap_list();

  if((flags & ICP_BLOCK_ALL)) {
    util_update_cdrom_list();

    for(sl = config.cdroms; sl; sl = sl->next) {
      slist_append_str(&device_list, sl->key);
    }

    for(sl = config.disks; sl; sl = sl->next) {
      slist_append_str(&device_list, sl->key);
    }
  }

  for(sl = config.partitions; sl; sl = sl->next) {
    slist_append_str(&device_list, sl->key);
  }

  for(i = 0, sl = device_list; sl; sl = sl->next) i++;

  /*
   * Just max values, actual lists might be shorter.
   * list1: swap, list2: with fs or empty, list3: with fs
   */
  items1 = calloc(i + 4, sizeof *items1);
  values1 = calloc(i + 4, sizeof *values1);
  items2 = calloc(i + 4, sizeof *items2);
  values2 = calloc(i + 4, sizeof *values2);
  items3 = calloc(i + 4, sizeof *items3);
  values3 = calloc(i + 4, sizeof *values3);

  for(item_cnt1 = item_cnt2 = item_cnt3 = 0, sl = device_list; sl; sl = sl->next) {
    if(
      sl->key && !slist_getentry(config.swaps, sl->key)		/* don't show active swaps */
    ) {
      if(blk_size(long_dev(sl->key)) < (128 << 10)) continue;

      if(*partition && !strcmp(sl->key, *partition)) found = 1;
      last_found = last_part && !strcmp(sl->key, last_part) ? 1 : 0;

      sprintf(buf, "%s (%s)", sl->key, blk_ident(long_dev(sl->key)));

      type = util_fstype(long_dev(sl->key), NULL);

      if(type && !strcmp(type, "swap")) {
        values1[item_cnt1] = strdup(sl->key);
        items1[item_cnt1++] = strdup(buf);
        if(last_found) last_item1 = item_cnt1;
      }
      else if(type || (flags & (ICP_SWAP | ICP_BLOCK_ALL))) {
        values2[item_cnt2] = strdup(sl->key);
        items2[item_cnt2++] = strdup(buf);
        if(last_found) last_item2 = item_cnt2;
        if(type) {
          values3[item_cnt3] = strdup(sl->key);
          items3[item_cnt3++] = strdup(buf);
          if(last_found) last_item3 = item_cnt3;
        }
      }
    }
  }

  if(*partition && !found) {
    sprintf(buf, "%s (%s)", *partition, blk_ident(long_dev(*partition)));
    values2[item_cnt2] = strdup(*partition);
    items2[item_cnt2++] = strdup(buf);
  }

  if((flags & ICP_SWAP)) {
    values1[item_cnt1] = NULL;
    items1[item_cnt1++] = strdup("create swap partition");
    item_mk_part = item_cnt1;
    if(config.swap_file_size) {
      values1[item_cnt1] = NULL;
      items1[item_cnt1++] = strdup("create swap file");
      item_mk_file = item_cnt1;
    }
  }

  if((flags & ICP_SWAP)) {
    item_cnt = item_cnt1;
    items = items1;
    values = values1;
    last_item = last_item1;
  }
  else if((flags & ICP_BLOCK_ALL)) {
    item_cnt = item_cnt2;
    items = items2;
    values = values2;
    last_item = last_item2;
  }
  else {
    item_cnt = item_cnt3;
    items = items3;
    values = values3;
    last_item = last_item3;
  }

  rc = 1;
  if(item_cnt) {
    i = dia_list(txt_menu, 36, NULL, items, last_item, align_left);

    if(i == 0) rc = -1;

    if(i > 0 && values[i - 1]) {
      str_copy(&last_part, values[i - 1]);
      str_copy(partition, values[i - 1]);
      rc = 0;
    }

    if((flags & ICP_SWAP)) {
      if(i == item_mk_part) {
        do {
          i = dia_list("create a swap partition", 36, NULL, items2, last_item2, align_left);
          if(i > 0 && values2[i - 1]) {
            str_copy(&last_part, values2[i - 1]);
            dev = long_dev(values2[i - 1]);
            sprintf(buf, "Warning: all data on %s will be deleted!", dev);
            j = dia_contabort(buf, NO);
            if(j == YES) {
              sprintf(buf, "/sbin/mkswap %s", dev);
              if(!lxrc_run(buf)) {
                log_info("swapon %s\n", dev);
                if(swapon(dev, 0)) {
                  log_info("swapon: ");
                  perror_info(dev);
                  dia_message("Error activating swap space.", MSGTYPE_ERROR);
                }
                else {
                  rc = 0;
                }
              }
              else {
                dia_message("mkswap failed", MSGTYPE_ERROR);
              }
            }
            else {
              rc = 1;
            }
          }
        }
        while(rc && i);
      }
      else if(i == item_mk_file) {
        do {
          i = dia_list("select partition for swap file", 36, NULL, items3, last_item3, align_left);
          if(i > 0 && values3[i - 1]) {
            str_copy(&last_part, values3[i - 1]);
            dev = long_dev(values3[i - 1]);
            util_fstype(dev, &module);
            if(module) mod_modprobe(module, NULL);
            j = util_mount_rw(dev, config.mountpoint.swap, NULL);
            if(j) {
              dia_message("mount failed", MSGTYPE_ERROR);
            }
            else {
              char *tmp, file[256];
              int fd;
              window_t win;
              unsigned swap_size = config.swap_file_size << (20 - 18);	/* in 256k chunks */

              sprintf(file, "%s/suseswap.img", config.mountpoint.swap);

              tmp = calloc(1, 1 << 18);

              fd = open(file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
              if(fd >= 0) {
                sprintf(buf, "creating swap file 'suseswap.img' (%u MB)", config.swap_file_size);
                dia_status_on(&win, buf);
                for(j = 0; j < swap_size; j++) {
                  if(write(fd, tmp, 1 << 18) != 1 << 18) break;
                  fsync(fd);
                  dia_status(&win, (j + 1) * 100 / swap_size);
                }
                close(fd);
                dia_status_off(&win);
              }
              free(tmp);

              if(j != swap_size) {
                dia_message("failed to create swapfile", MSGTYPE_ERROR);
              }
              else {
                sprintf(buf, "/sbin/mkswap %s", file);
                if(!lxrc_run(buf)) {
                  log_info("swapon %s\n", file);
                  if(swapon(file, 0)) {
                    log_info("swapon: ");
                    perror_info(file);
                    dia_message("Error activating swap space.", MSGTYPE_ERROR);
                  }
                  else {
                    umount2(config.mountpoint.swap, MNT_DETACH);
                    rc = 0;
                  }
                }
                else {
                  dia_message("mkswap failed", MSGTYPE_ERROR);
                }
              }
              if(rc) util_umount(config.mountpoint.swap);
            }
          }
        }
        while(rc && i);
      }
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

  slist_free(device_list);

  for(i = 0; i < item_cnt1; i++) { free(items1[i]); free(values1[i]); }
  free(items1);
  free(values1);
  for(i = 0; i < item_cnt2; i++) { free(items2[i]); free(values2[i]); }
  free(items2);
  free(values2);
  for(i = 0; i < item_cnt3; i++) { free(items3[i]); free(values3[i]); }
  free(items3);
  free(values3);

  // log_info("rc = %d\n", rc);

  return rc;
}


/*
 * Select and mount cdrom repo.
 *
 * return:
 *   0: ok
 *   1: failed
 */
int inst_do_cdrom()
{
  int err = 0;
  char *device = NULL;

  if(net_config_needed(0) && net_config()) return 1;

  if(
    config.url.install &&
    config.url.install->scheme == inst_cdrom
  ) {
    str_copy(&device, config.url.install->device);
  }

  url_free(config.url.install);
  config.url.install = url_set("cd:");
  str_copy(&config.url.install->device, device);
  str_copy(&config.url.install->used.device, long_dev(device));

  err = auto2_find_repo() ? 0 : 1;

  str_copy(&device, NULL);

  return err;
}


/*
 * Select and mount disk repo.
 *
 * return:
 *   0: ok
 *   1: failed
 */
int inst_do_harddisk()
{
  int err = 0;
  char *device = NULL, *path = NULL;

  if(net_config_needed(0) && net_config()) return 1;

  if(
    config.url.install &&
    (
      config.url.install->scheme == inst_disk ||
      config.url.install->scheme == inst_hd
    )
  ) {
    str_copy(&device, config.url.install->device);
    str_copy(&path, config.url.install->path);
  }

  if(inst_choose_partition(&device, 0, "Choose the hard disk partition.", "Enter the hard disk partition (e.g., /dev/sda1)")) err = 1;
  if(!err && dia_input2("Enter the source directory.", &path, 30, 0)) err = 1;

  if(!err) {
    url_free(config.url.install);
    config.url.install = url_set("hd:");
    str_copy(&config.url.install->device, device);
    str_copy(&config.url.install->path, path ?: "/");
    str_copy(&config.url.install->used.device, long_dev(device));

    err = auto2_find_repo() ? 0 : 1;
  }

  str_copy(&device, NULL);
  str_copy(&path, NULL);

  return err;
}


/*
 * Select and mount local repo.
 *
 * return:
 *   0: ok
 *   1: failed
 */
int inst_do_mountable(instmode_t scheme)
{
  int err = 0;
  char *device = NULL, *path = NULL;

  if(net_config_needed(0) && net_config()) return 1;

  if(
    config.url.install &&
    config.url.install->scheme == scheme
  ) {
    str_copy(&device, config.url.install->device);
    str_copy(&path, config.url.install->path);
  }

  if(url_is_blockdev(scheme)) {
    if(inst_choose_partition(&device, ICP_BLOCK_ALL, "Choose disk / partition.", "Enter the device name (e.g., /dev/sda1)")) err = 1;
  }

  if(!err && !url_is_nopath(scheme)) {
    if(dia_input2("Enter the source directory.", &path, 30, 0)) err = 1;
  }

  if(!err) {
    char *tmp_url = NULL;
    strprintf(&tmp_url, "%s:", url_scheme2name(scheme));
    url_free(config.url.install);
    config.url.install = url_set(tmp_url);
    str_copy(&tmp_url, NULL);
    str_copy(&config.url.install->device, device);
    str_copy(&config.url.install->path, path ?: "/");
    str_copy(&config.url.install->used.device, long_dev(device));

    err = auto2_find_repo() ? 0 : 1;
  }

  str_copy(&device, NULL);
  str_copy(&path, NULL);

  return err;
}


/*
 * Select and mount network repo.
 *
 * return:
 *   0: ok
 *   1: failed
 */
int inst_do_network(instmode_t scheme)
{
  int i, err = 0, port = 0, n_port = 0, proxy_port = 0;
  unsigned u;
  char *s;
  char *buf = NULL, *buf2 = NULL, *path = NULL, *share = NULL, *domain = NULL,
    *user = NULL, *password = NULL, *proxy_user = NULL, *proxy_password = NULL,
    *n_user = NULL, *n_password = NULL;
  char **to_free[] = {
    &buf, &buf2, &path, &share, &domain, &user, &password, &proxy_user, &proxy_password,
    &n_user, &n_password
  };
  inet_t server = {}, proxy = {};

  /* setup network */
  if(net_config_needed(1) && net_config()) return 1;

  /* get current values */
  if(config.url.install) {
    str_copy(&path, config.url.install->path);
    name2inet(&server, config.url.install->server);
    str_copy(&share, config.url.install->share);
    str_copy(&domain, config.url.install->domain);
    str_copy(&user, config.url.install->user);
    str_copy(&password, config.url.install->password);
    port = config.url.install->port;
  }

  /* server name */
  strprintf(&buf, "Enter the name of the %s server.", url_scheme2name_upper(scheme));
  if(net_get_address2(buf, &server, 1, &n_user, &n_password, &n_port)) err = 1;
  if(!err && n_port) port = n_port;

  /* smb share */
  if(!err && scheme == inst_smb && dia_input2("Enter the share name on the SMB server.", &share, 30, 0)) err = 1;

  /* path */
  if(!err && dia_input2("Enter the directory on the server.", &path, 30, 0)) err = 1;

  /* ensure leading "/" if mountable */
  if(!err && url_is_mountable(scheme)) {
    if(path) {
      if(*path != '/') {
        strprintf(&path, "/%s", path);
      }
    }
    else {
      path = strdup("/");
    }
  }

  /* user, password */
  if(!err && url_is_auth(scheme)) {
    if(!n_user) {
      strprintf(&buf,
        "Do you need a username and password to access the %s server?",
        url_scheme2name_upper(scheme)
      );
      i = dia_yesno(buf, NO);

      if(i == ESCAPE) {
        err = 1;
      }
      else {
        if(i == NO) {
          str_copy(&user, NULL);
          str_copy(&password, NULL);
        }
        else {
          strprintf(&buf, "Enter the user name with which to access the %s server.", url_scheme2name_upper(scheme));
          strprintf(&buf2, "Enter the password for the %s server.", url_scheme2name_upper(scheme));
          if(
            dia_input2(buf, &user, 20, 0) ||
            dia_input2(buf2, &password, 20, 1)
          ) err = 1;
        }
      }
    }
    else {
      str_copy(&user, n_user);
      str_copy(&password, n_password);
    }
  }

  /* smb user, password, workgroup */
  if(!err && scheme == inst_smb) {
    i = dia_yesno("If you want to access the share as guest,you do not need to specify user name, password, and workgroup.\n"
                  "Access the share as guest?", YES);

    if(i == ESCAPE) {
      err = 1;
    }
    else {
      if(i == YES) {
        str_copy(&user, NULL);
        str_copy(&password, NULL);
        str_copy(&domain, NULL);
      }
      else {
        if(
          dia_input2("Enter your user name on the SMB server.", &user, 20, 0) ||
          dia_input2("Enter your password on the SMB server.", &password, 20, 1) ||
          dia_input2("Enter your workgroup.If you do not need to specify a workgroup,leave the field empty.", &domain, 20, 0)
        ) err = 1;
      }
    }
  }

  /* proxy setup */
  if(!err && (scheme == inst_http || scheme == inst_https || scheme == inst_ftp)) {

    /* get current proxy values*/
    if(config.url.proxy) {
      proxy_port = config.url.proxy->port;
      name2inet(&proxy, config.url.proxy->server);
      str_copy(&proxy_user, config.url.proxy->user);
      str_copy(&proxy_password, config.url.proxy->password);
    }

    strprintf(&buf, "Use a %s proxy?", url_scheme2name_upper(inst_http));
    i = dia_yesno(buf, NO);
    if(i == ESCAPE) {
      err = 1;
    }
    else if(i == YES) {
      /* new proxy */
      strprintf(&buf, "Enter the name of the %s proxy.", url_scheme2name_upper(inst_http));
      if(net_get_address2(buf, &proxy, 1, &n_user, &n_password, &n_port)) err = 1;

      if(!err) {
        if(!n_port) {
          strprintf(&buf2, "%u", proxy_port);
          strprintf(&buf, "Enter the port of the %s proxy.", url_scheme2name_upper(inst_http));
          if(dia_input2(buf, &buf2, 6, 0)) err = 1;
          if(!err) {
            u = strtoul(buf2, &s, 0);
            if(*s) {
              dia_message("Invalid input.", MSGTYPE_ERROR);
              err = 1;
            }
            else {
              proxy_port = u;
            }
          }
        }
        else {
          proxy_port = n_port;
        }
      }

      /* proxy user, password */
      if(!err) {
        if(!n_user) {
          i = dia_yesno("Do you need a username and password to access the proxy?", NO);

          if(i == ESCAPE) {
            err = 1;
          }
          else {
            if(i == NO) {
              str_copy(&proxy_user, NULL);
              str_copy(&proxy_password, NULL);
            }
            else {
              strprintf(&buf, "Enter the user name with which to access the %s server.", "proxy");
              strprintf(&buf2, "Enter the password for the %s server.", "proxy");
              if(
                dia_input2(buf, &proxy_user, 20, 0) ||
                dia_input2(buf2, &proxy_password, 20, 1)
              ) err = 1;
            }
          }
        }
        else {
          str_copy(&proxy_user, n_user);
          str_copy(&proxy_password, n_password);
        }
      }
    }
    else {
      name2inet(&proxy, "");
    }

    /* set new proxy values */
    if(!err) {
      url_free(config.url.proxy);
      config.url.proxy = NULL;

      if(proxy.name) {
        strprintf(&buf, "http://%s", proxy.name);
        config.url.proxy = url_set(buf);

        str_copy(&config.url.proxy->user, proxy_user);
        str_copy(&config.url.proxy->password, proxy_password);
        config.url.proxy->port = proxy_port;
      }
    }
  }

  /* set new values */
  if(!err) {
    url_free(config.url.install);

    if(server.name && strchr(server.name, ':')) {
      strprintf(&buf, "%s://[%s]", url_scheme2name(scheme), server.name);
    }
    else {
      strprintf(&buf, "%s://%s", url_scheme2name(scheme), server.name);
    }
    config.url.install = url_set(buf);

    memset(&server, 0, sizeof server);

    config.url.install->port = port;

    str_copy(&config.url.install->path, path);

    if(scheme == inst_smb) {
      str_copy(&config.url.install->share, share);
      str_copy(&config.url.install->domain, domain);
    }

    if(scheme == inst_http || scheme == inst_http || scheme == inst_ftp || scheme == inst_smb) {
      str_copy(&config.url.install->user, user);
      str_copy(&config.url.install->password, password);
    }

    err = auto2_find_repo() ? 0 : 1;
  }

  /* free variables */
  for(i = 0; i < sizeof to_free / sizeof *to_free; i++) str_copy(*to_free, NULL);
  name2inet(&server, "");
  name2inet(&proxy, "");

  return err;
}


/*
 * Start YaST.
 *
 * return:
 *   0: ok
 *   1: err
 */
int inst_start_install()
{
  int err = 0;

  LXRC_WAIT

  util_splash_bar(60);

  if(config.manual) {
    util_umount_all();
    util_clear_downloads();

    url_free(config.url.instsys);
    config.url.instsys = url_set(config.rescue ? config.rescueimage : config.rootimage);

    if(inst_choose_source()) err = 1;
  }

  /* if the user specified a hostname, set it */
  util_set_hostname(config.net.realhostname);

  if(! err && config.rescue) {
    /* get rid of repo */
    url_umount(config.url.install);

    return 0;
  }

  if(!err &&
    (config.manual || (config.net.setup & NS_DISPLAY)) &&
    inst_choose_display()
  ) err = 1;

  if(config.debug >= 2) util_status_info(1);
  
  if(!err) err = inst_execute_yast();

  config.extend_list = slist_free(config.extend_list);
  unlink("/etc/instsys.parts");

  util_umount_all();
  util_clear_downloads();

  if(!err) {
    err = inst_commit_install();
    if(err) {
      config.rescue = 0;
      config.manual |= 1;
      util_disp_init();
    }
  }

  return err;
}


/*
 * Prepare instsys.
 *
 * return:
 *   0: ok
 *   1: error
 */
int add_instsys()
{
  char *buf = NULL, *argv[3] = { }, *mp;
  int err = 0, i;
  slist_t *sl;

  if(!config.url.instsys->mount) return 1;

  setenv("TERM", config.term ?: config.serial ? "screen" : "linux", 1);

  setenv("ESCDELAY", config.serial ? "1100" : "10", 1);

  setenv("YAST_DEBUG", "/debug/yast.debug", 1);

  if(!config.test) {
    for(sl = config.url.instsys_list; sl; sl = sl->next) {
      log_debug("instsys_list: key = %s, value = %s\n", sl->key, sl->value);
      if(!sl->value) return 1;
      argv[1] = sl->value;
      argv[2] = "/";
      util_lndir_main(3, argv);
    }
  }

  for(i = 0; i < config.update.ext_count; i++) {
    mp = new_mountpoint();
    strprintf(&buf, "%s/dud_%04u", config.download.base, i);
    if(!util_mount_ro(buf, mp, 0)) {
      if(!config.test) {
        argv[1] = mp;
        argv[2] = "/";
        util_lndir_main(3, argv);
      }
    }
  }

  file_read_info_file("file:/.instsys.config", kf_cfg);

  file_write_install_inf("");

  if(
    config.instsys_complain &&
    config.initrd_id &&
    config.instsys_id &&
    strcmp(config.initrd_id, config.instsys_id)
  ) {
    int win;

    if(!(win = config.win)) util_disp_init();

    /*
     * config.kexec settings:
     *   2: ask user whether to load kernel
     *   3: load kernel without user confirmation
     *
     * Note: config.kexec == 1 is handled in auto2_find_repo(); we never get
     * here in this case.
     */
    if(
      config.kexec == 3 ||
      (
        config.kexec == 2 &&
        YES == dia_yesno(
          "To use the selected repository a matching boot image is needed.\n"
          "\n"
          "Download it now and restart?",
          YES
        )
      )
    ) {
      auto2_kexec(config.url.install);
      log_info("kexec failed\n");
    }

    if(config.instsys_complain == 1) {
      dia_message(
        "Installation system does not match your boot medium.\n\n"
        "This may cause problems and make your bugreports worthless.",
        MSGTYPE_ERROR
      );
    }
    else {
      dia_message(
        "Installation system does not match your boot medium.\n\n"
        "Sorry, this will not work.",
        MSGTYPE_ERROR
      );
      err = 1;
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

      strprintf(&buf,
        "The following driver update has not been applied:\n\n%s\n\n"
        "You can continue, but things will not work as expected.\n"
        "If you don't want to see this message, boot with 'updatecomplain=0'.",
        sl->key
      );

      dia_message(buf, MSGTYPE_ERROR);

      if(!win) util_disp_done();
    }
  }

  str_copy(&buf, NULL);

  return err;
}


void inst_yast_done()
{
  int count;
  char *buf = NULL;

  if(config.test) return;

  lxrc_killall(0);

  for(count = 0; count < 8; count++) {
    strprintf(&buf, "/dev/loop%d", count);
    util_detach_loop(buf);
  }

  str_copy(&buf, NULL);
}


int inst_execute_yast()
{
  int i, err = 0;
  char *setupcmd = NULL;
  FILE *f;

  LXRC_WAIT

  if(config.url.install->scheme != inst_exec) err = add_instsys();
  if(err) {
    inst_yast_done();

    return err;
  }

  log_debug("= config.url.install =");
  url_log(config.url.install);
  log_debug("= config.url.instsys =");
  url_log(config.url.instsys);

  // we can't unmount if yast runs directly from the install medium
  if(config.download.instsys) {
    log_debug("unmounting installation medium");
    url_umount(config.url.instsys);
    url_umount(config.url.install);
  }
  else {
    log_debug("insufficient memory - unmounting installation medium not possible");
  }

  if(!config.test) {
    if(util_check_exist("/sbin/update")) lxrc_run("/sbin/update");
  }

  i = 0;
  util_free_mem();
  if(config.addswap) {
    i = ask_for_swap(
      config.addswap == 2 ? -1 : config.memoryXXX.min_yast - config.memoryXXX.min_free,
      "Your computer does not have enough memory to run YaST."
    );
  }

  if(i == -1) {
    inst_yast_done();
    return -1;
  }

  util_free_mem();

  if(!config.test && (config.net.sshpassword || config.net.sshpassword_enc)) {
    if((f = popen(config.net.sshpassword_enc ? "chpasswd -e" : "chpasswd", "w"))) {
      fprintf(f, "root:%s\n", config.net.sshpassword_enc ?: config.net.sshpassword);
      pclose(f);
    }
  }

  /* start shells only _after_ the swap dialog */
  if(!config.test && !config.noshell) {
    util_start_shell("/dev/tty2", "/bin/bash", 3);
    util_start_shell("/dev/tty5", "/bin/bash", 3);
    util_start_shell("/dev/tty6", "/bin/bash", 3);
  }

  disp_set_color(COL_WHITE, COL_BLACK);
  if(config.win) util_disp_done();

  str_copy(&setupcmd, config.setupcmd);

  if(config.url.install->scheme == inst_exec) {
    strprintf(&setupcmd, "setsid -wc %s",
      *config.url.install->path ? config.url.install->path : "/bin/sh"
    );
  }

  // inst_setup turns plymouth off, so don't do it here
  if(!(setupcmd && strstr(setupcmd, " inst_setup "))) {
    util_plymouth_off();
  }

  log_info("starting %s\n", setupcmd);

  LXRC_WAIT

  kbd_end(1);
  if(!config.test) util_notty();

  if(config.test) {
    err = system("/bin/bash 2>&1");
  }
  else {
    if(config.zombies) {
      err = system(setupcmd);
    }
    else {
      pid_t pid, inst_pid;

      inst_pid = fork();

      if(inst_pid) {
        // log_info("%d: inst_pid = %d\n", getpid(), inst_pid);

        while((pid = waitpid(-1, &err, 0))) {
          // log_info("%d: chld(%d) = %d\n", getpid(), pid, err);
          if(pid == inst_pid) {
            // log_info("%d: last chld\n", getpid());
            break;
          }
        }

        // log_info("%d: back from loop\n", getpid());
      }
      else {
        signal(SIGUSR1, SIG_IGN);

        // stdout = stderr
        dup2(1, 2);
        // close other file descriptors
        for(int fd = 3; fd < 10; fd++) close(fd);

        // log_info("%d: system()\n", getpid());
        err = system(setupcmd);
        // log_info("%d: exit(%d)\n", getpid(), err);
        exit(WEXITSTATUS(err));
      }

      // log_info("%d: back, err = %d\n", getpid(), err);
    }
  }

  if(err) {
    if(err == -1) {
      err = errno;
    }
    else if(WIFEXITED(err)) {
      err = WEXITSTATUS(err);
    }
  }

  if(!config.test && !config.listen) {
    freopen(config.console, "r", stdin);
    freopen(config.console, "a", stdout);
    freopen(config.console, "a", stderr);
  }
  else {
    dup2(1, 0);
    config.kbd_fd = 0;
  }
  kbd_init(0);
  util_notty();

  lxrc_readd_parts();

  str_copy(&setupcmd, NULL);

  log_info("install program exit code is %d\n", err);

  /* Redraw erverything and go back to the main menu. */
  config.redraw_menu = 1;

  log_info("sync...");
  sync();
  log_info(" ok\n");

  LXRC_WAIT

  i = file_read_yast_inf();
  if(!err) err = i;

  disp_cursor_off();
  kbd_reset();

  if(err || config.aborted) {
    config.rescue = 0;
    config.manual |= 1;
  }

  if(config.manual) util_disp_init();

  if(err && !config.aborted && config.win) {
    dia_message("An error occurred during the installation.", MSGTYPE_ERROR);
  }

  if(!config.test) {
    /* never trust yast */
    mount(0, "/", 0, MS_MGC_VAL | MS_REMOUNT, 0);
  }

  inst_yast_done();

  if(config.aborted) {
    config.aborted = 0;
    err = -1;
  }

  return err;
}


/*
 * If we should reboot, do it.
 *
 * return:
 *   0: ok (only in test mode, obviously)
 *   1: failed
 */
int inst_commit_install()
{
  int err = 1;

  switch(config.restart_method) {
    case 1:	/* reboot */
      if(config.rebootmsg){
        disp_clear_screen();
        util_disp_init();
        dia_message("The base system was successfully installed. Your machine must now be rebooted. Remove all installation media (CD-ROM, floppy).\n"
                    "Do not remove the floppy disk if the boot manager was installed on it.", MSGTYPE_INFO);
      }

      if(config.test) {
        log_info("*** reboot ***\n");
        break;
      }

      reboot(RB_AUTOBOOT);
      break;

    case 2:	/* power off */
      if(config.test) {
        log_info("*** power off ***\n");
        break;
      }

      reboot(RB_POWER_OFF);
      break;

    case 3:	/* kexec */
      if(config.test) {
        log_info("*** kexec ***\n");
        break;
      }

      lxrc_run("kexec -e");
      break;

    default:	/* do nothing */
      err = 0;
      break;
  }

  return err;
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
  char *dev;
  url_t *url;

  config.update.shown = 1;

  if(choose_dud(&dev)) return 1;

  if(!dev) return 0;

  url = url_set("disk:/");
  url->device = strdup(short_dev(dev));

  auto2_driverupdate(url);

  url_umount(url);
  url_free(url);

  util_do_driver_updates();

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
  return util_choose_disk_device(dev, 1, "Please choose the Driver Update medium.", "Please enter the Driver Update device.");
}


/*
 * Check if we still have enough free memory for 'size'. If not, ask user
 * for more swap.
 *
 * size: in kbytes!
 *
 * return: 0 ok, -1 error
 */
int ask_for_swap(int64_t size, char *msg)
{
  int i, j, did_init = 0;
  char *partition = NULL;
  char *argv[] = { NULL, NULL };

  if(size >= 0 && config.memoryXXX.current >= config.memoryXXX.min_free + size) return 0;

  if(!config.win) {
    util_disp_init();
    did_init = 1;
  }

  do {
    j = inst_choose_partition(&partition, ICP_SWAP, "To continue, activate some swap space.", "Enter the swap partition (e.g., /dev/sda2)");

    if(j == 0 && partition) {
      argv[1] = long_dev(partition);
      log_info("swapon %s\n", argv[1]);
      i = util_swapon_main(2, argv);
      if(i) {
        dia_message("Error activating swap space.", MSGTYPE_ERROR);
        j = 1;
      }
    }
    util_free_mem();
  }
  while(j > 0);

  str_copy(&partition, NULL);

  if(did_init) util_disp_done();

  return j;
}

