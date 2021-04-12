/*
 *
 * module.h      Header file for module.c
 *
 * Copyright (c) 1996-2002  Hubert Mantel, SuSE Linux AG  (mantel@suse.de)
 *
 */

typedef struct {
  char *ext;
  size_t len;
} mod_extensions_t;

// array of possible module extensions
extern const mod_extensions_t mod_extensions[];

int        mod_get_type(char *type_name);
int        mod_check_modules(char *type_name);
void       mod_init(int autoload);
module_t  *mod_get_entry(char *name);
void       mod_menu(void);
void       mod_unload_module(char *module);
int        mod_unload_modules(char *modules);
int        mod_is_loaded(char *module);
int        mod_load_modules(char *modules, int show);
int        mod_insmod(char *module, char *param);
int        mod_modprobe(char *module, char *param);
void       mod_show_modules(void);
void       mod_disk_text(char *buf, int type);
int        mod_cmp(char *str1, char *str2);
int        mod_basename_len(const char *file);
char      *mod_basename(const char *file);
char      *mod_find_module(const char *prefix, const char *module);
