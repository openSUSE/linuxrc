#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "global.h"
#include "dialog.h"
#include "window.h"
#include "util.h"
#include "keyboard.h"

#include <mediacheck.h>

static int check_media_device(char *device);
static int progress(unsigned percent);

window_t win;

/*
 * Check single SUSE installation medium.
 *
 * device: device name of the device to check
 *
 * return 1 if ok, else 0
 */
int check_media_device(char *device)
{
  int i, ok;
  mediacheck_t *media = NULL;

  log_info("digest_media_verify(%s)\n", device);

  media = mediacheck_init(long_dev(device), progress);

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
    return 1;
  }

  if(
    !mediacheck_digest_valid(media->digest.iso) &&
    !mediacheck_digest_valid(media->digest.part)
  ) {
    dia_message("No digests to check.", MSGTYPE_ERROR);
    config.manual=1;
    return 1;
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
    char buf[256];

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

  return ok;
}


/*
 * Check SUSE installation medium.
 *
 * device: device name of the device to check or NULL
 *
 * If device is NULL, a device selection dialog is shown to the user first.
 *
 * return 1 if ok, else 0
 */
int check_media(char *device)
{
  int item_cnt, item_width;
  int list_len = 0, device_name_len = 0, ok = 1;
  char **items, **values;
  window_t dia_win;
  slist_t *sl, *device_list = NULL;
  hd_t *hd;

  if(device) return check_media_device(device);

  dia_info(&dia_win, "Searching for storage devices...", MSGTYPE_INFO);
  update_device_list(0);
  win_close(&dia_win);

  for(hd = hd_list(config.hd_data, hw_block, 0, NULL); hd; hd = hd -> next) {
    if(
      !hd->unix_dev_name ||
      hd_is_hw_class(hd, hw_partition)
    ) continue;
    slist_append_str(&device_list, strdup(short_dev(hd->unix_dev_name)));
    list_len++;
  }

  /*
   * just max values, actual lists might be shorter
   *
   * "+2" due to "other device" + final NULL
   */
  items = calloc(list_len + 2, sizeof *items);
  values = calloc(list_len + 2, sizeof *values);

  item_cnt = 0;

  const char other_device[] = "- other device -";

  item_width = sizeof other_device - 1;

  for(sl = device_list; sl; sl = sl->next) {
    mediacheck_t *media = mediacheck_init(long_dev(sl->key), NULL);
    if(media && !media->err) {
      log_info("checkmedia: %s = %s\n", sl->key, media->app_id);
      /* max device name length */
      int len = strlen(sl->key);
      if(len > device_name_len) device_name_len = len;

      values[item_cnt] = strdup(sl->key);
      asprintf(items + item_cnt, "%*s: %s", device_name_len, sl->key, media->app_id);

      len = strlen(items[item_cnt]);
      if(len > item_width) item_width = len;

      item_cnt++;
    }
    else {
      log_info("checkmedia: %s is not a SUSE medium\n", sl->key);
    }

    mediacheck_done(media);
  }

  values[item_cnt] = NULL;
  items[item_cnt++] = strdup(other_device);

  if(item_width > 72) item_width = 72;

  int selected_item = 1;

  if(item_cnt > 1) {
    selected_item = dia_list("Please choose the device to check.", item_width + 2, NULL, items, selected_item, align_left);
  }

  if(selected_item > 0) {
    str_copy(&device, values[selected_item - 1]);
    if(!device) {
      ok = !dia_input2("Enter the device to check.", &device, 30, 0);
      if(ok) {
        if(util_check_exist(long_dev(device)) == 'b') {
          str_copy(&device, short_dev(device));
        }
        else {
          dia_message("Invalid device name.", MSGTYPE_ERROR);
          ok = 0;
        }
      }
    }
  }
  else {
    ok = 0;
  }

  int i;

  for(i = 0; i < item_cnt; i++) { free(items[i]); free(values[i]); }
  free(items);
  free(values);

  if(ok && device) {
    ok = check_media_device(device);
  }

  str_copy(&device, NULL);

  return ok;
}


/*
 * Callback function to visualize media check progress.
 *
 * This just shows the percentage.
 *
 * Check for ESC to allow aborting the checking process.
 */
int progress(unsigned percent)
{
  dia_status(&win, percent);

  return kbd_getch_old(0) == KEY_ESC ? 1 : 0;
}
