#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include <fcntl.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "global.h"
#include "dialog.h"
#include "util.h"
#include "md5.h"
#include "sha1.h"
#include "sha256.h"
#include "sha512.h"
#include "keyboard.h"

#define MAX_DIGEST_SIZE SHA512_DIGEST_SIZE

typedef enum {
  digest_none, digest_md5, digest_sha1, digest_sha224, digest_sha256, digest_sha384, digest_sha512
} digest_t;

typedef union {
  struct md5_ctx md5;
  struct sha1_ctx sha1;
  struct sha256_ctx sha224;
  struct sha256_ctx sha256;
  struct sha512_ctx sha384;
  struct sha512_ctx sha512;
} digest_ctx_t;


static void do_digest(char *file);
static void get_info(char *file);
static void update_progress(unsigned size);

static void digest_media_init(digest_ctx_t *ctx);
static void digest_media_process(digest_ctx_t *ctx, unsigned char *buffer, unsigned len);
static void digest_media_finish(digest_ctx_t *ctx, unsigned char *buffer);


struct {
  unsigned err:1;		/* some error */
  unsigned err_ofs;		/* read error pos */
  unsigned size;		/* in kb */
  unsigned media_nr;		/* media number */
  char *media_type;		/* media type */
  char vol_id[33];		/* volume id */
  char app_id[81];		/* application id */
  char app_data[0x201];		/* app specific data*/
  unsigned pad;			/* pad size in kb */
  struct {
    digest_t type;				/* digest type */
    char *name;					/* digest name */
    int size;					/* digest size */
    unsigned got_old:1;				/* got digest stored in iso */
    unsigned got_current:1;			/* calculated current digest */
    unsigned ok:1;				/* digest matches */
    unsigned char old[MAX_DIGEST_SIZE];		/* digest stored in iso */
    unsigned char current[MAX_DIGEST_SIZE];	/* digest of iso ex special area */
    unsigned char full[MAX_DIGEST_SIZE];	/* full digest of iso */
    digest_ctx_t ctx;
    digest_ctx_t full_ctx;
  } digest;
} iso;

window_t win;


void digest_media_verify(char *device)
{
  int i;
  char buf[256];

  iso.err = 1;

  log_info("digest_media_verify(%s)\n", device);

  if(device) {
    device = strdup(long_dev(device));
    get_info(device);
  }
  else {
    update_device_list(0);
    if(config.device) get_info(device = config.device);

    dia_message("No CD-ROM or DVD found!!!", MSGTYPE_ERROR);
  }

#if 0
  if(iso.err) {
    for(hd = fix_device_names(hd_list(config.hd_data, hw_cdrom, 0, NULL)); hd; hd = hd->next) {
      if(hd->is.notready) continue;
      get_info(device = hd->unix_dev_name);
      if(!iso.err) break;
    }
  }

  if(iso.err) {
    if(dia_message("Insert Installation CD-ROM or DVD.", MSGTYPE_INFO)) return;

    for(hd = fix_device_names(hd_list(config.hd_data, hw_cdrom, 0, NULL)); hd; hd = hd->next) {
      if(hd->is.notready) continue;
      get_info(device = hd->unix_dev_name);
      if(!iso.err) break;
    }
  }

  if(iso.err) {
    dia_message("No CD-ROM or DVD found.", MSGTYPE_ERROR);
    config.manual=1;
    return;
  }
#endif

  log_info(
    "  app: %s\nmedia: %s%d\n size: %u kB\n  pad: %u kB\n",
    iso.app_id,
    iso.media_type,
    iso.media_nr ?: 1,
    iso.size,
    iso.pad
  );

  log_info("  ref: ");
  if(iso.digest.got_old) for(i = 0; i < iso.digest.size; i++) log_info("%02x", iso.digest.old[i]);
  log_info("\n");

  if(!*iso.app_id || !iso.digest.got_old || iso.pad >= iso.size) {
    sprintf(buf, "This is not a %s medium.", config.product);
    dia_message(buf, MSGTYPE_ERROR);
    config.manual=1;
    return;
  }

  do_digest(device);

  if(iso.err_ofs) {
    log_info("  err: sector %u\n", iso.err_ofs >> 1);
  }

  log_info("check: ");
  if(iso.digest.got_old) {
    if(iso.digest.ok) {
      log_info("%ssum ok\n", iso.digest.name);
    }
    else {
      log_info("%ssum wrong\n", iso.digest.name);
    }
  }
  else {
    log_info("%ssum not checked\n", iso.digest.name);
  }

  if(iso.digest.got_current) {
    log_info("  %s: ", iso.digest.name);
    for(i = 0; i < iso.digest.size; i++) log_info("%02x", iso.digest.full[i]);
    log_info("\n");
  }

  if(iso.digest.ok) {
    dia_message("No errors found.", MSGTYPE_INFO);
  }
  else if(iso.err) {
    if(iso.err_ofs) {
      sprintf(buf, "Error reading sector %u.", iso.err_ofs >> 1);
    }
    else {
      sprintf(buf, "Checksum wrong.");
    }
    sprintf(buf + strlen(buf), "\nThis %s is broken.", *iso.media_type == 'C' ? "CD-ROM" : iso.media_type);
    dia_message(buf, MSGTYPE_ERROR);
    config.manual=1;
  }
  else {
    dia_message("Checksumming canceled.", MSGTYPE_INFO);
  }
}


/*
 * Calculate digest over iso.
 *
 * Normal digest, except that we assume
 *   - 0x0000 - 0x01ff is filled with zeros (0)
 *   - 0x8373 - 0x8572 is filled with spaces (' ').
 */
void do_digest(char *file)
{
  unsigned char buffer[64 << 10]; /* at least 36k! */
  int fd, err = 0;
  unsigned chunks = (iso.size - iso.pad) / (sizeof buffer >> 10);
  unsigned chunk, u;
  unsigned last_size = ((iso.size - iso.pad) % (sizeof buffer >> 10)) << 10;
  char msg[256];
  time_t t0 = 0, t1 = 0;

  if((fd = open(file, O_RDONLY | O_LARGEFILE)) == -1) return;

  sprintf(msg, "%s, %s%u", iso.app_id, iso.media_type, iso.media_nr ?: 1);
  dia_status_on(&win, msg);

  digest_media_init(&iso.digest.ctx);
  digest_media_init(&iso.digest.full_ctx);

  for(chunk = 0; chunk < chunks; chunk++) {
    if((u = read(fd, buffer, sizeof buffer)) != sizeof buffer) {
      err = 1;
      if(u > sizeof buffer) u = 0 ;
      iso.err_ofs = (u >> 10) + chunk * (sizeof buffer >> 10);
      break;
    };

    digest_media_process(&iso.digest.full_ctx, buffer, sizeof buffer);

    if(chunk == 0) {
      memset(buffer, 0, 0x200);
      memset(buffer + 0x8373, ' ', 0x200);
    }

    digest_media_process(&iso.digest.ctx, buffer, sizeof buffer);

    if(!(chunk % 16)) {
      update_progress((chunk + 1) * (sizeof buffer >> 10));

      t1 = time(NULL);

      // once a second is enough
      if(t1 != t0 && kbd_getch_old(0) == KEY_ESC) {
         digest_media_finish(&iso.digest.ctx, iso.digest.current);
         digest_media_finish(&iso.digest.full_ctx, iso.digest.full);
         dia_status_off(&win);
         iso.digest.got_current = 0;
         iso.digest.got_old = 0;
         iso.digest.ok = 0;
         iso.err_ofs = 0;
         iso.err = 0;
         close(fd);
         return;
      }

      t0 = t1;
    }
  }

  if(!err && last_size) {
    if((u = read(fd, buffer, last_size)) != last_size) {
      err = 1;
      if(u > sizeof buffer) u = 0 ;
      iso.err_ofs = (u >> 10) + chunk * (sizeof buffer >> 10);
    }
    else {
      digest_media_process(&iso.digest.ctx, buffer, last_size);
      digest_media_process(&iso.digest.full_ctx, buffer, last_size);

      update_progress(iso.size - iso.pad);
    }
  }

  if(!err) {
    memset(buffer, 0, 2 << 10);		/* 2k */
    for(u = 0; u < (iso.pad >> 1); u++) {
      digest_media_process(&iso.digest.ctx, buffer, 2 << 10);
      digest_media_process(&iso.digest.full_ctx, buffer, 2 << 10);

      update_progress(iso.size - iso.pad + ((u + 1) << 1));
    }
  }

  digest_media_finish(&iso.digest.ctx, iso.digest.current);
  digest_media_finish(&iso.digest.full_ctx, iso.digest.full);

  dia_status_off(&win);

  if(err) iso.err = 1;

  iso.digest.got_current = 1;

  if(iso.digest.got_old && !memcmp(iso.digest.current, iso.digest.old, iso.digest.size)) {
    iso.digest.ok = 1;
  }
  else {
    iso.err = 1;
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
    iso.digest.type = digest_md5;
    iso.digest.size = MD5_DIGEST_SIZE;
    iso.digest.name = "md5";
  }
  else if((s = strstr(iso.app_data, "sha1sum="))) {
    s += sizeof "sha1sum=" - 1;
    iso.digest.type = digest_sha1;
    iso.digest.size = SHA1_DIGEST_SIZE;
    iso.digest.name = "sha1";
  }
  else if((s = strstr(iso.app_data, "sha224sum="))) {
    s += sizeof "sha224sum=" - 1;
    iso.digest.type = digest_sha224;
    iso.digest.size = SHA224_DIGEST_SIZE;
    iso.digest.name = "sha224";
  }
  else if((s = strstr(iso.app_data, "sha256sum="))) {
    s += sizeof "sha256sum=" - 1;
    iso.digest.type = digest_sha256;
    iso.digest.size = SHA256_DIGEST_SIZE;
    iso.digest.name = "sha256";
  }
  else if((s = strstr(iso.app_data, "sha384sum="))) {
    s += sizeof "sha384sum=" - 1;
    iso.digest.type = digest_sha384;
    iso.digest.size = SHA384_DIGEST_SIZE;
    iso.digest.name = "sha384";
  }
  else if((s = strstr(iso.app_data, "sha512sum="))) {
    s += sizeof "sha512sum=" - 1;
    iso.digest.type = digest_sha512;
    iso.digest.size = SHA512_DIGEST_SIZE;
    iso.digest.name = "sha512";
  }

  if(iso.digest.type) {
    if(strlen(s) >= iso.digest.size) {
      for(u = 0 ; u < iso.digest.size; u++, s += 2) {
         if(sscanf(s, "%2x", &u1) == 1) {
           iso.digest.old[u] = u1;
         }
         else {
           break;
         }
      }
      if(u == iso.digest.size) iso.digest.got_old = 1;
    }
  }

  if((s = strstr(iso.app_data, "pad="))) {
    s += sizeof "pad=" - 1;
    if(isdigit(*s)) iso.pad = strtoul(s, NULL, 0) << 1;
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


void digest_media_init(digest_ctx_t *ctx)
{
  switch(iso.digest.type) {
    case digest_md5:
      md5_init_ctx(&ctx->md5);
      break;
    case digest_sha1:
      sha1_init_ctx(&ctx->sha1);
      break;
    case digest_sha224:
      sha224_init_ctx(&ctx->sha224);
      break;
    case digest_sha256:
      sha256_init_ctx(&ctx->sha256);
      break;
    case digest_sha384:
      sha384_init_ctx(&ctx->sha384);
      break;
    case digest_sha512:
      sha512_init_ctx(&ctx->sha512);
      break;
    default:
      break;
  }
}


/*
 * Note: digest_media_process() *requires* buffer sizes to be a
 * multiple of 128. Otherwise use XXX_process_bytes().
 */
void digest_media_process(digest_ctx_t *ctx, unsigned char *buffer, unsigned len)
{
  switch(iso.digest.type) {
    case digest_md5:
      md5_process_block(buffer, len, &ctx->md5);
      break;
    case digest_sha1:
      sha1_process_block(buffer, len, &ctx->sha1);
      break;
    case digest_sha224:
      sha256_process_block(buffer, len, &ctx->sha224);
      break;
    case digest_sha256:
      sha256_process_block(buffer, len, &ctx->sha256);
      break;
    case digest_sha384:
      sha512_process_block(buffer, len, &ctx->sha384);
      break;
    case digest_sha512:
      sha512_process_block(buffer, len, &ctx->sha512);
      break;
    default:
      break;
  }
}


void digest_media_finish(digest_ctx_t *ctx, unsigned char *buffer)
{
  switch(iso.digest.type) {
    case digest_md5:
      md5_finish_ctx(&ctx->md5, buffer);
      break;
    case digest_sha1:
      sha1_finish_ctx(&ctx->sha1, buffer);
      break;
    case digest_sha224:
      sha224_finish_ctx(&ctx->sha224, buffer);
      break;
    case digest_sha256:
      sha256_finish_ctx(&ctx->sha256, buffer);
      break;
    case digest_sha384:
      sha384_finish_ctx(&ctx->sha384, buffer);
      break;
    case digest_sha512:
      sha512_finish_ctx(&ctx->sha512, buffer);
      break;
    default:
      break;
  }
}

