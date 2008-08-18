/*
 *
 * rootimage.c   Loading of rootimage
 *
 * Copyright (c) 1996-2002  Hubert Mantel, SuSE Linux AG  (mantel@suse.de)
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/time.h>
#include <ctype.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <endian.h>
#include <errno.h>

#include "global.h"
#include "text.h"
#include "util.h"
#include "dialog.h"
#include "window.h"
#include "display.h"
#include "rootimage.h"
#include "module.h"
#include "linuxrc.h"
#include "install.h"

#include "linux_fs.h"

static int  root_check_root      (char *root_string_tv);

/*
 * Check if we still have enough free memory for 'size'. If not, ask user
 * for more swap.
 *
 * size: in kbytes!
 *
 * return: 0 ok, -1 error
 */
int ask_for_swap(int size, char *msg)
{
  int i, j, did_init = 0;
  char *partition = NULL;
  char *argv[] = { NULL, NULL };

  if(size >= 0 && config.memory.current >= config.memory.min_free + size) return 0;

  if(!config.win) {
    util_disp_init();
    did_init = 1;
  }

#if 0
  // sprintf(tmp, "%s\n\n%s", msg, txt_get(TXT_ADD_SWAP));
#endif

  do {
    j = inst_choose_partition(&partition, 1, txt_get(TXT_ADD_SWAP), txt_get(TXT_ENTER_SWAP));
    
    if(j == 0 && partition) {
      argv[1] = long_dev(partition);
      fprintf(stderr, "swapon %s\n", argv[1]);
      i = util_swapon_main(2, argv);
      if(i) {
        dia_message(txt_get(TXT_ERROR_SWAP), MSGTYPE_ERROR);
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


int root_check_root(char *root_string_tv)
{
  char buf[256];
  int rc;

  if(strstr(root_string_tv, "/dev/") == root_string_tv) {
    root_string_tv += sizeof "/dev/" - 1;
  }

  sprintf(buf, "/dev/%s", root_string_tv);

  if(util_mount_ro(buf, config.mountpoint.instdata, NULL)) return -1;

  sprintf(buf, "%s/etc/passwd", config.mountpoint.instdata);
  rc = util_check_exist(buf);

  umount(config.mountpoint.instdata);

  return rc == 'r' ? 0 : -1;
}


int root_boot_system()
{
  int  rc;
  char *module, *type;
  char buf[256], root[256];

  do {
    rc = inst_choose_partition(&config.device, 0, txt_get(TXT_CHOOSE_ROOT_FS), txt_get(TXT_ENTER_ROOT_FS));
    if(rc || !config.device) return -1;
    sprintf(root, "/dev/%s", config.device);

    if((type = util_fstype(root, &module))) {
      if(module && config.module.dir) {
        sprintf(buf, "%s/%s" MODULE_SUFFIX, config.module.dir, module);
        mod_modprobe(module, NULL);
      }
    }

    if((rc = root_check_root(root))) {
      dia_message(txt_get(TXT_INVALID_ROOT_FS), MSGTYPE_ERROR);
    }
  }
  while(rc);

  str_copy(&config.new_root, root);

  return 0;
}


