/*
 *
 * module.c      Load modules needed for installation
 *
 * Copyright (c) 1996-2002  Hubert Mantel, SuSE Linux AG  (mantel@suse.de)
 * Copyright (c) 2002-2021  SUSE LLC
 *
 */

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/mount.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <ctype.h>

#include <hd.h>

#include "global.h"
#include "module.h"
#include "util.h"
#include "dialog.h"
#include "display.h"
#include "window.h"
#include "net.h"
#include "info.h"
#include "keyboard.h"
#include "auto2.h"
#include "file.h"
#include "install.h"

// #define DEBUG_MODULE

#define MENU_WIDTH		55
#define MODULE_CONFIG		"module.config"
#define CARDMGR_PIDFILE		"/run/cardmgr.pid"

#define MOD_EXT(mod)	{ .ext = (mod), .len = sizeof(mod) - 1 }

// list of recognized module file name extensions
// list ends with an empty entry
const mod_extensions_t mod_extensions[] = {
  MOD_EXT(".ko.xz"),
  MOD_EXT(".ko.gz"),
  MOD_EXT(".ko.zst"),
  MOD_EXT(".ko"),
  { }
};

static int mod_types = 0;
static int mod_type[MAX_MODULE_TYPES] = {};
static int mod_menu_last = 0;
static int mod_show_kernel_messages = 0;

static void mod_update_list(void);
static int mod_show_type(int type);
static int mod_build_list(int type, char ***list, module_t ***mod_list);
static char *mod_get_title(int type);
static int mod_menu_cb(int item);
static int mod_load_manually(int type);
static char *mod_get_params(module_t *mod);
static void mod_load_module_manual(char *module, int show);
static int mod_list_loaded_modules(char ***list, module_t ***mod_list, dia_align_t align);
static void mod_delete_module(void);
static void mod_auto_detect(void);

/*
 * return:
 *   >= 0	numerical module type
 *   < 0	'type_name' is unknown
 */
int mod_get_type(char *type_name)
{
  int i;

  for(i = 0; i < MAX_MODULE_TYPES; i++) {
    if(
      config.module.type_name[i] &&
      !strcasecmp(config.module.type_name[i], type_name)
    ) {
      return i;
    }
  }

  return -1;
}


/*
 * return:
 *   1/0	modules of type 'type_name' exist/do not exist
 */
int mod_check_modules(char *type_name)
{
  int i;
  module_t **mod_items;

  i = mod_get_type(type_name);

  if(i < 0) return 0;

  i = mod_build_list(i, NULL, &mod_items);

  return !i || (i == 1 && !*mod_items) ? 0 : 1;
}

static inline int mod_cmp_ext(const char *file, const size_t len,
    const char *ext, const size_t ext_len)
{
  return len >= ext_len && !strcmp(file + len - ext_len, ext);
}

/*
 * Return length of module file name without module extension if it is a
 * module file name. Otherwise 0.
 */
int mod_basename_len(const char *file)
{
  const mod_extensions_t *mod_ext;
  size_t len = strlen(file);

  for(mod_ext = mod_extensions; mod_ext->ext; mod_ext++) {
    if(mod_cmp_ext(file, len, mod_ext->ext, mod_ext->len)) {
      return len - mod_ext->len;
    }
  }

  return 0;
}


/*
 * Return file name with module extension removed if it is a module; else
 * return a copy of the file name.
 *
 * The returned storage must be released using free().
 */
char *mod_basename(const char *file)
{
  char *buf = strdup(file);
  int base_len = mod_basename_len(file);

  if(buf && base_len) buf[base_len] = 0;

  return buf;
}


/*
 * Return full module file name (including dir and file extension) or NULL
 * if module was not found.
 *
 * Returned storage must be released using free().
 */
char *mod_find_module(const char *prefix, const char *module)
{
  char *file = NULL;
  const mod_extensions_t *mod_ext;

  for(mod_ext = mod_extensions; mod_ext->ext; mod_ext++) {
    strprintf(&file, "%s/%s%s", prefix, module, mod_ext->ext);
    if(util_check_exist(file)) return file;
  }

  free(file);

  return NULL;
}


/*
 * openprom, nvram
 */
void mod_init(int autoload)
{
  char *tmp = NULL;
  module_t *ml;

  if(!config.net.devices) {
    util_update_netdevice_list(NULL, 1);
  }

  strprintf(&tmp, "%s/" MODULE_CONFIG, config.module.dir);
  file_read_modinfo(tmp);
  free(tmp);

  if(autoload && !config.test) {
    for(ml = config.module.list; ml; ml = ml->next) {
      if(ml->type == 0 /* 'autoload' section */ && ml->autoload) {
        mod_modprobe(ml->name, ml->param);
      }
    }
  }
}


module_t *mod_get_entry(char *name)
{
  module_t *ml, *mod_found = NULL;

  if(!name) return NULL;

  /*
   * A module might appear several times in config.module.list (because it's
   * in different categories).
   *
   * Prefer to return an entry that's user-visible (has a 'descr' field).
   */
  for(ml = config.module.list; ml; ml = ml->next) {
    if(!mod_cmp(ml->name, name)) {
      mod_found = ml;
      if(ml->descr) break;
    }
  }

  return mod_found;
}


void mod_update_list()
{
  module_t *ml, **ml1;
  struct dirent *de;
  DIR *d;

  for(ml1 = &config.module.list; *ml1; ml1 = &(*ml1)->next) (*ml1)->exists = 0;

  if(!(d = opendir(config.module.dir))) return;

  while((de = readdir(d))) {
    if(mod_basename_len(de->d_name)) {
      char *basename = mod_basename(de->d_name);
      int found = 0;

      for(found = 0, ml = config.module.list; ml; ml = ml->next) {
        /* Don't stop if it is an 'autoload' entry! */
        if(!strcmp(ml->name, basename)) {
          found = 1;
          ml->exists = 1;
          if(ml->type != 0) break;	/* 0: autoload, cf. file_read_modinfo() */
        }
      }

      /* unknown module */
      if(!found) {
        ml = *ml1 = calloc(1, sizeof **ml1);
        ml->exists = 1;
        ml->type = MAX_MODULE_TYPES - 1;	/* reserved for 'other' */
        ml->name = strdup(basename);
        ml->descr = strdup("");

        ml1 = &ml->next;
      }

      free(basename);
    }
  }

  closedir(d);
}


int mod_show_type(int type)
{
  module_t *ml;

  if(!config.module.type_name[type]) return 0;
  if(config.module.more_file[type]) return 1;

  for(ml = config.module.list; ml; ml = ml->next) {
    if(ml->type == type && ml->exists && ml->descr) return 1;
  }

  return 0;
}


int mod_build_list(int type, char ***list, module_t ***mod_list)
{
  module_t *ml;
  static char **items = NULL;
  static module_t **mod_items = NULL;
  static int mods = 0;
  int i, width;

  if(items) {
    for(i = 0; i < mods; i++) free(items[i]);
    free(items);
    free(mod_items);
  }

  mods = config.module.more_file[type] ? 1 : 0;

  for(ml = config.module.list; ml; ml = ml->next) {
    if(ml->type == type && ml->exists && ml->descr) mods++;
  }

  items = calloc(mods + 1, sizeof *items);
  mod_items = calloc(mods + 1, sizeof *mod_items);

  for(width = 0, ml = config.module.list; ml; ml = ml->next) {
    if(ml->type == type && ml->exists && ml->descr) {
      i = strlen(ml->name);
      if(i > width) width = i;
    }
  }

  for(i = 0, ml = config.module.list; ml; ml = ml->next) {
    if(ml->type == type && ml->exists && ml->descr) {
      strprintf(&items[i], "%*s%s%s",
        width,
        ml->name,
        *ml->descr ? ml->detected ? ml->active ? " * " : " + " : " : " : "", ml->descr
      );
      mod_items[i++] = ml;
    }
  }

  if(config.module.disks && config.module.more_file[type]) {
    items[i++] = strdup("-- More Modules --");
  }

  if(list) *list = items;
  if(mod_list) *mod_list = mod_items;

  return mods;
}


char *mod_get_title(int type)
{
  char *s = NULL;

  /* we have translations for these... */
  if(type) {
    if(type == config.module.network_type) {
      s = "Load Network Card Modules";
    }
    if(type == config.module.fs_type) {
      s = "Load File System Modules";
    }
    if(type == MAX_MODULE_TYPES - 1) {
      s = "Load Unknown Modules";
    }
  }

  if(s) {
    s = strdup(s);
  }
  else {
    strprintf(&s, "Load %s Modules", config.module.type_name[type]);
  }

  return s;
}


void mod_menu()
{
  char *extra_items[] = {
    "Show Loaded Modules",
    "Unload Modules",
    "Add Driver Update",
    "Show Driver Updates"
  };
  char *items[MAX_MODULE_TYPES + sizeof extra_items / sizeof *extra_items];
  int i, again;

  net_stop();

  do {
    mod_update_list();

    /*
     * Get list of actually present module types.
     *
     * Start with module type 1. 0 is reserved for 'autoload' and does not
     * show up in this menu.
     * This implies that this part of the list has max. MAX_MODULE_TYPES - 1
     * entries.
     */
    for(mod_types = 0, i = 1 /* 0 is reserved for 'autoload' */; i < MAX_MODULE_TYPES; i++) {
      if(mod_show_type(i)) {
        mod_type[mod_types] = i;
        items[mod_types++] = mod_get_title(i);
      }
    }

    for (i = 0; i < sizeof extra_items / sizeof *extra_items; i++) {
      items[mod_types + i] = extra_items[i];
    }

    items[mod_types + i] = NULL;

    again = dia_list("Kernel Modules (Hardware Drivers)", 40, mod_menu_cb, items, mod_menu_last, align_center);

    for(i = 0; i < mod_types; i++) free(items[i]);
  }
  while(again);
}


/*
 * return values:
 * -1    : abort (aka ESC)
 *  0    : ok
 *  other: stay in menu
 */
int mod_menu_cb(int item)
{
  mod_menu_last = item--;

  if(item < mod_types) {
    return mod_load_manually(mod_type[item]) ? 0 : 1;
  }

  switch(item - mod_types) {
    case 0:
      mod_show_modules();
      break;

    case 1:
      mod_delete_module();
      break;

    case 2:
      inst_update_cd();
      break;

    case 3:
      show_driver_updates();
      break;
  }

  return 1;
}


int mod_load_manually(int type)
{
  int i, ok;
  char *s;
  char **items;
  module_t **mod_items;
  int added = 0;

  mod_auto_detect();

  i = mod_build_list(type, &items, &mod_items);

  if(!i || (i == 1 && !*mod_items)) {
    return 0;	/* no new modules */
  }

  if(!i || (i == 1 && !*mod_items)) return 0;

  if(i) {
    s = mod_get_title(type);
    i = dia_list(s, MENU_WIDTH, NULL, items, 1, align_left);

    if(i--) {
      if(mod_items[i]) {
        ok = 1;
        if(mod_items[i]->pre_inst) {
          ok = mod_load_modules(mod_items[i]->pre_inst, 1);
        }
        if(ok) ok = mod_load_modules(mod_items[i]->name, 2);
        if(ok && mod_items[i]->post_inst) {
          ok = mod_load_modules(mod_items[i]->post_inst, 1);
        }
      }
      else {
        added = 0;
      }
    }

    free(s);
  }

  return added;
}


void mod_unload_module(char *module)
{
  char *cmd = NULL;
  int err;

  strprintf(&cmd, "rmmod %s", module);
  err = lxrc_run(cmd);
  free(cmd);

  util_update_kernellog();

  if(!err) {
    util_update_netdevice_list(module, 0);
    util_update_disk_list(module, 0);
    util_update_cdrom_list();
  }
}


int mod_is_loaded(char *module)
{
  file_t *f0, *f;

  f0 = file_read_file("/proc/modules", kf_none);

  for(f = f0; f; f = f->next) {
    if(!mod_cmp(f->key_str, module)) break;
  }

  file_free_file(f0);

  return f ? 1 : 0;
}


int mod_unload_modules(char *modules)
{
  int ok = 1;
  slist_t *sl0, *sl;

  sl0 = slist_reverse(slist_split(' ', modules));

  for(sl = sl0; sl; sl = sl->next) {
    if(mod_is_loaded(sl->key)) {
      mod_unload_module(sl->key);
      if(mod_is_loaded(sl->key)) ok = 0;
    }
  }

  slist_free(sl0);

  return ok;
}


int mod_load_modules(char *modules, int show)
{
  int ok = 1;
  slist_t *sl0, *sl;

  sl0 = slist_split(' ', modules);

  for(sl = sl0; sl && ok; sl = sl->next) {
    if(mod_is_loaded(sl->key)) {
      if(show == 2) {
        char *buf = NULL;
        strprintf(&buf, "Module \"%s\" has already been loaded.", sl->key);
        dia_message(buf, MSGTYPE_INFO);
        free(buf);
      }
    }
    else {
      mod_load_module_manual(sl->key, show);
      if(!mod_is_loaded(sl->key)) ok = 0;
    }
  }

  slist_free(sl0);

  return ok;
}


char *mod_get_params(module_t *mod)
{
  char *buf = NULL, *buf2 = NULL;
  slist_t *sl;
  int err;

  strprintf(&buf, "Enter parameters for \"%s\".", mod->name);

  if(mod->param) {
    strprintf(&buf, "%s\n\nExample: %s", buf, mod->param);
    if(mod->autoload) str_copy(&buf2, mod->param);
  }

  sl = slist_getentry(config.module.input_params, mod->name);

  if(sl && sl->value) str_copy(&buf2, sl->value);

  err = dia_input2(buf, &buf2, 30, 0);

  str_copy(&buf, NULL);

  if(err) return NULL;

  if(!sl) {
    sl = slist_add(&config.module.input_params, slist_new());
    sl->key = strdup(mod->name);
  }

  free(sl->value);
  sl->value = buf2;

  return sl->value;
}


void mod_load_module_manual(char *module, int show)
{
  module_t *ml;
  char *s;
  window_t win;
  int i;

  if(!(ml = mod_get_entry(module))) {
    mod_insmod(module, NULL);

    return;
  }

  if(!config.win) show = 0;

  if(show && !ml->dontask) {
    s = mod_get_params(ml);
  }
  else {
    s = ml->param && (ml->autoload || ml->dontask) ? ml->param : "";
  }

  if(show && s) {
    char *buf = NULL;

    strprintf(&buf,
      "Trying to load module \"%s\"...\n\n"
      "During loading, you may want to watch the kernel messages on virtual console 4 (ALT-F4). Use ALT-F1 to switch back to this menu.",
      ml->name
    );
    dia_info(&win, buf, MSGTYPE_INFO);
    mod_insmod(ml->name, s);
    win_close(&win);
    i = mod_is_loaded(ml->name);
    if(i) {
      strprintf(&buf, "Module \"%s\" loaded successfully.", ml->name);
      dia_message(buf, MSGTYPE_INFO);
    }
    else {
      util_beep(FALSE);
      strprintf(&buf, "Failed to load module \"%s\".", ml->name);
      dia_message(buf, MSGTYPE_ERROR);
    }

    free(buf);
  }
  else {
    mod_insmod(ml->name, s);
  }
}


int mod_insmod(char *module, char *param)
{
  char *buf = NULL;
  int err, cnt;
  slist_t *sl;
  driver_t *drv;

  if((sl = slist_getentry(config.module.options, module))) {
    param = sl->value;
  }

  log_debug("mod_insmod(%s, %s)\n", module, param ?: "");

  if(!module || config.test) return 0;

  if(mod_is_loaded(module)) return 0;

  /*
   * When loading a module before linuxrc's module loading framework has
   * been initialized, use modprobe instead of insmod as module deps are not
   * yet known to linuxrc.
   */
  unsigned use_modprobe = config.module.modprobe_ok && (config.module.list ? 0 : 1);

  strprintf(&buf, "%s %s",
    use_modprobe ? "modprobe" : "insmod",
    config.forceinsmod ? "-f " : ""
  );

  /* insmod expects a full path as argument, modprobe not */
  if(use_modprobe || util_check_exist(module)) {
    strprintf(&buf, "%s%s", buf, module);
  }
  else {
    char *filename = mod_find_module(config.module.dir, module);
    if(!filename) {
      free(buf);
      return -1;
    }
    strprintf(&buf, "%s%s", buf, filename);
    free(filename);
  }

  if(slist_getentry(config.module.broken, module)) {
    log_info("%s tagged as broken, not loaded\n", module);
    return -1;
  }

  if(param && *param) strprintf(&buf, "%s '%s'", buf, param);

  if(config.run_as_linuxrc) {
    util_update_netdevice_list(NULL, 1);
    util_update_disk_list(NULL, 1);
    util_update_cdrom_list();

    if(mod_show_kernel_messages) kbd_switch_tty(config.kbd_fd, 4);
  }

  err = lxrc_run(buf);

  free(buf);

  if(config.module.delay > 0) sleep(config.module.delay);

  cnt = 0;

  if(!err) {
    for(drv = config.module.drivers; drv; drv = drv->next) {
      if(drv->name && !strcmp(drv->name, module)) {
        cnt += apply_driverid(drv);
      }
    }
  }
  else {
    log_info("insmod error: %d\n", err);
  }

  if(cnt) sleep(config.module.delay + 1);

  if(config.run_as_linuxrc) {
    scsi_rename();

    if(!err && param) {
      while(isspace(*param)) param++;
      if(*param) {
        sl = slist_add(&config.module.used_params, slist_new());
        sl->key = strdup(module);
        sl->value = strdup(param);
      }
    }

    if(mod_show_kernel_messages) kbd_switch_tty(config.kbd_fd, 1);

    util_update_kernellog();

    if(!err) {
      util_update_netdevice_list(module, 1);
      util_update_disk_list(module, 1);
      util_update_cdrom_list();
    }
  }

  return err;
}


int mod_modprobe(char *module, char *param)
{
  int err;
  module_t *ml;

  if(!module) return -1;

  ml = mod_get_entry(module);

  if(!ml) return mod_insmod(module, param);

  err = 0;
  if(ml->pre_inst) {
    err = !mod_load_modules(ml->pre_inst, 0);
  }
  if(!err) err = mod_insmod(ml->name, param);
  if(!err && ml->post_inst) {
    err = mod_load_modules(ml->post_inst, 0);
  }

  return err;
}


int mod_list_loaded_modules(char ***list, module_t ***mod_list, dia_align_t align)
{
  static char **item = NULL;
  static module_t **mods = NULL;
  static int max_mods = 0;
  char *s, *t;
  int i, items = 0;
  module_t *mod;
  file_t *f0, *f;

  if(item) {
    for(i = 0; i < max_mods; i++) free(item[i]);
    free(item);
    free(mods);
  }

  f0 = file_read_file("/proc/modules", kf_none);

  for(max_mods = 2, f = f0; f && f->next; f = f->next) max_mods++;

  item = calloc(max_mods, sizeof *item);
  mods = calloc(max_mods, sizeof *mods);

  for(; f; f = f->prev) {
    mod = mod_get_entry(f->key_str);
    if(mod && mod->descr) {
      t = *mod->descr ? mod->descr : mod->name;
      if(align == align_left) {
        s = malloc(MENU_WIDTH);
        strncpy(s, t, MENU_WIDTH);
        s[MENU_WIDTH - 1] = 0;
        util_fill_string(s, MENU_WIDTH - 4);
      }
      else {
        s = strdup(t);
      }
      mods[items] = mod;
      item[items++] = s;
      if(items >= max_mods - 1) break;
    }
  }

  file_free_file(f0);

  mods[items] = NULL;
  item[items] = NULL;

  if(list) *list = item;
  if(mod_list) *mod_list = mods;

  return items;
}


void mod_show_modules()
{
  char **list;
  int items;
#ifdef DEBUG_MODULE
  slist_t *sl;
#endif

  items = mod_list_loaded_modules(&list, NULL, align_left);

#ifdef DEBUG_MODULE
  for(sl = config.module.used_params; sl; sl = sl->next) {
    log_info("  %s: >%s<\n", sl->key, sl->value);
  }
#endif

  if(items) {
    dia_show_lines("Show Loaded Modules", list, items, MENU_WIDTH, FALSE);
  }
  else {
    dia_message("No modules currently loaded.", MSGTYPE_INFO);
  }
}


void mod_delete_module()
{
  char **list;
  int items, choice;
  module_t *mod, **mod_list;

  items = mod_list_loaded_modules(&list, &mod_list, align_none);

  if(items) {
    choice = dia_list("Choose the module to unload.", MENU_WIDTH, NULL, list, 1, align_left);
    if(choice > 0) {
      mod = mod_list[choice - 1];
      if(mod->post_inst) {
        mod_unload_modules(mod->post_inst);
      }
      mod_unload_modules(mod->name);
      if(mod->pre_inst) {
        mod_unload_modules(mod->pre_inst);
      }
    }
  }
  else {
    dia_message("No modules currently loaded.", MSGTYPE_INFO);
  }
}


void mod_disk_text(char *buf, int type)
{
  if(!buf) return;

  strcat(buf, "Insert a modules disk.");
}


/*
 * Compare module names.
 */
int mod_cmp(char *str1, char *str2)
{
  char *s;
  int i;

  if(!str1 || !str2) return 1;

  str1 = strdup(str1);
  str2 = strdup(str2);

  for(s = str1; *s; s++) if(*s == '-') *s = '_';
  for(s = str2; *s; s++) if(*s == '-') *s = '_';

  i = strcmp(str1, str2);  

  free(str1);
  free(str2);

  return i;
}


/*
 * Update list of detected modules.
 */
void mod_auto_detect()
{
  hd_data_t *hd_data = NULL;
  hd_t *hd;
  driver_info_t *di;
  str_list_t *sl;
  hd_hw_item_t hw_items[] = { hw_pci, hw_pcmcia, hw_usb, 0 };
  module_t *ml;

  if(config.manual >= 2) return;

  hd_data = calloc(1, sizeof *hd_data);

  hd = hd_list2(hd_data, hw_items, 1);

  for(; hd; hd = hd->next) {
    for(di = hd->driver_info; di; di = di->next) {
      if(di->any.type != di_module) continue;

      for(sl = di->module.names; sl; sl = sl->next) {
        for(ml = config.module.list; ml; ml = ml->next) {
          if(!mod_cmp(ml->name, sl->str)) {
            ml->detected = 1;
            ml->active = di->module.active ? 1 : 0;
          }
        }
      }
    }
  }

  hd_free_hd_data(hd_data);

  free(hd_data);

  file_t *f, *f0 = file_read_file("/proc/modules", kf_none);

  for(f = f0; f; f = f->next) {
    module_t *mod;
    for(mod = config.module.list; mod; mod = mod->next) {
      if(!mod_cmp(mod->name, f->key_str)) {
        mod->detected = mod->active = 1;
      }
    }
  }

  file_free_file(f0);
}
