/*
 *
 * util.c        Utility functions for linuxrc
 *
 * Copyright (c) 1996-2001  Hubert Mantel, SuSE GmbH  (mantel@suse.de)
 *
 */

#define __LIBRARY__

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/vfs.h>
#include <sys/mount.h>
#include <sys/ioctl.h>
#include <sys/kd.h>
#include <sys/utsname.h>
#include <fcntl.h>
#include <time.h>
#include <syscall.h>
#include <dirent.h>
#include <errno.h>
#include <sys/swap.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <utime.h>

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
#include "lsh.h"

#define LED_TIME     50000

static inline _syscall3 (int,syslog,int,type,char *,b,int,len);

static char  *util_loopdev_tm = "/dev/loop0";

#ifdef USE_VFAT
static void put_byte(int fd, unsigned char data);
static void put_short(int fd, unsigned short data);
static void put_int(int fd, unsigned data);
static unsigned mkdosfs(int fd, unsigned size);
#endif
static void do_file_cp(char *src_dir, char *dst_dir, char *name);

static struct hlink_s {
  struct hlink_s *next;
  dev_t dev;
  ino_t ino;
  char *dst;
} *hlink_list = NULL;

static char *exclude = NULL;
static int rec_level = 0;

static int do_cp(char *src, char *dst);
static char *walk_hlink_list(ino_t ino, dev_t dev, char *dst);
static void free_hlink_list(void);

void util_redirect_kmsg (void)
    {
    int   fd_ii;
    char  newvt_aci [2];


    if (serial_ig)
        syslog (8, 0, 1);
    else
        {
        fd_ii = open (console_tg, O_RDONLY);
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


void util_generate_button (button_t *button_prr, char *txt_tv)
    {
    memset (button_prr, 0, sizeof (button_t));
    strncpy (button_prr->text, txt_tv, BUTTON_SIZE);
    util_center_text (button_prr->text, BUTTON_SIZE);
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
        items_arr [i_ii].text = malloc (size_iv);
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


/* This function can only handle images <= 2GB. I think this is enough
   for the root image in the next month (kukuk@suse.de) */
int
util_fileinfo (char *file_tv, int32_t *size_plr, int *compressed_pir)
{
  unsigned char  buf_ti [2];
  int            handle_ii;


  *size_plr = 0;
  handle_ii = open (file_tv, O_RDONLY);
  if (!handle_ii)
    return (-1);

  if (read (handle_ii, buf_ti, 2) != 2)
    {
      close (handle_ii);
      return (-1);
    }

  if (buf_ti [0] == 037 && (buf_ti [1] == 0213 || buf_ti [1] == 0236))
    {
      unsigned char buf_size[4];

      if (lseek (handle_ii, (off_t) -4, SEEK_END) == (off_t)-1)
	{
	  close (handle_ii);
	  return (-1);
	}

      read (handle_ii, buf_size, sizeof (buf_size));
      *size_plr = (buf_size[3] << 24) + (buf_size[2] << 16) +
	(buf_size[1] << 8) + buf_size[0];
      *compressed_pir = TRUE;
    }
  else
    {
      *size_plr = (int32_t) lseek (handle_ii, (off_t) 0, SEEK_END);
      *compressed_pir = FALSE;
    }

  close (handle_ii);
  return (0);
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

    uname (&utsinfo_ri);
    sprintf (text_ti, ">>> Linuxrc v" LXRC_VERSION " (Kernel %s) (c) 1996-2001 SuSE GmbH <<<",
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

    rc_ii = util_try_mount (util_loopdev_tm, mountpoint_tv,
                            MS_MGC_VAL | MS_RDONLY, 0);

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


void util_truncate_dir (char *dir_tr)
    {
    if (dir_tr [0] == 0)
        return;

    if (dir_tr [strlen (dir_tr) - 1] == '/')
        dir_tr [strlen (dir_tr) - 1] = 0;

#if 0
    if (strlen (dir_tr) > 4 && !strcmp (&dir_tr [strlen (dir_tr) - 5], "/suse"))
        dir_tr [strlen (dir_tr) - 5] = 0;
#endif

    if (dir_tr [0] == 0)
        strcpy (dir_tr, "/");
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


int util_try_mount (const char *device_pcv,             char *dir_pcv,
                    unsigned long flags_lv, const void *data_prv)
    {
    int   i_ii;
    int   rc_ii;


    if (!device_pcv || !device_pcv [0])
        return (-1);

    i_ii = 0;
    do
        rc_ii = mount (device_pcv, dir_pcv, fs_types_atg [i_ii++],
                       flags_lv, data_prv);
    while (rc_ii && fs_types_atg [i_ii]);

    return (rc_ii);
    }


void util_print_ftp_error (int error_iv)
    {
    char  text_ti [200];

    sprintf (text_ti, txt_get (TXT_ERROR_FTP), ftpStrerror (error_iv));
    dia_message (text_ti, MSGTYPE_ERROR);
    }


void util_free_ramdisk (char *ramdisk_dev_tv)
    {
    int  fd_ii;

    fd_ii = open (ramdisk_dev_tv, O_RDWR);
    if (fd_ii)
        {
        if (ioctl (fd_ii, BLKFLSBUF))
            fprintf (stderr, "Cannot free ramdisk memory\n");
        else
            fprintf (stderr, "Ramdisk memory successfully freed\n");

        close (fd_ii);
        }
    else
        fprintf (stderr, "Cannot open ramdisk device\n");
    }


int util_open_ftp (char *server_tv)
    {
    return (ftpOpen (server_tv,
                     ftp_user_tg [0]     ? ftp_user_tg     : 0,
                     ftp_password_tg,
                     ftp_proxy_tg [0]    ? ftp_proxy_tg    : 0,
                     ftp_proxyport_ig));
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
  for(i_ii = 1; i_ii < max_y_ig; i_ii++) printf("\n"); printf("\033[9;0]");
  disp_cursor_off();
  util_print_banner();
}


void util_disp_done()
{
  disp_clear_screen();
  printf("\033c");
  fflush(stdout);

  config.win = 0;
}


/*
 * umount() with error message
 */
int util_umount(char *mp)
{
#ifdef LXRC_DEBUG
  int i;

  if((i = umount(mp))) {
    i = errno;
    if(i != ENOENT && i != EINVAL) {
      fprintf(stderr, "umount %s failed: %d\n", mp, i);
    }
  }
  else {
    fprintf(stderr, "umount %s ok\n", mp);
  }

  return i;
#else
  return umount(mp);
#endif
}

int _util_eject_cdrom(char *dev)
{
  int fd;

  if(!dev || !*dev) return 0;

  fprintf(stderr, "eject %s\n", dev);

  if((fd = open(dev, O_RDONLY | O_NONBLOCK)) < 0) return 1;
  umount(dev);
  if(ioctl(fd, CDROMEJECT, NULL) < 0) { close(fd); return 2; }
  close(fd);

  return 0;
}

int util_eject_cdrom(char *dev)
{
#ifdef USE_LIBHD
  hd_data_t *hd_data;
  hd_t *hd;
#endif

  if(dev && *dev) return _util_eject_cdrom(dev);

#ifdef USE_LIBHD
  hd_data = calloc(1, sizeof *hd_data);
  for(hd = hd_list(hd_data, hw_cdrom, 1, NULL); hd; hd = hd->next) {
    _util_eject_cdrom(hd->unix_dev_name);
  }
  hd_free_hd_list(hd);
  hd_free_hd_data(hd_data);
  free(hd_data);
#endif

  return 0;
}

void util_manual_mode()
{
  auto_ig = 0;
  auto2_ig = 0;
}

#ifdef USE_VFAT
void put_byte(int fd, unsigned char data)
{
  write(fd, &data, 1);
}

void put_short(int fd, unsigned short data)
{
  write(fd, &data, 2);
}

void put_int(int fd, unsigned data)
{
  write(fd, &data, 4);
}

/*
 * Create a FAT file system on fd; size in kbyte.
 * Return the actual free space in kbyte.
 *
 * size must be less than 32MB.
 */
unsigned mkdosfs(int fd, unsigned size)
{
  unsigned char sect[0x200];

  static unsigned char part1[] = {
    0xeb, 0xfe, 0x90, ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', 0, 2, 2, 1, 0,
    1, 0x20, 0
  };
  static unsigned char part2[] = {
    16, 0, 16, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x80, 0, 0x29, 1, 2, 3, 4,
    ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ',' ', ' ', ' ',
    'F', 'A', 'T', '1', '6', ' ', ' ', ' '
  };
  int i;
  unsigned clusters;
  unsigned fat_sectors;

  fat_sectors = ((16 * (size + 2) + 7) / 8 + 0x200 - 1) / 0x200;
  clusters = (size * 2 - fat_sectors - 2) / 2;

  /* clear fs meta data */
  memset(sect, 0, sizeof sect);
  i = fat_sectors + 1 + 16;
  if(i > 2 * size) i = 2 * size;
  lseek(fd, 0, SEEK_SET);
  while(i--) write(fd, sect, sizeof sect);
  lseek(fd, 0, SEEK_SET);

  for(i = 0; i < sizeof part1; i++) put_byte(fd, part1[i]);
  put_short(fd, size * 2);
  put_byte(fd, 0xf8);
  put_short(fd, fat_sectors);
  for(i = 0; i < sizeof part2; i++) put_byte(fd, part2[i]);

  lseek(fd, 0x1fe, SEEK_SET);
  put_short(fd, 0xaa55);
  put_int(fd, 0xfffffff8);
  for(i = 1; i < ((fat_sectors + 1) * 0x200) / 4; i++) put_int(fd, 0);

  return clusters;
}
#endif

void do_file_cp(char *src_dir, char *dst_dir, char *name)
{
  char buf[256];

  sprintf(buf, "cp %s/%s %s", src_dir, name, dst_dir);
  system(buf);

  sprintf(buf, "%s/%s", dst_dir, name);
  chmod(buf, 0755);
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
  char inst_dst[100], imod[200], rmod[100], *s;
  struct stat st;
  int i;
#ifdef USE_VFAT
  int fd;
  unsigned fssize;
#endif
  struct dirent *de;
  DIR *d;

  if(!dir) return 0;
  if(*driver_update_dir) return 0;

  sprintf(drv_src, "%s/linux/suse/" LX_ARCH "-" LX_REL, dir);
  sprintf(mods_src, "%s/linux/suse/" LX_ARCH "-" LX_REL "/modules", dir);
  sprintf(inst_src, "%s/linux/suse/" LX_ARCH "-" LX_REL "/install", dir);

  if(stat(drv_src, &st) == -1) return 0;
  if(!S_ISDIR(st.st_mode)) return 0;

  /* Driver update disk, even if the update dir is empty! */
  strcpy(driver_update_dir, "/update");

  deb_msg("driver update disk");
//  deb_str(dir);

#ifdef USE_VFAT
  fd = open("/dev/ram3", O_RDWR);
  if(fd < 0) return 0;
  fssize = mkdosfs(fd, 8000);
  close(fd);
#endif

//  deb_int(fssize);

  mkdir(driver_update_dir, 0755);

#ifdef USE_VFAT
  i = mount("/dev/ram3", driver_update_dir, "vfat", 0, 0);
#else
  i = mount("shmfs", driver_update_dir, "shm", 0, 0);
#endif

// Why does this not work???
// i = util_try_mount("/dev/ram3", driver_update_dir, MS_MGC_VAL | MS_RDONLY, 0);

//  deb_int(i);

  if(i) return 0;

//  deb_msg("mounted");

  sprintf(inst_dst, "%s/install", driver_update_dir);

  if(
    !stat(inst_src, &st) &&
    S_ISDIR(st.st_mode) &&
    !mkdir(inst_dst, 0755)
  ) {
    deb_msg("install");
    d = opendir(inst_src);
    if(d) {
      while((de = readdir(d))) {
        if(strcmp(de->d_name, ".") && strcmp(de->d_name, "..")) {
          do_file_cp(inst_src, inst_dst, de->d_name);
        }
      }
      closedir(d);
    }
  }

  if(
    !stat(mods_src, &st) &&
    S_ISDIR(st.st_mode)
  ) {
    deb_msg("modules");
    d = opendir(mods_src);
    if(d) {
      while((de = readdir(d))) {
        if(strcmp(de->d_name, ".") && strcmp(de->d_name, "..")) {
          if((s = strstr(de->d_name, ".o")) && !s[2]) {
            sprintf(imod, "%s/%s", mods_src, de->d_name);
            strcpy(rmod, de->d_name); rmod[s - de->d_name] = 0;
            mod_unload_module(rmod);
            mod_load_module(imod, NULL);
          }
        }
      }
      closedir(d);
    }
  }

  return 0;
}

void util_umount_driver_update()
{
  if(!*driver_update_dir) return;

  util_umount(driver_update_dir);
  *driver_update_dir = 0;

  util_free_ramdisk("/dev/ram3");
}


void util_status_info()
{
  char *l[17];		/* WATCH this!!! */
  int i, lc;
  char *s, t[100], t2[100];
  hd_data_t *hd_data;
  char *lxrc;

  hd_data = calloc(1, sizeof *hd_data);
  hd_data->debug = 1;
  hd_scan(hd_data);

  for(i = 0; i < sizeof l / sizeof *l; i++) {
    l[i] = calloc(256, 1);
  }

  lc = 0;

  if(hd_data->log) {
    s = strchr(hd_data->log, '\n');
    if(s) {
      i = s - hd_data->log;
      if(i > 255) i = 255;
      strncpy(l[lc++], hd_data->log, i);
    }
  }

  sprintf(l[lc++],
    "memory = %" PRIu64 ", bootmode = %d, net_config = 0x%x",
    memory_ig, bootmode_ig, valid_net_config_ig
  );
  lxrc = getenv("linuxrc");
  sprintf(l[lc++], "linuxrc = \"%s\"", lxrc ? lxrc : "");
  sprintf(l[lc++],
    "yast = %d, auto = %d, action = 0x%x, splash = %s",
    yast_version_ig,
    auto2_ig ? 2 : auto_ig ? 1 : 0,
    action_ig,
    splash_active ? "on" : "off"
  );
  s = l[lc++];
  strcpy(s, "floppies = (");
  for(i = 0; i < config.floppies; i++) {
    sprintf(t2, "%s\"%s\"%s",
      i ? ", " : " ",
      config.floppy_dev[i],
      i == config.floppy && config.floppies != 1 ? "*" : ""
    );
    strcat(s, t2);
  }
  strcat(s, " )");

  sprintf(l[lc++], "cdrom = \"%s\", suse_cd = %d", cdrom_tg, found_suse_cd_ig);
  sprintf(l[lc++], "driver_update_dir = \"%s\"", driver_update_dir);

  strcpy(t, inet_ntoa(ipaddr_rg));
  s = inet_ntoa(network_rg);
  sprintf(l[lc++], "ip = %s, network = %s", t, s);

  strcpy(t, inet_ntoa(broadcast_rg));
  s = inet_ntoa(netmask_rg);
  sprintf(l[lc++], "broadcast = %s, netmask = %s", t, s);

  strcpy(t, inet_ntoa(gateway_rg));
  s = inet_ntoa(nameserver_rg);
  sprintf(l[lc++], "gateway = %s, nameserver = %s", t, s);

  strcpy(t, inet_ntoa(nfs_server_rg));
  s = inet_ntoa(ftp_server_rg);
  sprintf(l[lc++], "nfs server = %s, ftp server = %s", t, s);

  s = inet_ntoa(plip_host_rg);
  sprintf(l[lc++], "plip host = %s", s);

  sprintf(l[lc++], "language = %d, keymap = \"%s\"", config.language, config.keymap ?: "");

  sprintf(l[lc++], "textmode = %d, yast2update = %d, yast2serial = %d", text_mode_ig, yast2_update_ig, yast2_serial_ig);

  sprintf(l[lc++], "vga = 0x%04x", frame_buffer_mode_ig);

  sprintf(l[lc++], "serial = %d, console = \"%s\", consoleparams = \"%s\"", serial_ig, console_tg, console_parms_tg);

  sprintf(l[lc++],
    "pcmcia = %d, pcmcia_chip = %s",
    auto2_pcmcia(),
    pcmcia_chip_ig == 2 ? "\"i82365\"" : pcmcia_chip_ig == 1 ? "\"tcic\"" : "0"
  );

  for(i = 0; i < lc; i++) {
    util_fill_string(l[i], 76-4);
  }

  dia_show_lines("Linuxrc v" LXRC_FULL_VERSION "/" LX_REL "-" LX_ARCH " (" __DATE__ ", " __TIME__ ")", l, lc, 76, FALSE);

  for(i = 0; i < sizeof l / sizeof *l; i++) free(l[i]);

  hd_free_hd_data(hd_data);
  free(hd_data);
}

void util_get_splash_status()
{
  FILE *f;
  char s[80];

  splash_active = FALSE;

#if 0
  if((f = fopen("/proc/splash", "w"))) {
    fprintf(f, "0x0f01\n");
    fclose(f);
  }
#endif

  if((f = fopen("/proc/splash", "r"))) {
    if(fgets(s, sizeof s, f)) {
      if(strstr(s, ": on")) splash_active = TRUE;
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

void show_proc(FILE *f, unsigned pid)
{
  char pe[100];
  char buf1[64], buf2[2], buf3[256];
  FILE *p;
  DIR *dir;
  struct dirent *de;
  unsigned status = 0;
  unsigned u, v;
  char *s;

#ifdef DIET
  memset(buf1, 0, sizeof buf1);
#endif
  sprintf(pe, "/proc/%u/status", pid);
  if((p = fopen(pe, "r"))) {
    if(fscanf(p, "Name: %63[^\n]", buf1) == 1) status |= 1;
    if(fscanf(p, "\nState: %1s", buf2) == 1) status |= 2;
    fclose(p);
  }

  sprintf(pe, "/proc/%u/cmdline", pid);
  if((p = fopen(pe, "r"))) {
    u = fread(buf3, 1, sizeof buf3, p);
    if(u > sizeof buf3 - 1) u = sizeof buf3 - 1;
    for(v = 0; v < u; v++) if(!buf3[v]) buf3[v] = ' ';
    buf3[u] = 0;
    status |= 4;
    fclose(p);
  }

  if(!(status & 1)) *buf1 = 0;
  if(!(status & 2)) *buf2 = 0;
  if(!(status & 4)) *buf3 = 0;

  fprintf(f, "%5u %s %-16s %s\n", pid, buf2, buf1, buf3);

  sprintf(pe, "/proc/%u/exe", pid);
  if(*(s = read_symlink(pe))) fprintf(f, "  exe: %s\n", s);

  sprintf(pe, "/proc/%u/cwd", pid);
  if(*(s = read_symlink(pe))) fprintf(f, "  cwd: %s\n", s);

  sprintf(pe, "/proc/%u/fd", pid);

  if((dir = opendir(pe))) {
    while((de = readdir(dir))) {
      if(*de->d_name != '.') {
        sprintf(pe, "/proc/%u/fd/%s", pid, de->d_name);
        if(*(s = read_symlink(pe))) fprintf(f, "  fd%s: %s\n", de->d_name, s);
      }
    }
    closedir(dir);
  }
}

void util_ps(FILE *f)
{
  DIR *proc;
  struct dirent *de;
  unsigned u;
  char *s;

  if((proc = opendir("/proc"))) {
    while((de = readdir(proc))) {
      if(de->d_name) {
        u = strtoul(de->d_name, &s, 10);
        if(!*s) show_proc(f, u);
      }
    }
    closedir(proc);
  }
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
  struct stat sbuf;
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

        i = mkdir(dst2, 0755);
        if(i) {
          err = 4;
          perror(dst2);
          break;
        }

        rec_level++;
        err = do_cp(src2, dst2);
        rec_level--;

        if(err) break;
      }

      else if(S_ISREG(sbuf.st_mode)) {
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
    fclose(stdin);
    fclose(stdout);
    fclose(stderr);
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
  util_ps(stderr);

  return 0;
}


int util_mount_main(int argc, char **argv)
{
  int i;
  char *dir, *srv_dir;
  char *mp_tmp = mountpoint_tg;
  char *type = NULL, *dev;

  argv++; argc--;

  if(argc != 3 && argc != 4) return 1;

  if(strstr(*argv, "-t") == *argv) {
    type = *argv + 2;
  }

  if(type && !*type) {
    type = *++argv;
  }

  dev = strcmp(argv[1], "none") ? argv[1] : NULL;

  dir = argv[2];

  if(!type) return 2;

  if(strcmp(type, "nfs")) {
    if(!mount(dev, dir, type, 0, 0)) return 0;
    perror("mount");
    return errno;
  }

  srv_dir = strchr(dev, ':');
  if(!srv_dir) {
    fprintf(stderr, "invalid mount src \"%s\"\n", dev);
    return 77;
  }

  *srv_dir++ = 0;

  mountpoint_tg = dir;

  i = net_mount_nfs(dev, srv_dir);

  mountpoint_tg = mp_tmp;

  return i;
}


int util_umount_main(int argc, char **argv)
{
  argv++; argc--;

  if(argc != 1) return 1;

  if(!umount(*argv)) return 0;

  perror(*argv);

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

  if(argc >= 1 && !strcmp(*argv, "-a")) {
    argv++; argc--;
    rec = 1;
  }

  if(argc >= 1 && !strcmp(*argv, "-p")) {
    argv++; argc--;
    preserve = 1;
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
      perror(*argv);
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
    fprintf(stderr, "mv %s %s failed\n", argv[0], argv[1]);
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
    perror(*argv);
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
    perror(*argv);
  }

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


slist_t *slist_split(char *text)
{
  slist_t *sl0 = NULL, *sl;
  int i, len;
  char *s;

  if(!text) return NULL;

  text = strdup(text);
  len = strlen(text);

  for(i = 0; i < len; i++) if(isspace(text[i])) text[i] = 0;

  for(s = text; s < text + len; s++) {
    if(*s && (s == text || !s[-1])) {
      sl = slist_append(&sl0, slist_new());
      sl->key = strdup(s);
    }
  }

  free(text);

  return sl0;
}

