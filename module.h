/*
 *
 * module.h      Header file for module.c
 *
 * Copyright (c) 1996-2002  Hubert Mantel, SuSE Linux AG  (mantel@suse.de)
 *
 */

#include "module_ids.h"

#define MOD_TYPE_SCSI      1
#define MOD_TYPE_NET       2
#define MOD_TYPE_OTHER     3

typedef struct
    {
    enum modid_t   id;
    char          *description;
    char           module_name [30];
    char          *example;
    int            order;
    } module_t;

int mod_get_type(char *type_name);
int mod_check_modules(char *type_name);
module2_t *mod_get_entry(char *name);
int mod_add_disk(int prompt, int type);
int mod_pcmcia_ok(void);
extern void  mod_menu            (void);
extern int   mod_load_module     (char *module_tv, char *params_tv);
extern void  mod_unload_module   (char *module_tv);
extern void  mod_show_modules    (void);
extern void  mod_free_modules    (void);
extern int   mod_get_ram_modules (int type_iv);
extern int   mod_load_by_user    (int mod_type_iv);
extern int   mod_auto            (int type_iv);
extern void  mod_init            (void);
extern int   mod_get_mod_type    (char *name_tv);
extern void  mod_autoload        (void);
extern int   mod_force_moddisk_im;
extern int   mod_load_modules    (char *modules, int show);
void mod_disk_text(char *buf, int type);
void mod_update_netdevice_list(char *module, int add);
