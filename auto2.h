int auto2_init(void);
void auto2_scan_hardware(void);
int auto2_find_repo(void);
int activate_driver(hd_data_t *hd_data, hd_t *hd, slist_t **mod_list, int show_modules);
void load_network_mods(void);
char *auto2_serial_console(void);
void pcmcia_socket_startup(void);
void auto2_driverupdate(url_t *url);
