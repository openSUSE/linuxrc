/*
 *
 * util.h        Header file for util.c
 *
 * Copyright (c) 1996-2000  Hubert Mantel, SuSE GmbH  (mantel@suse.de)
 *
 */

/* #define LXRCDEBUG */

#ifdef LXRCDEBUG
#define HERE fprintf (stderr, "%s, Line %d\n", __FILE__, __LINE__); fflush (stderr);
#else
#define HERE
#endif

extern void util_redirect_kmsg    (void);
extern void util_center_text      (char *txt_tr, int size_iv);
extern void util_generate_button  (button_t *button_prr, char *txt_tv);
extern int  util_format_txt       (char *txt_tv, char *lines_atr [],
                                   int width_iv);
extern void util_fill_string      (char *txt_tr, int size_iv);
extern void util_create_items     (item_t items_arr [], int nr_iv, int size_iv);
extern void util_free_items       (item_t items_arr [], int nr_iv);
extern int  util_fileinfo         (char *file_tv, long *size_plr,
                                   int *compressed_pir);
extern void util_update_kernellog (void);
extern void util_print_banner     (void);
extern void util_beep             (int  success_iv);
extern int  util_mount_loop       (char *file_tv, char *mountpoint_tv);
extern void util_umount_loop      (char *mountpoint_tv);
extern void util_truncate_dir     (char *dir_tr);
extern int  util_check_exist      (char *filename_tv);
extern int  util_check_break      (void);
extern int  util_try_mount        (char *device_pcv,      char *dir_pcv,
                                  unsigned long flags_lv, const void *data_prv);
extern void util_print_ftp_error  (int error_iv);
extern void util_free_ramdisk     (char *ramdisk_dev_tv);
extern int  util_open_ftp         (char *server_tv);
extern int  util_cd1_boot         (void);

extern int  util_umount           (char *mountpoint);
