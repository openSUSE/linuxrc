/*
 *
 * module.h      Header file for module.c
 *
 * Copyright (c) 1996-2002  Hubert Mantel, SuSE Linux AG  (mantel@suse.de)
 *
 */

int        mod_get_type(char *type_name);
int        mod_check_modules(char *type_name);
void       mod_free_modules(void);
void       mod_init(void);
module_t  *mod_get_entry(char *name);
void       mod_menu(void);
int        mod_add_disk(int prompt, int type);
void       mod_unload_module(char *module);
int        mod_load_modules(char *modules, int show);
int        mod_insmod(char *module, char *param);
int        mod_modprobe(char *module, char *param);
void       mod_show_modules(void);
int        mod_pcmcia_ok(void);
void       mod_disk_text(char *buf, int type);
int        mod_copy_modules(char *src_dir, int doit);
