#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "global.h"
#include "dialog.h"
#include "util.h"
#include "md5.h"

static void do_md5(char *file);
static void get_info(char *file);
static void update_progress(unsigned size);

struct {
  unsigned got_md5:1;		/* got md5sum */
  unsigned got_old_md5:1;	/* got md5sum stored in iso */
  unsigned md5_ok:1;		/* md5sums match */
  unsigned err:1;		/* some error */
  unsigned err_ofs;		/* read error pos */
  unsigned size;		/* in kb */
  unsigned media_nr;		/* media number */
  char *media_type;		/* media type */
  char vol_id[33];		/* volume id */
  char app_id[81];		/* application id */
  char app_data[0x201];		/* app specific data*/
  unsigned char old_md5[16];	/* md5sum stored in iso */
  unsigned char md5[16];	/* md5sum */
} iso;

window_t win;

void md5_verify()
{
  int i;
  slist_t *sl;
  char buf[256];

  dia_message("Insert Installation CD-ROM or DVD.", MSGTYPE_INFO);

  get_info(long_dev(config.cdrom));

  if(iso.err) {
    get_info(long_dev(config.cdromdev));
    if(!iso.err) str_copy(&config.cdrom, config.cdromdev);
  }

  if(iso.err) {
    util_update_cdrom_list();
    for(sl = config.cdroms; sl; sl = sl->next) {
      if(config.cdrom && !strcmp(config.cdrom, sl->key)) continue;
      if(config.cdromdev && !strcmp(config.cdromdev, sl->key)) continue;
      get_info(long_dev(sl->key));
      if(!iso.err) {
        str_copy(&config.cdrom, sl->key);
        break;
      }
    }
  }

  if(iso.err) {
    dia_message("No CD-ROM or DVD found.", MSGTYPE_ERROR);
    return;
  }

  fprintf(stderr,
    "  app: %s\nmedia: %s%d\n size: %u kB\n",
    iso.app_id,
    iso.media_type,
    iso.media_nr ?: 1,
    iso.size
  );

  fprintf(stderr, "  md5: ");
  if(iso.got_old_md5) for(i = 0; i < sizeof iso.old_md5; i++) fprintf(stderr, "%02x", iso.old_md5[i]);
  fprintf(stderr, "\n");

  if(!*iso.app_id || !iso.got_old_md5) {
    sprintf(buf, "This is not a %s CD-ROM.", config.product);
    dia_message(buf, MSGTYPE_ERROR);
    return;
  }

  do_md5(long_dev(config.cdrom));

  if(iso.err_ofs) {
    fprintf(stderr, "  err: sector %u\n", iso.err_ofs >> 1);
  }

  fprintf(stderr, "check: ");
  if(iso.got_old_md5) {
    if(iso.md5_ok) {
      fprintf(stderr, "md5sum ok");
    }
    else {
      fprintf(stderr, "md5sum wrong");
    }
  }
  else {
    fprintf(stderr, "md5sum not checked");
  }

  if(iso.got_md5 && !iso.md5_ok) {
    fprintf(stderr, " (");
    for(i = 0; i < sizeof iso.md5; i++) fprintf(stderr, "%02x", iso.md5[i]);
    fprintf(stderr, ")");
  }

  fprintf(stderr, "\n");

  if(iso.md5_ok) {
    dia_message("No errors found.", MSGTYPE_INFO);
  }
  else {
    if(iso.err_ofs) {
      sprintf(buf, "Error reading sector %u.", iso.err_ofs >> 1);
    }
    else {
      sprintf(buf, "Checksum wrong.");
    }
    sprintf(buf + strlen(buf), "\nThis %s is broken.", *iso.media_type == 'C' ? "CD-ROM" : iso.media_type);
    dia_message(buf, MSGTYPE_ERROR);
  }

}


/*
 * Calculate md5 sum.
 *
 * Normal md5sum, except we assume the area 0x8373 - 0x8572 to be filled
 * with spaces (' ').
 */
void do_md5(char *file)
{
  unsigned char buffer[64 << 10]; /* at least 36k! */
  struct md5_ctx ctx;
  int fd, err = 0;
  unsigned chunks = iso.size / (sizeof buffer >> 10);
  unsigned chunk, u;
  unsigned last_size = (iso.size % (sizeof buffer >> 10)) << 10;
  char msg[256];

  if((fd = open(file, O_RDONLY)) == -1) return;

  sprintf(msg, "%s, %s%u", iso.app_id, iso.media_type, iso.media_nr ?: 1);
  dia_status_on(&win, msg);

  md5_init_ctx(&ctx);

  /*
   * Note: md5_process_block() below *requires* buffer sizes to be a
   * multiple of 64. Otherwise use md5_process_bytes().
   */

  for(chunk = 0; chunk < chunks; chunk++) {
    if((u = read(fd, buffer, sizeof buffer)) != sizeof buffer) {
      err = 1;
      if(u > sizeof buffer) u = 0 ;
      iso.err_ofs = (u >> 10) + chunk * (sizeof buffer >> 10);
      break;
    };

    if(chunk == 0) memset(buffer + 0x8373, ' ', 0x200);

    md5_process_block(buffer, sizeof buffer, &ctx);

    update_progress((chunk + 1) * (sizeof buffer >> 10));
  }

  if(!err && last_size) {
    if((u = read(fd, buffer, last_size)) != last_size) {
      err = 1;
      if(u > sizeof buffer) u = 0 ;
      iso.err_ofs = (u >> 10) + chunk * (sizeof buffer >> 10);
    }
    else {
      md5_process_block(buffer, last_size, &ctx);

      update_progress(iso.size);
    }
  }

  md5_finish_ctx(&ctx, iso.md5);

  dia_status_off(&win);

  if(err) iso.err = 1;

  iso.got_md5 = 1;

  if(iso.got_old_md5 && !memcmp(iso.md5, iso.old_md5, sizeof iso.md5)) {
    iso.md5_ok = 1;
  }

  close(fd);
}


/*
 * Read all kinds of iso header info.
 */
void get_info(char *file)
{
  int fd, ok = 0;
  unsigned char buf[4];
  char *s;
  unsigned u, u1, idx;

  memset(&iso, 0, sizeof iso);

  iso.err = 1;

  if(!file) return;

  if((fd = open(file, O_RDONLY)) == -1) return;

  if(
    lseek(fd, 0x8028, SEEK_SET) == 0x8028 &&
    read(fd, iso.vol_id, 32) == 32
  ) {
    iso.vol_id[sizeof iso.vol_id - 1] = 0;
    ok++;
  }

  if(
    lseek(fd, 0x8050, SEEK_SET) == 0x8050 &&
    read(fd, buf, 4) == 4
  ) {
    iso.size = 2*(buf[0] + (buf[1] << 8) + (buf[2] << 16) + (buf[3] << 24));
    ok++;
  }

  if(
    lseek(fd, 0x823e, SEEK_SET) == 0x823e &&
    read(fd, iso.app_id, 80) == 80
  ) {
    iso.app_id[sizeof iso.app_id - 1] = 0;
    ok++;
  }

  if(
    lseek(fd, 0x8373, SEEK_SET) == 0x8373 &&
    read(fd, iso.app_data, 0x200) == 0x200
  ) {
    iso.app_data[sizeof iso.app_data - 1] = 0;
    ok++;
  }

  close(fd);

  if(ok != 4) return;

  iso.media_type = "CD";

  iso.err = 0;

  for(s = iso.app_id + sizeof iso.app_id - 1; s >= iso.app_id; *s-- = 0) {
    if(*s != 0 && *s != ' ') break;
  }

  if(!strncmp(iso.app_id, "MKISOFS", sizeof "MKISOFS" - 1)) *iso.app_id = 0;

  if((s = strrchr(iso.app_id, '#'))) *s = 0;

  if(strstr(iso.app_id, "-DVD-")) iso.media_type = "DVD";

  for(s = iso.vol_id + sizeof iso.vol_id - 1; s >= iso.vol_id; *s-- = 0) {
    if(*s != 0 && *s != ' ') break;
  }

  if(*iso.vol_id == 'S') {
    idx = iso.vol_id[strlen(iso.vol_id) - 1];
    if(idx >= '1' && idx <= '9') iso.media_nr = idx - '0';
  }

  if((s = strstr(iso.app_data, "md5sum="))) {
    s += sizeof "md5sum=" - 1;
    if(strlen(s) >= 32) {
      for(u = 0 ; u < sizeof iso.old_md5; u++, s += 2) {
         if(sscanf(s, "%2x", &u1) == 1) {
           iso.old_md5[u] = u1;
         }
         else {
           break;
         }
      }
      if(u == sizeof iso.old_md5) iso.got_old_md5 = 1;
    }
  }
}


void update_progress(unsigned size)
{
  static int last_percent = 0;
  int percent;

  if(!size) last_percent = 0;

  percent = (size * 100) / (iso.size ?: 1);
  if(percent != last_percent) {
    dia_status(&win, last_percent = percent);
  }
}

