int auto2_mount_cdrom(char *device);
int auto2_mount_harddisk(char *device);
void auto2_scan_hardware(char *log_file);
int auto2_init(void);
int auto2_pcmcia(void);
char *auto2_disk_list(int *boot_disk);
char *auto2_serial_console(void);
int auto2_find_install_medium(void);
