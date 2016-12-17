#ifndef AUTO2_H_
#define AUTO2_H_

int auto2_init(void);
void auto2_scan_hardware(void);
int auto2_find_repo(void);
int activate_driver(hd_data_t *hd_data, hd_t *hd, slist_t **mod_list, int show_modules);
void load_network_mods(void);
char *auto2_serial_console(void);
void auto2_driverupdate(url_t *url);
int auto2_add_extension(char *extension);
int auto2_remove_extension(char *extension);
void load_drivers(hd_data_t *hd_data, hd_hw_item_t hw_item);
void auto2_user_netconfig(void);
void auto2_user_netconfig(void);

#endif // AUTO2_H_
