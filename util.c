/*
 *
 * util.c        Utility functions for linuxrc
 *
 * Copyright (c) 1996-1999  Hubert Mantel, SuSE GmbH  (mantel@suse.de)
 *
 */

#define __LIBRARY__

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mount.h>
#include <sys/ioctl.h>
#include <sys/kd.h>
#include <sys/utsname.h>
#include <fcntl.h>
#include <syscall.h>
#include <errno.h>

#include <linux/loop.h>

#include "global.h"
#include "display.h"
#include "util.h"
#include "window.h"
#include "module.h"
#include "modparms.h"
#include "keyboard.h"
#include "text.h"
#include "dialog.h"
#include "ftp.h"

#define LED_TIME     50000

static inline _syscall3 (int,syslog,int,type,char *,b,int,len);

static char  *util_loopdev_tm = "/dev/loop0";

void util_redirect_kmsg (void)
    {
    int  fd_ii;
    char arg_aci [2];


    fd_ii = open (console_tg, O_RDONLY);
    if (fd_ii)
        {
        arg_aci [0] = 11;
        arg_aci [1] = 4;

        ioctl (fd_ii, TIOCLINUX, &arg_aci);
        close (fd_ii);
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
                    pos_ii--;
                    i_ii--;
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
            i_ii++;
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
        i_ii++;

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
        }
    }


void util_free_items (item_t items_arr [], int nr_iv)
    {
    int  i_ii;

    for (i_ii = 0; i_ii < nr_iv; i_ii++)
        free (items_arr [i_ii].text);
    }


int util_fileinfo (char *file_tv, long *size_plr, int *compressed_pir)
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
        return (-1L);
        }

    if (buf_ti [0] == 037 && (buf_ti [1] == 0213 || buf_ti [1] == 0236))
        {
        if ((long) lseek (handle_ii, (off_t) -4, SEEK_END) == -1L)
            return (-1L);

        read (handle_ii, (char *) size_plr, sizeof (long));
        *compressed_pir = TRUE;
        }
    else
        {
        *size_plr = (long) lseek (handle_ii, (off_t) 0, SEEK_END);
        *compressed_pir = FALSE;
        }

    close (handle_ii);
    return (0);
    }


void util_update_kernellog (void)
    {
    FILE  *outfile_pri;
    FILE  *lastfile_pri;
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

    size_ii = syslog (3, buffer_ti, sizeof (buffer_ti));
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

        i_ii++;
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
    sprintf (text_ti, ">>> Linuxrc v1.0b (Kernel %s) (c) 1996-99 SuSE GmbH <<<",
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

    if (strlen (dir_tr) > 4 && !strcmp (&dir_tr [strlen (dir_tr) - 5], "/suse"))
        dir_tr [strlen (dir_tr) - 5] = 0;

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


int util_try_mount (char *device_pcv,             char *dir_pcv,
                    unsigned long flags_lv, const void *data_prv)
    {
    static  char *fs_types_ats [] = { "minix",   "ext2",  "reiserfs", "msdos",
                                      "iso9660", "hpfs",  0 };
            int   i_ii;
            int   rc_ii;


    if (!device_pcv || !device_pcv [0])
        return (-1);

    i_ii = 0;
    do
        rc_ii = mount (device_pcv, dir_pcv, fs_types_ats [i_ii++],
                       flags_lv, data_prv);
    while (rc_ii && fs_types_ats [i_ii]);

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
        ioctl (fd_ii, BLKFLSBUF);
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
