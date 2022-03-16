/*
 *
 * util.c        Utility functions for linuxrc
 *
 * Copyright (c) 1996-2008  Hubert Mantel, SuSE Linux AG (mantel@suse.de)
 *
 */

#define __LIBRARY__

#define _GNU_SOURCE	/* stat64, asprintf */

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
#include <sys/klog.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <fcntl.h>
#include <time.h>
#include <syscall.h>
#include <dirent.h>
#include <errno.h>
#include <fnmatch.h>
#include <signal.h>
#include <sys/swap.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <utime.h>
#include <net/if.h>
#include <linux/major.h>
#include <linux/raid/md_u.h>
#include <execinfo.h>

#define CDROMEJECT	0x5309	/* Ejects the cdrom media */

#include <linux/posix_types.h>
#undef dev_t
#define dev_t __kernel_dev_t
#include <linux/loop.h>
#undef dev_t

#include "global.h"
#include "display.h"
#include "util.h"
#include "window.h"
#include "module.h"
#include "keyboard.h"
#include "dialog.h"
#include "net.h"
#include "auto2.h"
#include "file.h"
#include "fstype.h"
#include "scsi_rename.h"
#include "utf8.h"
#include "url.h"
#include "linuxrc.h"

extern char **environ;

#define LED_TIME     50000

typedef struct {
  instmode_t scheme;
  char *server;
  char *share;
  char *dir;
  char *user;
  char *password;
  char *domain;
  unsigned port;
} url1_t;

static struct hlink_s {
  struct hlink_s *next;
  dev_t dev;
  ino_t ino;
  char *dst;
} *hlink_list = NULL;

static char *exclude = NULL;
static int rec_level = 0;
static int extend_ready = 0;

static void add_flag(slist_t **sl, char *buf, int value, char *name);

static int do_cp(char *src, char *dst);
static char *walk_hlink_list(ino_t ino, dev_t dev, char *dst);
static void free_hlink_list(void);

static void add_driver_update(char *dir, char *loc);
static int cmp_dir_entry(slist_t *sl0, slist_t *sl1);
static int cmp_dir_entry_s(const void *p0, const void *p1);
static void create_update_name(unsigned idx);

static char *read_symlink(char *name);
static void scsi_rename_devices(void);
static void scsi_rename_onedevice(char **dev);

static int skip_spaces(unsigned char **str);
static int word_size(unsigned char *str, int *width, int *enc_len);

static char *mac_to_interface_log(char *mac, int log);

static void util_extend_usr1(int signum);
static int util_extend(char *extension, char task, int verbose);

static int cmp_alpha(slist_t *sl0, slist_t *sl1);
static int cmp_alpha_s(const void *p0, const void *p1);
static slist_t *get_kernel_list(char *dev);

void util_redirect_kmsg()
{
  static char newvt[2] = { 11, 4 /* console 4 */ };
  int fd, loglevel;

  loglevel = config.loglevel;

  /* serial: default to 1 (no logging) */
  if(!loglevel && config.serial) loglevel = 1;

  if(loglevel) klogctl(8, NULL, loglevel);

  if(!config.serial && (fd = open(config.console, O_RDONLY))) {
    ioctl(fd, TIOCLINUX, &newvt);
    close(fd);
    /* 'create' console 4 */
    fd = open("/dev/tty4", O_RDWR);
    close(fd);
  }
}


/*
 * Center text.
 */
void util_center_text(unsigned char *txt, int max_width)
{
  int width, pre, post, len;

  width = utf8_strwidth(txt);
  len = strlen(txt);

  /* functions seem to expect max_width to include the final '\0'. */
  max_width--;

  if(width >= max_width) return;

  pre = (max_width - width) / 2;
  post = max_width - width - pre;

  if(len) memmove(txt + pre, txt, len);
  if(pre) memset(txt, ' ', pre);
  if(post) memset(txt + pre + len, ' ', post);
  txt[pre + len + post] = 0;
}


/*
 * Left-align text.
 */
void util_fill_string(unsigned char *str, int max_width)
{
  int width, len;

  /* functions seem to expect max_width to include the final '\0'. */
  max_width--;

  width = utf8_strwidth(str);
  len = strlen(str);

  while(width++ < max_width) {
    str[len++] = ' ';
    str[len] = 0;
  }
}


void util_generate_button(button_t *button, char *txt, int size)
{
  size = size > BUTTON_SIZE_NORMAL ? BUTTON_SIZE_LARGE : BUTTON_SIZE_NORMAL;

  memset(button, 0, sizeof *button);
  utf8_strwcpy(button->text, txt, size);

  util_center_text(button->text, size + 1);
}


int skip_spaces(unsigned char **str)
{
  int spaces = 0;

  while(**str && (**str == ' ' || **str == '\t')) {
    (*str)++;
    spaces++;
  }

  return spaces;
}


int word_size(unsigned char *str, int *width, int *enc_len)
{
  unsigned char *s;
  int c;

  *width = *enc_len = 0;

  if(!*str) return 0;

  s = str;

  if(skip_spaces(&s)) {
    *width = (!*s || *s == '\n') ? 0 : 1;
    *enc_len = s - str;

    return 1;
  }

  while(*s && *s != ' ' && *s != '\t' && *s != '\n') {
    if(!(c = utf8_decode(s))) break;
    s += utf8_enc_len(*s);
    *width += utf32_char_width(c);
    if(c >= 0x3000) break;
  }

  *enc_len = s - str;

  return 0;
}


/*
 * Add linebreaks to txt.
 * Returns number of lines put into the lines array.
 */
int util_format_txt(unsigned char *txt, unsigned char **lines, int max_width)
{
  int line, pos, width, w_width, w_enc_len, w_space;

  line = 0;
  *(lines[line] = malloc(UTF8_SIZE(max_width))) = 0;

  width = pos = 0;
  skip_spaces(&txt);

  while(*txt) {
    if(*txt == '\n') {
      w_width = w_enc_len = 0;
      w_space = 1;
      txt++;
      skip_spaces(&txt);
    }
    else {
      w_space = word_size(txt, &w_width, &w_enc_len);

      if(w_width + width <= max_width || width == 0) {
        if(w_width && w_enc_len) {
          if(w_space) {
            lines[line][pos++] = ' ';
            lines[line][pos] = 0;
          }
          else {
            memcpy(lines[line] + pos, txt, w_enc_len);
            lines[line][pos += w_enc_len] = 0;
          }
        }
        width += w_width;
        txt += w_enc_len;
        continue;
      }
    }

    if(line >= MAX_Y - 1) break;
    *(lines[++line] = malloc(UTF8_SIZE(max_width))) = 0;
    width = pos = 0;
    if(!w_space && w_enc_len) {
      memcpy(lines[line] + pos, txt, w_enc_len);
      lines[line][pos += w_enc_len] = 0;
      width += w_width;
    }

    txt += w_enc_len;
  }

  line++;

  for(pos = 0; pos < line; pos++) {
    width = strlen(lines[pos]);
    while(width > 0 && lines[pos][width - 1] == ' ') lines[pos][--width] = 0;
    util_center_text(lines[pos], max_width);
  }

  return line;
}


void util_create_items (item_t items_arr [], int nr_iv, int size_iv)
    {
    int  i_ii;

    for (i_ii = 0; i_ii < nr_iv; i_ii++)
        {
        items_arr [i_ii].text = malloc (UTF8_SIZE(size_iv));
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


/*
 * FIXME: can't get >32-bit sizes, but the size feature is currently not
 * used, so it doesn't matter. -> Remove size parameter later.
 */
int util_fileinfo(char *file_name, int *size, int *compressed)
{
  unsigned char buf[4];
  int fd, err = 0;
  off_t ofs;

  if(size) *size = 0;
  if(compressed) *compressed = 0;

  if(!(fd = open(file_name, O_RDONLY | O_LARGEFILE))) return -1;

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
    char   buffer_ti [1 << 19];
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

    size_ii = klogctl (3, buffer_ti, sizeof (buffer_ti));

    if (size_ii > 0 && bootmsg_pri)
        fwrite (buffer_ti, 1, size_ii, bootmsg_pri);

    if (bootmsg_pri) fclose (bootmsg_pri);

    for (pos_ii = 0; pos_ii < size_ii; pos_ii++)
        {
        line_ti [i_ii] = buffer_ti [pos_ii];
        if (line_ti [i_ii] == '\n' || i_ii >= (int) sizeof (line_ti) - 2)
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

    klogctl (5, 0, 0);

    fclose (outfile_pri);
    fclose (lastfile_pri);
    }


void util_print_banner (void)
    {
    window_t       win_ri;
    char           text_ti [UTF8_SIZE(MAX_X)];
    struct utsname utsinfo_ri;

    if(!config.win) return;

    uname (&utsinfo_ri);
    if (config.linemode) {
      printf (">>> linuxrc " LXRC_FULL_VERSION " (Kernel %s) %s <<<\n", 
              utsinfo_ri.release,
              config.platform_name);
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

    sprintf (text_ti, ">>> linuxrc " LXRC_FULL_VERSION " (Kernel %s) %s <<<",
             utsinfo_ri.release,
             config.platform_name);
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


/**
 * Check whether 'file' exists and return file type.
 *
 * @return
 * -  0: does not exist.
 * -  'r', 'd', 'b', 1: type (1: other)
 */
int util_check_exist(char *file)
{
  struct stat64 sbuf;

  if(!file || stat64(file, &sbuf)) return 0;

  if(S_ISREG(sbuf.st_mode)) return 'r';
  if(S_ISDIR(sbuf.st_mode)) return 'd';
  if(S_ISBLK(sbuf.st_mode)) return 'b';
  if(S_ISCHR(sbuf.st_mode)) return 'c';

  return 1;
}


/**
 * Check whether 'dir/file' exists and return file type.
 *
 * @return
 * -  0: does not exist.
 * -  'r', 'd', 'b', 1: type (1: other)
 */
int util_check_exist2(char *dir, char *file)
{
  char *buf = NULL;
  int type;

  if(!dir || !file) return 0;

  strprintf(&buf, "%s/%s", dir, file);
  type = util_check_exist(buf);
  str_copy(&buf, NULL);

  return type;
}


int util_check_break (void)
    {
    if (kbd_getch (FALSE) == KEY_CTRL_C)
        {
        if (dia_yesno ("Abort?", 2) == YES)
            return (1);
        else
            return (0);
        }
    else
        return (0);
    }


void util_disp_init()
{
  int i_ii;

  log_debug("win on\n");

  config.win = 1;
  util_plymouth_off();
  disp_set_display();
  if (config.utf8) printf("\033%%G");
  fflush(stdout);
  if (config.linemode)
    return;
  for(i_ii = 1; i_ii < max_y_ig; i_ii++) {
    printf("\n");
  }
  printf("\033[9;0]");
  disp_cursor_off();
  util_print_banner();
}


void util_disp_done()
{
  log_debug("win off\n");

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

  if(!dir) return 0;

  if(stat(dir, &sbuf)) return -1;

  // log_info("umount: >%s<\n", dir);

  f0 = file_read_file("/proc/mounts", kf_none);

  if((i = umount(dir))) {
    // log_info("umount: %s: %s\n", dir, strerror(errno));
    if(errno != ENOENT && errno != EINVAL) {
      if(config.run_as_linuxrc) log_info("umount: %s: %s\n", dir, strerror(errno));
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

  if(config.mountpoint.base && strstr(dir, config.mountpoint.base) == dir) rmdir(dir);

  return i;
}


void util_umount_all()
{
  int i;
  char *buf = NULL;

  url_umount(config.url.instsys);
  sync(); /* umount seems to be racy; see bnc#443430 */
  url_umount(config.url.install);

  for(i = config.mountpoint.cnt; i-- > 0;) {
    strprintf(&buf, "%smp_%04u", config.mountpoint.base, i);
    if(util_check_exist(buf)) util_umount(buf);
  }

  str_copy(&buf, NULL);
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

  log_info("eject %s\n", dev);

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


void add_driver_update(char *dir, char *loc)
{
  unsigned u;
  char *copy_dir[] = { "install", "modules", "y2update", "inst-sys" };
  char *src = NULL, *dst = NULL, *buf1 = NULL, *buf2 = NULL;
  char *argv[3];
  struct dirent *de;
  DIR *d;
  FILE *f;
  file_t *ft0, *ft;
  slist_t *sl;
  unsigned prio;

  /* create destination, if missing */
  if(util_check_exist(config.update.dst) != 'd') {
    if(mkdir(config.update.dst, 0755)) return;
  }

  str_copy(&config.update.id, NULL);

  prio = config.update.count;

  /* preliminary config file read for update id & priority */
  strprintf(&buf1, "%s/dud.config", dir);
  ft0 = file_read_file(buf1, kf_cfg);
  for(ft = ft0; ft; ft = ft->next) {
    if(ft->key == key_updateid && *ft->value) {
      str_copy(&config.update.id, ft->value);
    }
    if(ft->key == key_updateprio && ft->is.numeric) {
      prio = ft->nvalue;
      if(prio > MAX_UPDATES - 100) prio = MAX_UPDATES - 100;
    }
  }
  file_free_file(ft0);
  free(buf1); buf1 = NULL;

  for(; prio < MAX_UPDATES; prio++) {
    if(!config.update.map[prio]) break;
  }

  if(prio >= MAX_UPDATES) {
    log_info("Error: Too many driver updates!!!\n");
    return;
  }

  if((sl = slist_getentry(config.update.id_list, config.update.id))) {
    log_info("dud: %s (duplicate of %s, skipped)\n", loc, sl->value);
    return;
  }

  config.update.map[prio] = 1;
  config.update.count++;

  strprintf(&dst, "%s/%03u", config.update.dst, prio);
  if(mkdir(dst, 0755)) return;

  log_info("dud %u: %s", prio, loc);
  if(config.update.id) {
    log_info(" (id %s)", config.update.id);
  }
  log_info("\n");

  if(config.update.id) {
    sl = slist_append_str(&config.update.id_list, config.update.id);
    strprintf(&sl->value, "%u", prio);
  }

  /* copy directories */
  for(u = 0; u < sizeof copy_dir / sizeof *copy_dir; u++) {
    strprintf(&src, "%s/%s", dir, copy_dir[u]);
    if(util_check_exist(src) != 'd') continue;

    strprintf(&buf1, "%s/%s", dst, copy_dir[u]);

    if(util_check_exist(buf1) == 'd' || !mkdir(buf1, 0755)) {
      util_do_cp(src, buf1);
    }

    /* for compatibility */
    if(!strcmp(copy_dir[u], "y2update")) {
      strprintf(&buf1, "%s/%s", config.update.dst, copy_dir[u]);

      if(util_check_exist(buf1) == 'd' || !mkdir(buf1, 0755)) {
        util_do_cp(src, buf1);
      }
    }
  }

  /* copy config file */
  strprintf(&src, "%s/dud.config", dir);
  if(util_check_exist(src) == 'r') {
    argv[1] = src;
    argv[2] = dst;
    util_cp_main(3, argv);
  }

  /* make sure scripts are executable */
  strprintf(&buf1, "%s/install", dst);
  if((d = opendir(buf1))) {
    while((de = readdir(d))) {
      if(strstr(de->d_name, "update.") == de->d_name) {
        strprintf(&buf2, "%s/%s", buf1, de->d_name);
        chmod(buf2, 0755);
      }
    }
    closedir(d);
  }

  /* module things: save order to "module.order" */
  strprintf(&buf1, "%s/modules", dir);
  if(util_check_exist(buf1) == 'd') {
    if((d = opendir(buf1))) {
      strprintf(&buf2, "%s/modules/module.order", dst);
      if(!util_check_exist(buf2) && (f = fopen(buf2, "w"))) {
        while((de = readdir(d))) {
          if(mod_basename_len(de->d_name)) {
            char *mod_name = mod_basename(de->d_name);
            fprintf(f, "%s\n", mod_name);
            free(mod_name);
          }
        }
        fclose(f);
      }
      closedir(d);
    }
  }

  free(src);
  free(dst);
  free(buf1);
  free(buf2);
}


int cmp_dir_entry(slist_t *sl0, slist_t *sl1)
{
  int i0, i1;

  i0 = strtol(sl0->key, NULL, 10);
  i1 = strtol(sl1->key, NULL, 10);

  return i0 - i1;
}


/* wrapper for qsort */
int cmp_dir_entry_s(const void *p0, const void *p1)
{
  slist_t **sl0, **sl1;

  sl0 = (slist_t **) p0;
  sl1 = (slist_t **) p1;

  return cmp_dir_entry(*sl0, *sl1);
}


/*
 * Check for a valid driver update directory below <dir>; copy the files
 * to /update.
 *
 * Note: you must call util_do_driver_updates() to actuall apply
 * the update.
 */
int util_chk_driver_update(char *dir, char *loc)
{
  char *drv_src = NULL, *dud_loc = NULL, *s;
  slist_t *sl0 = NULL, *sl;
  struct dirent *de;
  DIR *d;
  int is_update = 0;

  if(!dir || !loc || !config.tmpfs || !config.update.dir) return is_update;

  strprintf(&drv_src, "%s%s", dir, config.update.dir);

  if(util_check_exist(drv_src) == 'd') {
    strprintf(&dud_loc, "%s:%s", loc, config.update.dir);
    is_update = 1;
    add_driver_update(drv_src, dud_loc);
  }

  d = opendir(dir);
  if(d) {
    while((de = readdir(d))) {
      strtol(de->d_name, &s, 10);
      if(!*s) {
        strprintf(&drv_src, "%s/%s%s", dir, de->d_name, config.update.dir);
        if(util_check_exist(drv_src) == 'd') {
          is_update = 1;
          slist_append_str(&sl0, de->d_name);
        }
      }
    }
    closedir(d);
  }

  sl0 = slist_sort(sl0, cmp_dir_entry_s);

  for(sl = sl0; sl; sl = sl->next) {
    strprintf(&drv_src, "%s/%s%s", dir, sl->key, config.update.dir);
    strprintf(&dud_loc, "%s:/%s%s", loc, sl->key, config.update.dir);
    add_driver_update(drv_src, dud_loc);
  }

  free(drv_src);
  free(dud_loc);

  return is_update;
}


void util_do_driver_update(unsigned idx)
{
  char *buf1 = NULL, *buf2 = NULL, *dst = NULL;
  file_t *f0, *f;
  slist_t *sl;

  strprintf(&dst, "%s/%03u", config.update.dst, idx);

  /* read config file */
  config.update.name_added = 0;
  strprintf(&buf1, "file:/%s/dud.config", dst);
  file_read_info_file(buf1, kf_cfg);

  if(!config.update.name_added) create_update_name(idx);

  /* write update name, if there is one */
  log_info("dud %u:\n", idx);
  for(
    ;
    (sl = *config.update.next_name);
    config.update.next_name = &(*config.update.next_name)->next
  ) {
    log_info("  %s\n", sl->key);
    log_show_maybe(!config.win, "Driver Update: %s\n", sl->key);
  }

  /* read new driver data */
  strprintf(&buf1, "%s/modules/hd.ids", dst);
  if(util_check_exist(buf1) == 'r') {
    strprintf(&buf2,
      "cat %s %s >/var/lib/hardware/tmp.ids",
      buf1,
      util_check_exist("/var/lib/hardware/hd.ids") ? "/var/lib/hardware/hd.ids" : ""
    );
    lxrc_run_console(buf2);
    rename("/var/lib/hardware/tmp.ids", "/var/lib/hardware/hd.ids");
  }

  /* link new modules */
  strprintf(&buf1, "%s/modules/module.order", dst);
  f0 = file_read_file(buf1, kf_none);
  for(f = f0; f; f = f->next) {
    char *mod_name = mod_basename(f->key_str);
    char *update_mod_name = NULL;
    const mod_extensions_t *update_mod_ext;

    // check if module exists in driver update
    //
    // if so, update_mod_ext->ext points to the module extension used for the module
    // else, update_mod_ext->ext is NULL
    for(update_mod_ext = mod_extensions; update_mod_ext->ext; update_mod_ext++) {
      strprintf(&update_mod_name, "%s/modules/%s%s", dst, mod_name, update_mod_ext->ext);
      if(util_check_exist(update_mod_name)) break;
    }

    // if the driver update contains the module...
    if(update_mod_ext->ext) {
      // remove existing module, with any possible extension
      for(const mod_extensions_t *mod_ext = mod_extensions; mod_ext->ext; mod_ext++) {
        strprintf(&buf1, "/modules/%s%s", mod_name, mod_ext->ext);
        unlink(buf1);
      }

      // create a link to the updated module
      strprintf(&buf1, "/modules/%s%s", mod_name, update_mod_ext->ext);
      symlink(update_mod_name, buf1);
    }

    free(update_mod_name);
    free(mod_name);
  }

  /* load new modules */
  strprintf(&buf1, "%s/modules/module.config", dst);
  if(util_check_exist(buf1) == 'r') {
    strprintf(&buf2, "cp %s/modules/module.config /modules", dst);
    lxrc_run(buf2);
    mod_init(1);
  }
  else {
    str_copy(&buf1, "");
    // build list of modules to unload
    for(f = f0; f; f = f->next) {
      char *mod_name = mod_basename(f->key_str);
      strprintf(&buf1, "%s %s", buf1, mod_name);
      free(mod_name);
    }
    if(*buf1) mod_unload_modules(buf1);
    for(f = f0; f; f = f->next) {
      char *mod_name = mod_basename(f->key_str);
      mod_modprobe(mod_name, NULL);
      free(mod_name);
    }
  }

  file_free_file(f0);
  free(dst);
  free(buf1);
  free(buf2);
}


/*
 * In case the update description is missing, create it using module names.
 */
void create_update_name(unsigned idx)
{
  char *buf1 = NULL;
  file_t *f0, *f;
  module_t *ml;

  strprintf(&buf1, "%s/%03u/modules/module.order", config.update.dst, idx);
  f0 = file_read_file(buf1, kf_none);
  for(f = f0; f; f = f->next) {
    char *basename = mod_basename(f->key_str);

    ml = mod_get_entry(basename);
    slist_append_str(&config.update.name_list, ml && ml->descr && *ml->descr ? ml->descr : basename);

    free(basename);
  }

  file_free_file(f0);
  free(buf1);

}


/*
 * Apply all new driver updates.
 */
void util_do_driver_updates()
{
  unsigned u;

  if(!config.tmpfs) return;

  for(u = 0; u < MAX_UPDATES; u++) {
    if(config.update.map[u] == 1) {
      util_do_driver_update(u);
      config.update.map[u] = 2;
    }
  }
}


int show_driver_updates()
{

  if(config.update.name_list) {
    dia_show_lines2("Driver Update list", config.update.name_list, 64);
  }
  else {
    dia_message("No Driver Updates so far.", MSGTYPE_INFO);
  }

  return 0;
}


void add_flag(slist_t **sl, char *buf, int value, char *name)
{
  int l;

  if(value <= 0) return;

  if(!*buf) strcpy(buf, "  ");
  l = strlen(buf);

  sprintf(buf + strlen(buf), "%s%s", buf[l - 1] == ' ' ? "" : ", ", name);

  if(strlen(buf) > 40) {
    slist_append_str(sl, buf);
    *buf = 0;
  }
}


void util_status_info(int log_it)
{
  int i, j;
  char *s;
  hd_data_t *hd_data;
  slist_t *sl, *sl0 = NULL;
  char buf[256];
  language_t *lang;
  driver_t *drv;

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

  sprintf(buf, "release version: %s", config.releasever ?: "unset");
  slist_append_str(&sl0, buf);

  sprintf(buf,
    "memory (MB): total %lld, free %lld (%lld), ramdisk %lld",
    (long long) config.memoryXXX.total >> 20,
    (long long) config.memoryXXX.current >> 20,
    (long long) (config.memoryXXX.current - config.memoryXXX.free_swap) >> 20,
    (long long) (config.memoryXXX.free - config.memoryXXX.current) >> 20
  );
  slist_append_str(&sl0, buf);

  sprintf(buf,
    "memory limits (MB): min %lld, yast %lld, image %lld",
    (long long) config.memoryXXX.min_free >> 20,
    (long long) config.memoryXXX.min_yast >> 20,
    (long long) config.memoryXXX.load_image >> 20
  );
  slist_append_str(&sl0, buf);

  util_get_ram_size();

  sprintf(buf,
    "RAM size (MB): total %lld, min %lld",
    (long long) (config.memoryXXX.ram >> 20),
    (long long) (config.memoryXXX.ram_min >> 20)
  );
  slist_append_str(&sl0, buf);

  sprintf(buf, "swap file size: %u MB", config.swap_file_size);
  slist_append_str(&sl0, buf);

  sprintf(buf,
    "zram: root size \"%s\", swap size \"%s\"",
    config.zram.root_size ?: "",
    config.zram.swap_size ?: ""
  );
  slist_append_str(&sl0, buf);

  sprintf(buf,
    "InstsysID: %s%s",
    config.instsys_id ?: "unset",
    config.instsys_complain ? config.instsys_complain == 1 ? " (check)" : " (block)" : ""
  );
  slist_append_str(&sl0, buf);

  sprintf(buf, "InitrdID: %s", config.initrd_id ?: "unset");
  slist_append_str(&sl0, buf);

  for(sl = config.update.expected_name_list; sl; sl = sl->next) {
    sprintf(buf, "expected update: %s", sl->key);
    slist_append_str(&sl0, buf);
  }

  sprintf(buf, "flags = ");
  add_flag(&sl0, buf, config.test, "test");
  add_flag(&sl0, buf, config.tmpfs, "tmpfs");
  add_flag(&sl0, buf, config.console_option, "console");
  add_flag(&sl0, buf, config.switch_to_fb, "switch2fb");
  add_flag(&sl0, buf, config.manual, "manual");
  add_flag(&sl0, buf, config.utf8, "utf8");
  add_flag(&sl0, buf, config.rescue, "rescue");
  add_flag(&sl0, buf, config.vnc, "vnc");
  add_flag(&sl0, buf, config.usessh, "usessh");
  add_flag(&sl0, buf, config.sshd_only, "sshd");
  add_flag(&sl0, buf, config.textmode, "textmode");
  add_flag(&sl0, buf, config.rebootmsg, "rebootmsg");
  add_flag(&sl0, buf, config.use_ramdisk, "ramdisk");
  add_flag(&sl0, buf, config.ask_language, "ask_lang");
  add_flag(&sl0, buf, config.ask_keytable, "ask_keytbl");
  add_flag(&sl0, buf, config.addswap, "addswap");
  add_flag(&sl0, buf, config.noshell, "noshell");
  add_flag(&sl0, buf, config.had_segv, "segv");
  add_flag(&sl0, buf, config.scsi_before_usb, "scsibeforeusb");
  add_flag(&sl0, buf, config.scsi_rename, "scsirename");
  add_flag(&sl0, buf, config.net.all_ifs, "all_ifs");
  add_flag(&sl0, buf, config.ntfs_3g, "ntfs-3g");
  add_flag(&sl0, buf, config.secure, "secure");
  add_flag(&sl0, buf, config.sslcerts, "sslcerts");
  add_flag(&sl0, buf, config.mediacheck, "mediacheck");
  add_flag(&sl0, buf, config.net.ipv4, "ipv4");
  add_flag(&sl0, buf, config.net.ipv6, "ipv6");
  add_flag(&sl0, buf, config.efi, "efi");
  add_flag(&sl0, buf, config.efi_vars, "efivars");
  add_flag(&sl0, buf, config.udev_mods, "udev.mods");
  add_flag(&sl0, buf, config.devtmpfs, "devtmpfs");
  add_flag(&sl0, buf, config.plymouth, "plymouth");
  add_flag(&sl0, buf, config.withiscsi, "iscsi");
  add_flag(&sl0, buf, config.withfcoe, "fcoe");
  add_flag(&sl0, buf, config.withipoib, "ipoib");
  add_flag(&sl0, buf, config.upgrade, "upgrade");
  add_flag(&sl0, buf, config.media_upgrade, "media_upgrade");
  add_flag(&sl0, buf, config.net.sethostname, "hostname");
  add_flag(&sl0, buf, config.self_update, "self_update");
  add_flag(&sl0, buf, config.repomd, "repomd");
  add_flag(&sl0, buf, config.norepo, "norepo");
  add_flag(&sl0, buf, config.device_auto_config, "deviceautoconfig");
  if(*buf) slist_append_str(&sl0, buf);

  if(config.self_update_url) {
    sprintf(buf, "self-update URL: %s", config.self_update_url);
    slist_append_str(&sl0, buf);
  }

  if(config.core) {
    sprintf(buf, "Core Dumps: %s (%sactive)", config.core, config.core_setup ? "" : "not ");
    slist_append_str(&sl0, buf);
  }

  if(config.extern_scheme) {
    strcpy(buf, "additional URL schemes:");
    slist_append_str(&sl0, buf);
    for(sl = config.extern_scheme; sl; sl = sl->next) {
      if(!sl->key) continue;
      sprintf(buf, "  %s: %s", sl->key, sl->value ?: "");
      slist_append_str(&sl0, buf);
    }
  }

  sprintf(buf, "net_config_mask = 0x%x", net_config_mask());
  slist_append_str(&sl0, buf);

  sprintf(buf, "netsetup = 0x%x/0x%x", config.net.do_setup, config.net.setup);
  slist_append_str(&sl0, buf);

  if((s = url_print(config.url.install, 0))) {
    slist_append_str(&sl0, "install url:");
    sprintf(buf, "  %s", s);
    slist_append_str(&sl0, buf);
  }

  if((s = url_print(config.url.instsys, 0))) {
    slist_append_str(&sl0, "instsys url:");
    sprintf(buf, "  %s", s);
    slist_append_str(&sl0, buf);
  }

  if((s = url_print(config.url.proxy, 0))) {
    slist_append_str(&sl0, "proxy url:");
    sprintf(buf, "  %s", s);
    slist_append_str(&sl0, buf);
  }

  if((s = url_print(config.url.autoyast, 0))) {
    slist_append_str(&sl0, "autoyast url:");
    sprintf(buf, "  %s", s);
    slist_append_str(&sl0, buf);
  }

  if((s = url_print(config.url.autoyast, 5))) {
    slist_append_str(&sl0, "autoyast url (ay fmt):");
    sprintf(buf, "  %s", s);
    slist_append_str(&sl0, buf);
  }

  if((s = url_print(config.url.autoyast2, 0))) {
    slist_append_str(&sl0, "autoyast2 url:");
    sprintf(buf, "  %s", s);
    slist_append_str(&sl0, buf);
  }

  if((s = url_print(config.url.autoyast2, 5))) {
    slist_append_str(&sl0, "autoyast2 url (ay fmt):");
    sprintf(buf, "  %s", s);
    slist_append_str(&sl0, buf);
  }

  strcpy(buf, "net devices = (");
  for(i = 0, sl = config.net.devices; sl; sl = sl->next) {
    if(!sl->key) continue;
    j = !config.net.device || strcmp(sl->key, config.net.device) ? 0 : 1;
    sprintf(buf + strlen(buf), "%s%s%s", i ? ", " : " ", sl->key, j ? "*" : "");
    if(sl->value) sprintf(buf + strlen(buf), " [%s]", sl->value);
    i = 1;
  }
  strcat(buf, " )");
  slist_append_str(&sl0, buf);

  if(config.ifcfg.initial) {
    strcpy(buf, "initially configured network interfaces:");
    slist_append_str(&sl0, buf);
    for(sl = config.ifcfg.initial; sl; sl = sl->next) {
      if(!sl->key) continue;
      sprintf(buf, "  %s", sl->key);
      slist_append_str(&sl0, buf);
    }
  }

  if(config.ifcfg.ibft) {
    strcpy(buf, "ibft interfaces:");
    slist_append_str(&sl0, buf);
    for(sl = config.ifcfg.ibft; sl; sl = sl->next) {
      if(!sl->key) continue;
      sprintf(buf, "  %s", sl->key);
      slist_append_str(&sl0, buf);
    }
  }

  if(config.ifcfg.if_state) {
    strcpy(buf, "network interface states:");
    slist_append_str(&sl0, buf);
    for(sl = config.ifcfg.if_state; sl; sl = sl->next) {
      if(!sl->key) continue;
      sprintf(buf, "  %s: %s", sl->key, sl->value);
      slist_append_str(&sl0, buf);
    }
  }

  if(config.ifcfg.if_up) {
    strcpy(buf, "up interfaces:");
    slist_append_str(&sl0, buf);
    for(sl = config.ifcfg.if_up; sl; sl = sl->next) {
      if(!sl->key) continue;
      sprintf(buf, "  %s", sl->key);
      slist_append_str(&sl0, buf);
    }
  }

  if(config.ifcfg.all) {
    ifcfg_t *ifcfg;
    slist_t *sl_ifcfg;
    strcpy(buf, "ifcfg entries:");
    slist_append_str(&sl0, buf);
    for(ifcfg = config.ifcfg.all; ifcfg; ifcfg = ifcfg->next) {
      sl_ifcfg = slist_split('\n', ifcfg_print(ifcfg));
      for(sl = sl_ifcfg; sl; sl = sl->next) {
        if(*sl->key || ifcfg->next) {	// keep newline between entries
          sprintf(buf, "%s", sl->key);
          slist_append_str(&sl0, buf);
        }
      }
      slist_free(sl_ifcfg);
    }
  }

  if(config.ifcfg.manual) {
    ifcfg_t *ifcfg = config.ifcfg.manual;
    slist_t *sl_ifcfg;
    strcpy(buf, "manual ifcfg entry:");
    slist_append_str(&sl0, buf);
    sl_ifcfg = slist_split('\n', ifcfg_print(ifcfg));
    for(sl = sl_ifcfg; sl; sl = sl->next) {
      if(*sl->key) {
        sprintf(buf, "%s", sl->key);
        slist_append_str(&sl0, buf);
      }
    }
    slist_free(sl_ifcfg);
  }

  if(config.ifcfg.to_global) {
    strcpy(buf, "values to store in global network config file:");
    slist_append_str(&sl0, buf);
    for(sl = config.ifcfg.to_global; sl; sl = sl->next) {
      if(!sl->key) continue;
      sprintf(buf, "  %s", sl->key);
      slist_append_str(&sl0, buf);
    }
  }

  if(config.cdid) {
    sprintf(buf, "cdrom id = %s", config.cdid);
    slist_append_str(&sl0, buf);
  }

  sprintf(buf, "hostname = %s", inet2print(&config.net.hostname));
  slist_append_str(&sl0, buf);

  sprintf(buf, "network = %s", inet2print(&config.net.network));
  slist_append_str(&sl0, buf);

  sprintf(buf, "netmask = %s", inet2print(&config.net.netmask));
  slist_append_str(&sl0, buf);

  sprintf(buf, "gateway = %s", inet2print(&config.net.gateway));
  slist_append_str(&sl0, buf);

  for(i = 0; i < config.net.nameservers; i++) {
    sprintf(buf, "nameserver%d = %s", i + 1, inet2print(&config.net.nameserver[i]));
    slist_append_str(&sl0, buf);
  }

  if(config.net.vncpassword) {
    sprintf(buf, "vncpassword = %s", config.net.vncpassword);
    slist_append_str(&sl0, buf);
  }

  if(config.net.sshpassword) {
    sprintf(buf, "password = %s", config.net.sshpassword);
    slist_append_str(&sl0, buf);
  }

  if(config.net.sshpassword_enc) {
    sprintf(buf, "encrypted password = %s", config.net.sshpassword_enc);
    slist_append_str(&sl0, buf);
  }

  if(config.net.wlan.devices) {
    sprintf(buf, "wlan interfaces%s:", config.net.wlan.devices_fixed ? " (fixed)" : "");
    slist_append_str(&sl0, buf);
    for(sl = config.net.wlan.devices; sl; sl = sl->next) {
      if(!sl->key) continue;
      sprintf(buf, "  %s", sl->key);
      slist_append_str(&sl0, buf);
    }
  }

  if(config.net.wlan.auth) {
    static char *wlan_a[] = { "", "open", "wpa psk", "wpa eap" };
    sprintf(buf, "wlan auth = %d (%s)",
      config.net.wlan.auth,
      wlan_a[config.net.wlan.auth < sizeof wlan_a / sizeof *wlan_a ? config.net.wlan.auth : 0]
    );
    slist_append_str(&sl0, buf);
  }

  if(config.net.wlan.essid) {
    sprintf(buf, "wlan essid = \"%s\"", config.net.wlan.essid);
    slist_append_str(&sl0, buf);
  }

  if(config.net.wlan.wpa_psk) {
    sprintf(buf, "wlan psk = \"%s\"", config.net.wlan.wpa_psk);
    slist_append_str(&sl0, buf);
  }

  if(config.net.wlan.wpa_identity) {
    sprintf(buf, "wlan eap id = \"%s\"", config.net.wlan.wpa_identity);
    slist_append_str(&sl0, buf);
  }

  if(config.net.wlan.wpa_password) {
    sprintf(buf, "wlan eap pass = \"%s\"", config.net.wlan.wpa_password);
    slist_append_str(&sl0, buf);
  }

  sprintf(buf,
    "timeouts: dhcp* = %ds, tftp = %ds",
    config.net.dhcp_timeout, config.net.tftp_timeout
  );
  slist_append_str(&sl0, buf);

  if(config.net.retry) {
    sprintf(buf, "max connection retries: %d", config.net.retry);
    slist_append_str(&sl0, buf);
  }

  if(config.rootpassword) {
    sprintf(buf, "rootpassword = %s", config.rootpassword);
    slist_append_str(&sl0, buf);
  }

  if(config.net.ifup_wait) {
    sprintf(buf, "net config wait = %ds", config.net.ifup_wait);
    slist_append_str(&sl0, buf);
  }

  lang = current_language();

  sprintf(buf, "language = %s, keymap = %s", lang->locale, config.keymap ?: "");
  slist_append_str(&sl0, buf);

  sprintf(buf,
    "dud = %d, updates = %d, dir = \"%s\"",
    config.update.ask, config.update.count, config.update.dir
  );
  slist_append_str(&sl0, buf);

  if(config.term) {
    sprintf(buf, "term = \"%s\"", config.term);
    slist_append_str(&sl0, buf);
  }

  sprintf(buf, "console = \"%s\"", config.console);
  if(config.serial) sprintf(buf + strlen(buf), ", serial line params = \"%s\"", config.serial);
  slist_append_str(&sl0, buf);
  sprintf(buf, "esc delay: %dms", config.escdelay);
  slist_append_str(&sl0, buf);

  sprintf(buf, "debug level = %d", config.debug);
  slist_append_str(&sl0, buf);

  for(i = 0; i < sizeof config.log.dest / sizeof *config.log.dest; i++) {
    sprintf(buf,
      "log[%d]: mask = 0x%x, name = %s, fd = %d",
      i,
      config.log.dest[i].level,
      config.log.dest[i].name ?: "",
      config.log.dest[i].f ? fileno(config.log.dest[i].f) : -1
    );
    slist_append_str(&sl0, buf);
  }

  sprintf(buf, "rootimage = \"%s\"", config.rootimage);
  slist_append_str(&sl0, buf);

  if(config.rootimage2) {
    sprintf(buf, "rootimage2 = \"%s\"", config.rootimage2);
    slist_append_str(&sl0, buf);
  }

  sprintf(buf, "rescueimage = \"%s\"", config.rescueimage);
  slist_append_str(&sl0, buf);

  if(config.extend_option) {
    strcpy(buf, "extend option:");
    slist_append_str(&sl0, buf);
    for(sl = config.extend_option; sl; sl = sl->next) {
      if(!sl->key) continue;
      sprintf(buf, "  %s", sl->key);
      slist_append_str(&sl0, buf);
    }
  }

  sprintf(buf, "setup command = \"%s\"", config.setupcmd);
  slist_append_str(&sl0, buf);

  if(config.defaultrepo) {
    strcpy(buf, "default repo locations:");
    slist_append_str(&sl0, buf);
    for(sl = config.defaultrepo; sl; sl = sl->next) {
      if(!sl->key) continue;
      sprintf(buf, "  %s", sl->key);
      slist_append_str(&sl0, buf);
    }
  }

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

  if(config.ethtool) {
    strcpy(buf, "ethtool options:");
    slist_append_str(&sl0, buf);
    for(sl = config.ethtool; sl; sl = sl->next) {
      sprintf(buf, "  %s: %s", sl->key, sl->value);
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
      i = config.device && !strcmp(sl->key, config.device) ? 1 : 0;
      sprintf(buf, "  %s%s", sl->key, i ? "*" : "");
      if(sl->value) sprintf(buf + strlen(buf), " [%s]", sl->value);
      slist_append_str(&sl0, buf);
    }
  }

  if(config.module.drivers) {
    strcpy(buf, "new driver info (v d sv sd c cm, module, sysfs, usage):");
    slist_append_str(&sl0, buf);
    for(drv = config.module.drivers; drv; drv = drv->next) {
      sprintf(buf, "  %s, %s, %s, %u",   
        print_driverid(drv, 1),
        drv->name ?: "",
        drv->sysfs_name ?: "",
        drv->used
      );
      slist_append_str(&sl0, buf);
    }
  }

  if(config.repomd_data) {
    strcpy(buf, "repomd-data:");
    slist_append_str(&sl0, buf);
    for(sl = config.repomd_data; sl; sl = sl->next) {
      sprintf(buf, "  %s: %s", sl->key, sl->value);
      slist_append_str(&sl0, buf);
    }
  }

  if(config.digests.supported) {
    strcpy(buf, "digest types:");
    slist_append_str(&sl0, buf);
    for(sl = config.digests.supported; sl; sl = sl->next) {
      sprintf(buf, "  %s", sl->key);
      slist_append_str(&sl0, buf);
    }
  }

  if(config.digests.list) {
    strcpy(buf, "digests:");
    slist_append_str(&sl0, buf);
    for(sl = config.digests.list; sl; sl = sl->next) {
      sprintf(buf, "  %s: %s", sl->key, sl->value);
      slist_append_str(&sl0, buf);
    }
  }

  if(config.ptoptions) {
    strcpy(buf, "user defined options:");
    slist_append_str(&sl0, buf);
    for(sl = config.ptoptions; sl; sl = sl->next) {
      sprintf(buf, "  %s: %s", sl->key, sl->value ?: "<unset>");
      slist_append_str(&sl0, buf);
    }
  }

  if(config.module.options) {
    strcpy(buf, "module options:");
    slist_append_str(&sl0, buf);
    for(sl = config.module.options; sl; sl = sl->next) {
      sprintf(buf, "  %s: %s", sl->key, sl->value);
      slist_append_str(&sl0, buf);
    }
  }

  if(log_it) {
    log_debug("------  linuxrc " LXRC_FULL_VERSION " (" __DATE__ ", " __TIME__ ")  ------\n");
    for(sl = sl0; sl; sl = sl->next) {
      log_debug("  %s\n", sl->key);
    }
    log_debug("------  ------\n");
  }
  else {
    dia_show_lines2("linuxrc " LXRC_FULL_VERSION " (" __DATE__ ", " __TIME__ ")", sl0, 76);
  }

  slist_free(sl0);

  hd_free_hd_data(hd_data);
  free(hd_data);
}

/*
 * Set splash progress bar to num percent.
 */
void util_splash_bar(unsigned num)
{
  static unsigned old = 0;
  char buf[256];

  if(!config.plymouth) return;

  if(num > 100) num = 100;

  if(num < old) old = num;

  sprintf(buf, "/usr/bin/plymouth system-update --progress=%u", old);
  lxrc_run_console(buf);

  old = num;
}

/*
 * Set splash message.
 */
void util_splash_msg(char *msg)
{
  char buf[256];

  if(!config.plymouth) return;

  sprintf(buf, "/usr/bin/plymouth display-message --text=\"%s\"", msg);
  lxrc_run_console(buf);
}

/*
 * Set splash message.
 */
void util_splash_mode(char *mode)
{
  char buf[256];

  if(!config.plymouth) return;

  sprintf(buf, "/usr/bin/plymouth change-mode --%s", mode);
  lxrc_run_console(buf);
}


char *read_symlink(char *file)
{
  static char buf[256];
  int i;

  i = readlink(file, buf, sizeof buf);
  buf[sizeof buf - 1] = 0;
  if(i >= 0 && (unsigned) i < sizeof buf) buf[i] = 0;
  if(i < 0) *buf = 0;

  return buf;
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
  char src2[0x200];
  char dst2[0x200];
  char *s;
  int i, j;
  int err = 0;
  unsigned char buf[0x1000];
  int fd1, fd2;
  struct utimbuf ubuf;

  if((dir = opendir(src))) {
    while((de = readdir(dir))) {
      if(!strcmp(de->d_name, ".") || !strcmp(de->d_name, "..")) continue;
      snprintf(src2, sizeof src2, "%s/%s", src, de->d_name);
      snprintf(dst2, sizeof dst2, "%s/%s", dst, de->d_name);
      i = lstat(src2, &sbuf);
      if(i == -1) {
        perror_info(src2);
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
            perror_info(dst2);
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
            perror_info(dst2);
            break;
          }
        }
        else {
          // actually copy it

          fd1 = open(src2, O_RDONLY);
          if(fd1 < 0) {
            err = 5;
            perror_info(src2);
            break;
          }
          fd2 = open(dst2, O_WRONLY | O_CREAT | O_TRUNC, 0644);
          if(fd2 < 0) {
            err = 6;
            perror_info(dst2);
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
            perror_info(src2);
            err = 7;
          }
          if(j < 0) {
            perror_info(dst2);
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
          perror_info(src2);
          break;
        }
        else {
          buf[i] = 0;
        }
        unlink(dst2);
        i = symlink(buf, dst2);
        if(i) {
          err = 10;
          perror_info(dst2);
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
          perror_info(dst2);
          break;
        }
      }

      else {
        log_info("%s: type not supported\n", src2);
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
    perror_info(src);
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

  hlink_list = NULL;
}


/*
 * flags:
 *   bit 0: 0 = keep env, 1 = set new env
 *   bit 1: 1 = login shell ("-l")
 */
void util_start_shell(char *tty, char *shell, int flags)
{
  int fd, i;
  FILE *f;
  char *s, *args[] = { NULL, NULL, NULL };
  char *env[] = {
    NULL,	/* TERM */
    "LANG=en_US.UTF-8",
    "PS1=\\w # ",
    "HOME=/",
    "PATH=/bin:/sbin:/usr/bin:/usr/sbin:/usr/X11R6/bin:/usr/local/bin:/lbin",
    NULL
  };

  *args = (s = strrchr(shell, '/')) ? s + 1 : shell;

  if((flags & 2)) args[1] = "-l";

  strprintf(env + 0, "TERM=%s", getenv("TERM") ?: "linux");

  if(!fork()) {
    for(fd = 0; fd < 20; fd++) close(fd);
    setsid();
    fd = open(tty, O_RDWR);
    ioctl(fd, TIOCSCTTY, (void *) 1);
    dup(fd);
    dup(fd);

    printf("\033c");

    if(config.utf8) {
      printf("\033%%G");
      fflush(stdout);
    }

    if((f = fopen("/etc/motd", "r"))) {
      while((i = fgetc(f)) != EOF) putchar(i);
      fclose(f);
    }

    execve(shell, args, (flags & 1) ? env : environ);
    log_info("Couldn't start shell (errno = %d)\n", errno);
    exit(-1);
  }
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
      perror_info(src);
    }
    else {
      unlink(dst);
      fd2 = open(dst, O_WRONLY | O_CREAT | O_TRUNC, 0644);
      if(fd2 < 0) {
        err = 2;
        perror_info(dst);
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
          perror_info(src);
          err = 3;
        }
        if(j < 0) {
          perror_info(dst);
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


int util_swapon_main(int argc, char **argv)
{
  int i;

  argv++; argc--;

  if(argc != 1) return -1;

  i = swapon(*argv, 0);

  if(i) {
    i = errno;
    log_info("swapon: ");
    perror_info(*argv);
  }

  return i;
}


void util_free_mem()
{
  file_t *f0, *f;
  int64_t i, mem_total = 0, mem_free = 0, mem_free_swap = 0;
  char *s;

  f0 = file_read_file("/proc/meminfo", kf_mem);

  for(f = f0; f; f = f->next) {
    switch(f->key) {
      case key_memtotal:
      case key_swaptotal:
        i = strtoull(f->value, &s, 10);
        if(!*s || *s == ' ') mem_total += i;
        break;

      case key_memfree:
      case key_buffers:
      case key_cached:
      case key_swapfree:
        i = strtoll(f->value, &s, 10);
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

  config.memoryXXX.total = mem_total << 10;
  config.memoryXXX.free = mem_free << 10;
  config.memoryXXX.free_swap = mem_free_swap << 10;

  util_update_meminfo();
}


void util_update_meminfo()
{
  config.memoryXXX.current = config.memoryXXX.free;
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


slist_t *slist_free_entry(slist_t **sl0, char *str)
{
  slist_t *sl, *sl_prev, sl_tmp = { };

  if(!str) return *sl0;

  sl_tmp.next = *sl0;

  for(sl_prev = &sl_tmp, sl = sl_prev->next; sl; sl = sl->next) {
    if(sl->key && !strcmp(sl->key, str)) {
      free(sl->key);
      if(sl->value) free(sl->value);
      sl_prev->next = sl->next;
      free(sl);
      sl = sl_prev;
    }
    else {
      sl_prev = sl;
    }
  }

  return *sl0 = sl_tmp.next;
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


/*
 * Append key - value pair to list.
 *
 * If replace is 0, it will not update an existing entry (like setenv()).
 */
slist_t *slist_setentry(slist_t **sl0, char *key, char *value, int replace)
{
  slist_t *sl;

  sl = slist_getentry(*sl0, key);

  if(sl) {
    if(!replace) return sl;
  }
  else {
    sl = slist_append(sl0, slist_new());
    str_copy(&sl->key, key);
  }

  str_copy(&sl->value, value);

  return sl;
}


slist_t *slist_assign_values(slist_t **sl0, char *str)
{
  int todo = 0;
  slist_t *sl, *sl1;

  if(!sl0) return NULL;

  if(!str) str = "";

  switch(*str) {
    case '+':
      todo = 1;
      str++;
      break;

    case '-':
      todo = -1;
      str++;
      break;
  }

  sl1 = slist_split(',', str);

  if(todo) {
    for(sl = sl1; sl; sl = sl->next) {
      if(todo > 0) {
        if(!slist_getentry(*sl0, sl->key)) slist_append_str(sl0, sl->key);
      }
      else {
        slist_free_entry(sl0, sl->key);
      }
    }
  }
  else {
    slist_free(*sl0);
    *sl0 = sl1;
    sl1 = NULL;
  }

  slist_free(sl1);

  return *sl0;
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


slist_t *slist_sort(slist_t *sl0, int (*cmp_func)(const void *, const void *))
{
  int i, list_len;
  slist_t *sl1 = NULL, *sl;
  slist_t **slist_array;

  for(list_len = 0, sl = sl0; sl; sl = sl->next) list_len++;
  if(list_len < 2) return sl0;

  slist_array = malloc(list_len * sizeof *slist_array);

  for(i = 0, sl = sl0; sl; sl = sl->next) slist_array[i++] = sl;

  qsort(slist_array, list_len, sizeof *slist_array, cmp_func);

  for(i = 0; i < list_len; i++) {
    slist_append(&sl1, slist_array[i])->next = NULL;
  }

  free(slist_array);

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

  if(isblank(del)) {
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


char *slist_join(char *del, slist_t *str)
{
  char *s;
  slist_t *str0;
  int len = 0, del_len = 0;

  if(del) del_len = strlen(del);

  for(str0 = str; str0; str0 = str0->next) {
    if(str0->key) len += strlen(str0->key);
    if(str0->next) len += del_len;
  }

  if(!len) return NULL;

  len++;

  s = calloc(len, 1);

  for(; str; str = str->next) {
    if(str->key) strcat(s, str->key);
    if(str->next && del) strcat(s, del);
  }

  return s;
}


/*
 * return index-th key
 */
char *slist_key(slist_t *sl, int index)
{
  while(sl && index-- > 0) {
    sl = sl->next;
  }

  return sl ? sl->key : NULL;
}


/**
 * Clear 'inet' and add 'name' to it.
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

  if(inet_new.name) {
    inet_new.ok = 1;
    inet_new.ipv4 = 1;
  }

  *inet = inet_new;
}


char *inet2print(inet_t *inet)
{
  static char buf[256];
  const char *ip = NULL, *ip6 = NULL;
  char ip_buf[INET_ADDRSTRLEN], ip6_buf[INET6_ADDRSTRLEN];
  char prefix4[64], prefix6[64];

  if(!inet || (!inet->name && !inet->ok)) return "(no ip)";

  sprintf(buf, "%s", inet->name ?: "[no name]");

  *prefix4 = *prefix6 = 0;
  if(inet->prefix4) sprintf(prefix4, "/%u", inet->prefix4);
  if(inet->prefix6) sprintf(prefix6, "/%u", inet->prefix6);

  if(!inet->ipv4 && !inet->ipv6) {
    sprintf(buf + strlen(buf), " (no ip)");
    return buf;
  }

  if(inet->ipv4) ip = inet_ntop(AF_INET, &inet->ip, ip_buf, sizeof ip_buf);
  if(inet->ipv6) ip6 = inet_ntop(AF_INET6, &inet->ip6, ip6_buf, sizeof ip6_buf);

  if(ip && ip6) {
    sprintf(buf + strlen(buf), " (ip: %s%s, ip6: %s%s)", ip, prefix4, ip6, prefix6);
  }
  else if(ip) {
    if(!inet->name || strcmp(inet->name, ip) || *prefix4) {
      sprintf(buf + strlen(buf), " (ip: %s%s)", ip, prefix4);
    }
  }
  else if(ip6) {
    if(!inet->name || strcmp(inet->name, ip6) || *prefix6) {
      sprintf(buf + strlen(buf), " (ip6: %s%s)", ip6, prefix6);
    }
  }

  return buf;
}


/**
 * strdup src to *dst.
 * The previous contents is free'd (that is, iff *dst was non-NULL)
 * If src is NULL, *dst will be NULL.
 * (Does nothing if dst is NULL).
 */
void str_copy(char **dst, const char *src)
{
  char *s;

  if(!dst) return;

  s = src ? strdup(src) : NULL;
  if(*dst) free(*dst);
  *dst = s;
}


/*
 * Similar to asprintf() but frees old buffer.
 */
void strprintf(char **buf, char *format, ...)
{
  char *new_buf;
  va_list args;

  va_start(args, format);
  if(vasprintf(&new_buf, format, args) == -1) new_buf = NULL;
  va_end(args);

  if(*buf) free(*buf);

  *buf = new_buf;
}


int util_fstype_main(int argc, char **argv)
{
  char *s, buf[64], *compr, *archive;

  argv++; argc--;

  if(!argc) return log_info("usage: fstype blockdevice\n"), 1;

  while(argc--) {
    s = fstype(*argv);
    if(
      !s &&
      (compr = compressed_archive(*argv, &archive))
    ) {
      if(archive) {
        snprintf(buf, sizeof buf, "%s.%s", archive, compr);
        s = buf;
      }
      else {
        s = compr;
      }
    }
    printf("%s: %s\n", *argv, s ?: "unknown fs");
    argv++;
  }

  return 0;
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
    /* iso9660 is special */
    if(*module && !strcmp(*module, "iso9660")) *module = "isofs";

    if(
      !strcmp(type, "cpio") ||
      !strcmp(type, "tar") ||
      !strcmp(type, "rpm") ||
      (config.ntfs_3g && !strcmp(type, "ntfs"))
    ) {
      *module = NULL;
    }
    else {
      f0 = file_read_file("/proc/filesystems", kf_none);
      for(f = f0; f; f = f->next) {
        s = strcmp(f->key_str, "nodev") ? f->key_str : f->value;
        if(!strcmp(s, type)) {
          *module = NULL;
          break;
        }
      }
      file_free_file(f0);
    }
  }

  return type;
}


void util_extend_usr1(int signum)
{
  extend_ready = 1;
}


int util_extend(char *extension, char task, int verbose)
{
  FILE *f, *w;
  int err = 0;
  char buf[1024];

  extend_ready = 0;
  signal(SIGUSR1, util_extend_usr1);

  unlink("/tmp/extend.result");
  f = fopen("/tmp/extend.job", "w");
  if(f) {
    fprintf(f, "%d %c %s\n", (int) getpid(), task, extension);
    fclose(f);

    if(util_check_exist("/usr/src/packages") || getuid()) config.test = 1;

    if(config.test) {
      util_killall("linuxrc", SIGUSR1);
    }
    else {
      if(kill(1, SIGUSR1)) err = 2;
    }

    if(!err) {
      while(!extend_ready) { sleep(1); }

      if((f = fopen("/tmp/extend.result", "r"))) {
        fscanf(f, "%d", &err);
        fclose(f);
      }
      else {
        err = 1;
      }
    }
  }
  else {
    err = 1;
  }

  f = fopen("/tmp/extend.log", "r");
  if(f) {
    w = fopen("/var/log/extend", "a");
    while(fgets(buf, sizeof buf, f)) {
      if(verbose > 0) printf("%s", buf);
      if(w) fprintf(w, "%s", buf);
    }
    if(w) fclose(w);
    fclose(f);
  }

  if(verbose >= 0) log_show("%s: extend %s\n", extension, err ? "failed" : "ok");

  return err;
}


int util_extend_main(int argc, char **argv)
{
  int err = 0;
  char task = 'a';
  struct { unsigned verbose:1; unsigned help:1; } opt = {};

  argv++; argc--;

  while(argc) {
    if(!strcmp(*argv, "-r")) {
      task = 'r';
    }
    else if(!strcmp(*argv, "-h") || !strcmp(*argv, "--help")) {
      opt.help = 1;
    }
    else if(!strcmp(*argv, "-v")) {
      opt.verbose = 1;
    }
    else {
      break;
    }
    argv++; argc--;
  }

  if(!argc || opt.help) {
    return log_info("Usage: extend [-v] [-r] extension\nAdd or remove inst-sys extension.\n"), 1;
  }

  err = util_extend(*argv, task, opt.verbose);

  // remove it to keep internal list correct
  if(err && task == 'a') util_extend(*argv, 'r', -1);

  return err;
}


/*
 * returns loop device used
 */
char *util_attach_loop(char *file, int ro)
{
  struct loop_info loopinfo;
  int fd, rc, i, device, ok = 0;
  static char buf[32];

  if((fd = open(file, (ro ? O_RDONLY : O_RDWR) | O_LARGEFILE)) < 0) {
    perror_info(file);
    return NULL;
  }

  for(i = 0; i < 64; i++) {
    sprintf(buf, "/dev/loop%d", i);
    if((device = open(buf, (ro ? O_RDONLY : O_RDWR) | O_LARGEFILE)) >= 0) {
      memset(&loopinfo, 0, sizeof loopinfo);
      strncpy(loopinfo.lo_name, file, sizeof loopinfo.lo_name - 1);
      rc = ioctl(device, LOOP_SET_FD, fd);
      if(rc != -1) rc = ioctl(device, LOOP_SET_STATUS, &loopinfo);
      close(device);
      if(rc != -1) {
        ok = 1;
        break;
      }
    }
  }

  close(fd);

  return ok ? buf : NULL;
}


int util_detach_loop(char *dev)
{
  int i, fd;

  if((fd = open(dev, O_RDONLY | O_LARGEFILE)) < 0) {
    perror_debug(dev);
    return -1;
  }

  if((i = ioctl(fd, LOOP_CLR_FD, 0)) == -1) {
    perror_debug(dev);
  }

  close(fd);

  return i;
}


int util_mount(char *dev, char *dir, unsigned long flags, slist_t *file_list)
{
  char *type, *loop_dev, *cmd = NULL, *module, *tmp_dev, *cpio_opts = NULL, *s, *buf = NULL;
  char *compr = NULL;
  int err = -1;
  struct stat64 sbuf;

  log_info("mount: dev = %s, dir = %s, flags = 0x%lx\n", dev, dir, flags & 0xffff);

  if(!dev || !dir) return -1;

  if(
    config.mountpoint.base &&
    strstr(dir, config.mountpoint.base) == dir &&
    stat64(dir, &sbuf)
  ) {
    mkdir(dir, 0755);
  }

  if(stat64(dev, &sbuf)) {
    log_info("mount: %s: %s\n", dev, strerror(errno));
    return -1;
  }

  if(S_ISDIR(sbuf.st_mode)) {
    err = mount(dev, dir, "none", MS_BIND, 0);
    if(err && config.run_as_linuxrc) {
      log_info("mount: %s: %s\n", dev, strerror(errno));
    }
    return err;
  }

  if(!S_ISREG(sbuf.st_mode) && !S_ISBLK(sbuf.st_mode)) {
    log_info("mount: %s: not a block device\n", dev);
    return -1;
  }

  type = util_fstype(dev, &module);
  if(module) mod_modprobe(module, NULL);

  if(!type) compr = compressed_archive(dev, &type);

  log_info("%s: type = %s.%s\n", dev, type ?: "", compr ?: "");

  LXRC_WAIT

  if(
    type &&
    (!strcmp(type, "cpio") || !strcmp(type, "tar") || !strcmp(type, "rpm"))
  ) {
    char *buf = NULL;
    char *msg;

    err = mount("tmpfs", dir, "tmpfs", 0, "size=0,nr_inodes=0");
    if(err) {
      if(config.run_as_linuxrc) log_info("mount: tmpfs: %s\n", strerror(errno));
      return err;
    }

    chmod(dir, 0755);

    str_copy(&cpio_opts, "--quiet --sparse -dimu --no-absolute-filenames");

    if(file_list) {
      s = slist_join("' '", file_list);
      strprintf(&cpio_opts, "%s '%s'", cpio_opts, s);
      free(s);
    }

    if(!strcmp(type, "cpio")) {
      if(compr) {
        strprintf(&buf, "cd %s ; %s -dc %s | cpio %s", dir, compr, dev, cpio_opts);
      }
      else {
        strprintf(&buf, "cd %s ; cpio %s < %s", dir, cpio_opts, dev);
      }
      msg = "cpio";
    }
    else if(!strcmp(type, "tar")) {
      strprintf(&buf, "cd %s ; tar -xpf %s", dir, dev);
      msg = "tar";
    }
    else {
      strprintf(&buf, "cd %s ; rpm2cpio %s | cpio %s", dir, dev, cpio_opts);
      msg = "rpm unpacking";
    }

    str_copy(&cpio_opts, NULL);

    err = lxrc_run(buf);
    str_copy(&buf, NULL);

    if(err) {
      if(config.run_as_linuxrc) log_info("mount: %s failed\n", msg);
      umount(dir);
      return err;
    }
    else if(config.squash) {
      tmp_dev = new_download();
      // if we downloaded the file, overwrite it; else make a new copy
      if(strncmp(dev, config.download.base, strlen(config.download.base))) {
        tmp_dev = new_download();
      }
      else {
        tmp_dev = dev;
      }
      log_info("%s -> %s: converting to squashfs\n", dev, tmp_dev);
      strprintf(&buf, "mksquashfs %s %s -noappend -no-progress", dir, tmp_dev);
      err = lxrc_run(buf);
      if(err && config.run_as_linuxrc) log_info("mount: mksquashfs failed\n");
      if(!err) {
        umount(dir);
        err = util_mount(tmp_dev, dir, flags, NULL);
      }
    }

    return err;
  }

  if(type &&
    (
      S_ISREG(sbuf.st_mode) ||
      (!strcmp(type, "cramfs") && strstr(dev, "/dev/ram") == dev)
    )
  ) {
    if(config.run_as_linuxrc) log_info("mount: %s: we need a loop device\n", dev);
    loop_dev = util_attach_loop(dev, (flags & MS_RDONLY) ? 1 : 0);
    if(!loop_dev) {
      log_info("mount: no usable loop device found\n");
      return -1;
    }
    if(config.run_as_linuxrc) log_info("mount: using %s\n", loop_dev);

    strprintf(&buf, "%s/file_", config.download.base);

    // remove downloaded files immediately (so we don't have to cleanup after umount)
    if(!strncmp(dev, buf, strlen(buf))) unlink(dev);

    str_copy(&buf, NULL);

    dev = loop_dev;
  }

  if(config.ntfs_3g && type && !strcmp(type, "ntfs")) {
    asprintf(&cmd, "mount -t ntfs-3g%s %s %s", (flags & MS_RDONLY) ? " -oro" : "", dev, dir);
    err = lxrc_run(cmd);
    free(cmd);
  }
  else if(type) {
    err = mount(dev, dir, type, flags, 0);
    if(err && config.run_as_linuxrc) {
      log_info("mount: %s: %s\n", dev, strerror(errno));
    }
  }
  else {
    log_info("%s: unknown fs type\n", dev);
    err = -1;
  }

  return err;
}


int util_mount_ro(char *dev, char *dir, slist_t *file_list)
{
  return util_mount(dev, dir, MS_MGC_VAL | MS_RDONLY, file_list);
}


int util_mount_rw(char *dev, char *dir, slist_t *file_list)
{
  return util_mount(dev, dir, 0, file_list);
}


void util_update_netdevice_list(char *module, int add)
{
  file_t *f0, *f1, *f;
  slist_t *sl;

  f0 = file_read_file("/proc/net/dev", kf_none);
  if(!f0) return;

  /* skip 2 lines */
  if((f1 = f0->next)) f1 = f1->next;

  if(add) {
    for(f = f1; f; f = f->next) {
      if(!strcmp(f->key_str, "lo")) continue;
      if(strstr(f->key_str, "sit") == f->key_str) continue;
      if(!slist_getentry(config.net.devices, f->key_str)) {
        sl = slist_append_str(&config.net.devices, f->key_str);
        str_copy(&sl->value, module);
      }
    }
  }
  else {
    for(sl = config.net.devices; sl; sl = sl->next) {
      if(!file_getentry(f1, sl->key)) {
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

  hd_data->flags.list_md = 1;
  fix_device_names(hd_list(hd_data, hw_disk, 1, NULL));

  if(add) {
    for(hsl = hd_data->disks; hsl; hsl = hsl->next) {
      if(!slist_getentry(config.disks, hsl->str)) {
        sl = slist_append_str(&config.disks, hsl->str);
        str_copy(&sl->value, module);
        added++;
      }
    }
    for(hsl = hd_data->partitions; hsl; hsl = hsl->next) {
      if(!slist_getentry(config.partitions, hsl->str)) {
        sl = slist_append_str(&config.partitions, hsl->str);
        str_copy(&sl->value, module);
        added++;
      }
    }
  }
  else {
    for(sl = config.disks; sl; sl = sl->next) {
      if(!search_str_list(hd_data->disks, sl->key)) {
        str_copy(&sl->key, NULL);
        str_copy(&sl->value, NULL);
      }
    }
    for(sl = config.partitions; sl; sl = sl->next) {
      if(!search_str_list(hd_data->partitions, sl->key)) {
        str_copy(&sl->key, NULL);
        str_copy(&sl->value, NULL);
      }
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

  f0 = file_read_file("/proc/sys/dev/cdrom/info", kf_none);

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

  f0 = file_read_file("/proc/swaps", kf_none);

  for(f = f0; f; f = f->next) {
    if(f->key == key_none && strstr(f->key_str, "/dev/") == f->key_str) {
      slist_append_str(&config.swaps, f->key_str + sizeof "/dev/" - 1);
    }
  }

  file_free_file(f0);
}


int util_is_mountable(char *file)
{
  int i, compressed = 0;

  if(util_check_exist(file) == 'd') return 1;

  i = util_fileinfo(file, NULL, &compressed);

  return i || compressed ? 0 : 1;
}


void util_set_serial_console(char *str)
{
  slist_t *sl;

  if(!str || !*str) return;

  /* not a serial console */
  if(
    !strncmp(str, "tty", 3) &&
    (str[3] == 0 || (str[3] >= '0' && str[3] <= '9'))
  ) return;

  str_copy(&config.serial, str);

  sl = slist_split(',', config.serial);

  if(sl) {
    /* if it's uart, don't bother to find the matching ttySx */
    str_copy(&config.console, long_dev(strcmp(sl->key, "uart") ? sl->key : "console"));
    config.textmode = 1;
  }
     
  slist_free(sl);
}


void util_set_product_dir(char *prod)
{
  struct utsname ubuf;
  char *arch = "";

  if(prod && *prod) str_copy(&config.product_dir, prod);

  if(!uname(&ubuf)) {
    arch = ubuf.machine;
    if(arch[0] == 'i' && arch[2] == '8' && arch[3] == '6' && !arch[4]) arch = "i386";
  }

#if defined(__powerpc__) && !defined(__powerpc64__)
  if(!strcmp(arch, "ppc64")) arch = "ppc";
#endif

  strprintf(&config.rootimage, "boot/%s/root", arch);
  strprintf(&config.rescueimage, "boot/%s/rescue", arch);

  if(!strcmp(arch, "i386") || !strcmp(arch, "x86_64")) {
    strprintf(&config.kexec_kernel, "boot/%s/loader/linux", arch);
    strprintf(&config.kexec_initrd, "boot/%s/loader/initrd", arch);
  }
  else {
    strprintf(&config.kexec_kernel, "boot/%s/linux", arch);
    strprintf(&config.kexec_initrd, "boot/%s/initrd", arch);
  }
}


/*
 * Read product version from /usr/lib/os-release.
 *
 * The product version is used for replacing $releasever in URLs.
 */
void util_get_releasever()
{
  file_t *f0, *f;

  f0 = file_read_file("/usr/lib/os-release", kf_none);

  for(f = f0; f; f = f->next) {
    if(!strcmp(f->key_str, "VERSION_ID")) {
      str_copy(&config.releasever, f->value);
      break;
    }
  }

  file_free_file(f0);
}


/*
 * Rename SCSI devices so that usb and firewire devices are last.
 */
void scsi_rename()
{
  if(!config.scsi_rename) return;

  get_scsi_list();
  rename_scsi_devs();

  scsi_rename_devices();

  free_scsi_list();
}


void scsi_rename_devices()
{
  size_t i;
  slist_t *sl;
  char **rs[] = { &config.update.dev, &config.cdrom };

  for(i = 0; i < sizeof rs / sizeof *rs; i++) {
    scsi_rename_onedevice(rs[i]);
  }

  for(sl = config.disks; sl; sl = sl->next) {
    scsi_rename_onedevice(&sl->key);
  }
}


void scsi_rename_onedevice(char **dev)
{
  unsigned u, is_long = 0;
  char *s_dev;

  if(!dev || !*dev) return;

  s_dev = *dev;

  if(!strncmp(s_dev, "/dev/", sizeof "/dev/" - 1)) {
    s_dev += sizeof "/dev/" - 1;
    is_long = 1;
  }

  for(u = 0; u < scsi_list_len; u++) {
    if(scsi_list[u]->new_name && !strcmp(scsi_list[u]->name, s_dev)) {
      str_copy(dev, is_long ? long_dev(scsi_list[u]->new_name) : scsi_list[u]->new_name);
    }
  }
}


char *short_dev(char *dev)
{
  if(dev && !strncmp(dev, "/dev/", sizeof "/dev/" - 1)) {
    dev += sizeof "/dev/" - 1;
  }
  return dev;
}

char *long_dev(char *dev)
{
  static char *buf = NULL;

  if(dev && *dev != '/') {
    strprintf(&buf, "/dev/%s", dev);
    dev = buf;
  }

  return dev;
}


static int is_there(char *name);
static int is_dir(char *name);
static int is_link(char *name);
static char *read_symlink(char *name);
static int make_links(char *src, char *dst);


int util_lndir_main(int argc, char **argv)
{
  argv++; argc--;

  if(argc != 2) return 1;

  return make_links(argv[0], argv[1]);
}


int is_there(char *name)
{
  struct stat sbuf;

  if(stat(name, &sbuf) == -1) return 0;

  return 1;
}


int is_dir(char *name)
{
  struct stat sbuf;

  if(stat(name, &sbuf) == -1) return 0;

  if(S_ISDIR(sbuf.st_mode)) return 1;

  return 0;
}


int is_link(char *name)
{
  struct stat sbuf;

  if(lstat(name, &sbuf) == -1) return 0;

  if(S_ISLNK(sbuf.st_mode)) return 1;

  return 0;
}


/*
 * Link directory tree src to dst. Keep existing files in dst.
 */
int make_links(char *src, char *dst)
{
  DIR *dir;
  struct dirent *de;
  struct stat sbuf;
  struct utimbuf ubuf;
  char src2[0x400], dst2[0x400], tmp_dir[0x400], tmp_link[0x400], *tmp_s = NULL;
  char *s, *t;
  int err = 0;

  // log_info("make_links: %s -> %s\n", src, dst);

  if((dir = opendir(src))) {
    while((de = readdir(dir))) {
      if(!strcmp(de->d_name, ".") || !strcmp(de->d_name, "..")) continue;
      sprintf(src2, "%s/%s", src, de->d_name);
      sprintf(dst2, "%s/%s", dst, de->d_name);

      // log_info("?: %s -> %s\n", src2, dst2);

#if 0

> lndir /tmp/test/f2/{a,c}	// a + b -> c

  drwxr-xr-x   f2
  drwxr-xr-x   f2/a
  drwxr-xr-x   f2/a/dir1
  drwxr-xr-x   f2/a/dir1/dir2
  -rw-r--r--   f2/a/dir1/dir2/file1
  drwxr-xr-x   f2/b
  drwxr-xr-x   f2/b/dir1
  lrwxrwxrwx   f2/b/dir1/dir2 -> /tmp/test/f2/a/dir1/dir2
  drwxr-xr-x   f2/c
  drwxr-xr-x   f2/c/dir1
  drwxr-xr-x   f2/c/dir1/dir2
  lrwxrwxrwx   f2/c/dir1/dir2/file1 -> /tmp/test/f2/a/dir1/dir2/file1


> lndir /tmp/test/f1/{a,b}	// a + b -> b

  drwxr-xr-x   f1
  drwxr-xr-x   f1/a
  drwxr-xr-x   f1/a/dir1
  lrwxrwxrwx   f1/a/link1 -> dir1
  drwxr-xr-x   f1/b
  drwxr-xr-x   f1/b/dir1
  lrwxrwxrwx   f1/b/link1 -> dir1

#endif

      /* Why on earth '&& !is_link(src2)'??? */
      /* new try: && !relative_link() */
      if(is_dir(src2) /* && !is_link(src2) */) {
        /* add directory */

        // log_info("dir: %s -> %s\n", src2, dst2);

        if(is_dir(dst2)) {
          if(is_link(dst2)) {
            /* remove link and make directory */
            sprintf(tmp_dir, "%s/mklXXXXXX", dst);
            s = mkdtemp(tmp_dir);
            if(!s) {
              perror_info(tmp_dir);
              err = 2;
              continue;
            }
            str_copy(&tmp_s, read_symlink(dst2));
            s = tmp_s;
            if(!*s) {
              err = 3;
              continue;
            }
            if(*s != '/') {
              strcpy(tmp_link, dst2);
              t = strrchr(tmp_link, '/');
              strcpy(t ? t + 1 : tmp_link, s);
              // sprintf(tmp_link, "%s/%s", dst2, s);
              s = tmp_link;
            }
            if((err = make_links(s, tmp_dir))) continue;
            if(unlink(dst2)) {
              perror_info(dst2);
              err = 4;
              continue;
            }
            if(rename(tmp_dir, dst2)) {
              perror_info(tmp_dir);
              err = 5;
              continue;
            }
            if(stat(src2, &sbuf) != -1) {
              chmod(dst2, sbuf.st_mode);
              ubuf.actime = sbuf.st_atime;
              ubuf.modtime = sbuf.st_mtime;
              utime(dst2, &ubuf);
              lchown(dst2, sbuf.st_uid, sbuf.st_gid);
            }
          }
          if((err = make_links(src2, dst2))) continue;
        }
        else if(!is_there(dst2)) {
          unlink(dst2);
          s = src2;
          if(is_link(src2)) s = read_symlink(src2);
          if(symlink(s, dst2)) {
            perror_info(s);
            err = 7;
            continue;
          }
        }
      }
      else if(!is_there(dst2) || is_link(dst2)) {
        unlink(dst2);
        s = src2;
        if(is_link(src2)) s = read_symlink(src2);
        if(symlink(s, dst2)) {
          perror_info(s);
          err = 6;
          continue;
        }
      }
    }

    closedir(dir);

    free(tmp_s);
  }
  else {
    perror_info(src);
    return 1;
  }

  return err;
}


void util_notty()
{
  int fd;

  fd = open("/dev/tty", O_RDWR);

  if(fd != -1) {
    ioctl(fd, TIOCNOTTY);
    close(fd);
  }
}


void util_killall(char *name, int sig)
{
  pid_t mypid, pid;
  struct dirent *de;
  DIR *d;
  char *s;
  slist_t *sl0 = NULL, *sl;

  if(!name) return;

  mypid = getpid();

  if(!(d = opendir("/proc"))) return;

  /* make a (reversed) list of all process ids */
  while((de = readdir(d))) {
    pid = strtoul(de->d_name, &s, 10);
    if(!*s && *util_process_cmdline(pid)) {
      sl = slist_add(&sl0, slist_new());
      sl->key = strdup(de->d_name);
      sl->value = strdup(util_process_name(pid));
    }
  }

  closedir(d);

  for(sl = sl0; sl; sl = sl->next) {
    pid = strtoul(sl->key, NULL, 10);
    if(pid == mypid) continue;
    if(!strcmp(sl->value, name)) {
      log_debug("kill -%d %d\n", sig, pid);
      kill(pid, sig);
      usleep(20000);
    }
  }

  slist_free(sl0);

  while(waitpid(-1, NULL, WNOHANG) > 0);
}


void util_get_ram_size()
{
  hd_data_t *hd_data;
  hd_t *hd;
  hd_res_t *res;

  if(config.memoryXXX.ram) return;

  hd_data = calloc(1, sizeof *hd_data);

  hd = hd_list(hd_data, hw_memory, 1, NULL);

  if(hd && hd->base_class.id == bc_internal && hd->sub_class.id == sc_int_main_mem) {
    for(res = hd->res; res; res = res->next) {
      if(res->any.type == res_phys_mem) {
        config.memoryXXX.ram = res->phys_mem.range;
        log_info("RAM size: %llu MB\n", (unsigned long long) (config.memoryXXX.ram >> 20));
        break;
      }
    }
  }

  hd_free_hd_data(hd_data);
}


/*
 * Load basic usb support.
 */
void util_load_usb()
{
  hd_data_t *hd_data;
  hd_t *hd, *hd_usb;
  static int loaded = 0;

  if(loaded) return;

  loaded = 1;

  hd_data = calloc(1, sizeof *hd_data);

  hd_usb = hd_list(hd_data, hw_usb_ctrl, 1, NULL);

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

  for(hd = hd_usb; hd; hd = hd->next) activate_driver(hd_data, hd, NULL, 0);

  hd_free_hd_data(hd_data);
}


int util_set_attr(char* attr, char* value)
{
  int i, fd;

  if((fd = open(attr, O_WRONLY)) < 0) return -1;

  i = write(fd, value, strlen(value));
  
  close(fd);

  return i < 0 ? i : 0;
}


/**
 * Read contents of a file, trimmed of trailing whitespace.
 * Useful for sysfs attributes.
 * @return Trimmed contents, or ""; the result is in a static buffer.
 */
char *util_get_attr(char* attr)
{
  int i, fd;
  static char buf[1024];

  *buf = 0;

  if((fd = open(attr, O_RDONLY)) < 0) return buf;

  i = read(fd, buf, sizeof buf - 1);

  close(fd);

  if(i >= 0) {
    buf[i] = 0;

    while(i > 0 && (!buf[i - 1] || isspace(buf[i - 1]))) {
      buf[--i] = 0;
    }
  }

  return buf;
}


int util_get_int_attr(char* attr)
{
  return strtol(util_get_attr(attr), NULL, 0);
}


char *print_driverid(driver_t *drv, int with_0x)
{
  static char buf[256], *s;
  int items;

  *buf = 0;

  if(!drv) return buf;

  items = 7;
  if(!drv->driver_data) {
    items = 6;
    if(!drv->class_mask) {
      items = 4;
      if(drv->subdevice == ~0) {
        items--;
        if(drv->subvendor == ~0) {
          items--;
          if(drv->device == ~0) {
            items--;
            if(drv->vendor == ~0) {
              items--;
            }
          }
        }
      }
    }
  }

  if(!items) return buf;

  s = with_0x ? "0x" : "";

  sprintf(buf, "%s%x", s, drv->vendor);
  if(items >= 2) sprintf(buf + strlen(buf), " %s%x", s, drv->device);
  if(items >= 3) sprintf(buf + strlen(buf), " %s%x", s, drv->subvendor);
  if(items >= 4) sprintf(buf + strlen(buf), " %s%x", s, drv->subdevice);
  if(items >= 5) sprintf(buf + strlen(buf), " %s%x %s%x", s, drv->class, s, drv->class_mask);
  if(items >= 7) sprintf(buf + strlen(buf), " %s%lx", s, drv->driver_data);

  return buf;
}


int apply_driverid(driver_t *drv)
{
  char buf[256], *name;
  FILE *f;

  if(!drv) return 0;

  if(!(name = drv->sysfs_name ?: drv->name)) return 0;

  sprintf(buf, "/sys/bus/pci/drivers/%s/new_id", name);

  if(util_check_exist(buf) != 'r') return 0;

  if(!(f = fopen(buf, "w"))) return 0;
  fprintf(f, "%s\n", print_driverid(drv, 0));
  fclose(f);

  log_info("new id [%s]: %s\n", name, print_driverid(drv, 0));

  return 1;
}


void store_driverid(driver_t *drv)
{
  char first = ' ';
  FILE *f;

  if(!drv) return;

  if(drv->name && (f = fopen("/var/lib/hardware/hd.ids", "a"))) {
    fprintf(f, "\n# %s\n", print_driverid(drv, 1));

    if(((drv->class_mask >> 16) & 0xff) == 0xff) {
      fprintf(f, "%cbaseclass.id\t\t0x%03x\n", first, (drv->class >> 16) & 0xff);
      first = '&';
    }

    if(((drv->class_mask >> 8) & 0xff) == 0xff) {
      fprintf(f, "%csubclass.id\t\t0x%02x\n", first, (drv->class >> 8) & 0xff);
      first = '&';
    }

    if((drv->class_mask & 0xff) == 0xff) {
      fprintf(f, "%cprogif.id\t\t0x%02x\n", first, drv->class & 0xff);
      first = '&';
    }

    if(drv->vendor != ~0) {
      fprintf(f, "%cvendor.id\t\tpci 0x%04x\n", first, drv->vendor & 0xffff);
      first = '&';
    }

    if(drv->device != ~0) {
      fprintf(f, "%cdevice.id\t\tpci 0x%04x\n", first, drv->device & 0xffff);
      first = '&';
    }

    if(drv->subvendor != ~0) {
      fprintf(f, "%csubvendor.id\t\tpci 0x%04x\n", first, drv->subvendor & 0xffff);
      first = '&';
    }

    if(drv->subdevice != ~0) {
      fprintf(f, "%csubdevice.id\t\tpci 0x%04x\n", first, drv->subdevice & 0xffff);
      first = '&';
    }

    fprintf(f, "+driver.module.modprobe\t%s\n", drv->name);

    fclose(f);
  }

  if((f = fopen("/etc/newids", "a"))) {
    fprintf(f, "%s,%s", print_driverid(drv, 1), drv->name ?: "");
    if(drv->sysfs_name) fprintf(f, ",%s", drv->sysfs_name);
    fprintf(f, "\n");

    fclose(f);
  }
}


/*
 * Check if network device matches.
 *
 * Return:
 *   0: no match
 *   1: ok
 */
int match_netdevice(char *device, char *hwaddr, char *key)
{
  if(!key) return 0;

  if(
    (device && !fnmatch(key, device, 0)) ||
    (hwaddr && !fnmatch(key, hwaddr, FNM_CASEFOLD))
  ) return 1;

  return 0;
}

char* util_chop_lf(char* str)
{
  int lfp = strlen(str)-1;
  if(str[lfp] == '\n') str[lfp]=0;
  return str;
}

int util_read_and_chop(char* path, char* dst, int dst_size)
{
  FILE* fp;
  fp=fopen(path,"r");
  if(!fp) return 0;
  if(!fgets(dst,dst_size,fp)) { fclose(fp); return 0; }
  util_chop_lf(dst);
  fclose(fp);
  return 1;
}


/*
 * Find text for locale, or 'en' if no locale matches.
 */
char *get_translation(slist_t *trans, char *locale)
{
  slist_t *sl;
  char *search_loc = NULL, *s;

  if(!trans || !locale) return NULL;

  locale = strdup(locale);

  strprintf(&search_loc, "lang=\"%s\"", locale);

  for(sl = trans; sl; sl = sl->next) {
    if(sl->key && strstr(sl->key, search_loc)) break;
  }

  if(!sl && (s = strchr(locale, '_'))) {
    *s = 0;
    strprintf(&search_loc, "lang=\"%s\"", locale);
    for(sl = trans; sl; sl = sl->next) {
      if(sl->key && strstr(sl->key, search_loc)) break;
    }
  }

  if(!sl && strcmp(locale, "en")) {
    strprintf(&search_loc, "lang=\"en\"");
    for(sl = trans; sl; sl = sl->next) {
      if(sl->key && strstr(sl->key, search_loc)) break;
    }
  }

  free(locale);
  free(search_loc);

  return sl ? sl->value : NULL;
}


int util_process_running(char *name)
{
  pid_t pid;
  struct dirent *de;
  DIR *d;
  char *s;

  if(!name) return 0;

  if(!(d = opendir("/proc"))) return 0;

  while((de = readdir(d))) {
    pid = strtoul(de->d_name, &s, 10);
    if(!*s && !strcmp(util_process_name(pid), name)) break;
  }

  closedir(d);

  return de ? 1 : 0;
}


char *blk_ident(char *dev)
{
  char *type, *label, *size;
  static char *id = NULL;

  if(id) {
    free(id);
    id = NULL;
  }

  if(!dev) return id;

  if(!config.blkid.cache) blkid_get_cache(&config.blkid.cache, "/dev/null");

  type = blkid_get_tag_value(config.blkid.cache, "TYPE", dev);
  label = blkid_get_tag_value(config.blkid.cache, "LABEL", dev);
  size = blk_size_str(dev);

  if(!size) return id;

  asprintf(&id, "%s, %s%s%s", size, type ?: "no fs", label ? ", " : "", label ?: "");

  free(type);
  free(label);

  return id;
}


char *blk_size_str(char *dev)
{
  uint64_t size;
  static char *s = NULL, unit;

  if(s) {
    free(s);
    s = NULL;
  }

  size = blk_size(dev);

  unit = 'k';
  if(size >= (1000 << 10)) { unit = 'M'; size >>= 10; }
  if(size >= (1000 << 10)) { unit = 'G'; size >>= 10; }
  if(size >= (1000 << 10)) { unit = 'T'; size >>= 10; }
  if(size >= (1000 << 10)) { unit = 'P'; size >>= 10; }
  if(size >= (1000 << 10)) { unit = 'E'; size >>= 10; }
  if(size >= (1000 << 10)) { unit = 'Z'; size >>= 10; }
  if(size >= (1000 << 10)) { unit = 'Y'; size >>= 10; }

  if(size >= 10 * (1 << 10)) {
    size = (size + 512) >> 10;
    asprintf(&s, "%u %cB", (unsigned) size, unit);
  }
  else {
    size = ((10 * size) + 512) >> 10;
    asprintf(&s, "%u.%u %cB", ((unsigned) size) / 10, ((unsigned) size) % 10, unit);
  }
  
  return s;
}


uint64_t blk_size(char *dev)
{
  int fd;
  blkid_loff_t size;

  fd = open(dev, O_RDONLY | O_NONBLOCK);
  size = fd >= 0 ? blkid_get_dev_size(fd) : 0;
  close(fd);

  return size >= 0 ? size : 0;
}


/*
 * Update device list in config.hd_data (if udev got new events).
 *
 * Do nothing if config.lock_device_list is != 0.
 */
void update_device_list(int force)
{
  static time_t last_time;
  struct stat sbuf;
  hd_t *hd, *net_list;

  log_info("update_device_list(%d)\n", force);

  hd_hw_item_t hw_items[] = {
    hw_block, hw_network_ctrl, hw_network, 0
  };

  if(!config.hd_data) force = 1;

  if(stat("/run/udev/queue.bin", &sbuf)) {
    force = 1;
  }
  else if(last_time != sbuf.st_mtime) {
    last_time = sbuf.st_mtime;
    force = 1;
  }

  if(!force) return;

  if(config.lock_device_list) {
    log_info("device list locked - no update\n");
    return;
  }

  log_info("%sscanning devices\n", config.hd_data ? "re" : "");

  if(config.hd_data) {
    hd_free_hd_data(config.hd_data);
    free(config.hd_data);
  }

  config.hd_data = calloc(1, sizeof *config.hd_data);

  // consider also raid devices
  config.hd_data->flags.list_md = 1;

  fix_device_names(hd_list2(config.hd_data, hw_items, 1));

  // update wlan interface list
  net_list = hd_list(config.hd_data, hw_network_ctrl, 0, NULL);
  for(hd = net_list; hd; hd = hd->next) {
    if(hd->is.wlan) util_set_wlan(hd->unix_dev_name);
  }
  hd_free_hd_list(net_list);
}


/*
 * Create new tmp mountpoint.
 */
char *new_mountpoint()
{
  static char *buf = NULL;

  strprintf(&buf, "%smp_%04u", config.mountpoint.base, config.mountpoint.cnt++);
  mkdir(buf, 0755);

  return buf;
}


/*
 * Copy 'src_dir/src_file' to 'dst'.
 *
 * return:
 *   0: ok
 *   1: failed
 */
int util_copy_file(char *src_dir, char *src_file, char *dst)
{
  int err = 0;
  char *buf = NULL, *argv[3];

  if(!src_dir || !src_file || !dst) return 1;

  strprintf(&buf, "%s/%s", src_dir, src_file);

  if(util_check_exist(buf) == 'r') {
    unlink(dst);
    argv[1] = buf;
    argv[2] = dst;
    if(util_cp_main(3, argv)) {
      unlink(dst);
      err = 1;
    }
  }
  else {
    err = 1;
  }

  str_copy(&buf, NULL);

  return err;
}


/*
 * Return new download image name.
 */
char *new_download()
{
  static char *buf = NULL;

  strprintf(&buf, "%s/file_%04u", config.download.base, config.download.cnt++);

  return buf;
}


void util_clear_downloads()
{
  int i;
  char *buf = NULL;

  for(i = config.download.cnt; i-- > 0;) {
    strprintf(&buf, "%s/file_%04u", config.download.base, i);
    if(util_check_exist(buf)) unlink(buf);
  }

  str_copy(&buf, NULL);
}


/*
 * Stop and allow to start a shell.
 *
 * Current code position is passed as arg.
 *
 * Useful for debugging. See linuxrc.debug and debug.wait options.
 */
void util_wait(const char *file, int line, const char *func)
{
  char *pos = NULL, *s;
  slist_t *sl = NULL;

  if(!config.debugwait || config.debugwait_off) return;

  str_copy(&pos, file);
  if((s = strrchr(pos, '.'))) *s = 0;
  strprintf(&pos, "%s:%d", pos, line);

  if(config.debugwait_list) {
    for(sl = config.debugwait_list; sl; sl = sl->next) {
      if(!fnmatch(sl->key, pos, 0) || !fnmatch(sl->key, func, 0)) break;
    }
  }

  if(!config.debugwait_list || sl) {
    log_info("== %s in %s() ==\n", pos, func);
    printf("== %s in %s() ==\n(enter = next, s = shell, c = continue normally, q = quit)? ", pos, func);

    switch(getchar()) {
      case 'q':
        util_umount_all();
        util_clear_downloads();
        config.debugwait = 0;
        lxrc_end();
        exit(0);
        break;

      case 's':
        kbd_end(0);
        if(config.win) disp_cursor_on();
        if(!config.linemode) {
          printf("\033c");
          if(config.utf8) printf("\033%%G");
          fflush(stdout);
        }

        system("PS1='\\w # ' /bin/bash 2>&1");

        kbd_init(0);
        if(config.win) {
          disp_cursor_off();
          if(!config.linemode) disp_restore_screen();
        }
        break;

      case 'c':
        config.debugwait_off = 1;
        break;

      default:
        break;
    }
  }

  str_copy(&pos, NULL);
}


void util_umount_all_devices ()
    {
    FILE *fd;
    char buffer [1000];
    char dir [1000];
    char *dirs [1000];
    int  nr_dirs = 0;
    int  i, j;

    fd = fopen ("/proc/mounts", "r");
    if (fd)
        {
        while (fgets (buffer, sizeof (buffer), fd))
            if (!strncmp (buffer, "/dev/", 5))
                {
                i = j = 0;
                while (buffer [i] != ' ') i++;
                i++;
                while (buffer [i] != ' ') dir [j++] = buffer [i++];
                dir [j] = 0;
                dirs [nr_dirs++] = strdup (dir);
                }

        fclose (fd);

        if (!nr_dirs) return;

        log_info("Trying to unmount %d directories:\n", --nr_dirs);

        /* we need to unmount in reverse order */

        do
            {
            if (dirs [nr_dirs])
                {
                if (dirs [nr_dirs][1])
		    {
		    log_info("Unmounting %s...\n", dirs [nr_dirs]);
		    umount (dirs [nr_dirs]);
		    }
		free (dirs [nr_dirs]);
                }
            }
        while (nr_dirs--);
        }
    }


void run_braille()
{
  hd_t *hd, *hd1;
  hd_data_t *hd_data = calloc(1, sizeof *hd_data);
  char *cmd = NULL;
  FILE *f;

  hd_data->debug = -1;

  log_show("Activating usb devices...\n");

  /* braille dev might need usb modules */
  util_load_usb();

  sleep(config.usbwait + 1);

  hd_list(hd_data, hw_usb, 1, NULL);
  load_drivers(hd_data, hw_usb);

  sleep(config.usbwait + 1);

  log_show("detecting braille devices...\n");

  hd = hd_list(hd_data, hw_braille, 1, NULL);

  if(config.debug) {
    f = fopen("/var/log/braille.log", "w");
    if(f) {
      fprintf(f, "%s\n", hd_data->log);
      for(hd1 = hd_data->hd; hd1; hd1 = hd1->next) {
        hd_dump_entry(hd_data, hd1, f);
      }
      hd_dump_entry(hd_data, hd, f);
      fclose(f);
    }
  }

  for(; hd; hd = hd->next) {
    if(
      hd->base_class.id == bc_braille &&        /* is a braille display */
      hd->unix_dev_name &&                      /* and has a device name */
      hd->device.name
    ) {
      break;
    }
  }

  if(hd) {
    str_copy(&config.braille.dev, hd->unix_dev_name);
    str_copy(&config.braille.type, hd->device.name);
  }
  else {
    str_copy(&config.braille.dev, NULL);
    str_copy(&config.braille.type, NULL);
  }

  hd_free_hd_data(hd_data);
  free(hd_data);

  if(config.braille.dev) {
    log_show("%s: %s\n", config.braille.dev, config.braille.type);

    if(util_check_exist("/etc/suse-blinux.conf") == 'r') {
      strprintf(&cmd,
        "sed -i -e 's#^brlname=.*#brlname=%s#; s#^brlport=.*#brlport=%s#' /etc/suse-blinux.conf",
        config.braille.type, config.braille.dev
      );

      lxrc_run(cmd);

      str_copy(&cmd, NULL);

      lxrc_run("/etc/init.d/brld start");
      lxrc_run("/etc/init.d/sbl start");

      str_copy(&config.setupcmd, "inst_setup yast");

      LXRC_WAIT
    }
  }
}


void util_setup_udevrules()
{
  slist_t *rule;
  file_t *f, *f_mac, *f_name;
  FILE *ff;

  for(rule = config.udevrules; rule; rule = rule->next) {
    f = file_parse_buffer(rule->key, kf_comma + kf_none);
    if(
      (f_mac = file_getentry(f, "mac")) &&
      (f_name = file_getentry(f, "name"))
    ) {
      log_info("udev net rule: mac = \"%s\", name = \"%s\"\n", f_mac->value, f_name->value);
      if((ff = fopen("/etc/udev/rules.d/70-persistent-net.rules", "a"))) {
        fprintf(ff,
          "SUBSYSTEM==\"net\", ACTION==\"add\", DRIVERS==\"?*\", ATTR{address}==\"%s\", ATTR{type}==\"1\", KERNEL==\"eth*\", NAME=\"%s\"\n",
          f_mac->value,
          f_name->value
        );
        fclose(ff);
      }
    }
    file_free_file(f);
  }
}


void util_error_trace(char *format, ...)
{
  void *buffer[64], **p;
  int nptrs;
  va_list args;

  if(!config.error_trace) return;

  va_start(args, format);
  vfprintf(stderr, format, args);
  va_end(args);
  
  nptrs = backtrace(buffer, sizeof buffer / sizeof *buffer);
  p = buffer;
  if(nptrs > 1) nptrs--, p++;
  backtrace_symbols_fd(p, nptrs, STDERR_FILENO);

  fprintf(stderr, "--------\n");
}


/*
 * Change device names to match requested naming scheme.
 */
hd_t *fix_device_names(hd_t *hd)
{
  hd_t *hd0 = hd;
  str_list_t *sl, *sl0;
  char *s = NULL;

  if(!config.namescheme || !*config.namescheme) return hd0;

  asprintf(&s, "/%s/", config.namescheme);

  for(; hd; hd = hd->next) {
    if(!hd->unix_dev_name || !hd->unix_dev_names) continue;

    if(strstr(hd->unix_dev_name, s)) continue;

    for(sl = hd->unix_dev_names; sl; sl = sl->next) {
      if(strstr(sl->str, s)) {
        sl0 = calloc(1 , sizeof *sl0);
        sl0->next = hd->unix_dev_names;
        hd->unix_dev_names = sl0;
        sl0->str = strdup(sl->str);
        // ########## FIXME: should be freed
//        free(hd->unix_dev_name);
        hd->unix_dev_name = strdup(sl->str);
        break;
      }
    }
  }

  free(s);

  return hd0;
}


/*
 * fcoe = fibre channel + network
 * edd/raw_data[interface] = 'FIBRE' + edd/pci_dev = network device
 */
int fcoe_check()
{
  struct dirent *de;
  DIR *d, *dn;
  int fd, raw_len, fcoe_ok = 0;
  char *attr;
  const char *sysfs_edd = "/sys/firmware/edd";
  unsigned char raw[64];

  if(!(d = opendir(sysfs_edd))) {
    log_debug("fcoe_check: no edd\n");
    return fcoe_ok;
  }

  while((de = readdir(d))) {
    log_debug("checking %s\n", de->d_name);
    asprintf(&attr, "%s/%s/raw_data", sysfs_edd, de->d_name);
    fd = open(attr, O_RDONLY);
    if(fd >= 0) {
      raw_len = read(fd, raw, sizeof raw);
      if(raw_len >= 48 && !memcmp(raw + 40, "FIBRE", 5)) {
        log_info("%s: fibre channel\n", de->d_name);
      }

      close(fd);
    }
    free(attr);

    asprintf(&attr, "%s/%s/pci_dev/net", sysfs_edd, de->d_name);
    if((dn = opendir(attr))) {
      log_info("%s: network\n", de->d_name);
      fcoe_ok = 1;
      closedir(dn);
    }
    free(attr);
  }

  closedir(d);

  log_info("fcoe_check: %d\n", fcoe_ok);

  return fcoe_ok;
}


/*
 * Don't actually do anything except loading the required modules and
 * logging a bit.
 *
 * ibft devices are handled by wicked now.
 */
int iscsi_check()
{
  int iscsi_ok = 0;
  char *s, *sysfs_ibft = NULL, *attr = NULL;
  char *if_name = NULL, *if_mac = NULL, *ibft_mac = NULL;
  unsigned use_dhcp = 0;
  int mac_match;
  struct dirent *de;
  DIR *d;

  if(util_check_exist("/modules/iscsi_ibft.ko")) {
    lxrc_run("/sbin/modprobe iscsi_ibft");
    sleep(1);
  }

  if((d = opendir("/sys/firmware"))) {
    while((de = readdir(d))) {
      if(!strcmp(de->d_name, "ibft")) {
        sysfs_ibft = "/sys/firmware/ibft/ethernet0";
      }
      if(!strncmp(de->d_name, "iscsi_boot", sizeof "iscsi_boot" - 1)) {
        iscsi_ok = 1;
      }
    }
    closedir(d);
  }

  if(iscsi_ok) {
    log_info("ibft: iscsi_boot\n");
    return 1;
  }

  if(!util_check_exist(sysfs_ibft)) return 0;

  log_info("ibft: sysfs dir = %s\n", sysfs_ibft);

  strprintf(&attr, "%s/origin", sysfs_ibft);
  s = util_get_attr(attr);
  log_info("ibft: origin = %s\n", s);
  if(s[0] == '3') use_dhcp = 1;
  log_info("ibft: dhcp = %d\n", use_dhcp);

  strprintf(&attr, "%s/mac", sysfs_ibft);
  s = util_get_attr(attr);
  log_info("ibft: ibft mac = %s\n", s);
  str_copy(&ibft_mac, *s ? s : NULL);

  strprintf(&attr, "%s/device/net", sysfs_ibft);
  if((d = opendir(attr))) {
    while((de = readdir(d))) {
      if(de->d_name[0] == '.') continue;
      str_copy(&if_name, de->d_name);
      break;
    }
    closedir(d);
  }

  log_info("ibft: if = %s\n", if_name ?: "");

  if(if_name) {
    strprintf(&attr, "%s/device/net/%s/address", sysfs_ibft, if_name);
    s = util_get_attr(attr);
    str_copy(&if_mac, *s ? s : NULL);
  }

  log_info("ibft: if mac = %s\n", if_mac ?: "");

  mac_match = if_mac && ibft_mac && !strcasecmp(if_mac, ibft_mac) ? 1 : 0;

  log_info("ibft: macs %smatch\n", mac_match ? "" : "don't ");

  str_copy(&attr, NULL);
  str_copy(&if_name, NULL);
  str_copy(&if_mac, NULL);
  str_copy(&ibft_mac, NULL);

  return 1;
}


/*
 * Get mac address from network interface name.
 *
 * return value must be freed
 */
char *interface_to_mac(char *device)
{
  char *buf = NULL;

  if(!device) return NULL;

  strprintf(&buf, "/sys/class/net/%s/address", device);

  char *addr = util_get_attr(buf);

  if(!strcmp(addr, "00:00:00:00:00:00")) *addr = 0;

  log_debug("if_to_mac: %s = %s\n", device, addr);

  str_copy(&buf, addr ?: NULL);

  return buf;
}


/*
 * Internal function, use mac_to_interface().
 *
 * return value must be freed
 */
char *mac_to_interface_log(char *mac, int log)
{
  struct dirent *de;
  DIR *d;
  char *sys = "/sys/class/net", *if_name = NULL, *attr, *if_mac;

  if(!mac) return NULL;

  if(util_check_exist2(sys, mac)) return strdup(mac);

  if(log) log_debug("%s = ?\n", mac);

  if(!(d = opendir(sys))) return NULL;

  while((de = readdir(d))) {
    if(de->d_name[0] == '.') continue;
    asprintf(&attr, "%s/%s/address", sys, de->d_name);
    if_mac = util_get_attr(attr);
    free(attr);
    if(!*if_mac || !strcmp(if_mac, "00:00:00:00:00:00")) continue;

    if(!if_name && !fnmatch(mac, if_mac, FNM_CASEFOLD)) {
      if_name = strdup(de->d_name);
    }

    if(log) {
      log_debug("%s = %s%s\n",
        if_mac,
        de->d_name,
        if_name && !strcmp(if_name, de->d_name) ? " *" : ""
      );
    }
  }

  closedir(d);

  return if_name;
}


/**
 * Get network interface name from mac. If max_offset
 * is set decrease mac and retry up to max_offset.
 *
 * If max_offset is not NULL, set to actual offset.
 *
 * Note: The max_offset param is there to help ibft parsing. Don't worry too
 * much about it.
 *
 * return value must be freed
 */
char *mac_to_interface(char *mac, int *max_offset)
{
  char *if_name, *s, *t;
  unsigned u;
  int ofs = 0, max_ofs = 0;

  if(max_offset) max_ofs = *max_offset;

  if(!mac || mac[0] == 0 || mac[0] == '.') return NULL;

  if_name = mac_to_interface_log(mac, 1);

  if(!if_name) {
    /* no direct match, retry with offset */

    mac = strdup(mac);

    if((s = strrchr(mac, ':'))) {
      if(strlen(s) == 3) {
        u = strtoul(s + 1, &t, 16);
        if(!*t) {
          for(ofs = 1; ofs <= max_ofs; ofs++) {
            sprintf(s + 1, "%02x", (u - ofs) & 0xff);
            if_name = mac_to_interface_log(mac, 0);
            if(if_name) break;
          }
        }
      }
    }

    free(mac);
  }

  if(if_name && max_offset) *max_offset = ofs;

  log_debug("if = %s", if_name);
  if(if_name && ofs) log_debug(", offset = %u", ofs);
  log_debug("\n");

  return if_name;
}


/*
 * Outsource some setup tasks to shell scripts.
 *
 * Several linuxrc config settings are passed as environment vars to the
 * scripts:
 *
 *   config.loghost        -> $LOGHOST
 *   !config.auto_assembly -> $linuxrc_no_auto_assembly
 *   config.debug          -> $linuxrc_debug
 *
 * /etc/install.inf is available when the scripts run.
 *
 * Scripts can pass settings back to linuxrc by putting them into
 * /tmp/script.result (ususal info-file syntax).
 */
void util_run_script(char *name)
{
  char *buf = NULL;

  if(config.test) return;

  if(util_check_exist2("/scripts", name) != 'r') return;

  file_write_install_inf("");

  if(config.debug) {
    strprintf(&buf, "%d", config.debug);
    setenv("linuxrc_debug", buf, 1);
  }

  /* pass the negated setting to stay compatible */
  if(!config.auto_assembly) {
    setenv("linuxrc_no_auto_assembly", "1", 1);
  }

  if(config.loghost) setenv("LOGHOST", config.loghost, 1);

  strprintf(&buf, "/scripts/%s", name);

  lxrc_run(buf);

  str_copy(&buf, NULL);

  file_read_info_file("file:/tmp/script.result", kf_cfg);

  unlink("/tmp/script.result");
}


void util_plymouth_off()
{
  if(!config.plymouth) return;

  config.plymouth = 0;

  if(util_check_exist("/usr/bin/plymouth") != 'r') return;

  lxrc_run_console("/usr/bin/plymouth quit");

  kbd_init(0);
}


/*
 * Let user enter a device
 * (*dev = NULL if she changed her mind).
 *
 * type: 0 = all block devs
 *       1 = skip whole disk if it has partitions
 *       2 = skip partitions
 *
 * return values:
 *  0    : ok
 *  1    : abort
 */
int util_choose_disk_device(char **dev, int type, char *list_title, char *input_title)
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
    dia_info(&win, "Searching for storage devices...", MSGTYPE_INFO);
    fix_device_names(hd_list(hd_data, hw_block, 1, NULL));
    win_close(&win);
  }

  for(i = 0, hd = hd_data->hd; hd; hd = hd->next) {
    if(!hd_is_hw_class(hd, hw_block)) continue;

    if(
      type == 1 &&	// skip whole disks if there are partitions
      (hd1 = hd_get_device_by_idx(hd_data, hd->attached_to)) &&
      hd1->base_class.id == bc_storage_device
    ) {
      hd1->status.available = status_no;
    }

    if(
      type == 2 &&	// skip partitions
      hd_is_hw_class(hd, hw_partition)
    ) {
      hd->status.available = status_no;
    }

    i++;
  }

  /* just max values, actual lists might be shorter */
  items = calloc(i + 1 + 2, sizeof *items);
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

      // log_info("<%s>\n", s);

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
    i = dia_list(list_title, item_width + 2, NULL, items, last_item, align_left);
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
      i = dia_input2(input_title, &buf, 30, 0);
      if(!i) {
        if(util_check_exist(long_dev(buf)) == 'b') {
          str_copy(&config.update.dev, short_dev(buf));
          *dev = config.update.dev;
        }
        else {
          dia_message("Invalid device name.", MSGTYPE_ERROR);
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

  // log_info("dud dev = %s\n", *dev);

  return err;
}


void util_restart()
{
  if(config.restarting || config.restarted) return;

  config.restarting = 1;
  lxrc_end();
  setenv("restarted", "42", 1);
  execve(*config.argv, config.argv, environ);
}


/*
 * buf: at least 6 bytes
 */
char *compress_type(void *buf)
{
  if(!memcmp(buf, "\x1f\x8b", 2)) {
    return "gzip";
  }

  if(!memcmp(buf, "\xfd""7zXZ", 6) /* yes, including final \0 */) {
    return "xz";
  }

  return NULL;
}


char *compressed_file(char *name)
{
  int fd;
  char buf[8];
  char *compr = NULL;

  fd = open(name, O_RDONLY | O_LARGEFILE);

  if(fd >= 0) {
    if(read(fd, buf, sizeof buf) == sizeof buf) {
      compr = compress_type(buf);
    }

    close(fd);
  }
  else {
    perror_debug(name);
  }

  return compr;
}


char *compressed_archive(char *name, char **archive)
{
  char *compr = compressed_file(name);
  char buf1[64], buf2[0x108];
  FILE *f;
  char *type = NULL;

  if(!archive) return compr;

  if(compr) {
    snprintf(buf1, sizeof buf1, "%s -dc %s", compr, name);

    if((f = popen(buf1, "r"))) {
      if(fread(buf2, 1, sizeof buf2, f) == sizeof buf2) {
        if(!memcmp(buf2, "070701", 6)) type = "cpio";
        if(!memcmp(buf2, "\xc7\x71", 2)) type = "cpio";
        if(!memcmp(buf2 + 0x101, "ustar", 6 /* with \0 */)) type = "tar";
      }

      pclose(f);
    }
  }

  *archive = type;

  log_debug("%s = %s.%s\n", name, type ?: "", compr ?: "");

  return compr;
}


/*
 * Helper function: sort alphanumerically.
 */
int cmp_alpha(slist_t *sl0, slist_t *sl1)
{
  return strcmp(sl0->key, sl1->key);
}


/*
 * Wrapper around cmp_alpha() for qsort.
 */
int cmp_alpha_s(const void *p0, const void *p1)
{
  slist_t **sl0, **sl1;

  sl0 = (slist_t **) p0;
  sl1 = (slist_t **) p1;

  return cmp_alpha(*sl0, *sl1);
}


/*
 * Scan parition mounted at /mnt for kernel & initrd.
 */
slist_t *get_kernel_list(char *dev)
{
#if defined(__s390__) || defined(__s390x__)
  char *kernel_pattern = "image-*";
#elif defined(__x86_64__) || defined(__i386__)
  char *kernel_pattern = "vmlinuz-*";
#elif defined(__aarch64__)
  char *kernel_pattern = "Image-*";
#else
  char *kernel_pattern = "vmlinux-*";
#endif
  char *dirs[] = { "/mnt", "/mnt/boot", "/mnt/efi/boot", "/mnt/efi/SuSE" };

  int i;
  DIR *d;
  struct dirent *de;
  char *buf = NULL;
  slist_t *sl, *kernel_list = NULL;

  for(i = 0; i < sizeof dirs/sizeof *dirs; i++) {
    char link_name[2];

    // skip boot -> . symlink and absolute symlinks
    if(
      readlink(dirs[i], link_name, sizeof link_name) == 1 &&
      (*link_name == '.' || *link_name == '/')
    ) continue;

    if((d = opendir(dirs[i]))) {
      while((de = readdir(d))) {
        if(!fnmatch(kernel_pattern, de->d_name, FNM_PATHNAME)) {
          char *t = strchr(de->d_name, '-');
          if(t) {
            strprintf(&buf, "%s/initrd%s", dirs[i], t);
            log_info("%s matched, initrd? %s\n", de->d_name, buf);
            if(util_check_exist(buf) == 'r') {
              char *t2 = dirs[i] + sizeof "/mnt" - 1;
              sl = slist_append(&kernel_list, slist_new());
              strprintf(&sl->key, "%s:%s/%s", dev, t2, de->d_name);
              strprintf(&sl->value, "%s:%s/initrd%s", dev, t2, t);
              log_info("kernel: %s / %s\n", sl->key, sl->value);
            }
            str_copy(&buf, NULL);
          }

        }
      }
      closedir(d);
    }
  }

  return kernel_list;
}


/*
 * Boot installed system.
 *
 *   1. analyze disks: mount every partition and
 *       - look for /etc/{os,SuSE}-release; every such partition is considered
 *         to be a root file system
 *       - look for <kernel>-XXX and matching initrd-XXX files; every such pair
 *         is considered a bootable kernel/initrd combination; <kernel> may be
 *         vmlinux, vmlinuz, or image depending on the architecture
 *
 *   2. present 4 (resp. 5) dialogs to user:
 *       - select root partition
 *       - select kernel/initrd
 *       - optionally select alternative (persistent) root partition name
 *       - optionally add further kernel parameters
 *       - if in debug mode: optionally edit kexec options
 */
void util_boot_system()
{
  window_t win;
  char *buf = NULL;
  slist_t *sl;
  slist_t *root_list = NULL, *root = NULL;
  slist_t *kernel_list = NULL, *kernel = NULL;
  char *kernel_options = NULL;
  int i, items;
  char **item_list;

  strprintf(&buf, "Analysing disks...");
  log_info("%s\n", buf);
  if(config.win) {
    dia_info(&win, buf, MSGTYPE_INFO);
  }
  else {
    printf("%s\n", buf);
    fflush(stdout);
  }
  str_copy(&buf, NULL);

  util_update_disk_list(NULL, 1);

  for(sl = config.partitions; sl; sl = sl->next) {
    char *type = util_fstype(long_dev(sl->key), NULL);
    char *blk_id = blk_ident(long_dev(sl->key));
    if(type && strcmp(type, "swap")) {
      if(!util_mount_ro(long_dev(sl->key), "/mnt", NULL)) {
        char *os_name = NULL;

        if(util_check_exist("/mnt/etc/os-release")) {
          char *s = util_get_attr("/mnt/etc/os-release");
          char *t = strstr(s, "PRETTY_NAME=\"");
          if(t) {
            t += sizeof "PRETTY_NAME=\"" - 1;
            char *t2 = strchr(t, '"');
            if(t2) {
              *t2 = 0;
              os_name = t;
            }
          }
        }
        else if(util_check_exist("/mnt/etc/SuSE-release")) {
          char *s = util_get_attr("/mnt/etc/SuSE-release");
          char *t = strchr(s, '\n');
          if(t) *t = 0;
          if(*s) os_name = s;
        }

        strprintf(&buf, "%s (%s) -- %s", sl->key, blk_id, os_name ?: "");
        log_info("%s\n", buf);
        if(os_name) {
          slist_t *sl2 = slist_append_str(&root_list, long_dev(sl->key));
          str_copy(&sl2->value, buf);
        }
        str_copy(&buf, NULL);

        slist_append(&kernel_list, get_kernel_list(sl->key));

        util_umount("/mnt");
      }
    }
  }

  if(config.win) win_close(&win);

  if(!root_list || !kernel_list) {
    dia_message("No bootable system found.", MSGTYPE_ERROR);
    return;
  }

  root_list = slist_sort(root_list, cmp_alpha_s);
  kernel_list = slist_sort(kernel_list, cmp_alpha_s);

  for(items = 0, sl = root_list; sl; sl = sl->next) items++;

  item_list = calloc(items + 1, sizeof *item_list);

  for(i = 0, sl = root_list; sl; sl = sl->next, i++) {
    item_list[i] = sl->value;
  }

  i = dia_list("Select a system to boot", 72, NULL, item_list, 0, align_left);

  free(item_list);
  item_list = NULL;

  if(i <= 0) {
    slist_free(root_list);

    return;
  }

  // get the i-th (1-based) entry from list
  for(root = root_list; root && i > 1; root = root->next, i--);

  // root = system partition

  for(items = 0, sl = kernel_list; sl; sl = sl->next) items++;

  item_list = calloc(items + 1, sizeof *item_list);

  for(i = 0, sl = kernel_list; sl; sl = sl->next, i++) {
    item_list[i] = sl->key;
  }

  strprintf(&buf, "Select a kernel to boot\n%s", root->value);
  i = dia_list(buf, 72, NULL, item_list, 0, align_left);
  str_copy(&buf, NULL);

  free(item_list);
  item_list = NULL;

  if(i <= 0) {
    slist_free(root_list);
    slist_free(kernel_list);

    return;
  }

  // get the i-th (1-based) entry from list
  for(kernel = kernel_list; kernel && i > 1; kernel = kernel->next, i--);

  // kernel = kernel/initrd pair

  // maybe user wants a persistent device name for 'root' option...

  hd_data_t *hd_data = calloc(1, sizeof *hd_data);

  hd_data->flags.list_md = 1;
  hd_t *hd = hd_list(hd_data, hw_partition, 1, NULL);

  for(; hd; hd = hd->next) {
    if(hd->unix_dev_name && !strcmp(hd->unix_dev_name, root->key)) {
      str_list_t *hsl;

      for(items = 0, hsl = hd->unix_dev_names; hsl; hsl = hsl->next) items++;

      if(items) {
        item_list = calloc(items + 1, sizeof *item_list);

        for(i = 0, hsl = hd->unix_dev_names; hsl; hsl = hsl->next, i++) {
          item_list[i] = hsl->str;
        }

        i = dia_list("You may select an alternative device name for the system partition", 72, NULL, item_list, 0, align_left);

        if(i > 0) {
          str_copy(&root->key, item_list[i - 1]);
        }

        free(item_list);
        item_list = NULL;
      }

      break;
    }
  }

  hd_free_hd_data(hd_data);
  free(hd_data);

  strprintf(&kernel_options, "root=%s", root->key);

  if(dia_input2("Edit kernel options", &kernel_options, 57, 0)) {
    str_copy(&kernel_options, NULL);
    slist_free(root_list);
    slist_free(kernel_list);

    return;
  }

  // show what we are doing
  log_info("going to boot %s, append=\"%s\"\n", kernel->key, kernel_options);

  // ok, now mount partition again, and load kernel & initrd

  char *kernel_name = strchr(kernel->key, ':');
  char *initrd_name = strchr(kernel->value, ':');

  // can't happen, but anyway...
  if(!kernel_name || !initrd_name) return;

  *kernel_name++ = 0;
  *initrd_name++ = 0;

  if(util_mount_ro(long_dev(kernel->key), "/mnt", NULL)) {
    log_info("oops, mounting partition failed\n");
    slist_free(root_list);
    slist_free(kernel_list);

    return;
  }

  strprintf(&buf,
    "kexec -a -l '/mnt/%s' --initrd='/mnt/%s' --append='%s'",
    kernel_name, initrd_name, kernel_options
  );

  if(config.debug) {
    char *buf1 = NULL;

    if(dia_input2("Enter additional kexec options", &buf1, 57, 0)) {
      util_umount("/mnt");

      str_copy(&buf1, NULL);
      str_copy(&kernel_options, NULL);
      slist_free(root_list);
      slist_free(kernel_list);

      return;
    }

    if(buf1) {
      strprintf(&buf, "%s %s", buf, buf1);
      str_copy(&buf1, NULL);
    }
  }

  if(!config.test) {
    int err = lxrc_run(buf);
    util_umount("/mnt");
    if(!err) {
      util_umount_all();
      sync();
      // dia_message("Now!", MSGTYPE_INFO);
      LXRC_WAIT
      lxrc_run("kexec --exec");
    }
  }
  else {
    log_info("%s\n", buf);
  }

  str_copy(&buf, NULL);
  str_copy(&kernel_options, NULL);
  slist_free(root_list);
  slist_free(kernel_list);

  // oops, we failed

  dia_message("Sorry, system didn't boot.", MSGTYPE_ERROR);
}


/*
 * Remember that device is a wlan interface.
 */
void util_set_wlan(char *device)
{
  if(!device || config.net.wlan.devices_fixed) return;

  slist_setentry(&config.net.wlan.devices, device, NULL, 0);
}


/*
 * Return 1 if device is a wlan interface.
 */
int util_is_wlan(char *device)
{
  return slist_getentry(config.net.wlan.devices, device) ? 1 : 0;
}


/*
 * Write log message.
 *
 * level is a bitmask determining the destination to log to.
 *
 * Add a time stamp when the destination has the LOG_TIMESTAMP flag set.
 */
void util_log(unsigned level, char *format, ...)
{
  va_list args;
  char *buf, *caller = NULL;
  int buf_len = 0;
  log_file_t *lf;

  time_t t = time(NULL);
  struct tm *gm = gmtime(&t);

  va_start(args, format);
  if(vasprintf(&buf, format, args) == -1) buf = NULL;
  if(buf) buf_len = strlen(buf);
  va_end(args);

  for(lf = config.log.dest; lf < config.log.dest + sizeof config.log.dest / sizeof *config.log.dest; lf++) {
    if((level & lf->level)) {
      if(!lf->f && lf->name) {
        lf->f = fopen(lf->name, "a");
      }
      if(lf->f) {
        if((lf->level & LOG_TIMESTAMP)) {
          if(gm) {
            fprintf(lf->f, "%02d:%02d:%02d <%u>", gm->tm_hour, gm->tm_min, gm->tm_sec, level);
            if((lf->level & LOG_CALLER)) {
              if(!caller) caller = util_get_caller(1);
              if(caller) fprintf(lf->f, " %-28s", caller);
            }
            fprintf(lf->f, ": ");
          }
        }
        if(buf_len) {
          fwrite(buf, buf_len, 1, lf->f);
          // supplement a missing trailing newline
          if(
            buf[buf_len - 1] != '\n' &&
            (lf->level & LOG_TIMESTAMP)
          ) {
            fputc('\n', lf->f);
          }
          fflush(lf->f);
        }
      }
    }
  }

  str_copy(&buf, NULL);
}


/*
 * Run command and redirect and log stderr to linuxrc log file.
 *
 * If log_stdout is != 0, redirect and log also stdout.
 */
int util_run(char *cmd, unsigned log_stdout)
{
  char buf[1024], *buf2 = NULL, *cmd2 = NULL;
  int fd, i, err = -1;

  if(!cmd) return err;

  fd = open("/tmp", O_TMPFILE | O_RDWR, S_IRUSR | S_IWUSR);

  if(fd == -1) {
    perror_debug("failed to create tmp file");

    return err;
  }

  strprintf(&cmd2, "%s 2>&%d%s", cmd, fd, log_stdout ? " >&2" : "");

  err = WEXITSTATUS(system(cmd2));

  log_info_maybe(config.debug, "exec: %s = %d\n", cmd, err);

  lseek(fd, 0, SEEK_SET);

  while((i = read(fd, buf, sizeof buf - 1)) > 0) {
    buf[i] = 0;
    strprintf(&buf2, "%s%s", buf2 ?: "", buf);
  }

  close(fd);

  if(buf2) {
    log_debug("%sstderr:\n%s", log_stdout ? "stdout + " : "", buf2);
  }

  str_copy(&buf2, NULL);
  str_copy(&cmd2, NULL);

  return err;
}


/*
 * Like perror() but using logging function.
 */
void util_perror(unsigned level, char *msg)
{
  char *str = strerror(errno);

  if(msg && *msg) {
    util_log(level, "%s: %s\n", msg, str);
  }
  else {
    util_log(level, "%s\n", str);
  }
}


/*
 * Run backtrace to get the name of the calling function.
 *
 * Skip 'skip' calling levels.
 *
 * If the symbolic name can't be found, it (recursively) tries to get the
 * name of the parent function and appends ".?" to the name.
 */
char *util_get_caller(int skip)
{
  void *buffer[8], **p;
  char **syms, *s, *t;
  static char *buf = NULL;
  int i, len;

  str_copy(&buf, NULL);

  len = backtrace(buffer, sizeof buffer / sizeof *buffer);

  if((len -= ++skip) <= 0) return buf;
  p = buffer + skip;

  if(!(syms = backtrace_symbols(p, len))) return buf;

  for(i = 0, s = 0; i < len; i++) {
    if((s = strchr(syms[i], '(')) && s[1] != ')') {
      if((t = strchr(++s, ')'))) {
        *t = 0;
        i = i >= 3 ? 0 : 2 * (3 - i);
        asprintf(&buf, "%s%s", s, ".?.?.?" + i);
      }
      break;
    }
  }

  free(syms);

  return buf;
}


/*
 * Convenience function:
 * Set hostname and log this.
 */
void util_set_hostname(char *hostname)
{
  if(!hostname) return;

  sethostname(hostname, strlen(hostname));

  log_info("set hostname: %s\n", hostname);
}


/*
 * Run debug shell.
 *
 * This takes care of screen settings and restoring standard file
 * descriptors.
 */
void util_run_debugshell()
{
  kbd_end(1);

  if(config.win) {
    disp_cursor_on();
  }
  if(!config.linemode) {
    printf("\033c");
    if(config.utf8) printf("\033%%G");
    fflush(stdout);
  }

  char *cmd = NULL;
  strprintf(&cmd, "exec %s 2>&1", config.debugshell ?: "/bin/sh");
  system(cmd);
  free(cmd);

  freopen(config.console, "r", stdin);
  freopen(config.console, "a", stdout);
  freopen(config.console, "a", stderr);

  kbd_init(0);

  if(config.win) {
    disp_cursor_off();
    if(!config.linemode) disp_restore_screen();
  }
}


/*
 * Enable linuxrc to dump core.
 *
 * config.core is expected to be either a block device or a char device
 *
 * block device:
 *   - the device is mounted at /coredumps and core files are stored there
 *
 * char device:
 *   - the core dump is written uuencoded to the char device (e.g. serial console)
 *
 * util_setup_coredumps() can be called several times. Each time it checks
 * if all prerequisites are all fulfilled and if so, enables core dumps.
 *
 * The point here is that e.g. a block device may appear only after udev has
 * done its work while a char device may exist much earlier.
 *
 */
void util_setup_coredumps()
{
  FILE *f;
  int core_type;
  char *core_dir = "/coredumps";
  char *core_pattern = "%e.core.pid_%P.sig_%s.time_%t";

  if(config.core_setup || !config.core || config.core[0] != '/') return;

  core_type = util_check_exist(config.core);

  if(!core_type) return;

  if(core_type == 'b') {
    mkdir(core_dir, 0755);
    if(util_check_exist(core_dir) != 'd') return;

    if(util_mount(config.core, core_dir, MS_SYNCHRONOUS, NULL)) return;

    if((f = fopen("/proc/sys/kernel/core_pattern", "w"))) {
      fprintf(f, "%s/%s", core_dir, core_pattern);
      fclose(f);
    }

    log_info("dumping core to %s", config.core);
  }
  else {
    if((f = fopen("/proc/sys/kernel/core_pattern", "w"))) {
      fprintf(f, "|/dumpling %s", core_pattern);
      fclose(f);
    }

    if((f = fopen("/dumpling", "w"))) {
      fprintf(f, "#! /bin/sh\nexec /usr/bin/uuencode $1 >%s\n", config.core);
      fchmod(fileno(f), 0755);
      fclose(f);
    }

    log_info("dumping core uuencoded to %s", config.core);
  }

  setrlimit(RLIMIT_CORE, &(struct rlimit) { rlim_cur: RLIM_INFINITY, rlim_max: RLIM_INFINITY });

  signal(SIGBUS, SIG_DFL);
  signal(SIGSEGV, SIG_DFL);

  config.core_setup = 1;
}


/*
 * Write s390 config values into /boot/zipl/active_devices.txt
 * (see bsc#1095062).
 */
void util_write_active_devices(char *format, ...)
{
  const char *file_name = "/boot/zipl/active_devices.txt";
  va_list args;
  FILE *f;

  f = fopen(file_name, "a");
  if(f) {
    va_start(args, format);
    vfprintf(f, format, args);
    va_end(args);
    fclose(f);
  }
  else {
    log_info("failed to open %s\n", file_name);
  }
}


/*
 * Re-parse URL that might be interpreted differently after udevd has been
 * started.
 *
 * This concerns URLs that might optionally start the path with a block
 * device name.
 *
 * As it is unknown which part of the path covers a block device until the
 * block device really exists, this works properly only after udevd has been
 * started.
 *
 * This does not affect URLs that use an explicit 'device' query parameter.
 */
void util_reparse_blockdev_url(url_t **url_ptr)
{
  url_t *url = *url_ptr;

  // not needed here
  if(!url || !url->is.blockdev || !url->str) return;

  log_info("re-parsing url: %s\n", url->str);
  url = url_set(url->str);

  if(url) {
    url_free(*url_ptr);
    *url_ptr = url;
  }
}


/*
 * Re-parse some URLs that might be interpreted differently after udevd has
 * been started.
 */
void util_reparse_blockdev_urls()
{
  util_reparse_blockdev_url(&config.url.autoyast);
  util_reparse_blockdev_url(&config.url.autoyast2);
  util_reparse_blockdev_url(&config.url.install);
  util_reparse_blockdev_url(&config.url.instsys);
}

/*
 * Apply S390 I/O device auto-config.
 *
 * Ask user before doing so, if requested.
 */
void util_device_auto_config()
{
  unsigned do_it = config.device_auto_config;

  if(do_it && !util_has_device_auto_config()) do_it = 0;

  if(do_it == 2) {
    int win_old = config.win;
    char *msg = config.device_auto_config_done ?
      "Reapply I/O device auto-configuration?" : "Apply I/O device auto-configuration?";

    if(!config.win) util_disp_init();
    do_it = dia_yesno(msg, YES) == YES;
    if(config.win && !win_old) util_disp_done();
  }

  if(do_it) {
    log_info("applying I/O device auto-configuration\n");
    util_run_script("device_auto_config");
    config.device_auto_config_done = 1;
    config.device_auto_config = 1;
  }
  else {
    config.device_auto_config = 0;
  }
}


/*
 * Check if S390 I/O device auto-config data is available.
 */
int util_has_device_auto_config()
{
  FILE *f;
  int has_it = 0;

  if((f = fopen("/sys/firmware/sclp_sd/config/data", "r"))) {
    has_it = fgetc(f) != EOF;
    fclose(f);
  }

  log_info("has I/O device auto-config data: %d\n", has_it);

  return has_it;
}
