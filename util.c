/*
 *
 * util.c        Utility functions for linuxrc
 *
 * Copyright (c) 1996-2002  Hubert Mantel, SuSE Linux AG (mantel@suse.de)
 *
 */

#define __LIBRARY__

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdarg.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/vfs.h>
#include <sys/mount.h>
#include <sys/ioctl.h>
#include <sys/kd.h>
#include <sys/utsname.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <time.h>
#include <syscall.h>
#include <dirent.h>
#include <errno.h>
#include <signal.h>
#include <sys/swap.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <utime.h>
#include <net/if.h>
#include <linux/major.h>
#include <linux/raid/md_u.h>

// #include <linux/cdrom.h>
#define CDROMEJECT	0x5309	/* Ejects the cdrom media */
/* Glibc 2.2 defines __dev_t_defined instead. */
#ifndef dev_t
#define dev_t dev_t
#endif
#include <linux/loop.h>

#include <hd.h>

#include "global.h"
#include "display.h"
#include "util.h"
#include "window.h"
#include "module.h"
#include "keyboard.h"
#include "text.h"
#include "dialog.h"
#include "ftp.h"
#include "net.h"
#include "auto2.h"
#include "file.h"
#include "lsh.h"
#include "bootpc.h"
#include "http.h"
#include "fstype.h"

#define LED_TIME     50000

static inline _syscall3 (int,syslog,int,type,char *,b,int,len);

static char  *util_loopdev_tm = "/dev/loop0";

static void show_lsof_info(FILE *f, unsigned pid);
static void show_ps_info(FILE *f, unsigned pid);

static void net_read_cleanup();

static struct hlink_s {
  struct hlink_s *next;
  dev_t dev;
  ino_t ino;
  char *dst;
} *hlink_list = NULL;

static char *exclude = NULL;
static int rec_level = 0;

static void add_flag(slist_t **sl, char *buf, int value, char *name);

static int do_cp(char *src, char *dst);
static char *walk_hlink_list(ino_t ino, dev_t dev, char *dst);
static void free_hlink_list(void);

static char *util_attach_loop(char *file, int ro);
static int util_detach_loop(char *dev);


void util_redirect_kmsg (void)
    {
    int   fd_ii;
    char  newvt_aci [2];


    if (config.serial)
        syslog (8, 0, 1);
    else
        {
        fd_ii = open (config.console, O_RDONLY);
        if (fd_ii)
            {
            newvt_aci [0] = 11;
            newvt_aci [1] = 4;
            ioctl (fd_ii, TIOCLINUX, &newvt_aci);
            close (fd_ii);
            }
        }
    }


void util_center_text (char *txt_tr, int size_iv)
    {
    int  length_ii;
    char tmp_txt_ti [MAX_X];

    strcpy (tmp_txt_ti, txt_tr);
    length_ii = strlen (tmp_txt_ti);
    memset (txt_tr, 32, size_iv);
    if (length_ii < size_iv)
        memcpy (&txt_tr [(size_iv - length_ii - 1) / 2],
                tmp_txt_ti, length_ii);
    else
        memcpy (txt_tr, tmp_txt_ti, size_iv);

    txt_tr [size_iv - 1] = 0;
    }


void util_generate_button(button_t *button, char *txt, int size)
{
  size = size > 8 ? BUTTON_SIZE_LARGE : BUTTON_SIZE_NORMAL;

  memset(button, 0, sizeof *button);
  strncpy(button->text, txt, size - 1);
  util_center_text(button->text, size);
}


int util_format_txt (char *txt_tv, char *lines_atr [], int width_iv)
    {
    int  current_line_ii;
    int  i_ii;
    int  pos_ii;


    current_line_ii = 0;
    i_ii = 0;
    pos_ii = 0;
    lines_atr [current_line_ii] = malloc (width_iv);
    lines_atr [current_line_ii][0] = 0;

    while (txt_tv [i_ii] && current_line_ii < MAX_Y)
        {
        while (txt_tv [i_ii] && txt_tv [i_ii] != '\n' && pos_ii < width_iv)
            lines_atr [current_line_ii][pos_ii++] = txt_tv [i_ii++];

        if (pos_ii == width_iv || txt_tv [i_ii] == '\n')
            {
            if (pos_ii == width_iv)
                {
                do
                    {
                    --pos_ii;
                    --i_ii;
                    }
                while (lines_atr [current_line_ii][pos_ii] != ' ' && pos_ii);

                if (pos_ii == 0)
                    {
                    pos_ii = width_iv;
                    i_ii += width_iv - 1;
                    }
                }

            lines_atr [current_line_ii][pos_ii] = 0;
            pos_ii = 0;
            ++i_ii;
            if (txt_tv [i_ii])
                lines_atr [++current_line_ii] = malloc (width_iv);
            }
        else
            lines_atr [current_line_ii][pos_ii] = 0;
        }

    for (i_ii = 0; i_ii <= current_line_ii; i_ii++)
        util_center_text (lines_atr [i_ii], width_iv);

    return (current_line_ii + 1);
    }


void util_fill_string (char *txt_tr, int size_iv)
    {
    int i_ii = 0;

    while (txt_tr [i_ii] && i_ii < size_iv - 1)
        ++i_ii;

    while (i_ii < size_iv - 1)
        txt_tr [i_ii++] = ' ';

    txt_tr [i_ii] = 0;
    }


void util_create_items (item_t items_arr [], int nr_iv, int size_iv)
    {
    int  i_ii;

    for (i_ii = 0; i_ii < nr_iv; i_ii++)
        {
        items_arr [i_ii].text = malloc (size_iv + 1);
        items_arr [i_ii].func = 0;
        items_arr [i_ii].di = 0;
        }
    }


void util_free_items (item_t items_arr [], int nr_iv)
    {
    int  i_ii;

    for (i_ii = 0; i_ii < nr_iv; i_ii++)
        free (items_arr [i_ii].text);
    }


int util_fileinfo(char *file_name, int *size, int *compressed)
{
  unsigned char buf[4];
  int fd, err = 0;
  off_t ofs;

  if(size) *size = 0;
  if(compressed) *compressed = 0;

  if(!(fd = open(file_name, O_RDONLY))) return -1;

  if(read(fd, buf, 2) != 2) {
    close(fd);
    return -1;
  }

  if(buf[0] == 0x1f && (buf[1] == 0x8b || buf[1] == 0x9e)) {
    if(
      lseek(fd, (off_t) -4, SEEK_END) == (off_t) -1 ||
      read(fd, buf, 4) != 4
    ) {
      err = -1;
    }
    else {
      if(size) *size = (buf[3] << 24) + (buf[2] << 16) + (buf[1] << 8) + buf[0];
      if(compressed) *compressed = 1;
    }
  }
  else {
    if(size) {
      ofs = lseek(fd, 0, SEEK_END);
      if(ofs != -1) {
        *size = ofs;
      }
      else {
        err = -1;
      }
    }
  }

  close(fd);

  return err;
}


void util_update_kernellog (void)
    {
    FILE  *outfile_pri;
    FILE  *lastfile_pri;
    FILE  *bootmsg_pri;
    char   buffer_ti [16384];
    char   line_ti [MAX_X - 30];
    int    i_ii = 1;
    int    pos_ii;
    int    size_ii;


    outfile_pri = fopen (kernellog_tg, "a");
    if (!outfile_pri)
        return;

    lastfile_pri = fopen (lastlog_tg, "w");
    if (!lastfile_pri)
        {
        fclose (outfile_pri);
        return;
        }

    bootmsg_pri = fopen (bootmsg_tg, "a");

    size_ii = syslog (3, buffer_ti, sizeof (buffer_ti));

    if (size_ii > 0 && bootmsg_pri)
        fwrite (buffer_ti, 1, size_ii, bootmsg_pri);

    if (bootmsg_pri) fclose (bootmsg_pri);

    for (pos_ii = 0; pos_ii < size_ii; pos_ii++)
        {
        line_ti [i_ii] = buffer_ti [pos_ii];
        if (line_ti [i_ii] == '\n' || i_ii >= sizeof (line_ti) - 2)
            {
            line_ti [i_ii + 1] = 0;
            if (line_ti [1] == '<')
                {
                fputs (line_ti + 4, outfile_pri);
                fputs (line_ti + 4, lastfile_pri);
                }
            else
                {
                fputs (line_ti + 1, outfile_pri);
                fputs (line_ti + 1, lastfile_pri);
                }
            i_ii = 0;
            }

        ++i_ii;
        }

    (void) syslog (5, 0, 0);

    fclose (outfile_pri);
    fclose (lastfile_pri);
    }


void util_print_banner (void)
    {
    window_t       win_ri;
    char           text_ti [MAX_X];
    struct utsname utsinfo_ri;

    if(!config.win) return;

    uname (&utsinfo_ri);
    if (config.linemode) {
      printf (">>> Linuxrc v" LXRC_VERSION " (Kernel %s) (c) 1996-2002 SuSE Linux AG <<<\n", utsinfo_ri.release);
        return;
    }
    memset (&win_ri, 0, sizeof (window_t));
    win_ri.x_left = 1;
    win_ri.y_left = 1;
    win_ri.x_right = max_x_ig;
    win_ri.y_right = max_y_ig;
    win_ri.style = STYLE_RAISED;
    win_ri.head = 3;
    if (colors_prg->has_colors)
        {
        win_ri.bg_color = colors_prg->bg;
        win_ri.fg_color = COL_BLACK;
        }
    else
        {
        win_ri.bg_color = colors_prg->msg_win;
        win_ri.fg_color = colors_prg->msg_fg;
        }
    win_open (&win_ri);
    win_clear (&win_ri);

    win_ri.x_left = 2;
    win_ri.y_left = 2;
    win_ri.x_right = max_x_ig - 1;
    win_ri.y_right = 4;
    win_ri.head = 0;
    win_ri.style = STYLE_SUNKEN;
    win_open (&win_ri);

    sprintf (text_ti, ">>> Linuxrc v" LXRC_VERSION " (Kernel %s) (c) 1996-2002 SuSE Linux AG <<<",
             utsinfo_ri.release);
    util_center_text (text_ti, max_x_ig - 4);
    disp_set_color (colors_prg->has_colors ? COL_BWHITE : colors_prg->msg_fg,
                    win_ri.bg_color);
    win_print (&win_ri, 1, 1, text_ti);
    fflush (stdout);
    }


void util_beep (int  success_iv)
    {
    int  fd_ii;


    fd_ii = open ("/dev/console", O_RDWR);
    if (fd_ii < 0)
        return;

    ioctl (fd_ii, KDMKTONE, (150 << 16) | 1000);
    usleep (150 * 1000);

    if (success_iv)
        ioctl (fd_ii, KDMKTONE, (150 << 16) | 4000);
    else
        {
        ioctl (fd_ii, KDMKTONE, (150 << 16) | 1000);
        usleep (150 * 1000);
        ioctl (fd_ii, KDMKTONE, (150 << 16) | 1000);
        }

    close (fd_ii);
    }


int util_mount_loop (char *file_tv, char *mountpoint_tv)
    {
    struct loop_info  loopinfo_ri;
    int               fd_ii;
    int               device_ii;
    int               rc_ii;


    fprintf (stderr, "Trying loopmount %s\n", file_tv);

    fd_ii = open (file_tv, O_RDONLY);
    if (fd_ii < 0)
        return (-1);

    device_ii = open (util_loopdev_tm, O_RDONLY);
    if (device_ii < 0)
        {
        close (fd_ii);
        return (-1);
        }

    memset (&loopinfo_ri, 0, sizeof (loopinfo_ri));
    strcpy (loopinfo_ri.lo_name, file_tv);
    rc_ii = ioctl (device_ii, LOOP_SET_FD, fd_ii);
    if (!(rc_ii < 0))
        rc_ii = ioctl (device_ii, LOOP_SET_STATUS, &loopinfo_ri);

    close (fd_ii);
    close (device_ii);
    if (rc_ii < 0)
        return (rc_ii);

    rc_ii = util_mount_ro (util_loopdev_tm, mountpoint_tv);

    fprintf (stderr, "Loopmount returns %d\n", rc_ii);

    return (rc_ii);
    }


void util_umount_loop (char *mountpoint_tv)
    {
    int   fd_ii;


    umount (mountpoint_tv);

    fd_ii = open (util_loopdev_tm, O_RDONLY);
    if (fd_ii >= 0)
        {
        ioctl (fd_ii, LOOP_CLR_FD, 0);
        close (fd_ii);
        }
    }


/*
 * Do we really need this?
 *
 * Never lengthen the string!
 */
void util_truncate_dir(char *dir)
{
  int l;

  if(!dir) return;

  l = strlen(dir);

  if(l > 1 && dir[l - 1] == '/') dir[l - 1] = 0;
}


int util_check_exist (char *filename_tv)
    {
    struct stat  dummy_status_ri;


    if (stat (filename_tv, &dummy_status_ri))
        return (FALSE);
    else
        return (TRUE);
    }


int util_check_break (void)
    {
    if (kbd_getch (FALSE) == KEY_CTRL_C)
        {
        if (dia_yesno (txt_get (TXT_ASK_BREAK), 2) == YES)
            return (1);
        else
            return (0);
        }
    else
        return (0);
    }


void util_print_net_error()
{
  char txt[256];

  if(!config.net.error) return;

  fprintf(stderr, "%s error: %s\n", get_instmode_name(config.instmode), config.net.error);

  if(config.win) {
    if(config.instmode == inst_ftp) {
      sprintf(txt, txt_get(TXT_ERROR_FTP), config.net.error);
    }
    else {
      sprintf(txt, "%s network error:\n\n%s", get_instmode_name_up(config.instmode), config.net.error);
    }
    dia_message(txt, MSGTYPE_ERROR);
  }
}


int util_free_ramdisk(char *ramdisk_dev)
{
  int fd;
  int err = 0;

  if((fd = open(ramdisk_dev, O_RDWR)) >= 0) {
    if(ioctl(fd, BLKFLSBUF)) {
      err = errno;
      perror(ramdisk_dev);
    }
    else {
      fprintf(stderr, "ramdisk %s freed\n", ramdisk_dev);
    }
    close(fd);
  }
  else {
    err = errno;
    perror(ramdisk_dev);
  }

  return err;
}


int util_cd1_boot (void)
    {
#ifdef __i386__
    struct statfs fs_status_ri;

    (void) statfs ("/", &fs_status_ri);

    fprintf (stderr, "Size of ramdisk is %ld\n", fs_status_ri.f_blocks);

    if (fs_status_ri.f_blocks > 3200)
        return (TRUE);
    else
        return (FALSE);
#else
    return (TRUE);
#endif
    }


void util_disp_init()
{
  int i_ii;

  config.win = 1;
  disp_set_display();
  if (config.linemode)
    return;
  for(i_ii = 1; i_ii < max_y_ig; i_ii++) printf("\n"); printf("\033[9;0]");
  disp_cursor_off();
  util_print_banner();
}


void util_disp_done()
{
  if (config.linemode) {
    printf("\n\n");
    config.win = 0;
    return;
  }
  disp_clear_screen();
  printf("\033c");
  fflush(stdout);

  config.win = 0;
}


int util_umount(char *dir)
{
  int i;
  file_t *f0, *f;
  struct stat sbuf;

  if(!dir) return -1;

  if(stat(dir, &sbuf)) return -1;

  // fprintf(stderr, "umount: >%s<\n", dir);

  f0 = file_read_file("/proc/mounts");

  if((i = umount(dir))) {
    // fprintf(stderr, "umount: %s: %s\n", dir, strerror(errno));
    if(errno != ENOENT && errno != EINVAL) {
      if(config.run_as_linuxrc) fprintf(stderr, "umount: %s: %s\n", dir, strerror(errno));
    }
  }

  if(!i) {
    for(f = f0; f; f = f->next) {
      if(
        strstr(f->key_str, "/dev/loop") == f->key_str &&
        strstr(f->value, dir) == f->value &&
        isspace(f->value[strlen(dir)])
      ) {
        util_detach_loop(f->key_str);
      }
    }
  }

  file_free_file(f0);

  if(!i && strstr(dir, "/mounts/") == dir) {
    rmdir(dir);
  }

  return i;
}

int _util_eject_cdrom(char *dev)
{
  int fd;
  char buf[64];

  if(!dev) return 0;

  if(strstr(dev, "/dev/") != dev) {
    sprintf(buf, "/dev/%s", dev);
    dev = buf;
  }

  fprintf(stderr, "eject %s\n", dev);

  if((fd = open(dev, O_RDONLY | O_NONBLOCK)) < 0) return 1;
  util_umount(dev);
  if(ioctl(fd, CDROMEJECT, NULL) < 0) { close(fd); return 2; }
  close(fd);

  return 0;
}

int util_eject_cdrom(char *dev)
{
  slist_t *sl;

  if(dev) return _util_eject_cdrom(dev);
  util_update_cdrom_list();

  for(sl = config.cdroms; sl; sl = sl->next) {
    _util_eject_cdrom(sl->key);
  }

  return 0;
}

void util_manual_mode()
{
  config.manual = 1;
  auto_ig = 0;
  auto2_ig = 0;
}

/*
 * Check for a valid driver update directory below <dir>; copy the
 * necessary stuff into a ramdisk and mount it at /update. The global
 * variable driver_update_dir[] then holds the mount point (and is ""
 * otherwise).
 *
 * Do nothing if *driver_update_dir != 0 (stay with the first driver
 * update medium).
 */
int util_chk_driver_update(char *dir)
{
  char drv_src[100], mods_src[100], inst_src[100];
  char inst_dst[100], buf[256], rmod[100], *s;
  struct stat st;
  int i;
  struct dirent *de;
  DIR *d;
  char *copy_dir[] = { "y2update", "install" };		/* "install" _must_ be last! */

  if(!dir) return 0;
  if(*driver_update_dir) return 0;

  sprintf(drv_src, "%s%s", dir, config.updatedir);
  sprintf(mods_src, "%s%s/modules", dir, config.updatedir);

  if(stat(drv_src, &st) == -1) return 0;
  if(!S_ISDIR(st.st_mode)) return 0;

  /* Driver update disk, even if the update dir is empty! */
  strcpy(driver_update_dir, "/update");

  fprintf(stderr, "found driver update disk\n");

  mkdir(driver_update_dir, 0755);

  if(mount("shmfs", driver_update_dir, "shm", 0, 0)) return 0;

  for(i = 0; i < sizeof copy_dir / sizeof *copy_dir; i++) {
    sprintf(inst_src, "%s%s/%s", dir, config.updatedir, copy_dir[i]);
    sprintf(inst_dst, "%s/%s", driver_update_dir, copy_dir[i]);
    if(
      !stat(inst_src, &st) &&
      S_ISDIR(st.st_mode) &&
      !mkdir(inst_dst, 0755)
    ) {
      fprintf(stderr, "copying %s dir\n", copy_dir[i]);
      util_do_cp(inst_src, inst_dst);
    }
  }

  // make sure scripts are executable
  sprintf(inst_dst, "%s/install", driver_update_dir);
  if((d = opendir(inst_dst))) {
    while((de = readdir(d))) {
      if(strstr(de->d_name, "update.") == de->d_name) {
        sprintf(buf, "%ss/%s", inst_dst, de->d_name);
        chmod(buf, 0755);
      }
    }
    closedir(d);
  }

  if(
    !stat(mods_src, &st) &&
    S_ISDIR(st.st_mode)
  ) {
    sprintf(buf, "%s/module.config", mods_src);
    if(config.tmpfs && util_check_exist(buf)) {
      fprintf(stderr, "copying modules\n");
      mod_copy_modules(mods_src, 1);
      mod_init();
      sprintf(buf, "%s/hd.ids", mods_src);
      if(util_check_exist(buf)) {
        sprintf(buf, "cp %s/hd.ids /var/lib/hardware", mods_src);
        system(buf);
      }
    }
    else {
      fprintf(stderr, "loading modules\n");
      d = opendir(mods_src);
      if(d) {
        while((de = readdir(d))) {
          if(strcmp(de->d_name, ".") && strcmp(de->d_name, "..")) {
            if((s = strstr(de->d_name, ".o")) && !s[2]) {
              sprintf(buf, "%s/%s", mods_src, de->d_name);
              strcpy(rmod, de->d_name); rmod[s - de->d_name] = 0;
              mod_unload_module(rmod);
              mod_insmod(buf, NULL);
            }
          }
        }
        closedir(d);
      }
    }
  }

  sprintf(buf, "file:/%s/linuxrc.config", inst_src);
  file_read_info_file(buf, NULL);

  return 0;
}

void util_umount_driver_update()
{
  if(!*driver_update_dir) return;

  util_umount(driver_update_dir);
  *driver_update_dir = 0;
}


void add_flag(slist_t **sl, char *buf, int value, char *name)
{
  int l;

  if(!value) return;

  if(!*buf) strcpy(buf, "  ");
  l = strlen(buf);

  sprintf(buf + strlen(buf), "%s%s", buf[l - 1] == ' ' ? "" : ", ", name);

  if(strlen(buf) > 40) {
    slist_append_str(sl, buf);
    *buf = 0;
  }
}


void util_status_info()
{
  int i, j;
  char *s, *t;
  hd_data_t *hd_data;
  slist_t *sl, *sl0 = NULL;
  char buf[256];
  language_t *lang;

  hd_data = calloc(1, sizeof *hd_data);
  hd_data->debug = 1;
  hd_scan(hd_data);

  if(hd_data->log) {
    s = strchr(hd_data->log, '\n');
    if(s) {
      *s = 0;
      slist_append_str(&sl0, hd_data->log);
    }
  }

  sprintf(buf, "product = \"%s\"", config.product);
  slist_append_str(&sl0, buf);

  sprintf(buf,
    "memory (kB): total %d, free %d (%d), ramdisk %d",
    config.memory.total, config.memory.current,
    config.memory.current - config.memory.free_swap,
    config.memory.free - config.memory.current
  );
  slist_append_str(&sl0, buf);

  sprintf(buf,
    "memory limits: min %d, yast %d/%d, modules %d, image %d",
    config.memory.min_free, config.memory.min_yast, config.memory.min_yast_text,
    config.memory.min_modules, config.memory.load_image
  );
  slist_append_str(&sl0, buf);

  sprintf(buf,
    "instmode = %s%s%s [%s], net_config_mask = 0x%x",
    get_instmode_name(config.instmode),
    config.instmode == config.instmode_extra ? "" : "/",
    config.instmode == config.instmode_extra ? "" : get_instmode_name(config.instmode_extra),
    get_instmode_name(config.insttype),
    net_config_mask()
  );
  slist_append_str(&sl0, buf);

  sprintf(buf, "linuxrc = %s", config.linuxrc ?: "");
  slist_append_str(&sl0, buf);

  sprintf(buf, "flags = ");
  add_flag(&sl0, buf, config.test, "test");
  add_flag(&sl0, buf, config.tmpfs, "tmpfs");
  add_flag(&sl0, buf, config.manual, "manual");
  add_flag(&sl0, buf, config.rescue, "rescue");
  add_flag(&sl0, buf, config.demo, "demo");
  add_flag(&sl0, buf, config.vnc, "vnc");
  add_flag(&sl0, buf, config.usessh, "usessh");
  add_flag(&sl0, buf, config.hwcheck, "hwcheck");
  add_flag(&sl0, buf, config.textmode, "textmode");
  add_flag(&sl0, buf, config.rebootmsg, "rebootmsg");
  add_flag(&sl0, buf, config.nopcmcia, "nopcmcia");
  add_flag(&sl0, buf, config.net.use_dhcp, "dhcp");
  add_flag(&sl0, buf, config.net.dhcp_active, "dhcp_active");
  add_flag(&sl0, buf, config.use_ramdisk, "ramdisk");
  add_flag(&sl0, buf, config.ask_language, "ask_lang");
  add_flag(&sl0, buf, config.ask_keytable, "ask_keytbl");
  add_flag(&sl0, buf, config.activate_storage, "act_storage");
  add_flag(&sl0, buf, config.activate_network, "act_net");
  add_flag(&sl0, buf, config.pivotroot, "pivotroot");
  add_flag(&sl0, buf, config.addswap, "addswap");
  add_flag(&sl0, buf, config.splash, "splash");
  add_flag(&sl0, buf, config.noshell, "noshell");
  add_flag(&sl0, buf, config.run_memcheck, "memcheck");
  add_flag(&sl0, buf, config.hwdetect, "hwdetect");
  add_flag(&sl0, buf, config.had_segv, "segv");
  if(*buf) slist_append_str(&sl0, buf);

  if(config.autoyast) {
    sprintf(buf, "autoyast = %s", config.autoyast);
    slist_append_str(&sl0, buf);
  }

  s = config.info.loaded;
  if(!s) s = "";
  t = config.info.add_cmdline ? "cmdline" : "";
  if(!*s || !strcmp(s, t)) { s = t; t = ""; }
  sprintf(buf, "info = ");
  if(*s) sprintf(buf + strlen(buf), "%s", s);
  if(*t) sprintf(buf + strlen(buf), ", %s", t);
  slist_append_str(&sl0, buf);

  sprintf(buf, "auto = %d", auto2_ig ? 2 : auto_ig ? 1 : 0);
  slist_append_str(&sl0, buf);

  strcpy(buf, "floppies = (");
  for(i = 0; i < config.floppies; i++) {
    sprintf(buf + strlen(buf), "%s%s%s",
      i ? ", " : " ",
      config.floppy_dev[i],
      i == config.floppy && config.floppies != 1 ? "*" : ""
    );
  }
  strcat(buf, " )");
  slist_append_str(&sl0, buf);

  strcpy(buf, "net devices = (");
  for(i = 0, sl = config.net.devices; sl; sl = sl->next) {
    if(!sl->key) continue;
    j = strcmp(sl->key, netdevice_tg) ? 0 : 1;
    sprintf(buf + strlen(buf), "%s%s%s", i ? ", " : " ", sl->key, j ? "*" : "");
    if(sl->value) sprintf(buf + strlen(buf), " [%s]", sl->value);
    i = 1;
  }
  strcat(buf, " )");
  slist_append_str(&sl0, buf);

  if(config.cdid) {
    sprintf(buf, "cdrom id = %s", config.cdid);
    slist_append_str(&sl0, buf);
  }

  if(*driver_update_dir) {
    sprintf(buf, "driver_update = %s", driver_update_dir);
    slist_append_str(&sl0, buf);
  }

  sprintf(buf, "hostname = %s", inet2print(&config.net.hostname));
  slist_append_str(&sl0, buf);

  sprintf(buf, "domain = %s", config.net.domain ?: "");
  slist_append_str(&sl0, buf);

  sprintf(buf, "network = %s", inet2print(&config.net.network));
  slist_append_str(&sl0, buf);

  sprintf(buf, "netmask = %s", inet2print(&config.net.netmask));
  slist_append_str(&sl0, buf);

  sprintf(buf, "broadcast = %s", inet2print(&config.net.broadcast));
  slist_append_str(&sl0, buf);

  sprintf(buf, "gateway = %s", inet2print(&config.net.gateway));
  slist_append_str(&sl0, buf);

  sprintf(buf, "nameserver = %s", inet2print(&config.net.nameserver));
  slist_append_str(&sl0, buf);

  sprintf(buf, "proxy = %s", inet2print(&config.net.proxy));
  if(config.net.proxyport) {
    sprintf(buf + strlen(buf), ", proxyport = %d", config.net.proxyport);
  }
  if(config.net.proxyproto) {
    sprintf(buf + strlen(buf), ", proxyproto = %s", get_instmode_name(config.net.proxyproto));
  }
  slist_append_str(&sl0, buf);

  sprintf(buf, "server = %s", inet2print(&config.net.server));
  slist_append_str(&sl0, buf);

  sprintf(buf, "plip host = %s", inet2print(&config.net.pliphost));
  slist_append_str(&sl0, buf);

  if(config.serverdir) {
    sprintf(buf, "server dir = %s", config.serverdir);
    slist_append_str(&sl0, buf);
  }

  if(config.net.workgroup) {
    sprintf(buf, "workgroup = %s", config.net.workgroup);
    slist_append_str(&sl0, buf);
  }

  if(config.net.user || config.net.password) {
    *buf = 0;
    if(config.net.user) sprintf(buf, "user = %s", config.net.user);
    sprintf(buf + strlen(buf), "%spassword = %s", *buf ? ", " : "", config.net.password);
    slist_append_str(&sl0, buf);
  }

  if(config.net.vncpassword) {
    sprintf(buf, "vncpassword = %s", config.net.vncpassword);
    slist_append_str(&sl0, buf);
  }

  if(config.net.sshpassword) {
    sprintf(buf, "sshpassword = %s", config.net.sshpassword);
    slist_append_str(&sl0, buf);
  }


  if(config.net.use_dhcp) {
    s = "", t = "*";
  }
  else {
    s = "*", t = "";
  }
  sprintf(buf,
    "timeouts: bootp%s = %ds, dhcp%s = %ds, tftp = %ds",
    s, config.net.bootp_timeout, t, config.net.dhcp_timeout, config.net.tftp_timeout
  );
  slist_append_str(&sl0, buf);

  if(config.net.nfs_port || config.net.bootp_wait) {
    *buf = 0;
    if(config.net.nfs_port) sprintf(buf, "nfs port = %d", config.net.nfs_port);
    if(config.net.bootp_wait) {
      sprintf(buf + strlen(buf),
        "%sbootp wait = %d",
        config.net.nfs_port ? ", " : "",
        config.net.bootp_wait
      );
    }
    slist_append_str(&sl0, buf);
  }

  lang = current_language();

  sprintf(buf, "language = %s (%s), keymap = %s", lang->yastcode, lang->locale, config.keymap ?: "");
  slist_append_str(&sl0, buf);

  sprintf(buf, "yast2update = %d, yast2serial = %d", yast2_update_ig, yast2_serial_ig);
  slist_append_str(&sl0, buf);

  sprintf(buf, "vga = 0x%04x", frame_buffer_mode_ig);
  slist_append_str(&sl0, buf);

  if(config.term) {
    sprintf(buf, "term = \"%s\"", config.term);
    slist_append_str(&sl0, buf);
  }

  sprintf(buf, "console = \"%s\"", config.console);
  if(config.serial) sprintf(buf + strlen(buf), ", serial line params = \"%s\"", config.serial);
  slist_append_str(&sl0, buf);

  sprintf(buf, "stderr = \"%s\"", config.stderr_name);
  slist_append_str(&sl0, buf);

  sprintf(buf,
    "pcmcia = %d, pcmcia_chip = %s",
    auto2_pcmcia(),
    pcmcia_chip_ig == 2 ? "\"i82365\"" : pcmcia_chip_ig == 1 ? "\"tcic\"" : "0"
  );
  slist_append_str(&sl0, buf);

  if(config.instsys) {
    sprintf(buf, "instsys = \"%s\"", config.instsys);
    slist_append_str(&sl0, buf);
  }

  sprintf(buf, "rootimage = \"%s\"", config.rootimage);
  slist_append_str(&sl0, buf);

  sprintf(buf, "rescueimage = \"%s\"", config.rescueimage);
  slist_append_str(&sl0, buf);

  sprintf(buf, "evalimage = \"%s\"", config.demoimage);
  slist_append_str(&sl0, buf);

  sprintf(buf, "installdir = \"%s\"", config.installdir);
  slist_append_str(&sl0, buf);

  sprintf(buf, "setup command = \"%s\"", config.setupcmd);
  slist_append_str(&sl0, buf);

  if(config.module.broken) {
    strcpy(buf, "broken modules:");
    slist_append_str(&sl0, buf);
    for(sl = config.module.broken; sl; sl = sl->next) {
      if(!sl->key) continue;
      sprintf(buf, "  %s", sl->key);
      slist_append_str(&sl0, buf);
    }
  }

  if(config.module.initrd) {
    strcpy(buf, "extra initrd modules:");
    slist_append_str(&sl0, buf);
    for(sl = config.module.initrd; sl; sl = sl->next) {
      if(!sl->key) continue;
      sprintf(buf, "  %s", sl->key);
      slist_append_str(&sl0, buf);
    }
  }

  if(config.cdroms) {
    strcpy(buf, "cdroms:");
    slist_append_str(&sl0, buf);
    for(sl = config.cdroms; sl; sl = sl->next) {
      if(!sl->key) continue;
      i = config.cdrom && !strcmp(sl->key, config.cdrom) ? 1 : 0;
      sprintf(buf, "  %s%s", sl->key, i ? "*" : "");
      if(sl->value) sprintf(buf + strlen(buf), " [%s]", sl->value);
      slist_append_str(&sl0, buf);
    }
  }

  if(config.disks) {
    strcpy(buf, "disks:");
    slist_append_str(&sl0, buf);
    for(sl = config.disks; sl; sl = sl->next) {
      if(!sl->key) continue;
      sprintf(buf, "  %s", sl->key);
      if(sl->value) sprintf(buf + strlen(buf), " [%s]", sl->value);
      slist_append_str(&sl0, buf);
    }
  }

  if(config.partitions) {
    strcpy(buf, "partitions:");
    slist_append_str(&sl0, buf);
    for(sl = config.partitions; sl; sl = sl->next) {
      if(!sl->key) continue;
      i = config.partition && !strcmp(sl->key, config.partition) ? 1 : 0;
      sprintf(buf, "  %s%s", sl->key, i ? "*" : "");
      if(sl->value) sprintf(buf + strlen(buf), " [%s]", sl->value);
      slist_append_str(&sl0, buf);
    }
  }

  sprintf(buf, "inst_ramdisk = %d", config.inst_ramdisk);
  slist_append_str(&sl0, buf);

  for(i = 0; i < sizeof config.ramdisk / sizeof *config.ramdisk; i++) {
    if(config.ramdisk[i].inuse) {
      sprintf(buf, "ramdisk %s: %d kB",
        config.ramdisk[i].dev, (config.ramdisk[i].size + 1023) >> 10
      );
      if(config.ramdisk[i].mountpoint) {
        sprintf(buf + strlen(buf), " mounted at \"%s\"", config.ramdisk[i].mountpoint);
      }
      slist_append_str(&sl0, buf);
    }
  }

  dia_show_lines2("Linuxrc v" LXRC_FULL_VERSION "/" LX_ARCH "-" LX_REL " (" __DATE__ ", " __TIME__ ")", sl0, 76);

  slist_free(sl0);

  hd_free_hd_data(hd_data);
  free(hd_data);
}

void util_get_splash_status()
{
  FILE *f;
  char s[80];

  config.splash = 0;

  if((f = fopen("/proc/splash", "r"))) {
    if(fgets(s, sizeof s, f)) {
      if(strstr(s, ": on")) config.splash = 1;
    }
    fclose(f);
  }
}


char *read_symlink(char *file)
{
  static char buf[256];
  int i;

  i = readlink(file, buf, sizeof buf);
  buf[sizeof buf - 1] = 0;
  if(i >= 0 && i < sizeof buf) buf[i] = 0;
  if(i < 0) *buf = 0;

  return buf;
}


void show_lsof_info(FILE *f, unsigned pid)
{
  char pe[128];
  char buf1[64], buf2[128], buf3[256];
  FILE *p;
  DIR *dir;
  struct dirent *de;
  unsigned status = 0;
  char *s, c;
  struct stat sbuf;
  file_t *f0, *f1;
  slist_t *sl0 = NULL, *sl;

  sprintf(pe, "/proc/%u/status", pid);
  if((p = fopen(pe, "r"))) {
    if(fscanf(p, "Name: %63[^\n]", buf1) == 1) status = 1;
    fclose(p);
  }

  if(!status) *buf1 = 0;

  sprintf(buf2, "%-9s %5u ", buf1, pid);

  sprintf(pe, "/proc/%u/cwd", pid);
  if(*(s = read_symlink(pe))) fprintf(f, "%s cwd  %s\n", buf2, s);

  sprintf(pe, "/proc/%u/root", pid);
  if(*(s = read_symlink(pe))) fprintf(f, "%s rtd  %s\n", buf2, s);

  sprintf(pe, "/proc/%u/exe", pid);
  if(*(s = read_symlink(pe))) fprintf(f, "%s txt  %s\n", buf2, s);

  sprintf(pe, "/proc/%u/fd", pid);
  if((dir = opendir(pe))) {
    while((de = readdir(dir))) {
      if(*de->d_name != '.') {
        sprintf(pe, "/proc/%u/fd/%s", pid, de->d_name);
        if(*(s = read_symlink(pe))) {
          c = ' ';
          if(!lstat(pe, &sbuf)) {
            if((sbuf.st_mode & S_IRUSR)) c = 'r';
            if((sbuf.st_mode & S_IWUSR)) c = c == 'r' ? 'u' : 'w';
          }
          fprintf(f, "%s %3s%c %s\n", buf2, de->d_name, c, s);
        }
      }
    }
    closedir(dir);
  }

  sprintf(pe, "/proc/%u/maps", pid);
  f0 = file_read_file(pe);

  for(f1 = f0; f1; f1 = f1->next) {
    *buf3 = 0;
    if(sscanf(f1->value, "%*s %*s %*s %*s %255[^\n]", buf3)) {
      if(*buf3 && !slist_getentry(sl0, buf3)) {
        sl = slist_append(&sl0, slist_new());
        sl->key = strdup(buf3);
      }
    }
  }

  file_free_file(f0);

  for(sl = sl0; sl; sl = sl->next) {
    fprintf(f, "%s mem  %s\n", buf2, sl->key);
  }

  slist_free(sl0);
}

char *util_process_cmdline(pid_t pid)
{
  char pe[100];
  static char buf[256];
  FILE *p;
  unsigned status = 0;
  unsigned u, v;

  sprintf(pe, "/proc/%u/cmdline", pid);
  if((p = fopen(pe, "r"))) {
    u = fread(buf, 1, sizeof buf, p);
    if(u > sizeof buf - 1) u = sizeof buf - 1;
    for(v = 0; v < u; v++) if(!buf[v]) buf[v] = ' ';
    buf[u] = 0;
    status = 1;
    fclose(p);
  }

  if(!status) *buf = 0;

  return buf;
}

void show_ps_info(FILE *f, unsigned pid)
{
  char pe[100];
  char buf1[64], buf2[2], *buf3;
  FILE *p;
  unsigned status = 0;

  sprintf(pe, "/proc/%u/status", pid);
  if((p = fopen(pe, "r"))) {
    if(fscanf(p, "Name: %63[^\n]", buf1) == 1) status |= 1;
    if(fscanf(p, "\nState: %1s", buf2) == 1) status |= 2;
    fclose(p);
  }

  buf3 = util_process_cmdline(pid);

  if(!(status & 1)) *buf1 = 0;
  if(!(status & 2)) *buf2 = 0;

  fprintf(f, "%5u %s %-16s %s\n", pid, buf2, buf1, buf3);
}

int util_do_cp(char *src, char *dst)
{
  int i;

  exclude = strrchr(dst, '/');
  if(exclude) exclude++; else exclude = dst;
  rec_level = 0;

  i = do_cp(src, dst);
  free_hlink_list();

  return i;
}


/*
 * Copy recursively src to dst. Both must be existing directories.
 */
int do_cp(char *src, char *dst)
{
  DIR *dir;
  struct dirent *de;
  struct stat sbuf, sbuf2;
  char src2[0x100];
  char dst2[0x100];
  char *s;
  int i, j;
  int err = 0;
  unsigned char buf[0x1000];
  int fd1, fd2;
  struct utimbuf ubuf;

  if((dir = opendir(src))) {
    while((de = readdir(dir))) {
      if(!strcmp(de->d_name, ".") || !strcmp(de->d_name, "..")) continue;
      sprintf(src2, "%s/%s", src, de->d_name);
      sprintf(dst2, "%s/%s", dst, de->d_name);
      i = lstat(src2, &sbuf);
      if(i == -1) {
        perror(src2);
        err = 2;
        break;
      }

      if(S_ISDIR(sbuf.st_mode)) {
        // avoid infinite recursion
        if(exclude && !rec_level && !strcmp(exclude, de->d_name)) continue;

        i = stat(dst2, &sbuf2);
        if(i || !S_ISDIR(sbuf2.st_mode)) {
          unlink(dst2);
          i = mkdir(dst2, 0755);
          if(i) {
            err = 4;
            perror(dst2);
            break;
          }
        }

        rec_level++;
        err = do_cp(src2, dst2);
        rec_level--;

        if(err) break;
      }

      else if(S_ISREG(sbuf.st_mode)) {
        unlink(dst2);
        s = NULL;
        if(sbuf.st_nlink > 1) {
          s = walk_hlink_list(sbuf.st_ino, sbuf.st_dev, dst2);
        }

        if(s) {
          // just make a link

          i = link(s, dst2);
          if(i) {
            err = 12;
            perror(dst2);
            break;
          }
        }
        else {
          // actually copy it

          fd1 = open(src2, O_RDONLY);
          if(fd1 < 0) {
            err = 5;
            perror(src2);
            break;
          }
          fd2 = open(dst2, O_WRONLY | O_CREAT | O_TRUNC, 0644);
          if(fd2 < 0) {
            err = 6;
            perror(dst2);
            close(fd1);
            break;
          }
          do {
            i = read(fd1, buf, sizeof buf);
            j = 0;
            if(i > 0) {
              j = write(fd2, buf, i);
            }
          }
          while(i > 0 && j > 0);
          if(i < 0) {
            perror(src2);
            err = 7;
          }
          if(j < 0) {
            perror(dst2);
            err = 8;
          }
          close(fd1);
          close(fd2);
          if(err) break;
        }
      }

      else if(S_ISLNK(sbuf.st_mode)) {
        i = readlink(src2, buf, sizeof buf - 1);
        if(i < 0) {
          err = 9;
          perror(src2);
          break;
        }
        else {
          buf[i] = 0;
        }
        unlink(dst2);
        i = symlink(buf, dst2);
        if(i) {
          err = 10;
          perror(dst2);
          break;
        }
      }

      else if(
        S_ISCHR(sbuf.st_mode) ||
        S_ISBLK(sbuf.st_mode) ||
        S_ISFIFO(sbuf.st_mode) ||
        S_ISSOCK(sbuf.st_mode)
      ) {
        unlink(dst2);
        i = mknod(dst2, sbuf.st_mode, sbuf.st_rdev);
        if(i) {
          err = 11;
          perror(dst2);
          break;
        }
      }

      else {
        fprintf(stderr, "%s: type not supported\n", src2);
        err = 3;
        break;
      }

      // fix owner/time/permissions

      lchown(dst2, sbuf.st_uid, sbuf.st_gid);
      if(!S_ISLNK(sbuf.st_mode)) {
        chmod(dst2, sbuf.st_mode);
        ubuf.actime = sbuf.st_atime;
        ubuf.modtime = sbuf.st_mtime;
        utime(dst2, &ubuf);
      }

    }
  } else {
    perror(src);
    return 1;
  }

  closedir(dir);

  return err;
}


/*
 * Return either the file name belonging to dev/inode or, if
 * that's not found, return NULL and append dev/inode/dst to
 * the current list.
 */
char *walk_hlink_list(ino_t ino, dev_t dev, char *dst)
{
  struct hlink_s **hl;

  for(hl = &hlink_list; *hl; hl = &(*hl)->next) {
    if((*hl)->dev == dev && (*hl)->ino == ino) {
      return (*hl)->dst;
    }
  }

  *hl = calloc(1, sizeof **hl);

  (*hl)->dev = dev;
  (*hl)->ino = ino;
  (*hl)->dst = strdup(dst);

  return NULL;
}


void free_hlink_list()
{
  struct hlink_s *hl, *next;

  for(hl = hlink_list; hl; hl = next) {
    next = hl->next;
    if(hl->dst) free(hl->dst);
    free(hl);
  }
}


void util_start_shell(char *tty, char *shell, int new_env)
{
  int fd;
  char *s, *args[] = { NULL, NULL };
  char *env[] = {
    "TERM=linux",
    "PS1=\\w # ",
    "HOME=/",
    "PATH=/bin:/sbin:/usr/bin:/usr/sbin:/usr/lib/YaST2/bin:/usr/X11R6/bin",
    NULL
  };
  extern char **environ;

  *args = (s = strrchr(shell, '/')) ? s + 1 : shell;

  if(!fork()) {
    for(fd = 0; fd < 20; fd++) close(fd);
    setsid();
    fd = open(tty, O_RDWR);
    ioctl(fd, TIOCSCTTY, (void *) 1);
    dup(fd);
    dup(fd);

    execve(shell, args, new_env ? env : environ);
    fprintf(stderr, "Couldn't start shell (errno = %d)\n", errno);
    exit(-1);
  }
}


int util_ps_main(int argc, char **argv)
{
  DIR *proc;
  struct dirent *de;
  unsigned u;
  char *s;

  if((proc = opendir("/proc"))) {
    while((de = readdir(proc))) {
      if(de->d_name) {
        u = strtoul(de->d_name, &s, 10);
        if(!*s) show_ps_info(stdout, u);
      }
    }
    closedir(proc);
  }

  return 0;
}


int util_lsof_main(int argc, char **argv)
{
  DIR *proc;
  struct dirent *de;
  unsigned u;
  char *s;

  if((proc = opendir("/proc"))) {
    while((de = readdir(proc))) {
      if(de->d_name) {
        u = strtoul(de->d_name, &s, 10);
        if(!*s) show_lsof_info(stdout, u);
      }
    }
    closedir(proc);
  }

  return 0;
}


int util_mount_main(int argc, char **argv)
{
  int i, notype = 0;
  char *dir, *srv_dir;
  char *type = NULL, *dev, *module;
  inet_t inet = {};

  argv++; argc--;

  if(!argc) {
    return system("cat /proc/mounts");
  }

  if(argc < 2) return fprintf(stderr, "mount: invalid number of arguments\n"), 1;

  if(strstr(*argv, "-t") == *argv) {
    type = *argv + 2;

    if(!*type) {
      type = *++argv;
      argc--;
    }

    argv++;
    argc--;
  }

  if(argc != 2) return fprintf(stderr, "mount: invalid number of arguments\n"), 1;

  dev = strcmp(*argv, "none") ? *argv : NULL;

  dir = argv[1];

  if(!type) {
    if(strchr(dev, ':')) {
      type = "nfs";
    }
    else {
      notype = 1;
      type = util_fstype(dev, &module);
      if(module) {
        return fprintf(stderr, "mount: fs type not supported, load module \"%s\" first\n", module), 2;
      }
    }
    // if(!type) return fprintf(stderr, "mount: no fs type given\n"), 2;
  }

  if(notype) {
    if(!util_mount(dev, dir, 0)) return 0;
    perror("mount");
    return errno;
  }

  if(strcmp(type, "nfs")) {
    if(!mount(dev, dir, type, 0, 0)) return 0;
    perror("mount");
    return errno;
  }

  srv_dir = strchr(dev, ':');
  if(!srv_dir) return fprintf(stderr, "mount: directory to mount not in host:dir format\n"), 1;

  *srv_dir++ = 0;

  inet.name = dev;
  i = net_mount_nfs(dir, &inet, srv_dir);

  if(i && errno) {
    i = errno;
    perror("mount");
  }

  return i;
}


int util_umount_main(int argc, char **argv)
{
  int force = 0;
  int i;

  argv++; argc--;

  if(argc == 2 && !strcmp(*argv, "-f")) {
    force = 1;
    argv++;
    argc--;
  }

  if(argc != 1) fprintf(stderr, "u%s", "mount: invalid number of arguments\n"), 1;

  i = force ? umount2(*argv, MNT_FORCE) : util_umount(*argv);

  if(!i) return 0;

  fprintf(stderr, "umount: "); perror(*argv);

  return errno;
}


int util_cat_main(int argc, char **argv)
{
  FILE *f;
  int i, c;

  argv++; argc--;

  if(!argc) {
    while((c = fgetc(stdin)) != EOF) fputc(c, stdout);
    fflush(stdout);
    return 0;
  }

  for(i = 0; i < argc; i++) {
    if((f = fopen(argv[i], "r"))) {
      while((c = fgetc(f)) != EOF) fputc(c, stdout);
      fclose(f);
    }
    else {
      perror(argv[i]);
      return 1;
    }
  }

  fflush(stdout);

  return 0;
}


int util_hex_main(int argc, char **argv)
{
  FILE *f;
  int i, j = 0;
  char s[17];

  if(argc > 1) {
    f = fopen(argv[1], "r");
    if(!f) {
      perror(argv[1]);
      return errno;
    }
  }
  else {
    f = stdin;
  }

  s[16] = 0;
  while((i = fgetc(f)) != EOF) {
    i = i & 0xff;
    s[j & 15] = (i >= 0x20 && i <= 0x7e) ? i : '.';
    if(!(j & 15)) {
      printf("%06x  ", j);
    }
    if(!(j & 7) && (j & 15)) printf(" ");
    printf("%02x ", (int) i);
    if(!(++j & 15)) {
      printf(" %s\n", s);
    }
  }

  if(j & 15) {
    s[j & 15] = 0;
    if(!(j & 8)) printf(" ");
    printf("%*s %s\n", 3 * (16 - (j & 15)), "", s);
  }

  fflush(stdout);

  return 0;
}


int util_echo_main(int argc, char **argv)
{
  int i;

  argv++; argc--;

  for(i = 0; i < argc; i++) {
    printf("%s%s", i ? " " : "", argv[i]);
  }

  printf("\n");

  return 0;
}


int util_nothing_main(int argc, char **argv)
{
  return 0;
}


int util_cp_main(int argc, char **argv)
{
  int err = 0, rec = 0, preserve = 0;
  char *src, *dst, *s;
  char dst2[0x100];
  int i, j, fd1, fd2;
  unsigned char buf[0x1000];
  struct stat sbuf;
  struct utimbuf ubuf;

  argv++; argc--;

  while(argc) {
    if(argc >= 1 && !strcmp(*argv, "-a")) {
      argv++; argc--;
      rec = 1;
      continue;
    }

    if(argc >= 1 && !strcmp(*argv, "-p")) {
      argv++; argc--;
      preserve = 1;
      continue;
    }
    break;
  }

  if(argc != 2) return 1;

  src = argv[0];
  dst = argv[1];

  if(rec) {
    if(!util_check_exist(dst)) mkdir(dst, 0755);
    err = util_do_cp(src, dst);
  }
  else {
    i = stat(dst, &sbuf);
    if(!i && S_ISDIR(sbuf.st_mode)) {
      s = strrchr(src, '/');
      s = s ? s + 1 : src;
      sprintf(dst2, "%s/%s", dst, s);
      dst = dst2;
    }
    
    fd1 = open(src, O_RDONLY);
    if(fd1 < 0) {
      err = 1;
      perror(src);
    }
    else {
      unlink(dst);
      fd2 = open(dst, O_WRONLY | O_CREAT | O_TRUNC, 0644);
      if(fd2 < 0) {
        err = 2;
        perror(dst);
        close(fd1);
      }
      else {
        do {
          i = read(fd1, buf, sizeof buf);
          j = 0;
          if(i > 0) {
            j = write(fd2, buf, i);
          }
        }
        while(i > 0 && j > 0);
        if(i < 0) {
          perror(src);
          err = 3;
        }
        if(j < 0) {
          perror(dst);
          err = 4;
        }
        close(fd1);
        close(fd2);
      }
    }

    if(!err) {
      i = stat(src, &sbuf);
      if(!i) {
        chmod(dst, sbuf.st_mode);
        if(preserve) {
          chown(dst, sbuf.st_uid, sbuf.st_gid);
          ubuf.actime = sbuf.st_atime;
          ubuf.modtime = sbuf.st_mtime;
          utime(dst, &ubuf);
        }
      }
    }
  }

  return err;
}


int util_ls_main(int argc, char **argv)
{
  int full = 0, fulltime = 0;
  char *src;
  int i;
  char buf[0x100], c, *s, *t;
  struct stat sbuf;
  struct dirent *de;
  DIR *d;

  argv++; argc--;

  if(argc >= 1 && !strcmp(*argv, "-l")) {
    argv++; argc--;
    full = 1;
  }

  if(argc >= 1 && !strcmp(*argv, "--full-time")) {
    argv++; argc--;
    fulltime = 1;
  }

  src = argc ? *argv : ".";

  if(!(d = opendir(src))) {
    fprintf(stderr, "%s: No such file or directory\n", src);
    return 1;
  }

  while((de = readdir(d))) {
    if(full) {
      sprintf(buf, "%s/%s", src, de->d_name);
      i = lstat(buf, &sbuf);
      if(i) {
        printf("?????????  %s\n", de->d_name);
      }
      else {
             if(S_ISREG(sbuf.st_mode))  c = '-';
        else if(S_ISDIR(sbuf.st_mode))  c = 'd';
        else if(S_ISLNK(sbuf.st_mode))  c = 'l';
        else if(S_ISCHR(sbuf.st_mode))  c = 'c';
        else if(S_ISBLK(sbuf.st_mode))  c = 'b';
        else if(S_ISFIFO(sbuf.st_mode)) c = 'p';
        else if(S_ISSOCK(sbuf.st_mode)) c = 's';
        else c = '?';

        t = NULL;
        if(c == 'l') t = read_symlink(buf);

        buf[10] = 0;
        buf[0] = c;

        buf[1] = (sbuf.st_mode & S_IRUSR) ? 'r' : '-';
        buf[4] = (sbuf.st_mode & S_IRGRP) ? 'r' : '-';
        buf[7] = (sbuf.st_mode & S_IROTH) ? 'r' : '-';
        buf[2] = (sbuf.st_mode & S_IWUSR) ? 'w' : '-';
        buf[5] = (sbuf.st_mode & S_IWGRP) ? 'w' : '-';
        buf[8] = (sbuf.st_mode & S_IWOTH) ? 'w' : '-';
        buf[3] = (sbuf.st_mode & S_IXUSR) ? 'x' : '-';
        buf[6] = (sbuf.st_mode & S_IXGRP) ? 'x' : '-';
        buf[9] = (sbuf.st_mode & S_IXOTH) ? 'x' : '-';
        if((sbuf.st_mode & S_ISUID)) {
          buf[3] = (sbuf.st_mode & S_IXUSR) ? 's' : 'S';
        }
        if((sbuf.st_mode & S_ISGID)) {
          buf[6] = (sbuf.st_mode & S_IXGRP) ? 's' : 'S';
        }
        if((sbuf.st_mode & S_ISVTX)) {
          buf[9] = (sbuf.st_mode & S_IXOTH) ? 't' : 'T';
        }

        s = ctime(&sbuf.st_mtime);
        if(fulltime) {
          s[24] = 0;
        }
        else {
          s += 4; s[12] = 0;
        }

        printf("%s %4d %-8d %-8d %8ld %s %s",
          buf,
          (int) sbuf.st_nlink,
          (int) sbuf.st_uid,
          (int) sbuf.st_gid,
          (long) sbuf.st_size,
          s,
          de->d_name
        );
        if(t) printf(" -> %s", t);
        printf("\n");
      }
    }
    else {
      printf("%s\n", de->d_name);
    }
  }

  closedir(d);

  return 0;
}


/*
 * Redirect all output of system() to tty3.
 */
int util_sh_main(int argc, char **argv)
{
#if 0
  freopen("/dev/tty3", "a", stdout);
  freopen("/dev/tty3", "a", stderr);
  printf("Executing: \"%s\"\n", argv[2]);
#endif
  dup2(2, 1);

  return lsh_main(argc, argv);
}


char *util_process_name(pid_t pid)
{
  FILE *f;
  char proc_status[64];
  static char buf[64];

  *buf = 0;
  sprintf(proc_status, "/proc/%u/status", (unsigned) pid);
  if((f = fopen(proc_status, "r"))) {
    if(fscanf(f, "Name: %30s", buf) != 1) *buf = 0;
    fclose(f);
  }

  return buf;
}


int util_rm_main(int argc, char **argv)
{
  int i;

  argv++; argc--;

  while(argc--) {
    i = unlink(*argv);
    if(i) {
      fprintf(stderr, "rm: "); perror(*argv);
      return errno;
    }
    argv++;
  }

  return 0;
}


int util_mv_main(int argc, char **argv)
{
  int i;

  argv++; argc--;

  if(argc != 2) return -1;

  i = rename(argv[0], argv[1]);
  if(i) {
    i = errno;
    fprintf(stderr, "mv: "); perror(argv[0]);
  }

  return i;
}


int util_ln_main(int argc, char **argv)
{
  int sym = 0, i;

  argv++; argc--;

  if(argc >= 1 && !strcmp(*argv, "-s")) {
    argv++; argc--;
    sym = 1;
  }

  if(argc != 2) return -1;

  i = sym ? symlink(argv[0], argv[1]) : link(argv[0], argv[1]);
  if(i) {
    i = errno;
    fprintf(stderr, "ln: "); perror(argv[0]);
  }

  return i;
}


int util_swapon_main(int argc, char **argv)
{
  int i;

  argv++; argc--;

  if(argc != 1) return -1;

  i = swapon(*argv, 0);

  if(i) {
    i = errno;
    fprintf(stderr, "swapon: "); perror(*argv);
  }

  return i;
}


int util_swapoff_main(int argc, char **argv)
{
  int i;

  argv++; argc--;

  if(argc != 1) return -1;

  i = swapoff(*argv);

  if(i) {
    i = errno;
    fprintf(stderr, "swapoff: "); perror(*argv);
  }

  return i;
}


int util_freeramdisk_main(int argc, char **argv)
{
  argv++; argc--;

  if(argc != 1) return -1;

  return util_free_ramdisk(*argv);
}


int util_lsmod_main(int argc, char **argv)
{
  return system("cat /proc/modules");
}


int util_raidautorun_main(int argc, char **argv)
{
  int err = 0;
  int fd;

  if((fd = open("/dev/md0", O_RDWR)) >= 0) {
    if(ioctl(fd , RAID_AUTORUN, 0)) {
      err = errno;
      perror("/dev/md0");
    }
    close(fd);
  }
  else {
    err = errno;
    perror("/dev/md0");
  }

  return err;
}


void util_free_mem()
{
  file_t *f0, *f;
  int i, mem_total = 0, mem_free = 0, mem_free_swap = 0;
  char *s;

  f0 = file_read_file("/proc/meminfo");

  for(f = f0; f; f = f->next) {
    switch(f->key) {
      case key_memtotal:
      case key_swaptotal:
        i = strtoul(f->value, &s, 10);
        if(!*s || *s == ' ') mem_total += i;
        break;

      case key_memfree:
      case key_buffers:
      case key_cached:
      case key_swapfree:
        i = strtol(f->value, &s, 10);
        if(!*s || *s == ' ') {
          mem_free += i;
          if(f->key == key_swapfree) {
            mem_free_swap += i;
          }
        }
        break;

      default:
        break;
    }
  }

  file_free_file(f0);

  config.memory.total = mem_total;
  config.memory.free = mem_free;
  config.memory.free_swap = mem_free_swap;

  util_update_meminfo();
}


void util_update_meminfo()
{
  int i;
  int rd_mem = 0;

  for(i = 0; i < sizeof config.ramdisk / sizeof *config.ramdisk; i++) {
    if(config.ramdisk[i].inuse) rd_mem += config.ramdisk[i].size;
  }

  config.memory.current = config.memory.free - (rd_mem >> 10);
}


int util_free_main(int argc, char **argv)
{
  util_free_mem();

  printf("MemTotal: %9d kB\nMemFree: %10d kB\n", config.memory.total, config.memory.free);

  return 0;
}


int util_mkdir_main(int argc, char **argv)
{
  int i;

  argv++; argc--;

  while(argc--) {
    i = mkdir(*argv, 0755);
    if(i) {
      fprintf(stderr, "mkdir: "); perror(*argv);
      return errno;
    }
    argv++;
  }

  return 0;
}


int util_kill_main(int argc, char **argv)
{
  int i, sig = SIGTERM;
  pid_t pid;
  char *s;

  argv++; argc--;

  if(**argv == '-') {
    sig = strtol(*argv + 1, &s, 0);
    if(*s) return fprintf(stderr, "kill: bad signal spec \"%s\"\n", *argv + 1), 1;

    argv++;
    argc--;
  }

  while(argc--) {
    pid = strtoul(*argv, &s, 0);
    if(*s) return fprintf(stderr, "kill: %s: no such pid\n", *argv), 1;
    i = kill(pid, sig);
    if(i) {
      fprintf(stderr, "kill: "); perror(*argv);
      return errno;
    }
    argv++;
  }

  return 0;
}


int util_bootpc_main(int argc, char **argv)
{
  int i;
  char *dev = "eth0";

  argv++; argc--;

  if(argc && !strcmp(*argv, "-t")) {
    bootp_testing = 1;
    argc--; argv++;
  }

  if(argc) {
    dev = *argv;
  }

  i = performBootp(dev,
    "255.255.255.255", "", config.net.bootp_timeout,
    0, NULL, 0, 1, BP_PUT_ENV | BP_PRINT_OUT, 1
  );

  return i;
}


slist_t *slist_new()
{
  return calloc(1, sizeof (slist_t));
}


slist_t *slist_free(slist_t *sl)
{
  slist_t *next;

  for(; sl; sl = next) {
    next = sl->next;
    if(sl->key) free(sl->key);
    if(sl->value) free(sl->value);
    free(sl);
  }

  return NULL;
}


slist_t *slist_append(slist_t **sl0, slist_t *sl)
{
  for(; *sl0; sl0 = &(*sl0)->next);
  return *sl0 = sl;
}


slist_t *slist_append_str(slist_t **sl0, char *str)  
{
  slist_t *sl;

  sl = slist_append(sl0, slist_new());
  sl->key = strdup(str);

  return sl;
}


slist_t *slist_add(slist_t **sl0, slist_t *sl)
{
  sl->next = *sl0;
  return *sl0 = sl;
}


slist_t *slist_getentry(slist_t *sl, char *key)
{
  if(key) {
    for(; sl; sl = sl->next) {
      if(sl->key && !strcmp(key, sl->key)) return sl;
    }
  }

  return NULL;
}


slist_t *slist_reverse(slist_t *sl0)
{
  slist_t *sl1 = NULL, *sl, *next;

  for(sl = sl0; sl; sl = next) {
    next = sl->next;
    slist_add(&sl1, sl);
  }

  return sl1;
}


/*
 * split a string
 *
 * Note: works differently depending on whether 'del' is whitespace or not!
 */
slist_t *slist_split(char del, char *text)
{
  slist_t *sl0 = NULL, *sl;
  int i, len;
  char *s, *t;

  if(!text) return NULL;

  text = strdup(text);

  if(isspace(del)) {
    len = strlen(text);

    for(i = 0; i < len; i++) if(isspace(text[i])) text[i] = 0;

    for(s = text; s < text + len; s++) {
      if(*s && (s == text || !s[-1])) {
        sl = slist_append(&sl0, slist_new());
        sl->key = strdup(s);
      }
    }
  }
  else {
    for(s = text; (t = strchr(s, del)); s = t + 1) {
      *t = 0;
      sl = slist_append(&sl0, slist_new());
      sl->key = strdup(s);
    }
    sl = slist_append(&sl0, slist_new());
    sl->key = strdup(s);
  }

  free(text);

  return sl0;
}


/*
 * Clear 'inet' und add 'name' to it.
 *
 * 'inet' is unchanged if 'name' is NULL.
 * If 'name' is "", 'inet' is just cleared.
 */
void name2inet(inet_t *inet, char *name)
{
  inet_t inet_new = {};

  if(!inet || !name) return;

  if(*name) inet_new.name = strdup(name);

  if(inet->name) free(inet->name);

  *inet = inet_new;
}


void s_addr2inet(inet_t *inet, unsigned long s_addr)
{
  inet_t inet_new = {};

  if(!inet) return;

  if(inet->name) free(inet->name);

  inet_new.ip.s_addr = s_addr;
  inet_new.name = strdup(inet_ntoa(inet_new.ip));

  if(inet_new.name) inet_new.ok = 1;

  *inet = inet_new;
}


char *inet2print(inet_t *inet)
{
  static char buf[256];
  char *s;

  *buf = 0;

  if(!inet || (!inet->name && !inet->ok)) return buf;

  s = inet_ntoa(inet->ip);

  if(inet->name && s && !strcmp(s, inet->name)) s = NULL;
  if(!inet->ok) s = "no ip";

  sprintf(buf, "%s%s%s%s",
    inet->name ? inet->name : "",
    s ? " (" : "",
    s ? s : "",
    s ? ")" : ""
  );

  return buf;
}


#if 0

ftp://flup:flup1@ftp.zap:21/pub/suse
file:/blub/blubber

#endif

url_t *parse_url(char *str)
{
  static url_t url = {};
  char *s, *s0, *s1;
  unsigned u;
  int scheme = -1, i;

  if(!str) return NULL;
  str = strdup(str);

#if 0
  fprintf(stderr, "url = \"%s\"\n", str);
#endif

  if(url.server) free(url.server);
  if(url.dir) free(url.dir);
  if(url.user) free(url.user);
  if(url.password) free(url.password);

  memset(&url, 0, sizeof url);

  if((s0 = strchr(str, ':'))) {
    *s0++ = 0;

    if(*str) scheme = file_sym2num(str);

    if(s0[0] == '/' && s0[1] == '/') {
      s0 += 2;
      if((s = strchr(s0, '/'))) {
        url.dir = strdup(s);
        *s = 0;
      }
      else {
        url.dir = strdup("/");
      }

      if((s = strchr(s1 = s0, '@'))) {
        *s = 0;
        s0 = s + 1;

        if((s = strchr(s1, ':'))) {
         *s++ = 0;
         if(*s) url.password = strdup(s);
        }
        if(*s1) url.user = strdup(s1);
      }

      if((s = strchr(s0, ':'))) {
        *s++ = 0;
        if(*s) {
          u = strtoul(s, &s1, 0);
          if(!*s1) url.port = u;
        }
      }
      if(*s0) url.server = strdup(s0);
    }
    else {
      url.dir = strdup(*s0 ? s0 : "/");
    }
  }
  else {
    i = file_sym2num(str);
    if(i > 0) {
      scheme  = i;
    }
  }

  if(scheme == inst_smb && url.dir && *url.dir == '/') {
    str_copy(&url.dir, url.dir + 1);
  }


  free(str);
  if(scheme >= 0) url.scheme = scheme;

#if 0
  fprintf(stderr,
    "  scheme = %s, server = \"%s\", dir = \"%s\"\n"
    "  user = \"%s\", password = \"%s\", port = %u\n",
    get_instmode_name(url.scheme), url.server, url.dir,
    url.user, url.password, url.port
  );
#endif

  if(url.scheme || url.dir || url.server) return &url;

  return NULL;
}


/*
 * copy strings, *dst points to malloc'ed memory
 */
void str_copy(char **dst, char *src)
{
  char *s;

  if(!dst) return;

  s = src ? strdup(src) : NULL;
  if(*dst) free(*dst);
  *dst = s;
}


void strprintf(char **buf, char *format, ...)
{
  char sbuf[1024];
  
  va_list args;

  va_start(args, format);
  vsnprintf(sbuf, sizeof sbuf - 1, format, args);
  va_end(args);

  sbuf[sizeof sbuf - 1] = 0;

  if(*buf) free(*buf);

  *buf = strcpy(malloc(strlen(sbuf) + 1), sbuf);
}


void set_instmode(instmode_t instmode)
{
  config.instmode_extra = config.instmode = instmode;

  switch(instmode) {
    case inst_cdrom:
    case inst_cdwithnet:
    case inst_dvd:
      config.insttype = inst_cdrom;
      config.instmode = inst_cdrom;
      break;

    case inst_hd:
      config.insttype = inst_hd;
      break;

    case inst_floppy:
      config.insttype = inst_floppy;
      break;

    default:
      config.insttype = inst_net;
  }

  if(
    (instmode == inst_ftp || instmode == inst_http) &&
    !config.net.proxyproto
  ) {
    config.net.proxyproto = inst_http;
  }

#if 0
  if(instmode == inst_tftp) {
    config.fullnetsetup = 1;
  }
#endif
}


char *get_instmode_name(instmode_t instmode)
{
  return file_num2sym("no scheme", instmode);
}


char *get_instmode_name_up(instmode_t instmode)
{
  static char *name = NULL;
  int i;

  str_copy(&name, file_num2sym("no scheme", instmode));

  if(name) {
    for(i = 0; name[i]; i++) name[i] = toupper(name[i]);
  }

  return name;
}


/*
 * reset some values
 */
void net_read_cleanup()
{
  config.net.ftp_sock = -1;
  config.net.file_length = 0;
  memset(&config.net.tftp, 0, sizeof config.net.tftp);
  str_copy(&config.cache.buf, NULL);
  config.cache.size = config.cache.cnt = 0;
}

/*
 * return a file handle or, if 'filename' is NULL just check if
 # we can connect to the ftp server
 */
int net_open(char *filename)
{
  int fd = -1, len = 0;
  char *user, *password;
  char buf[256];
  char *instmode_name = get_instmode_name(config.instmode);
  struct sockaddr_in sa;

  user = config.net.user;
  password = config.net.password;

  if(user && !*user) user = NULL;
  if(password && !*password) password = NULL;

  str_copy(&config.net.error, NULL);

  net_read_cleanup();

  if(net_check_address2(&config.net.server, 1)) {
    sprintf(buf, "invalid %s server address", instmode_name);
    str_copy(&config.net.error, buf);
    return -1;
  }

  if(config.net.proxyport && config.net.proxy.name && net_check_address2(&config.net.proxy, 1)) {
    sprintf(buf, "invalid %s proxy address", instmode_name);
    str_copy(&config.net.error, buf);
    return -1;
  }

  if(
    config.instmode == inst_ftp &&
    !(config.net.proxyport && config.net.proxyproto == inst_http)
  ) {

    if(config.net.proxyport && config.net.proxy.ok) {
      config.net.ftp_sock = ftpOpen(inet_ntoa(config.net.server.ip), user, password, inet_ntoa(config.net.proxy.ip), config.net.proxyport);
    }
    else {
      config.net.ftp_sock = ftpOpen(inet_ntoa(config.net.server.ip), user, password, NULL, config.net.port);
    }

    if(config.net.ftp_sock < 0) {
      str_copy(&config.net.error, (char *) ftpStrerror(config.net.ftp_sock));
      return config.net.ftp_sock = -1;
    }

    if(!filename) {
      ftpClose(config.net.ftp_sock);   
      config.net.ftp_sock = -1;

      return 0;
    }

    fd = ftpGetFileDesc(config.net.ftp_sock, filename);

    if(fd < 0) {
      str_copy(&config.net.error, (char *) ftpStrerror(fd));
      ftpClose(config.net.ftp_sock);
      return config.net.ftp_sock = -1;
    }

  }

  if(
    config.instmode == inst_http ||
    (config.net.proxyport && config.net.proxyproto == inst_http)
  ) {
    if(config.net.proxyport && config.net.proxy.ok) {
      fd = http_connect(
        &config.net.server, filename, get_instmode_name(config.instmode),
        &config.net.proxy, config.net.port, config.net.proxyport,
        &len
      );
    }
    else {
      fd = http_connect(&config.net.server, filename, NULL, NULL, config.net.port, 0, &len);
    }

    if(fd < 0) {
//      if(errno) str_copy(&config.net.error, strerror(errno));
      return -1;
    }

    if(len) {
      config.net.file_length = len;
      if(filename) fprintf(stderr, "http: %s (%d bytes)\n", filename, config.net.file_length);
    }
  }

  if(config.instmode == inst_tftp) {

    sa.sin_addr = config.net.server.ip;
    sa.sin_family = AF_INET;
    fd = tftp_open(&config.net.tftp, &sa, filename ?: "", config.net.tftp_timeout);

    if(fd < 0) {
      str_copy(&config.net.error, tftp_error(&config.net.tftp));
      return fd;
    }

    if(!filename) {
      tftp_close(&config.net.tftp);
      return 0;
    }

  }

  return fd;
}


void net_close(int fd)
{
  if(fd > 0) close(fd);

  if(config.instmode == inst_ftp) {
    if(config.net.ftp_sock < 0) return;

    ftpGetFileDone(config.net.ftp_sock);
    ftpClose(config.net.ftp_sock);
  }

  net_read_cleanup();
}


int net_read(int fd, char *buf, int len)
{
  int l;

  if(config.cache.buf) {
    if(config.cache.cnt < config.cache.size) {
      l = config.cache.size - config.cache.cnt;
      if(len > l) len = l;
      memcpy(buf, config.cache.buf + config.cache.cnt, len);
      config.cache.cnt += len;
      return len;
    }
  }

  if(config.instmode != inst_tftp) {
    return read(fd, buf, len);
  }

  l = tftp_read(&config.net.tftp, buf, len);

  if(l < 0) {
    str_copy(&config.net.error, tftp_error(&config.net.tftp));
  }

  return l;
}


int util_wget_main(int argc, char **argv)
{
  url_t *url;
  int i, j, fd = -1;
  unsigned char buf[0x1000];
  char *s;

  argv++; argc--;

  if(argc != 1) return fprintf(stderr, "usage: wget url\n"), 1;

  url = parse_url(*argv);

  if(!url || !url->scheme) return fprintf(stderr, "invalid url\n"), 2;

  set_instmode(url->scheme);

  config.net.tftp_timeout = 10;
  
  config.net.port = url->port;
  str_copy(&config.serverdir, url->dir);
  str_copy(&config.net.user, url->user);
  str_copy(&config.net.password, url->password);
  if(config.insttype == inst_net) {
    name2inet(&config.net.server, url->server);

    if((s = getenv("proxy"))) {
      url = parse_url(s);
      if(url) {
        config.net.proxyproto = inst_http;
        if(url->server) name2inet(&config.net.proxy, url->server);
        if(url->scheme) config.net.proxyproto = url->scheme;
        if(url->port) config.net.proxyport = url->port;
        // fprintf(stderr, "using proxy %s://%s:%d\n", get_instmode_name(config.net.proxyproto), config.net.proxy.name, config.net.proxyport);
      }
    }

    fd = net_open(config.serverdir);
  }

  if(fd < 0) {
    util_print_net_error();
    return 1;
  }

  do {
    i = net_read(fd, buf, sizeof buf);
    j = 0;
    if(i > 0) {
      j = write(1, buf, i);
    }
  }  
  while(i > 0 && j > 0);

  if(i < 0) {
    perror(config.serverdir);
  }

  net_close(fd);

  return 0;
}


int util_fstype_main(int argc, char **argv)
{
  char *s;

  argv++; argc--;

  if(argc != 1) return fprintf(stderr, "usage: fstype blockdevice\n"), 1;

  s = fstype(*argv);

  printf("%s: %s\n", *argv, s ?: "unknown fs");

  return 0;
}


int util_modprobe_main(int argc, char **argv)
{
  argv++; argc--;

  if(!argc) return fprintf(stderr, "usage: modprobe module [module params]\n"), 1;

  return mod_modprobe(argv[0], argc > 1 ? argv[1] : NULL);
}


/*
 * Return fs name. If we have to load a module first, return it in *module.
 */
char *util_fstype(char *dev, char **module)
{
  char *type, *s;
  file_t *f0, *f;

  type = dev ? fstype(dev) : NULL;

  if(module) *module = type;

  if(!type) return NULL;

  if(module) {
    f0 = file_read_file("/proc/filesystems");
    for(f = f0; f; f = f->next) {
      s = strcmp(f->key_str, "nodev") ? f->key_str : f->value;
      if(!strcmp(s, type)) {
        *module = NULL;
        break;
      }
    }

    file_free_file(f0);
  }

  return type;
}



/*
 * returns loop device used
 */
char *util_attach_loop(char *file, int ro)
{
  struct loop_info loopinfo;
  int fd, rc, i, device;
  static char buf[32];

  if((fd = open(file, ro ? O_RDONLY : O_RDWR)) < 0) {
    perror(file);
    return NULL;
  }

  for(i = 0; i < 8; i++) {
    sprintf(buf, "/dev/loop%d", i);
    if((device = open(buf, ro ? O_RDONLY : O_RDWR)) >= 0) {
      memset(&loopinfo, 0, sizeof loopinfo);
      strcpy(loopinfo.lo_name, file);
      rc = ioctl(device, LOOP_SET_FD, fd);
      if(rc != -1) rc = ioctl(device, LOOP_SET_STATUS, &loopinfo);
      close(device);
      if(rc != -1) break;
    }
  }

  close(fd);

  return i < 8 ? buf : NULL;
}


int util_detach_loop(char *dev)
{
  int i, fd;

  if((fd = open(dev, O_RDONLY)) < 0) {
    perror(dev);
    return -1;
  }

  if((i = ioctl(fd, LOOP_CLR_FD, 0)) == -1) {
    perror(dev);
  }

  close(fd);

  return i;
}


int util_mount(char *dev, char *dir, unsigned long flags)
{
  char *type, *loop_dev;
  int err = -1;
  static char *fs_types[] = {
    "minix", "ext2", "reiserfs", "xfs",
    "vfat", "iso9660", "msdos", "hpfs",
    0
  };
  char **fs_type = fs_types;
  struct stat sbuf;

  if(!dev || !dir) return -1;

  if(strstr(dir, "/mounts/") == dir && stat(dir, &sbuf)) {
    mkdir(dir, 0755);
  }

  if(stat(dev, &sbuf)) {
    fprintf(stderr, "mount: %s: %s\n", dev, strerror(errno));
    return -1;
  }

  if(S_ISDIR(sbuf.st_mode)) {
    err = mount(dev, dir, "none", MS_BIND, 0);
    if(err && config.run_as_linuxrc) {
      fprintf(stderr, "mount: %s: %s\n", dev, strerror(errno));
    }
    return err;
  }

  if(!S_ISREG(sbuf.st_mode) && !S_ISBLK(sbuf.st_mode)) {
    fprintf(stderr, "mount: %s: not a block device\n", dev);
    return -1;
  }

  type = util_fstype(dev, NULL);

  if(type &&
    (
      S_ISREG(sbuf.st_mode) ||
      (!strcmp(type, "cramfs") && strstr(dev, "/dev/ram") == dev)
    )
  ) {
    if(config.run_as_linuxrc) fprintf(stderr, "mount: %s: we need a loop device\n", dev);
    loop_dev = util_attach_loop(dev, (flags & MS_RDONLY) ? 1 : 0);
    if(!loop_dev) {
      fprintf(stderr, "mount: no usable loop device found\n");
      return -1;
    }
    if(config.run_as_linuxrc) fprintf(stderr, "mount: using %s\n", loop_dev);
    dev = loop_dev;
  }

  if(type) {
    err = mount(dev, dir, type, flags, 0);
    if(err && config.run_as_linuxrc) {
      fprintf(stderr, "mount: %s: %s\n", dev, strerror(errno));
    }
  }
  else {
    fprintf(stderr, "%s: unknown fs type\n", dev);
    while(*fs_type) {
      err = mount(dev, dir, *fs_type, flags, 0);
      if(!err) break;
      fs_type++;
    }
  }

  return err;
}


int util_mount_ro(char *dev, char *dir)
{
  return util_mount(dev, dir, MS_MGC_VAL | MS_RDONLY);
}


int util_mount_rw(char *dev, char *dir)
{
  return util_mount(dev, dir, 0);
}


void util_update_netdevice_list(char *module, int add)
{
  file_t *f0, *f;
  slist_t *sl;

  f0 = file_read_file("/proc/net/dev");
  if(!f0) return;

  if((f = f0) && (f = f->next)) {	/* skip 2 lines */
    for(f = f->next; f; f = f->next) {
      if(!strcmp(f->key_str, "lo")) continue;
      if(strstr(f->key_str, "sit") == f->key_str) continue;
      sl = slist_getentry(config.net.devices, f->key_str);
      if(!sl && add) {
        sl = slist_append_str(&config.net.devices, f->key_str);
        str_copy(&sl->value, module);
      }
      else if(sl && !add) {
        str_copy(&sl->key, NULL);
        str_copy(&sl->value, NULL);
      }
    }
  }

  file_free_file(f0);
}


int util_update_disk_list(char *module, int add)
{
  str_list_t *hsl;
  slist_t *sl;
  int added = 0;

  hd_data_t *hd_data = calloc(1, sizeof *hd_data);

  hd_set_probe_feature(hd_data, pr_partition);
  hd_data->flags.list_md = 1;
  hd_scan(hd_data);

  for(hsl = hd_data->disks; hsl; hsl = hsl->next) {
    sl = slist_getentry(config.disks, hsl->str);
    if(!sl && add) {
      sl = slist_append_str(&config.disks, hsl->str);
      str_copy(&sl->value, module);
      added++;
    }
    else if(sl && !add) {
      str_copy(&sl->key, NULL);
      str_copy(&sl->value, NULL);
    }
  }

  for(hsl = hd_data->partitions; hsl; hsl = hsl->next) {
    sl = slist_getentry(config.partitions, hsl->str);
    if(!sl && add) {
      sl = slist_append_str(&config.partitions, hsl->str);
      str_copy(&sl->value, module);
      added++;
    }
    else if(sl && !add) {
      str_copy(&sl->key, NULL);
      str_copy(&sl->value, NULL);
    }
  }

  hd_free_hd_data(hd_data);
  free(hd_data);

  return added;
}


void util_update_cdrom_list()
{
  slist_t *sl;
  file_t *f0, *f;
  char *s, *t;

  config.cdroms = slist_free(config.cdroms);

  f0 = file_read_file("/proc/sys/dev/cdrom/info");

  for(f = f0; f; f = f->next) {
    if(!strcmp(f->key_str, "drive") && strstr(f->value, "name:") == f->value) {
      s = strchr(f->value, ':') + 1;
      while((t = strsep(&s, " \t\n"))) {
        if(!*t) continue;
        sl = slist_add(&config.cdroms, slist_new());
        str_copy(&sl->key, t);
      }
      break;
    }
  }

  file_free_file(f0);
}


void util_update_swap_list()
{
  file_t *f0, *f;

  config.swaps = slist_free(config.swaps);

  f0 = file_read_file("/proc/swaps");

  for(f = f0; f; f = f->next) {
    if(f->key == key_none && strstr(f->key_str, "/dev/") == f->key_str) {
      slist_append_str(&config.swaps, f->key_str + sizeof "/dev/" - 1);
    }
  }

  file_free_file(f0);
}


int util_is_dir(char *dir)
{
  struct stat sbuf;

  if(stat(dir, &sbuf)) return 0;

  return S_ISDIR(sbuf.st_mode) ? 1 : 0;
}


int util_is_mountable(char *file)
{
  int i, compressed = 0;

  i = util_fileinfo(file, NULL, &compressed);

  return i || compressed ? 0 : 1;
}


void util_debugwait(char *msg)
{
#ifdef LXRC_DEBUG
  if(config.debugwait) {
    int win_old;
    if(!(win_old = config.win)) util_disp_init();
    dia_message(msg ?: "hi", MSGTYPE_INFO);
    if(!win_old) util_disp_done();
  }
#endif
}

#include "hwcheck.h"

void util_hwcheck()
{
  int i, j;
  static char *floppy = NULL, *log = NULL;
  char buf[256], old_bg, old_fg;
  window_t win;

  fprintf(stderr, "Checking hardware...\n");
  if(config.win) {
    dia_info(&win, "Checking hardware");
  }
  else {
    printf("Checking hardware...\n");
    fflush(stdout);
  }

  i = do_hwcheck();

  if(config.win) win_close(&win);

  if(!config.win) util_disp_init();

  old_bg = colors_prg->msg_win;
  old_fg = colors_prg->msg_fg;

  if(i) {
    colors_prg->msg_win = COL_RED;
    colors_prg->msg_fg = COL_BWHITE;
  }

  sprintf(buf, "Hardware check results: Ready for %s", config.product);
  dia_show_file(!i ? buf : "Hardware check results", "/tmp/hw_overview.log", FALSE);

  colors_prg->msg_win = old_bg;
  colors_prg->msg_fg = old_fg;

  i = dia_yesno("Save results?", NO);
  if(i != YES) return;

  if(!floppy) {
    floppy = strdup(config.floppy_dev[0] ?: "/dev/fd0");
  }

  i = dia_input2("Floppy device name", &floppy, 30, 0);
  if(i) return;

  if(util_mount_rw(floppy, config.mountpoint.floppy)) {
    dia_message("Unable to mount floppy", MSGTYPE_ERROR);
    return;
  }

  if(!log || sscanf(log, "hw_%d.log", &i) == 1) {
    for(i = 0; i < 100; i++) {
      sprintf(buf, "%s/hw_%02d.log", config.mountpoint.floppy, i);
      if(!util_check_exist(buf)) break;
    }
    sprintf(buf, "hw_%02d.log", i);
    str_copy(&log, buf);
  }
  
  i = dia_input2("Logfile name", &log, 30, 0);
  if(i || !log || !*log) return;

  sprintf(buf,
    "cat /tmp/hw_overview.log /tmp/hw_detail.log >%s/%s",
    config.mountpoint.floppy,
    log
  );

  i = system(buf);

  j = util_umount(config.mountpoint.floppy);

  if(i || j) {
    dia_message("Error writing logfile", MSGTYPE_ERROR);
    return;
  }

  dia_message("Results saved", MSGTYPE_INFO);
}


void util_set_serial_console(char *str)
{
  slist_t *sl;
  char buf[256];

  if(!str || !*str) return;

  str_copy(&config.serial, str);

  sl = slist_split(',', config.serial);

  if(sl) {
    if (strncmp("/dev/", sl->key, 5) == 0)
      sprintf(buf, "%s", sl->key);
    else
      sprintf(buf, "/dev/%s", sl->key);
    if(!config.console || strcmp(buf, config.console)) {
      str_copy(&config.console, buf);
      freopen(config.console, "r", stdin);
      freopen(config.console, "a", stdout);
    }
    config.textmode = 1;
  }
     
  slist_free(sl);
}


void util_set_stderr(char *name)
{
  if(!name || !*name) return;

  str_copy(&config.stderr_name, name);

  freopen(config.stderr_name, "a", stderr);

#ifndef DIET
  setlinebuf(stderr);
#else
  {
    static char buf[128];
    setvbuf(stderr, buf, _IOLBF, sizeof buf);
  }
#endif
}


void util_set_product_dir(char *prod)
{
  if(!prod | !*prod) return;

  str_copy(&config.product_dir, prod);

  str_copy(&config.installdir, "/boot/inst-sys");
  str_copy(&config.rootimage, "/boot/root");
  str_copy(&config.rescueimage, "/boot/rescue");
  str_copy(&config.demoimage, "/boot/cd-demo");
}

