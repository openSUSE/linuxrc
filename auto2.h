int auto2_mount_cdrom(char *device);
int auto2_mount_harddisk(char *device);
void auto2_scan_hardware(char *log_file);
int auto2_init(void);
void auto2_chk_expert(void);
int auto2_pcmcia(void);
int auto2_full_libhd(void);
#if 0
void auto2_find_braille(void);
#endif
char *auto2_usb_module(void);
#if 0
char *auto2_xserver(char **version, char **busid);
#endif
char *auto2_disk_list(int *boot_disk);
char *auto2_serial_console(void);
#if 0
void auto2_print_x11_opts(FILE *);
#endif
int auto2_find_install_medium(void);
