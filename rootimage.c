/*
 *
 * rootimage.c   Loading of rootimage
 *
 * Copyright (c) 1996-2001  Hubert Mantel, SuSE GmbH  (mantel@suse.de)
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

#include "global.h"
#include "text.h"
#include "util.h"
#include "dialog.h"
#include "window.h"
#include "display.h"
#include "rootimage.h"
#include "module.h"
#include "ftp.h"
#include "linuxrc.h"


#define BLOCKSIZE        10240

typedef struct
    {
    char *dev_name;
    int   major;
    int   minor;
    } device_t;

static device_t root_devices_arm [] =
{
    { "hda",           3,   0 },
    { "hdb",           3,  64 },
    { "hdc",          22,   0 },
    { "hdd",          22,  64 },
    { "hde",          33,   0 },
    { "hdf",          33,  64 },
    { "hdg",          34,   0 },
    { "hdh",          34,  64 },
    { "hdi",          56,   0 },
    { "hdj",          56,  64 },
    { "hdk",          57,   0 },
    { "hdl",          57,  64 },
    { "sda",           8,   0 },
    { "sdb",           8,  16 },
    { "sdc",           8,  32 },
    { "sdd",           8,  48 },
    { "sde",           8,  64 },
    { "sdf",           8,  80 },
    { "sdg",           8,  96 },
    { "sdh",           8, 112 },
    { "sdi",           8, 128 },
    { "sdj",           8, 144 },
    { "sdk",           8, 160 },
    { "sdl",           8, 176 },
    { "sdm",           8, 192 },
    { "sdn",           8, 208 },
    { "sdo",           8, 224 },
    { "sdp",           8, 240 },
    { "rd/c0d0p",     48,   0 },
    { "rd/c0d1p",     48,   8 },
    { "rd/c0d2p",     48,  16 },
    { "rd/c0d3p",     48,  24 },
    { "rd/c0d4p",     48,  32 },
    { "rd/c0d5p",     48,  40 },
    { "rd/c0d6p",     48,  48 },
    { "rd/c0d7p",     48,  56 },
    { "rd/c1d0p",     49,   0 },
    { "rd/c1d1p",     49,   8 },
    { "rd/c1d2p",     49,  16 },
    { "rd/c1d3p",     49,  24 },
    { "rd/c1d4p",     49,  32 },
    { "rd/c1d5p",     49,  40 },
    { "rd/c1d6p",     49,  48 },
    { "rd/c1d7p",     49,  56 },
    { "ida/c0d0p",    72,   0 },
    { "ida/c0d1p",    72,  16 },
    { "ida/c0d2p",    72,  32 },
    { "ida/c0d3p",    72,  48 },
    { "ida/c0d4p",    72,  64 },
    { "ida/c0d5p",    72,  80 },
    { "ida/c0d6p",    72,  96 },
    { "ida/c0d7p",    72, 112 },
    { "ida/c1d0p",    73,   0 },
    { "ida/c1d1p",    73,  16 },
    { "ida/c1d2p",    73,  32 },
    { "ida/c1d3p",    73,  48 },
    { "ida/c1d4p",    73,  64 },
    { "ida/c1d5p",    73,  80 },
    { "ida/c1d6p",    73,  96 },
    { "ida/c1d7p",    73, 112 },
    { "cciss/c0d0p", 104,   0 },
    { "cciss/c0d1p", 104,  16 },
    { "cciss/c0d2p", 104,  32 },
    { "cciss/c0d3p", 104,  48 },
    { "cciss/c0d4p", 104,  64 },
    { "cciss/c0d5p", 104,  80 },
    { "cciss/c0d6p", 104,  96 },
    { "cciss/c0d7p", 104, 112 },
    { "cciss/c1d0p", 105,   0 },
    { "cciss/c1d1p", 105,  16 },
    { "cciss/c1d2p", 105,  32 },
    { "cciss/c1d3p", 105,  48 },
    { "cciss/c1d4p", 105,  64 },
    { "cciss/c1d5p", 105,  80 },
    { "cciss/c1d6p", 105,  96 },
    { "cciss/c1d7p", 105, 112 },
    { "ram",         1,   0 },
    { "md0",         9,   0 },
    { "md1",         9,   1 },
    { "md2",         9,   2 },
    { "md3",         9,   3 },
    { 0,             0,   0 }
};

static int       root_nr_blocks_im;
static window_t  root_status_win_rm;
static int       root_infile_im;
static int       root_outfile_im;


static int  root_check_root      (char *root_string_tv);
static void root_update_status   (int  blocks_iv);
static void root_load_compressed (void);

static int root_is_reiserfs(char *dev);
static int root_reiserfs_loaded(void);

#define REISERFS_SUPER_MAGIC_STRING "ReIsErFs"
#define REISER2FS_SUPER_MAGIC_STRING "ReIsEr2Fs"

int root_is_reiserfs(char *dev)
{
  int fd;
  char magic0[11], magic1[11];

  fd = open(dev, O_RDONLY);

  if(fd < 0) return 0;

  if(
    lseek(fd, 0x2034, SEEK_SET) != 0x2034 ||
    read(fd, magic0, sizeof magic0) != sizeof magic0
  ) *magic0 = 0;

  if(
    lseek(fd, 0x10034, SEEK_SET) != 0x10034 ||
    read(fd, magic0, sizeof magic1) != sizeof magic1
  ) *magic1 = 0;

  magic0[sizeof magic0 -1] = magic1[sizeof magic1 -1] = 0;

  close(fd);

  if(
    !strcmp(magic0, REISERFS_SUPER_MAGIC_STRING) ||
    !strcmp(magic0, REISER2FS_SUPER_MAGIC_STRING) ||
    !strcmp(magic1, REISERFS_SUPER_MAGIC_STRING) ||
    !strcmp(magic1, REISER2FS_SUPER_MAGIC_STRING)
  ) {
    return 1;
  }

  return 0;
}

int root_reiserfs_loaded()
{
  FILE *f;
  char s[100];
  int ok = 0;

  if((f = fopen("/proc/filesystems", "r"))) {
    while(fgets(s, sizeof s, f)) {
      if(strstr(s, "reiserfs")) {
        ok = 1;
        break;
      }
    }
    fclose(f);
  }

  return ok;
}



int root_load_rootimage (char *infile_tv)
    {
    char  buffer_ti [BLOCKSIZE];
    int   bytes_read_ii;
    int   rc_ii;
    int   current_block_ii;
    int   compressed_ii;
    int32_t filesize_li;
    int   error_ii = FALSE;
    int   socket_ii = -1;


    fprintf (stderr, "Loading Image \"%s\"%s\n", infile_tv, auto2_ig ? "" : "...");
    mod_free_modules ();
    if (config.instmode == inst_floppy || config.instmode == inst_ftp)
        {
        if (config.instmode == inst_floppy)
            root_nr_blocks_im = (int) ((4000L * 1024L) / BLOCKSIZE);
        else
            root_nr_blocks_im = (int) ((11151L * 1024L) / BLOCKSIZE);
        compressed_ii = TRUE;
        sprintf (buffer_ti, "%s%s", txt_get (TXT_LOADING), auto2_ig ? "" : "...");
        }
    else
        {
        rc_ii = util_fileinfo (infile_tv, &filesize_li, &compressed_ii);
        if (rc_ii)
            {
            if (!config.suppress_warnings)
                dia_message (txt_get (TXT_RI_NOT_FOUND), MSGTYPE_ERROR);
            return (rc_ii);
            }

        root_nr_blocks_im = (int) (filesize_li / BLOCKSIZE);

        sprintf (buffer_ti, "%s (%d kB)%s", txt_get (TXT_LOADING),
                 (int) ((long) root_nr_blocks_im * BLOCKSIZE / 1024L),
                 auto2_ig ? "" : "...");
        }

    dia_status_on (&root_status_win_rm, buffer_ti);

    if (config.instmode == inst_ftp)
        {
        socket_ii = util_open_ftp (inet_ntoa (config.net.server.ip));
        if (socket_ii < 0)
            {
            util_print_ftp_error (socket_ii);
            error_ii = TRUE;
            }
        else
            {
            root_infile_im = ftpGetFileDesc (socket_ii, infile_tv);
            if (root_infile_im < 0)
                {
                util_print_ftp_error (root_infile_im);
                error_ii = TRUE;
                }
            }
        }
    else
        {
        root_infile_im = open (infile_tv, O_RDONLY);
        if (root_infile_im < 0)
            error_ii = TRUE;
        }

    root_outfile_im = open (RAMDISK_2, O_RDWR | O_CREAT | O_TRUNC, 0644);
    if (root_outfile_im < 0)
        error_ii = TRUE;

    if (error_ii)
        {
        close (root_infile_im);
        close (root_outfile_im);
        win_close (&root_status_win_rm);
        return (-1);
        }

    if (compressed_ii)
        root_load_compressed ();
    else
        {
        current_block_ii = 0;
        bytes_read_ii = read (root_infile_im, buffer_ti, BLOCKSIZE);
        while (bytes_read_ii > 0)
            {
            rc_ii = write (root_outfile_im, buffer_ti, bytes_read_ii);
            if (rc_ii != bytes_read_ii)
                return (-1);

            root_update_status (++current_block_ii);

            bytes_read_ii = read (root_infile_im, buffer_ti, BLOCKSIZE);
            }
        }

    (void) close (root_infile_im);
    (void) close (root_outfile_im);

    if (config.instmode == inst_ftp)
        {
        (void) ftpGetFileDone (socket_ii);
        ftpClose (socket_ii);
        }

    win_close (&root_status_win_rm);
    root_set_root (RAMDISK_2);

    if (config.instmode == inst_floppy)
        dia_message (txt_get (TXT_REMOVE_DISK), MSGTYPE_INFO);

    return (0);
    }


static int root_check_root (char *root_string_tv)
    {
    char        *tmp_string_pci;
    char         device_ti [50];
    char         filename_ti [MAX_FILENAME];
    int          rc_ii;
    char        *compare_file_pci;


    if (!strncmp ("/dev/", root_string_tv, 5))
        tmp_string_pci = root_string_tv + 5;
    else
        tmp_string_pci = root_string_tv;

    sprintf (device_ti, "/dev/%s", tmp_string_pci);
    if (!mount (device_ti, mountpoint_tg, "ext2", MS_MGC_VAL | MS_RDONLY, 0))
        compare_file_pci = "/etc/passwd";
    else if (!mount (device_ti, mountpoint_tg, "reiserfs", MS_MGC_VAL | MS_RDONLY, 0))
        compare_file_pci = "/etc/passwd";
    else if (!mount (device_ti, mountpoint_tg, "msdos", MS_MGC_VAL | MS_RDONLY, 0))
        compare_file_pci = "/linux/etc/passwd";
    else
        return (-1);
        
    sprintf (filename_ti, "%s/%s", mountpoint_tg, compare_file_pci);
    rc_ii = util_check_exist (filename_ti);
    umount (mountpoint_tg);

    if (rc_ii == TRUE)
        return (0);
    else
        return (-1);
    }


void root_set_root (char *root_string_tv)
    {
    FILE  *proc_root_pri;
    int    root_ii;
    char  *tmp_string_pci;
    int    found_ii = FALSE;
    int    i_ii = 0;


    lxrc_new_root = strdup (root_string_tv);
    if (!strncmp ("/dev/", root_string_tv, 5))
        tmp_string_pci = root_string_tv + 5;
    else
        tmp_string_pci = root_string_tv;

    while (!found_ii && root_devices_arm [i_ii].dev_name)
        if (!strncmp (tmp_string_pci, root_devices_arm [i_ii].dev_name,
                      strlen (root_devices_arm [i_ii].dev_name)))
            found_ii = TRUE;
        else
            i_ii++;

    if (!found_ii)
        return;

    root_ii = root_devices_arm [i_ii].major * 256 +
              root_devices_arm [i_ii].minor +
              atoi (tmp_string_pci + strlen (root_devices_arm [i_ii].dev_name));

    root_ii *= 65537;

    proc_root_pri = fopen ("/proc/sys/kernel/real-root-dev", "w");
    if (!proc_root_pri)
        return;

    fprintf (proc_root_pri, "%d\n", root_ii);
    fclose (proc_root_pri);
    }


int root_boot_system (void)
    {
    int  rc_ii;
    char root_ti [40];


    strcpy (root_ti, "/dev/");

    do
        {
        rc_ii = dia_input (txt_get (TXT_ENTER_ROOT_FS), root_ti, sizeof root_ti - 1, 25);
        if (rc_ii)
            return (rc_ii);

        if(
          root_is_reiserfs(root_ti) &&
          !root_reiserfs_loaded()
        ) {
          if(!util_check_exist("modules/reiserfs.o")) {
            char s[200];

            sprintf(s, "%s\n\n%s", txt_get(TXT_REISERFS), txt_get(TXT_ENTER_MODDISK));

            rc_ii = dia_okcancel(s, YES) == YES ? 1 : 0;

            if(rc_ii) mod_add_disk(0, mod_get_type("file system"));
          }

          mod_load_module("reiserfs", NULL);
        }

        rc_ii = root_check_root (root_ti);
        if (rc_ii)
            dia_message (txt_get (TXT_INVALID_ROOT_FS), MSGTYPE_ERROR);
        }
    while (rc_ii);
    root_set_root (root_ti);
    return (0);
    }


static void root_update_status (int  blocks_iv)
    {
    static int  old_percent_is;
    int         percent_ii;

    percent_ii = (blocks_iv * 100) / root_nr_blocks_im;
    if (percent_ii != old_percent_is)
        {
        dia_status (&root_status_win_rm, percent_ii);
        old_percent_is = percent_ii;
        }
    }


/* --------------------------------- GZIP -------------------------------- */

#define OF(args)  args

#define memzero(s, n)     memset ((s), 0, (n))

typedef unsigned char  uch;
typedef unsigned short ush;
typedef unsigned long  ulg;

#define INBUFSIZ 4096
#define WSIZE 0x8000    /* window size--must be a power of two, and */
                        /*  at least 32K for zip's deflate method */

static uch *inbuf;
static uch *window;

static unsigned insize = 0;  /* valid bytes in inbuf */
static unsigned inptr = 0;   /* index of next byte to be processed in inbuf */
static unsigned outcnt = 0;  /* bytes in output buffer */
static int exit_code = 0;
static long bytes_out = 0;

#define get_byte()  (inptr < insize ? inbuf[inptr++] : fill_inbuf())
                
/* Diagnostic functions (stubbed out) */
#define Assert(cond,msg)
#define Trace(x)
#define Tracev(x)
#define Tracevv(x)
#define Tracec(c,x)
#define Tracecv(c,x)

#define STATIC static

static int  fill_inbuf(void);
static void flush_window(void);
static void error(char *m);
static void gzip_mark(void **);
static void gzip_release(void **);

#include "inflate.c"

static void gzip_mark(void **ptr)
{
}

static void gzip_release(void **ptr)
{
}


/* ===========================================================================
 * Fill the input buffer. This is called only when the buffer is empty
 * and at least one byte is really needed.
 */
static int fill_inbuf()
{
fd_set emptySet, readSet;
struct timeval timeout;
int rc;

        if (config.instmode == inst_ftp)
            {
            FD_ZERO(&emptySet);
            FD_ZERO(&readSet);
            FD_SET(root_infile_im, &readSet);

            timeout.tv_sec = TIMEOUT_SECS;
            timeout.tv_usec = 0;

            rc = select (root_infile_im + 1, &readSet, &emptySet,
                         &emptySet, &timeout);

            if (rc <= 0)
                {
                util_print_ftp_error (rc);
                exit_code = 1;
                insize = INBUFSIZ;
                inptr = 1;
                return (-1);
                }
            }

        insize = read (root_infile_im, inbuf, INBUFSIZ);

        if (insize == 0) return -1;

        inptr = 1;

        return inbuf[0];
}

/* ===========================================================================
 * Write the output window window[0..outcnt-1] and update crc and bytes_out.
 * (Used for the decompressed data only.)
 */
static void flush_window()
{
    ulg c = crc;         /* temporary variable */
    unsigned n;
    uch *in, ch;

    if (exit_code)
        {
        fprintf (stderr, ".");
        fflush (stdout);
        bytes_out += (ulg)outcnt;
        outcnt = 0;
        return;
        }
    
    write (root_outfile_im, window, outcnt);
    in = window;
    for (n = 0; n < outcnt; n++) {
            ch = *in++;
            c = crc_32_tab[((int)c ^ ch) & 0xff] ^ (c >> 8);
    }
    crc = c;
    bytes_out += (ulg)outcnt;
    root_update_status ((int) (bytes_out / BLOCKSIZE));
    outcnt = 0;
}

static void error(char *x)
{
        printf(x);
        exit_code = 1;
}

static void root_load_compressed (void)
    {
    inbuf = (uch *) malloc (INBUFSIZ);
    window = (uch *) malloc (WSIZE);
    insize = 0;
    inptr = 0;
    outcnt = 0;
    exit_code = 0;
    bytes_out = 0;
    crc = 0xffffffffL;

    makecrc ();
    gunzip ();

    free (inbuf);
    free (window);
    }
