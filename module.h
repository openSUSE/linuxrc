/*
 *
 * module.h      Header file for module.c
 *
 * Copyright (c) 1996-2002  Hubert Mantel, SuSE Linux AG  (mantel@suse.de)
 *
 */

int        mod_get_type(char *type_name);
int        mod_check_modules(char *type_name);
module2_t *mod_get_entry(char *name);
int        mod_add_disk(int prompt, int type);
int        mod_pcmcia_ok(void);
void       mod_menu(void);
int        mod_load_module(char *module, char *param);
void       mod_unload_module(char *module);
void       mod_show_modules(void);
void       mod_free_modules(void);
void       mod_init(void);
int        mod_load_modules(char *modules, int show);
void       mod_disk_text(char *buf, int type);
void       mod_update_netdevice_list(char *module, int add);
