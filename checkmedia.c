#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "global.h"
#include "dialog.h"
#include "util.h"
#include "keyboard.h"

#include <mediacheck.h>

static int progress(unsigned percent);

window_t win;

void digest_media_verify(char *device)
{
  int i, ok;
  mediacheck_t *media = NULL;
  char buf[256];

  log_info("digest_media_verify(%s)\n", device);

  if(device) {
    device = strdup(long_dev(device));
    media = mediacheck_init(device, progress);
  }
  else {
    update_device_list(0);
    if(config.device) {
      device = config.device;
      media = mediacheck_init(device, progress);
    }

    dia_message("No CD-ROM or DVD found!!!", MSGTYPE_ERROR);
  }

  if(media) {
    for(i = 0; i < sizeof media->tags / sizeof *media->tags; i++) {
      if(!media->tags[i].key) break;
      log_info("media tags: key = \"%s\", value = \"%s\"\n", media->tags[i].key, media->tags[i].value);
    }
  }

  if(
    !media ||
    media->err ||
    (media->iso_blocks && media->pad_blocks >= media->iso_blocks)
  ) {
    dia_message("This is not a SUSE medium.", MSGTYPE_ERROR);
    config.manual=1;
    return;
  }

  if(
    !mediacheck_digest_valid(media->digest.iso) &&
    !mediacheck_digest_valid(media->digest.part)
  ) {
    dia_message("No digests to check.", MSGTYPE_ERROR);
    config.manual=1;
    return;
  }

  if(media->app_id) log_info("app: %s\n", media->app_id);

  if(media->iso_blocks) {
    log_info(
      "iso size: %u%s kB\n",
      media->iso_blocks >> 1,
      (media->iso_blocks & 1) ? ".5" : ""
    );
  }

  if(media->pad_blocks) log_info("pad: %u kB\n", media->pad_blocks >> 1);

  if(media->part_blocks) {
    log_info(
      "partition: start %u%s kB, size %u%s kB\n",
      media->part_start >> 1,
      (media->part_start & 1) ? ".5" : "",
      media->part_blocks >> 1,
      (media->part_blocks & 1) ? ".5" : ""
    );
  }

  if(media->full_blocks) {
    log_info(
      "full size: %u%s kB\n",
      media->full_blocks >> 1,
      (media->full_blocks & 1) ? ".5" : ""
    );
  }

  if(mediacheck_digest_valid(media->digest.iso)) {
    log_info("iso ref: %s\n", mediacheck_digest_hex_ref(media->digest.iso));
  }

  if(mediacheck_digest_valid(media->digest.part)) {
    log_info("part ref: %s\n", mediacheck_digest_hex_ref(media->digest.part));
  }

  dia_status_on(&win, media->app_id);
  mediacheck_calculate_digest(media);
  dia_status_off(&win);

  log_info("media check: %s\n", media->abort ? "aborted" : "finished");

  if(media->err && media->err_block) {
    log_info("err: block %u\n", media->err_block);
  }

  if(media->iso_blocks) {
    log_info(
      "result: iso %s %s\n",
      mediacheck_digest_name(media->digest.iso),
      mediacheck_digest_ok(media->digest.iso) ? "ok" : "wrong"
    );
  }

  if(media->part_blocks) {
    log_info(
      "result: partition %s %s\n",
      mediacheck_digest_name(media->digest.part),
      mediacheck_digest_ok(media->digest.part) ? "ok" : "wrong"
    );
  }

  if(mediacheck_digest_valid(media->digest.iso)) {
    log_info(
      "iso %6s: %s\n",
      mediacheck_digest_name(media->digest.iso),
      mediacheck_digest_hex(media->digest.iso)
    );
  }

  if(mediacheck_digest_valid(media->digest.part)) {
    log_info(
      "part %6s: %s\n",
      mediacheck_digest_name(media->digest.part),
      mediacheck_digest_hex(media->digest.part)
    );
  }

  if(mediacheck_digest_valid(media->digest.full)) {
    log_info(
      "%s: %s\n",
      mediacheck_digest_name(media->digest.full),
      mediacheck_digest_hex(media->digest.full)
    );
  }

  ok = mediacheck_digest_ok(media->digest.iso) || mediacheck_digest_ok(media->digest.part);

  if(ok) {
    dia_message("No errors found.", MSGTYPE_INFO);
  }
  else if(media->abort) {
    dia_message("Media check canceled.", MSGTYPE_INFO);
  }
  else {
    if(media->err_block) {
      sprintf(buf, "Error reading block %u.", media->err_block);
    }
    else {
      sprintf(buf, "Checksum wrong.");
    }
    sprintf(buf + strlen(buf), "\nThis medium is broken.");
    dia_message(buf, MSGTYPE_ERROR);
    config.manual=1;
  }

  mediacheck_done(media);
}


int progress(unsigned percent)
{
  dia_status(&win, percent);

  return kbd_getch_old(0) == KEY_ESC ? 1 : 0;
}
