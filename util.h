/*
 *
 * util.h        Header file for util.c
 *
 * Copyright (c) 1996-2001  Hubert Mantel, SuSE GmbH  (mantel@suse.de)
 *
 */

/* #define LXRCDEBUG */

#ifdef LXRCDEBUG
#define HERE fprintf (stderr, "%s, Line %d\n", __FILE__, __LINE__); fflush (stderr);
#else
#define HERE
#endif

extern void util_redirect_kmsg     (void);
extern void util_center_text       (char *txt_tr, int size_iv);
extern void util_generate_button   (button_t *button_prr, char *txt_tv);
extern int  util_format_txt        (char *txt_tv, char *lines_atr [],
                                    int width_iv);
extern void util_fill_string       (char *txt_tr, int size_iv);
extern void util_create_items      (item_t items_arr [], int nr_iv, int size_iv);
extern void util_free_items        (item_t items_arr [], int nr_iv);
extern int  util_fileinfo          (char *file_tv, int32_t *size_plr,
                                    int *compressed_pir);
extern void util_update_kernellog  (void);
extern void util_print_banner      (void);
extern void util_beep              (int  success_iv);
extern int  util_mount_loop        (char *file_tv, char *mountpoint_tv);
extern void util_umount_loop       (char *mountpoint_tv);
extern void util_truncate_dir      (char *dir_tr);
extern int  util_check_exist       (char *filename_tv);
extern int  util_check_break       (void);
extern int  util_try_mount         (const char *device_pcv,      char *dir_pcv,
                                    unsigned long flags_lv, const void *data_prv);
extern void util_print_ftp_error   (int error_iv);
extern void util_free_ramdisk      (char *ramdisk_dev_tv);
extern int  util_open_ftp          (char *server_tv);
extern int  util_cd1_boot          (void);

extern void util_disp_init         (void);
extern void util_disp_done         (void);
extern int  util_umount            (char *mountpoint);
extern int  util_eject_cdrom       (char *dev);
extern void util_manual_mode       (void);
extern int  util_chk_driver_update (char *dir);
extern void util_umount_driver_update (void);
extern void util_status_info       (void);
extern int  util_mount_main        (int argc, char **argv);
extern int  util_umount_main       (int argc, char **argv);
extern int  util_cat_main          (int argc, char **argv);
extern int  util_echo_main         (int argc, char **argv);
extern int  util_nothing_main      (int argc, char **argv);
extern int  util_sh_main           (int argc, char **argv);
extern void util_get_splash_status (void);
extern void util_ps                (FILE *f);
extern int  util_ps_main           (int argc, char **argv);
extern int  util_do_cp             (char *src, char *dst);
extern void util_start_shell       (char *tty, char *shell, int new_env);
