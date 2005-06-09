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
#include "ftp.h"
#include "linuxrc.h"
#include "install.h"

#include "linux_fs.h"


#define BLOCKSIZE	10240
#define BLOCKSIZE_KB	(BLOCKSIZE >> 10)

static int       root_nr_blocks_im;
static window_t  root_status_win_rm;
static int       root_infile_im;
static int       root_outfile_im;

static struct {
  int rd;
} image = { rd: -1 };

#define fd_read		root_infile_im

static int  root_check_root      (char *root_string_tv);
static void root_update_status(int block);
static int fill_inbuf(void);
static void flush_window(void);
static void error(char *msg);
static int root_load_compressed(void);


int ramdisk_open()
{
  int i;

  for(i = 0; (unsigned) i < sizeof config.ramdisk / sizeof *config.ramdisk; i++) {
    if(!config.ramdisk[i].inuse) {
      config.ramdisk[i].fd = open(config.ramdisk[i].dev, O_RDWR | O_CREAT | O_TRUNC, 0644);
      if(config.ramdisk[i].fd < 0) {
        perror(config.ramdisk[i].dev);
        return -1;
      }
      config.ramdisk[i].inuse = 1;

      return i;
    }
  }

  fprintf(stderr, "error: no free ramdisk!\n");

  return -1;
}


void ramdisk_close(int rd)
{
  if(rd < 0 || (unsigned) rd >= sizeof config.ramdisk / sizeof *config.ramdisk) return;

  if(!config.ramdisk[rd].inuse) return;

  if(config.ramdisk[rd].fd >= 0) {
    close(config.ramdisk[rd].fd);
    config.ramdisk[rd].fd = -1;
  }
}


void ramdisk_free(int rd)
{
  int i;

  if(rd < 0 || (unsigned) rd >= sizeof config.ramdisk / sizeof *config.ramdisk) return;

  if(!config.ramdisk[rd].inuse) return;

  ramdisk_close(rd);

  ramdisk_umount(rd);

  i = util_free_ramdisk(config.ramdisk[rd].dev);

  if(!i) {
    config.ramdisk[rd].inuse = 0;
    config.ramdisk[rd].size = 0;
    str_copy(&config.ramdisk[rd].mountpoint, NULL);
    util_update_meminfo();
  }
}


int ramdisk_write(int rd, void *buf, int count)
{
  int i;

  if(
    rd < 0 ||
    (unsigned) rd >= sizeof config.ramdisk / sizeof *config.ramdisk ||
    !config.ramdisk[rd].inuse
  ) {
    fprintf(stderr, "oops: trying to write to invalid ramdisk %d\n", rd);
    return -1;
  }

  util_update_meminfo();

  if(ask_for_swap((count + 0x3ff) >> 10, txt_get(TXT_LOW_MEMORY1))) return -1;

  i = write(config.ramdisk[rd].fd, buf, count);

  if(i >= 0) config.ramdisk[rd].size += i;

  return i;
}


int ramdisk_umount(int rd)
{
  int i;

  if(rd < 0 || (unsigned) rd >= sizeof config.ramdisk / sizeof *config.ramdisk) return -1;

  if(!config.ramdisk[rd].inuse) return -1;

  if(!config.ramdisk[rd].mountpoint) return 0;

  i = util_umount(config.ramdisk[rd].mountpoint);
  if(!i || errno == ENOENT || errno == EINVAL) {
    str_copy(&config.ramdisk[rd].mountpoint, NULL);
  }

  return i;
}


int ramdisk_mount(int rd, char *dir)
{
  int i;

  if(rd < 0 || (unsigned) rd >= sizeof config.ramdisk / sizeof *config.ramdisk) return -1;

  if(!config.ramdisk[rd].inuse) return -1;

  if(config.ramdisk[rd].mountpoint) return -1;

  if(!(i = util_mount_ro(config.ramdisk[rd].dev, dir))) {
    str_copy(&config.ramdisk[rd].mountpoint, dir);
  }
  else {
    fprintf(stderr, "mount: %s: %s\n", config.ramdisk[rd].dev, strerror(errno));
  }

  return i;
}


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
  int i, did_init = 0;
  char tmp[256];
  char *partition = NULL;
  char *argv[] = { NULL, tmp };

  if(config.memory.current >= config.memory.min_free + size) return 0;

  if(!config.win) {
    util_disp_init();
    did_init = 1;
  }
  sprintf(tmp, "%s\n\n%s", msg, txt_get(TXT_ADD_SWAP));
  i = dia_contabort(tmp, YES);
  util_free_mem();
  if(i != YES) {
    if(did_init) util_disp_done();
    return -1;
  }

  if(config.memory.current >= config.memory.min_free + size) {
    if(did_init) util_disp_done();
    return 0;
  }

  do {
    if(inst_choose_partition(&partition, 1, txt_get(TXT_CHOOSE_SWAP), txt_get(TXT_ENTER_SWAP))) {
      i = -1;
      break;
    }

    if(partition) {
      sprintf(tmp, "/dev/%s", partition);
      i = util_swapon_main(2, argv);
      if(i) {
        dia_message(txt_get(TXT_ERROR_SWAP), MSGTYPE_ERROR);
      }
    }
    util_free_mem();
  }
  while(i);

  str_copy(&partition, NULL);

  if(did_init) util_disp_done();

  return i;
}


/*
 * returns ramdisk index if successful
 */
int load_image(char *file_name, instmode_t mode, char *label)
{
  char buffer[BLOCKSIZE], cramfs_name[17];
  int bytes_read, current_pos;
  int i, rc, compressed = 0;
  int err = 0, got_size = 0;
  char *real_name = NULL;
  char *buf2;
  struct cramfs_super_block *cramfssb;

  fprintf(stderr, "Loading image \"%s\"%s\n", file_name, config.win ? "" : "...");

  /* check if have to actually _load_ the image to get info about it */
  if(mode == inst_ftp || mode == inst_http || mode == inst_tftp) {
    mode = inst_net;
  }

  /* assume 10MB */
  root_nr_blocks_im = 10240 / BLOCKSIZE_KB;

  image.rd = -1;

  if(mode != inst_floppy && mode != inst_net) {
    rc = util_fileinfo(file_name, &i, &compressed);
    if(rc) {
      if(!config.suppress_warnings && !config.noerrors) {
        dia_message(txt_get(TXT_RI_NOT_FOUND), MSGTYPE_ERROR);
      }
      return -1;
    }

    got_size = 1;
    root_nr_blocks_im = i / BLOCKSIZE;
  }

  if(mode == inst_net) {
    fd_read = net_open(file_name);
    if(fd_read < 0) {
      if(!config.noerrors) util_print_net_error();
      err = 1;
    }
  }
  else {
    fd_read = open(file_name, O_RDONLY);
    if(fd_read < 0) err = 1;
  }

  if((image.rd = ramdisk_open()) < 0) {
    if(!config.noerrors) dia_message("No usable ramdisk found.", MSGTYPE_ERROR);
    err = 1;
  }

  if(err) {
    net_close(fd_read);
    ramdisk_free(image.rd);
    return -1;
  }

  if(!got_size) {
    /* read maybe more to check super block? */
    buf2 = malloc(config.cache.size = 256);
    config.cache.cnt = 0;

    for(i = 0; (unsigned) i < config.cache.size; i += bytes_read) {
      bytes_read = net_read(fd_read, buf2 + i, config.cache.size - i);
      // fprintf(stderr, "got %d bytes\n", bytes_read);
      if(bytes_read <= 0) break;
    }

    config.cache.buf = buf2;

    if(i) config.cache.size = i;

#if 0
    fprintf(stderr, "cache: %d\n", config.cache.size);
    for(i = 0; i < 32; i++) {
      fprintf(stderr, " %02x", (unsigned char) buf2[i]);
    }
    fprintf(stderr, "\n");
#endif

    if(config.cache.size > 64) {
      if(buf2[0] == 0x1f && (unsigned char) buf2[1] == 0x8b) {
        /* gzip'ed image */
        compressed = 1;

        // fprintf(stderr, "compressed\n");

        if((buf2[3] & 0x08)) {
          real_name = buf2 + 10;
          for(i = 0; i < (int) config.cache.size - 10 && i < 128; i++) {
            if(!real_name[i]) break;
          }
          if(i > 128) real_name = NULL;
        }
      }
      else {
        cramfssb = (struct cramfs_super_block *) config.cache.buf;
        if(cramfsmagic((*cramfssb)) == CRAMFS_SUPER_MAGIC) {
          /* cramfs */
          memcpy(cramfs_name, config.cache.buf + 0x30, sizeof cramfs_name - 1);
          cramfs_name[sizeof cramfs_name - 1] = 0;
          real_name = cramfs_name;
          fprintf(stderr, "cramfs: \"%s\"\n", real_name);
        }
      }
    }

    if(real_name) {
      // fprintf(stderr, "file name: \"%s\"\n", real_name);

      i = 0;
      if(sscanf(real_name, "%*s %d", &i) >= 1) {
        if(i > 0) {
          root_nr_blocks_im = i / BLOCKSIZE_KB;
          got_size = 1;
          fprintf(stderr, "image size: %d kB\n", i);
        }
      }
    }

    if(!compressed && !got_size) {
      /* check fs superblock */
    }

  }

  if(got_size) {
    if(ask_for_swap(root_nr_blocks_im * (BLOCKSIZE >> 10), txt_get(TXT_LOW_MEMORY1))) {
      net_close(fd_read);
      ramdisk_free(image.rd);
      return image.rd = -1;
    }

    sprintf(buffer, "%s %s (%d kB)%s",
      "Loading",	// txt_get(TXT_LOADING)
      label,
      root_nr_blocks_im * BLOCKSIZE_KB,
      config.win ? "" : " -     "
    );
  }
  else {
    sprintf(buffer, "%s %s%s", "Loading" /* txt_get(TXT_LOADING) */, label, config.win ? "" : " -     ");
  }

  dia_status_on(&root_status_win_rm, buffer);

  if(compressed) {
    err = root_load_compressed();
  }
  else {
    current_pos = 0;
    while((bytes_read = net_read(fd_read, buffer, BLOCKSIZE)) > 0) {
      rc = ramdisk_write(image.rd, buffer, bytes_read);
      if(rc != bytes_read) {
        err = 1;
        break;
      }
      root_update_status((current_pos += bytes_read) / BLOCKSIZE);
    }
  }

  net_close(fd_read);
  ramdisk_close(image.rd);

  dia_status_off(&root_status_win_rm);

  if(err) {
    fprintf(stderr, "error loading ramdisk\n");
    ramdisk_free(image.rd);
    image.rd = -1;
  }
  else if(config.instmode == inst_floppy) {
    dia_message(txt_get(TXT_REMOVE_DISK), MSGTYPE_INFO);
  }

  return image.rd;
}


int root_check_root(char *root_string_tv)
{
  char buf[256];
  int rc;

  if(strstr(root_string_tv, "/dev/") == root_string_tv) {
    root_string_tv += sizeof "/dev/" - 1;
  }

  sprintf(buf, "/dev/%s", root_string_tv);

  if(util_mount_ro(buf, config.mountpoint.instdata)) return -1;

  sprintf(buf, "%s/etc/passwd", config.mountpoint.instdata);
  rc = util_check_exist(buf);

  umount(config.mountpoint.instdata);

  return rc == 'r' ? 0 : -1;
}


void root_set_root(char *dev)
{
  FILE  *f;
  int root;
  struct stat sbuf;

  str_copy(&config.new_root, dev);

  if(stat(dev, &sbuf) || !S_ISBLK(sbuf.st_mode)) {
    fprintf(stderr, "new root: %s\n", config.new_root);
    return;
  }

  root = (major(sbuf.st_rdev) << 8) + minor(sbuf.st_rdev);
#if 0
  root *= 0x10001;
#endif

  fprintf(stderr,
    "new root: %s (major 0x%x, minor 0x%x)\n",
    config.new_root, major(sbuf.st_rdev), minor(sbuf.st_rdev)
  );

  if(!(f = fopen ("/proc/sys/kernel/real-root-dev", "w"))) return;
  fprintf(f, "%d\n", root);
  fclose(f);
}


int root_boot_system()
{
  int  rc, mtype;
  char *module, *type;
  char buf[256], root[64];

  do {
    rc = inst_choose_partition(&config.partition, 0, txt_get(TXT_CHOOSE_ROOT_FS), txt_get(TXT_ENTER_ROOT_FS));
    if(rc || !config.partition) return -1;
    sprintf(root, "/dev/%s", config.partition);

    if((type = util_fstype(root, &module))) {
      if(module && config.module.dir) {
        sprintf(buf, "%s/%s" MODULE_SUFFIX, config.module.dir, module);
        if(!util_check_exist(buf)) {
          mtype = mod_get_type("file system");

          sprintf(buf, txt_get(TXT_FILE_SYSTEM), type);
          strcat(buf, "\n\n");
          mod_disk_text(buf + strlen(buf), mtype);

          rc = dia_okcancel(buf, YES) == YES ? 1 : 0;

          if(rc) mod_add_disk(0, mtype);
        }

        mod_modprobe(module, NULL);
      }
    }

    if((rc = root_check_root(root))) {
      dia_message(txt_get(TXT_INVALID_ROOT_FS), MSGTYPE_ERROR);
    }
  }
  while(rc);

  root_set_root(root);

  return 0;
}


void root_update_status(int block)
{
  static int old_percent_is;
  int percent;

  percent = (block * 100) / (root_nr_blocks_im ?: 1);
  if(percent != old_percent_is) {
    dia_status(&root_status_win_rm, old_percent_is = percent);
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
int fill_inbuf()
{
  fd_set emptySet, readSet;
  struct timeval timeout;
  int rc;

  if(
    config.instmode == inst_ftp ||
    config.instmode == inst_http
    /* !!! NOT '|| config.instmode == inst_tftp' !!! */
  ) {
    FD_ZERO(&emptySet);
    FD_ZERO(&readSet);
    FD_SET(root_infile_im, &readSet);

    timeout.tv_sec = TIMEOUT_SECS;
    timeout.tv_usec = 0;

    rc = select(root_infile_im + 1, &readSet, &emptySet, &emptySet, &timeout);

    if(rc <= 0) {
      util_print_net_error();
      exit_code = 1;
      insize = INBUFSIZ;
      inptr = 1;
      return -1;
    }
  }

  insize = net_read(root_infile_im, inbuf, INBUFSIZ);

  if(insize <= 0) {
    exit_code = 1;
    return -1;
  }

  inptr = 1;

  return inbuf[0];
}


/* ===========================================================================
 * Write the output window window[0..outcnt-1] and update crc and bytes_out.
 * (Used for the decompressed data only.)
 */
void flush_window()
{
  ulg c = crc;		/* temporary variable */
  unsigned n;
  uch *in, ch;
  int i;

  if(exit_code) {
    fprintf(stderr, ".");
    fflush(stderr);
    bytes_out += (ulg) outcnt;
    outcnt = 0;
    return;
  }
    
  if(image.rd >= 0) {
    i = ramdisk_write(image.rd, window, outcnt);
    if(i < 0) exit_code = 1;
  }
  else {
    write(root_outfile_im, window, outcnt);
  }

  in = window;
  for(n = 0; n < outcnt; n++) {
    ch = *in++;
    c = crc_32_tab[((int)c ^ ch) & 0xff] ^ (c >> 8);
  }
  crc = c;

  bytes_out += (ulg) outcnt;
  root_update_status(bytes_out / BLOCKSIZE);
  outcnt = 0;
}


void error(char *msg)
{
  fprintf(stderr, "%s\n", msg);
  exit_code = 1;
}


int root_load_compressed()
{
  int err;

  inbuf = malloc(INBUFSIZ);
  window = malloc(WSIZE);
  insize = 0;
  inptr = 0;
  outcnt = 0;
  exit_code = 0;
  bytes_out = 0;
  crc = 0xffffffffL;

  makecrc();
  err = gunzip();

  free(inbuf);
  free(window);

  return err;
}

