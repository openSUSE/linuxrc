/*
 *
 * util.h        Header file for util.c
 *
 * Copyright (c) 1996-2008  Hubert Mantel, SuSE Linux AG  (mantel@suse.de)
 *
 */

/* #define LXRCDEBUG */

#ifdef LXRCDEBUG
#define HERE fprintf (stderr, "%s, Line %d\n", __FILE__, __LINE__); fflush (stderr);
#else
#define HERE
#endif

extern void util_redirect_kmsg     (void);
extern void util_center_text       (unsigned char *txt, int size);
extern void util_generate_button   (button_t *button, char *txt, int size);
extern int  util_format_txt        (unsigned char *txt, unsigned char **lines, int max_width);
extern void util_fill_string       (unsigned char *str, int max_width);
extern void util_create_items      (item_t items_arr [], int nr_iv, int size_iv);
extern void util_free_items        (item_t items_arr [], int nr_iv);
extern int  util_fileinfo          (char *file_name, int *size, int *compressed);
extern void util_update_kernellog  (void);
extern void util_print_banner      (void);
extern void util_beep              (int  success_iv);
extern void util_truncate_dir      (char *dir_tr);
extern int  util_check_exist(char *file);
extern int  util_check_exist2(char *dir, char *file);
extern int  util_check_break       (void);

extern void util_disp_init         (void);
extern void util_disp_done         (void);
extern int  util_umount            (char *mountpoint);
void util_umount_all(void);
extern int  util_eject_cdrom       (char *dev);
int util_chk_driver_update(char *dir, char *loc);
extern void util_do_driver_updates (void);
extern int show_driver_updates(void);
extern void util_status_info       (int log_it);
extern void util_get_splash_status (void);
void   util_splash_bar(unsigned num, char *trigger);
extern int  util_cp_main           (int argc, char **argv);
extern int  util_do_cp             (char *src, char *dst);
extern int  util_swapon_main       (int argc, char **argv);
extern int  util_extend_main       (int argc, char **argv);
extern void util_start_shell       (char *tty, char *shell, int flags);
extern char *util_process_name     (pid_t pid);
extern char *util_process_cmdline  (pid_t pid);
extern void util_umount_all_devices (void);

slist_t *slist_new(void);
slist_t *slist_free(slist_t *sl);
slist_t *slist_free_entry(slist_t **sl0, char *str);
slist_t *slist_append(slist_t **sl0, slist_t *sl);
slist_t *slist_append_str(slist_t **sl0, char *str);
slist_t *slist_add(slist_t **sl0, slist_t *sl);
slist_t *slist_assign_values(slist_t **sl0, char *str);
slist_t *slist_getentry(slist_t *sl, char *key);
slist_t *slist_reverse(slist_t *sl0);
slist_t *slist_sort(slist_t *sl0, int (*cmp_func)(const void *, const void *));
slist_t *slist_split(char del, char *text);
char *slist_join(char *del, slist_t *str);

char *util_attach_loop(char *file, int ro);
int util_detach_loop(char *dev);

void name2inet(inet_t *inet, char *name);
void s_addr2inet(inet_t *inet, unsigned long s_addr);
char *inet2print(inet_t *inet);
void str_copy(char **dst, char *src);
void strprintf(char **buf, char *format, ...) __attribute__ ((format (printf, 2, 3)));
char *get_instmode_name(instmode_t instmode);
char *get_instmode_name_up(instmode_t instmode);

void util_free_mem(void);
void util_update_meminfo(void);

int util_fstype_main(int argc, char **argv);
char *util_fstype(char *dev, char **module);
int util_mount(char *dev, char *dir, unsigned long flags, slist_t *file_list);
int util_mount_ro(char *dev, char *dir, slist_t *file_list);
int util_mount_rw(char *dev, char *dir, slist_t *file_list);

void util_update_netdevice_list(char *module, int add);
int util_update_disk_list(char *module, int add);
void util_update_cdrom_list(void);
void util_update_swap_list(void);
int util_is_mountable(char *file);
void util_set_serial_console(char *str);
void util_set_stderr(char *name);
void util_set_product_dir(char *prod);

void scsi_rename(void);

char *short_dev(char *dev);
char *long_dev(char *dev);

void util_mkdevs(void);
void get_net_unique_id(void);

int util_lndir_main(int argc, char **argv);

void util_notty(void);
void util_killall(char *name, int sig);

void util_get_ram_size(void);
void util_load_usb(void);

int util_set_attr(char* attr, char* value);
char *util_get_attr(char* attr);
int util_get_int_attr(char* attr);

char *print_driverid(driver_t *drv, int with_0x);
int apply_driverid(driver_t *drv);
void store_driverid(driver_t *drv);

int match_netdevice(char *device, char *hwaddr, char *key);

char* util_chop_lf(char* str);
int util_read_and_chop(char* path, char* dst, int dst_size);

char *get_translation(slist_t *trans, char *locale);
int util_process_running(char *name);

int system_log(char *cmd);

char *blk_size_str(char *dev);
uint64_t blk_size(char *dev);
char *blk_ident(char *dev);
void update_device_list(int force);
char *new_mountpoint(void);
int util_copy_file(char *src_dir, char *src_file, char *dst);
char *new_download(void);
void util_clear_downloads(void);
void util_wait(const char *file, int line);
void run_braille(void);
void util_setup_udevrules(void);

void util_error_trace(char *format, ...);
hd_t *fix_device_names(hd_t *hd);

int fcoe_check(void);
int iscsi_check(void);

char *mac_to_interface(char *mac, int *max_offset);

void util_run_script(char *name);

void util_plymouth_off(void);
int util_choose_disk_device(char **dev, int type, char *list_title, char *input_title);

void util_restart();

void check_ptp(void);

char *compress_type(void *buf);
char *compressed_file(char *name);
char *compressed_archive(char *name, char **archive);

