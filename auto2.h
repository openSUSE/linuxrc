int auto2_mount_cdrom(char *device);
int auto2_mount_harddisk(char *dev);
void auto2_scan_hardware(void);
int auto2_init(void);
char *auto2_serial_console(void);
int auto2_find_install_medium(void);
void load_network_mods(void);
int activate_driver(hd_data_t *hd_data, hd_t *hd, slist_t **mod_list, int show_modules);
void pcmcia_socket_startup(void);
