/*
 *
 * util.h        Header file for util.c
 *
 * Copyright (c) 1996-2002  Hubert Mantel, SuSE Linux AG  (mantel@suse.de)
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
extern void util_generate_button   (button_t *button, char *txt, int size);
extern int  util_format_txt        (char *txt_tv, char *lines_atr [],
                                    int width_iv);
extern void util_fill_string       (char *txt_tr, int size_iv);
extern void util_create_items      (item_t items_arr [], int nr_iv, int size_iv);
extern void util_free_items        (item_t items_arr [], int nr_iv);
extern int  util_fileinfo          (char *file_name, int *size, int *compressed);
extern void util_update_kernellog  (void);
extern void util_print_banner      (void);
extern void util_beep              (int  success_iv);
extern int  util_mount_loop        (char *file_tv, char *mountpoint_tv);
extern void util_umount_loop       (char *mountpoint_tv);
extern void util_truncate_dir      (char *dir_tr);
extern int  util_check_exist       (char *filename_tv);
extern int  util_check_break       (void);
extern void util_print_net_error   (void);
extern int  util_free_ramdisk      (char *ramdisk_dev_tv);
extern int  util_cd1_boot          (void);

extern void util_disp_init         (void);
extern void util_disp_done         (void);
extern int  util_umount            (char *mountpoint);
extern int  util_eject_cdrom       (char *dev);
extern int  util_chk_driver_update (char *dir, char *loc);
extern void util_do_driver_updates (void);
extern void util_write_update_pre  (void);
// extern void util_umount_driver_update (void);
extern int show_driver_updates(void);
extern void util_status_info       (void);
extern int  util_mount_main        (int argc, char **argv);
extern int  util_umount_main       (int argc, char **argv);
extern int  util_cat_main          (int argc, char **argv);
extern int  util_hex_main          (int argc, char **argv);
extern int  util_echo_main         (int argc, char **argv);
extern int  util_nothing_main      (int argc, char **argv);
extern int  util_sh_main           (int argc, char **argv);
extern void util_get_splash_status (void);
void   util_splash_bar(unsigned num);
extern int  util_ps_main           (int argc, char **argv);
extern int  util_lsof_main         (int argc, char **argv);
extern int  util_cp_main           (int argc, char **argv);
extern int  util_do_cp             (char *src, char *dst);
extern int  util_ls_main           (int argc, char **argv);
extern int  util_rm_main           (int argc, char **argv);
extern int  util_mv_main           (int argc, char **argv);
extern int  util_ln_main           (int argc, char **argv);
extern int  util_swapon_main       (int argc, char **argv);
extern int  util_swapoff_main      (int argc, char **argv);
extern int  util_freeramdisk_main  (int argc, char **argv);
extern int  util_lsmod_main        (int argc, char **argv);
extern int  util_raidautorun_main  (int argc, char **argv);
extern int  util_free_main         (int argc, char **argv);
extern int  util_mkdir_main        (int argc, char **argv);
extern int  util_chroot_main       (int argc, char **argv);
extern int  util_kill_main         (int argc, char **argv);
extern int  util_bootpc_main       (int argc, char **argv);
extern int  util_wget_main         (int argc, char **argv);
extern int  util_fstype_main       (int argc, char **argv);
extern int  util_modprobe_main     (int argc, char **argv);
extern void util_start_shell       (char *tty, char *shell, int new_env);
extern char *util_process_name     (pid_t pid);
extern char *util_process_cmdline  (pid_t pid);

slist_t *slist_new(void);
slist_t *slist_free(slist_t *sl);
slist_t *slist_append(slist_t **sl0, slist_t *sl);
slist_t *slist_append_str(slist_t **sl0, char *str);
slist_t *slist_add(slist_t **sl0, slist_t *sl);
slist_t *slist_getentry(slist_t *sl, char *key);
slist_t *slist_reverse(slist_t *sl0);
slist_t *slist_sort(slist_t *sl0, int (*cmp_func)(const void *, const void *));
slist_t *slist_split(char del, char *text);

void name2inet(inet_t *inet, char *name);
void s_addr2inet(inet_t *inet, unsigned long s_addr);
char *inet2print(inet_t *inet);
url_t *parse_url(char *str);
void str_copy(char **dst, char *src);
void strprintf(char **buf, char *format, ...) __attribute__ ((format (printf, 2, 3)));
void set_instmode(instmode_t instmode);
char *get_instmode_name(instmode_t instmode);
char *get_instmode_name_up(instmode_t instmode);

int net_open(char *filename);
void net_close(int fd);
int net_read(int fd, char *buf, int len);

void util_free_mem(void);
void util_update_meminfo(void);

char *util_fstype(char *dev, char **module);
int util_mount(char *dev, char *dir, unsigned long flags);
int util_mount_ro(char *dev, char *dir);
int util_mount_rw(char *dev, char *dir);

void util_update_netdevice_list(char *module, int add);
int util_update_disk_list(char *module, int add);
void util_update_cdrom_list(void);
void util_update_swap_list(void);
int util_is_dir(char *dir);
int util_is_mountable(char *file);
void util_debugwait(char *msg);
void util_hwcheck(void);
void util_set_serial_console(char *str);
void util_set_stderr(char *name);
void util_set_product_dir(char *prod);

int util_usbscsi_main(int argc, char **argv);
int usbscsi_change(int action);
void usbscsi_off(void);
void usbscsi_on(void);
char *pcmcia_driver(int pcmcia_type);

char *short_dev(char *dev);
char *long_dev(char *dev);

void util_mkdevs(void);
