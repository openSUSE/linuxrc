/*
 *
 * module.c      Load modules needed for installation
 *
 * Copyright (c) 1996-2001  Hubert Mantel, SuSE GmbH  (mantel@suse.de)
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

#include "global.h"
#include "text.h"
#include "module.h"
#include "util.h"
#include "dialog.h"
#include "display.h"
#include "window.h"
#include "modparms.h"
#include "rootimage.h"
#include "net.h"
#include "info.h"
#include "keyboard.h"
#include "auto2.h"
#include "file.h"

#include "module_list.h"

/* defined in pcmcia/cardmgr.c */
extern char *pidfile;

#define NR_SCSI_MODULES     (sizeof(mod_scsi_mod_arm)/sizeof(mod_scsi_mod_arm[0]))
#define NR_CDROM_MODULES    (sizeof(mod_cdrom_mod_arm)/sizeof(mod_cdrom_mod_arm[0]))
#define NR_NET_MODULES      (sizeof(mod_net_mod_arm)/sizeof(mod_net_mod_arm[0]))
#define NR_MODULES          (NR_SCSI_MODULES+NR_CDROM_MODULES+NR_NET_MODULES+50)
#define NR_NO_AUTOPROBE     (sizeof(mod_noauto_arm)/sizeof(mod_noauto_arm[0]))
#define MENU_WIDTH          55

static module_t  mod_current_arm [NR_MODULES];
static int       mod_show_kernel_im = FALSE;
int       mod_force_moddisk_im = FALSE;


static int       mod_try_auto         (module_t *module_prv,
                                       window_t *status_prv);
static int       mod_auto_allowed     (enum modid_t id_iv);
static void      mod_delete_module    (void);
static int       mod_menu_cb          (int what_iv);
static int       mod_get_current_list (int mod_type_iv, int *nr_modules_pir,
                                       int *more_pir);
static void      mod_sort_list        (module_t modlist_parr [], int nr_modules_iv);
static int       mod_getmoddisk       (int mod_type);


#define DEBUG_MODULE

#define MODULE_CONFIG "module.config"

static int mod_types = 0;
static int mod_type[MAX_MODULE_TYPES] = {};
static int mod_menu_last = 0;
static char *mod_param_text = NULL;

static int mod_copy_modules(char *src_dir, int doit);
static module2_t *mod_get_entry(char *name);
static void mod_update_list(void);
static char *mod_get_title(int type);
static int mod_show_type(int type);
static int mod_build_list(int type, char ***list, module2_t ***mod_list);
static int mod_load_manually(int type);
static int mod_add_disk(int type);
static int mod_list_loaded_modules(char ***list, module2_t ***mod_list, dia_align_t align);
static int mod_is_loaded(char *module);
static int mod_unload_modules(char *modules);
static int mod_load_modules(char *modules, int show);
static char *mod_get_params(module2_t *mod);
static void mod_load_module_manual(char *module, int show);
static int mod_pcmcia_ok(void);
static int mod_load_pcmcia(void);
static int mod_pcmcia_chipset(void);


int mod_copy_modules(char *src_dir, int doit)
{
  struct dirent *de;
  DIR *d;
  char buf[256];
  int i, i1, i2, cnt = 0, ok;
  window_t win;
  static int files = 0;
  struct stat sbuf1, sbuf2;

  if(doit == 2 && !files) return 0;
  if(!(d = opendir(src_dir))) return 0;

  if(doit == 2) {
    dia_status_on(&win, "Copying modules...");
  }
  else {
    files = 0;
  }

  while((de = readdir(d))) {
    i = strlen(de->d_name);
    if(
      i >= 3 &&
      (
        (de->d_name[i - 2] == '.' && de->d_name[i - 1] == 'o') ||
        !strcmp(de->d_name, MODULE_CONFIG)
      )
    ) {
      ok = 0;
      if(doit == 2) {
        /*
         * Copy only modules that are 'new': different size & date. This is
         * not perfect but will do in this case.
         */
        sprintf(buf, "%s/%s", src_dir, de->d_name);
        i1 = stat(buf, &sbuf1);
        sprintf(buf, "%s/%s", config.module.dir, de->d_name);
        i2 = stat(buf, &sbuf2);
        if(!i1 && !i2) {
          if(sbuf1.st_size == sbuf2.st_size && sbuf1.st_mtime == sbuf2.st_mtime) ok = 1;
        }
      }
      if(!ok) {
        if(doit) {
          sprintf(buf, "cp -p %s/%s %s", src_dir, de->d_name, config.module.dir);
          fprintf(stderr, "%s\n", buf);
          system(buf);
          if(doit == 2) dia_status(&win, (cnt++ * 100) / files);
        }
        else {
          files++;
        }
      }
    }
  }

  closedir(d);

  if(doit == 2) {
    if(cnt) usleep(200000);
    win_close(&win);
  }

  return cnt;
}



void mod_free_modules()
{
  if(config.module.ramdisk) {
    umount(config.module.dir);
    util_free_ramdisk(RAMDISK_2);
    config.module.ramdisk = 0;
  }
}


/*
 * openprom, nvram
 */
void mod_init()
{
  char tmp[256];
  module2_t *ml;

  setenv("MODPATH", config.module.dir, 1);

  sprintf(tmp, "%s/" MODULE_CONFIG, config.module.dir);
  file_read_modinfo(tmp);

  for(ml = config.module.list; ml; ml = ml->next) {
    if(ml->type == 1 /* 'autoload' section */ && ml->autoload) {
      mod_load_module(ml->name, ml->param);
    }
  }
}


module2_t *mod_get_entry(char *name)
{
  module2_t *ml;

  if(!name) return NULL;

  for(ml = config.module.list; ml; ml = ml->next) {
    if(!strcmp(ml->name, name)) break;
  }

  return ml;
}


void mod_update_list()
{
  module2_t *ml, **ml1;
  struct dirent *de;
  DIR *d;
  char buf[32];
  int i;

  for(ml1 = &config.module.list; *ml1; ml1 = &(*ml1)->next) (*ml1)->exists = 0;

  if(!(d = opendir(config.module.dir))) return;

  while((de = readdir(d))) {
    i = strlen(de->d_name);
    if(
      i >= 3 &&
      i < sizeof buf &&
      de->d_name[i - 2] == '.' &&
      de->d_name[i - 1] == 'o'
    ) {
      strcpy(buf, de->d_name);
      buf[i - 2] = 0;

      for(ml = config.module.list; ml; ml = ml->next) {
        if(!strcmp(ml->name, buf)) {
          ml->exists = 1;
          break;
        }
      }

      /* unknown module */
      if(!ml) {
        ml = *ml1 = calloc(1, sizeof **ml1);
        ml->exists = 1;
        ml->type = MAX_MODULE_TYPES - 1;	/* reserved for 'other' */
        ml->name = strdup(buf);
        ml->descr = strdup("");

        ml1 = &ml->next;
      }
    }
  }

  closedir(d);

}


int mod_show_type(int type)
{
  module2_t *ml;

  if(!config.module.type_name[type]) return 0;
  if(config.module.more_file[type]) return 1;

  for(ml = config.module.list; ml; ml = ml->next) {
    if(ml->type == type && ml->exists && ml->descr) return 1;
  }

  return 0;
}


int mod_build_list(int type, char ***list, module2_t ***mod_list)
{
  module2_t *ml;
  static char **items = NULL;
  static module2_t **mod_items = NULL;
  static int mods = 0;
  int i;
  char buf[256];

  if(items) {
    for(i = 0; i < mods; i++) if(items[i]) free(items[i]);
    free(items);
    free(mod_items);
  }

  mods = config.module.more_file[type] ? 1 : 0;

  for(ml = config.module.list; ml; ml = ml->next) {
    if(ml->type == type && ml->exists && ml->descr) mods++;
  }

  items = calloc(mods + 1, sizeof *items);
  mod_items = calloc(mods + 1, sizeof *mod_items);

  for(i = 0, ml = config.module.list; ml; ml = ml->next) {
    if(ml->type == type && ml->exists && ml->descr) {
      sprintf(buf, "%14s%s%s", ml->name, *ml->descr ? " : " : "", ml->descr);
      items[i] = strdup(buf);
      mod_items[i++] = ml;
    }
  }

  if(config.module.more_file[type]) {
    items[i++] = strdup(txt_get(TXT_MORE_MODULES));
  }

  if(list) *list = items;
  if(mod_list) *mod_list = mod_items;

  return mods;
}


char *mod_get_title(int type)
{
  char buf[256], *s = NULL;

  /* we have translations for these... */
  if(type) {
    if(type == config.module.scsi_type) {
      s = txt_get(TXT_LOAD_SCSI);
    }
    if(type == config.module.cdrom_type) {
      s = txt_get(TXT_LOAD_CDROM);
    }
    if(type == config.module.network_type) {
      s = txt_get(TXT_LOAD_NET);
    }
    if(type == config.module.pcmcia_type) {
      s = txt_get(TXT_LOAD_PCMCIA);
    }
  }

  if(!s) {
    sprintf(buf, "Load %s modules", config.module.type_name[type]);
    s = buf;
  }

  return strdup(s);
}


void mod_menu(void)
{
  char *items[MAX_MODULE_TYPES + 3];
  int i;
  int again;

  net_stop();

  do {
    mod_update_list();

    for(mod_types = 0, i = 2; i < MAX_MODULE_TYPES; i++) {
      if(mod_show_type(i)) {
        mod_type[mod_types] = i;
        items[mod_types++] = mod_get_title(i);
      }
    }

    i = mod_types;

    items[i++] = txt_get(TXT_SHOW_MODULES);
    items[i++] = txt_get(TXT_DEL_MODULES);
#if 0
    items[i++] = txt_get(TXT_AUTO_LOAD);
#endif

    items[i] = NULL;

    again = dia_list(txt_get(TXT_MENU_MODULES), 40, mod_menu_cb, items, mod_menu_last, align_center);

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
int mod_menu_cb (int item)
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
      mod_autoload();
      break;
  }

  return 1;
}


int mod_load_manually(int type)
{
  int i, ok;
  char *s;
  char **items;
  module2_t **mod_items;
  int added = 0;

  i = mod_build_list(type, &items, &mod_items);

  if(i == 1 && !*mod_items) {
    /* list has just a 'more modules' line */
    mod_add_disk(type);
    if(type == config.module.pcmcia_type) mod_load_pcmcia();
    added = 1;
  }
  else if(i) {
    if(type == config.module.pcmcia_type) {
      mod_load_pcmcia();
    }
    else {
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
          mod_add_disk(type);
          added = 1;
        }
      }

      free(s);
    }
  }

  return added;
}


int mod_add_disk(int type)
{
  char buf[256];
  int i, err = 0, added = 0;
  int got_image = 0;

  if(dia_message(txt_get(TXT_ENTER_MODDISK), MSGTYPE_INFO) == -1) return -1;

  mod_free_modules();

  for(i = 0; i < config.floppies; i++) {
    if(!util_try_mount(config.floppy_dev[i], config.mountpoint.floppy, MS_MGC_VAL | MS_RDONLY, 0)) break;
  }

  if(i < config.floppies) {
    config.floppy = i;	// remember currently used floppy
  }
  else {
    err = 1;
    /* Try /dev/fd0 anyway, in case the user has inserted a floppy _now_. */
    if(!config.floppies) {
      err = util_try_mount("/dev/fd0", config.mountpoint.floppy, MS_MGC_VAL | MS_RDONLY, 0);
    }
    if(err) {
      dia_message(txt_get(TXT_ERROR_READ_DISK), MSGTYPE_ERROR);
      return err;
    }
  }

  *buf = 0;
  if(config.module.more_file[type] && *config.module.more_file[type]) {
    sprintf(buf, "%s/%s", config.mountpoint.floppy, config.module.more_file[type]);
    if(util_check_exist(buf)) {
      err = root_load_rootimage(buf);
      if(!err) got_image = 1;
    }
  }

  if(*buf && !err && got_image) {
    err = util_try_mount(
      RAMDISK_2,
      config.tmpfs ? config.mountpoint.ramdisk2 : config.module.dir,
      MS_MGC_VAL | MS_RDONLY,
      0
    );

    if(err) {
      dia_message(txt_get(TXT_ERROR_READ_DISK), MSGTYPE_ERROR);
    }
    else {
      if(config.tmpfs) {
        mod_copy_modules(config.mountpoint.ramdisk2, 1);
        umount(config.mountpoint.ramdisk2);
        util_free_ramdisk(RAMDISK_2);
      }
      else {
        config.module.ramdisk = 1;
      }
      mod_init();
    }
  }

  if(config.tmpfs) {
    mod_copy_modules(config.mountpoint.floppy, 0);
    added = mod_copy_modules(config.mountpoint.floppy, 2);
    mod_init();
  }

  umount(config.mountpoint.floppy);

  if(!err) {
    if(!got_image && !added) {
      dia_message("No new modules found.", MSGTYPE_INFO);
    }
    else {
      /* not needed, we're falling back to mod_menu() anyway */
      /* mod_update_list(); */
    }
  }

  return err;
}


void mod_unload_module(char *module)
{
  char cmd[300];

  sprintf(cmd, "rmmod %s", module);
  system(cmd);
  util_update_kernellog();
}


int mod_is_loaded(char *module)
{
  file_t *f0, *f;

  f0 = file_read_file("/proc/modules");

  for(f = f0; f; f = f->next) {
    if(!strcmp(f->key_str, module)) break;
  }

  file_free_file(f0);

  return f ? 1 : 0;
}


int mod_unload_modules(char *modules)
{
  char *s;
  int i, len, ok = 1;

  modules = strdup(modules);

  len = strlen(modules);

  for(i = 0; i < len; i++) if(isspace(modules[i])) modules[i] = 0;

  for(s = modules + len - 1; s >= modules; s--) {
    if(*s && (s == modules || !s[-1])) {
      if(mod_is_loaded(s)) {
        mod_unload_module(s);
        if(mod_is_loaded(s)) ok = 0;
      }
    }
  }

  free(modules);

  return ok;
}


int mod_load_modules(char *modules, int show)
{
  char *s, buf[256];
  int i, len, ok = 1;

  modules = strdup(modules);

  len = strlen(modules);

  for(i = 0; i < len; i++) if(isspace(modules[i])) modules[i] = 0;

  for(s = modules; s < modules + len && ok; s++) {
    if(*s && (s == modules || !s[-1])) {
      if(mod_is_loaded(s)) {
        if(show == 2) {
          sprintf(buf, "Module \"%s\" has already been loaded.", s);
          dia_message(buf, MSGTYPE_INFO);
        }
      }
      else {
        mod_load_module_manual(s, show);
        if(!mod_is_loaded(s)) ok = 0;
      }
    }
  }

  free(modules);

  return ok;
}


char *mod_get_params(module2_t *mod)
{
  char buf[MAX_PARAM_LEN + 100];
  char buf2[MAX_PARAM_LEN];
  slist_t *sl;

  if(mod_param_text) {
    strcpy(buf, mod_param_text);
  }
  else {
    sprintf(buf, txt_get(TXT_ENTER_PARAMS), mod->name);
  }

  *buf2 = 0;

  if(mod->param) {
    strcat(buf, txt_get(TXT_EXAMPLE_PAR));
    strcat(buf, mod->param);
    if(mod->autoload) strcpy(buf2, mod->param);
  }

  sl = slist_getentry(config.module.input_params, mod->name);

  if(sl && sl->value) strcpy(buf2, sl->value);

  if(dia_input(buf, buf2, sizeof buf2 - 1, 30)) return NULL;

  if(!sl) {
    sl = slist_add(&config.module.input_params, slist_new());
    sl->key = strdup(mod->name);
  }
  if(sl->value) free(sl->value);
  sl->value = strdup(buf2);

  return sl->value;
}


void mod_load_module_manual(char *module, int show)
{
  module2_t *ml;
  char *s, buf[256];
  window_t win;
  int i;

  if(!(ml = mod_get_entry(module))) return;

  if(!config.win) show = 0;

  if(show) {
    s = mod_get_params(ml);
  }
  else {
    s = ml->param && ml->autoload ? ml->param : "";
  }

  if(show) {
    if(s) {
      sprintf(buf, txt_get(TXT_TRY_TO_LOAD), ml->name);
      dia_info(&win, buf);
      mod_load_module(ml->name, s);
      win_close(&win);
      i = mod_is_loaded(ml->name);
      if(i) {
        sprintf(buf, txt_get (TXT_LOAD_SUCCESSFUL), ml->name);
        dia_message(buf, MSGTYPE_INFO);
      }
      else {
        util_beep(FALSE);
        sprintf(buf, txt_get(TXT_LOAD_FAILED), ml->name);
        dia_message(buf, MSGTYPE_ERROR);
      }
    }
  }
  else {
    mod_load_module(ml->name, s);
    i = mod_is_loaded(ml->name);
    if(!i) {
      util_beep(FALSE);
    }
  }
}


int mod_load_module(char *module, char *param)
{
  char cmd[300];
  int err;
  char *force = config.forceinsmod ? "-f " : "";
  slist_t *sl;

#if 0
  if(!config.win) {
    printf("loading module %s...", module);
    fflush(stdout);
  }
#endif

  if(mod_is_loaded(module)) return 0;

  sprintf(cmd, "insmod %s%s ", force, module);

  if(param && *param) strcat(cmd, param);

  fprintf(stderr, "%s\n", cmd);

  if(mod_show_kernel_im) kbd_switch_tty(4);

  err = system(cmd);

  if(!err && param) {
    sl = slist_add(&config.module.used_params, slist_new());
    sl->key = strdup(module);
    sl->value = strdup(param);
  }

  if(mod_show_kernel_im) kbd_switch_tty(1);

  util_update_kernellog();

#if 0
  if(!config.win) {
    printf(" %s\n", rc_ii ? "failed" : "ok");
  }
#endif

  return err;
}


int mod_list_loaded_modules(char ***list, module2_t ***mod_list, dia_align_t align)
{
  static char **item = NULL;
  static module2_t **mods = NULL;
  static int max_mods = 0;
  char *s, *t;
  int i, items = 0;
  module2_t *mod;
  file_t *f0, *f;

  if(item) {
    for(i = 0; i < max_mods; i++) if(item[i]) free(item[i]);
    free(item);
    free(mods);
  }

  f0 = file_read_file("/proc/modules");

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
    fprintf(stderr, "  %s: %s\n", sl->key, sl->value);
  }
#endif

  if(items) {
    dia_show_lines(txt_get(TXT_SHOW_MODULES), list, items, MENU_WIDTH, FALSE);
  }
  else {
    dia_message(txt_get(TXT_NO_MODULES), MSGTYPE_INFO);
  }
}


void mod_delete_module()
{
  char **list;
  int items, choice;
  module2_t *mod, **mod_list;

  items = mod_list_loaded_modules(&list, &mod_list, align_none);

  if(items) {
    choice = dia_list(txt_get(TXT_DELETE_MODULE), MENU_WIDTH, NULL, list, 1, align_left);
    if(choice > 0) {
      mod = mod_list[choice - 1];
      if(mod->post_inst) {
        mod_unload_modules(mod->post_inst);
      }
      mod_unload_modules(mod->name);
    }
  }
  else {
    dia_message(txt_get(TXT_NO_MODULES), MSGTYPE_INFO);
  }
}


int mod_get_ram_modules (int type_iv)
    {
    char      testfile_ti [MAX_FILENAME];
    char     *modfile_pci;
    int       rc_ii = 0;
    int i;


    strcpy (testfile_ti, config.module.dir);
    if (type_iv == MOD_TYPE_SCSI)
        {
        strcat (testfile_ti, "/SCSI");
        modfile_pci = "scsi-mod.gz";
        }
    else if (type_iv == MOD_TYPE_NET)
        {
        strcat (testfile_ti, "/NET");
        modfile_pci = "net-mod.gz";
        }
    else
        {
        strcat (testfile_ti, "/OTHER");
        modfile_pci = "other-mod.gz";
        }

    if (!util_check_exist (testfile_ti))
        mod_free_modules ();

    if (!util_check_exist (testfile_ti) || mod_force_moddisk_im)
        {
        for(i = 0; i < config.floppies; i++) {
          if(!util_try_mount(config.floppy_dev[i], mountpoint_tg, MS_MGC_VAL | MS_RDONLY, 0)) break;
        }
        if(i < config.floppies) {
          config.floppy = i;	// remember currently used floppy
        }
        else {
          rc_ii = -1;
          /* Try /dev/fd0 anyway, in case the user has inserted a floppy _now_. */
          if(!config.floppies) {
            rc_ii = util_try_mount("/dev/fd0", mountpoint_tg, MS_MGC_VAL | MS_RDONLY, 0);
          }
          if(rc_ii) {
            dia_message (txt_get (TXT_ERROR_READ_DISK), MSGTYPE_ERROR);
            mod_force_moddisk_im = FALSE;
            return rc_ii;
          }
        }

        sprintf (testfile_ti, "%s/%s", mountpoint_tg, modfile_pci);
        rc_ii = root_load_rootimage (testfile_ti);
        umount (mountpoint_tg);
        if (!rc_ii)
            {
            rc_ii = util_try_mount (RAMDISK_2, config.module.dir, MS_MGC_VAL | MS_RDONLY, 0);

            if (rc_ii)
                dia_message (txt_get (TXT_ERROR_READ_DISK), MSGTYPE_ERROR);

//            mod_ram_modules_im = TRUE;
            mod_force_moddisk_im = FALSE;
            }
        }

    return (rc_ii);
    }


int mod_auto (int type_iv)
    {
    int       nr_modules_ii;
    int       i_ii;
    window_t  win_ri;
    int       rc_ii;
    int       success_ii;
    int       dummy_ii;


    rc_ii = mod_get_ram_modules (type_iv);
    if (rc_ii)
        return (1);

    rc_ii = mod_get_current_list (type_iv, &nr_modules_ii, &dummy_ii);
    if (rc_ii)
        return (1);

    dia_status_on (&win_ri, "");
    success_ii = FALSE;
    i_ii = 0;

    while (i_ii < nr_modules_ii && !success_ii)
        {
        dia_status (&win_ri, ((i_ii + 1) * 100) / nr_modules_ii);
        if (!mod_try_auto (&mod_current_arm [i_ii], &win_ri))
            success_ii = TRUE;
        else
            i_ii++;
        }

    win_close (&win_ri);
    if (success_ii)
        return (0);
    else
        return (-1);
    }


int mod_get_mod_type (char *name_tv)
    {
    int  i_ii;
    int  found_ii = FALSE;

    /* This really needs fixing! -- snwint */
    if(strstr(name_tv, "i2o_") == name_tv) return MOD_TYPE_SCSI;

    i_ii = 0;
    while (i_ii < NR_SCSI_MODULES && !found_ii)
        if (!strcmp (name_tv, mod_scsi_mod_arm [i_ii].module_name))
            found_ii = TRUE;
        else
            i_ii++;

    if (found_ii)
        return (MOD_TYPE_SCSI);

    i_ii = 0;
    while (i_ii < NR_CDROM_MODULES && !found_ii)
        if (!strcmp (name_tv, mod_cdrom_mod_arm [i_ii].module_name))
            found_ii = TRUE;
        else
            i_ii++;

    if (found_ii)
        return (MOD_TYPE_OTHER);

    i_ii = 0;
    while (i_ii < NR_NET_MODULES && !found_ii)
        if (!strcmp (name_tv, mod_net_mod_arm [i_ii].module_name))
            found_ii = TRUE;
        else
            i_ii++;

    if (found_ii)
        return (MOD_TYPE_NET);
    else
        return (-1);
    }


void mod_autoload (void)
    {
    int       i_ii;
    int       nr_iv = 0;
    window_t  win_ri;
    char      text_ti [200];
    int       rc_ii;
    int       nr_modules_ii, more_ii;

    rc_ii = mod_get_current_list (MOD_TYPE_SCSI, &nr_modules_ii, &more_ii);

    /* what if there are _no_ network mods on the modules disk??? */
    if (rc_ii || !nr_modules_ii)
    {
        if (mod_getmoddisk (MOD_TYPE_SCSI)) return;
    }

    dia_status_on (&win_ri, "");

    config.suppress_warnings = 1;
    rc_ii = mod_get_ram_modules (MOD_TYPE_SCSI);
    config.suppress_warnings = 0;

    if (!rc_ii)
        {
        for (i_ii = 0; i_ii < NR_SCSI_MODULES; i_ii++)
            {
            dia_status (&win_ri, (nr_iv++ * 100) / (NR_SCSI_MODULES + NR_NET_MODULES));
            if (!mod_try_auto (&mod_scsi_mod_arm [i_ii], &win_ri))
                {
                if (!scsi_tg [0])
                    strcpy (scsi_tg, mod_scsi_mod_arm [i_ii].module_name);
                sprintf (text_ti, txt_get (TXT_LOAD_SUCCESSFUL),
                         mod_scsi_mod_arm [i_ii].module_name);
                strcat (text_ti, "\n\n");
                strcat (text_ti, txt_get (TXT_WANT_MORE_SCSI));
                if (!auto_ig && dia_yesno (text_ti, NO) != YES)
                    {
                    i_ii = NR_SCSI_MODULES;
                    nr_iv = NR_SCSI_MODULES;
                    }
                }
            }
        }

    config.suppress_warnings = 1;
    rc_ii = mod_get_ram_modules (MOD_TYPE_NET);
    config.suppress_warnings = 0;

    if (!rc_ii)
        {
        for (i_ii = 0; i_ii < NR_NET_MODULES; i_ii++)
            {
            dia_status (&win_ri, (nr_iv++ * 100) / (NR_SCSI_MODULES + NR_NET_MODULES));
            if (!mod_try_auto (&mod_net_mod_arm [i_ii], &win_ri))
                {
                if (!net_tg [0])
                    strcpy (net_tg, mod_net_mod_arm [i_ii].module_name);
                sprintf (text_ti, txt_get (TXT_LOAD_SUCCESSFUL),
                         mod_net_mod_arm [i_ii].module_name);
                strcat (text_ti, "\n\n");
                strcat (text_ti, txt_get (TXT_WANT_MORE_NET));
                if (!auto_ig && dia_yesno (text_ti, NO) != YES)
                    {
                    i_ii = NR_NET_MODULES;
                    nr_iv = NR_SCSI_MODULES + NR_NET_MODULES;
                    }
                }
            }
        }

#if 0
    if (!mod_getmoddisk ())
        {
        rc_ii = mod_get_ram_modules (MOD_TYPE_OTHER);
        if (rc_ii)
            {
            win_close (&win_ri);
            return;
            }
        for (i_ii = 0; i_ii < NR_CDROM_MODULES; i_ii++)
            {
            dia_status (&win_ri, (nr_iv++ * 100) / NR_MODULES);
            if (!mod_try_auto (&mod_cdrom_mod_arm [i_ii], &win_ri))
                {
                if (!cdrom_tg [0])
                    strcpy (cdrom_tg, mod_cdrom_mod_arm [i_ii].module_name);
                sprintf (text_ti, txt_get (TXT_LOAD_SUCCESSFUL),
                         mod_cdrom_mod_arm [i_ii].module_name);
                strcat (text_ti, "\n\n");
                strcat (text_ti, txt_get (TXT_WANT_MORE_CDROM));
                if (dia_yesno (text_ti, NO) != YES)
                    i_ii = NR_CDROM_MODULES;
                }
            }
        }
#endif

    win_close (&win_ri);
    if (!auto_ig)
        {
        (void) dia_show_file (txt_get (TXT_INFO_KERNEL), kernellog_tg, TRUE);
        mod_show_modules ();
        }
    }


#if 0
/*
 *
 * Local functions
 *
 */

static int mod_choose_cb (int what_iv)
    {
    char      text_ti [200];
    int       rc_ii = 1;
    char      params_ti [MAX_PARAM_LEN];
    char      pcd_params_ti [MAX_PARAM_LEN];
    window_t  win_ri;
    module_t  pcd_ri = { ID_PPCD, "", "pcd", "drive0=0x378" };


    if (mod_current_arm [what_iv - 1].example)
        strcpy (params_ti, mod_current_arm [what_iv - 1].example);
    else
        params_ti [0] = 0;

    if (!mpar_get_params (&mod_current_arm [what_iv - 1], params_ti))
        {
        sprintf (text_ti, txt_get (TXT_TRY_TO_LOAD),
                 mod_current_arm [what_iv - 1].module_name);

        dia_info (&win_ri, text_ti);
        rc_ii = mod_load_module (mod_current_arm [what_iv - 1].module_name,
                                 params_ti);
        if (!rc_ii && mod_is_ppcd (mod_current_arm [what_iv - 1].module_name))
            {
            win_close (&win_ri);
            pcd_params_ti [0] = 0;
            mpar_get_params (&pcd_ri, pcd_params_ti);
            sprintf (text_ti, txt_get (TXT_TRY_TO_LOAD), "pcd");
            dia_info (&win_ri, text_ti);
            rc_ii = mod_load_module ("pcd", pcd_params_ti);
            if (!rc_ii)
                {
                mpar_save_modparams ("pcd", pcd_params_ti);
                strcpy (cdrom_tg, "pcd0");
                strcpy (ppcd_tg, mod_current_arm [what_iv - 1].module_name);
                }
            }

        win_close (&win_ri);

        if (rc_ii == 0)
            {
            sprintf (text_ti, txt_get (TXT_LOAD_SUCCESSFUL),
                     mod_current_arm [what_iv - 1].module_name);
            (void) dia_message (text_ti, MSGTYPE_INFO);
            mpar_save_modparams (mod_current_arm [what_iv - 1].module_name,
                                 params_ti);
            }
        else
            {
            if (mod_is_ppcd (mod_current_arm [what_iv - 1].module_name))
                mod_unload_module (mod_current_arm [what_iv - 1].module_name);

            util_beep (FALSE);
            sprintf (text_ti, txt_get (TXT_LOAD_FAILED),
                     mod_current_arm [what_iv - 1].module_name);
            (void) dia_message (text_ti, MSGTYPE_ERROR);
            }

        (void) dia_show_file (txt_get (TXT_INFO_KERNEL), lastlog_tg, TRUE);
        }

    if (rc_ii)
        return (what_iv);
    else
        return (0);
    }
#endif


static int mod_try_auto (module_t *module_prv, window_t *status_prv)
    {
    char   text_ti [STATUS_SIZE];
    int    rc_ii = -1;
    char   tmp_ti [12];


    memset (text_ti, ' ', STATUS_SIZE);
    strncpy (tmp_ti, module_prv->module_name, sizeof (tmp_ti) - 1);
    tmp_ti [sizeof (tmp_ti) - 1] = 0;
/*
 * Don't use translated text here for now. We should get around the
 * fixed buffer size at some point, though...
 */
//    sprintf (text_ti + 13, txt_get (TXT_AUTO_RUNNING), tmp_ti);
    sprintf (text_ti + 13, "Autoprobing (%s)...      ", tmp_ti);
    disp_set_color (status_prv->fg_color, status_prv->bg_color);
    win_print (status_prv, 2, 2, text_ti);
    fflush (stdout);

    if (mod_auto_allowed (module_prv->id) ||
        (/* demo_ig && */ mod_current_arm == mod_cdrom_mod_arm))
        rc_ii = mod_load_module (module_prv->module_name, module_prv->example);

    if (rc_ii == 0)
        mpar_save_modparams (module_prv->module_name, module_prv->example);

    return (rc_ii);
    }


static int mod_auto_allowed (enum modid_t id_iv)
    {
    int  i_ii = 0;
    int  found_ii = FALSE;

    while (i_ii < NR_NO_AUTOPROBE && !found_ii)
        if (id_iv == mod_noauto_arm [i_ii])
            found_ii = TRUE;
        else
            i_ii++;

    return (!found_ii);
    }


#if 0
static int mod_is_ppcd (char *name_tv)
    {
    int        i_ii = 0;
    int        found_ii = FALSE;
    module_t  *module_pri;


    module_pri = mod_get_description (name_tv);
    if (!module_pri)
        return (FALSE);

    while (i_ii < NR_PPCD && !found_ii)
        if (module_pri->id == mod_is_ppcd_arm [i_ii])
            found_ii = TRUE;
        else
            i_ii++;

    return (found_ii);
    }
#endif

#if 0
static module_t *mod_get_description (char *name_tv)
    {
    int  i_ii;
    int  found_ii = FALSE;


    i_ii = 0;
    while (i_ii < NR_SCSI_MODULES && !found_ii)
        if (!strcmp (name_tv, mod_scsi_mod_arm [i_ii].module_name))
            found_ii = TRUE;
        else
            i_ii++;

    if (found_ii)
        return (&mod_scsi_mod_arm [i_ii]);

    i_ii = 0;
    while (i_ii < NR_CDROM_MODULES && !found_ii)
        if (!strcmp (name_tv, mod_cdrom_mod_arm [i_ii].module_name))
            found_ii = TRUE;
        else
            i_ii++;

    if (found_ii)
        return (&mod_cdrom_mod_arm [i_ii]);

    i_ii = 0;
    while (i_ii < NR_NET_MODULES && !found_ii)
        if (!strcmp (name_tv, mod_net_mod_arm [i_ii].module_name))
            found_ii = TRUE;
        else
            i_ii++;

    if (found_ii)
        return (&mod_net_mod_arm [i_ii]);
    else
        return (0);
    }
#endif


static int mod_get_current_list (int mod_type_iv, int *nr_modules_pir,
                                 int *more_pir)
    {
    module_t      *test_modules_pri;
    struct dirent *module_pri;
    DIR           *directory_ri;
    int            i_ii;
    int            max_ii = 0;
    int            found_ii;
    char           tmp_ti [30];


    switch (mod_type_iv)
        {
        case MOD_TYPE_SCSI:
            test_modules_pri = mod_scsi_mod_arm;
            max_ii = NR_SCSI_MODULES;
            break;
        case MOD_TYPE_OTHER:
            test_modules_pri = mod_cdrom_mod_arm;
            max_ii = NR_CDROM_MODULES;
            break;
        case MOD_TYPE_NET:
            test_modules_pri = mod_net_mod_arm;
            max_ii = NR_NET_MODULES;
            break;
        default:
            return (-1);
            break;
        }

    directory_ri = opendir (config.module.dir);
    if (!directory_ri)
        return (-1);

    *nr_modules_pir = 0;
    *more_pir = FALSE;
    module_pri = readdir (directory_ri);
    while (module_pri)
        {
        i_ii = 0;
        found_ii = FALSE;

        while (i_ii < max_ii && !found_ii)
            {
            sprintf (tmp_ti, "%s.o", test_modules_pri [i_ii].module_name);
            if (!strcmp (tmp_ti, module_pri->d_name))
                found_ii = TRUE;
            else
                i_ii++;
            }

        if (found_ii)
            memcpy (&mod_current_arm [(*nr_modules_pir)++], &test_modules_pri [i_ii],
                    sizeof (module_t));
        else if (!strcmp ("MORE", module_pri->d_name))
            *more_pir = TRUE;

        module_pri = readdir (directory_ri);
        }

    (void) closedir (directory_ri);

    mod_sort_list (mod_current_arm, *nr_modules_pir);

    return (0);
    }


static void mod_sort_list (module_t modlist_parr [], int nr_modules_iv)
    {
    int       index_ii = 0;
    int       i_ii;
    module_t  tmp_mod_ri;


    while (index_ii < nr_modules_iv)
        {
        for (i_ii = index_ii; i_ii < nr_modules_iv; i_ii++)
            if (modlist_parr [i_ii].order < modlist_parr [index_ii].order)
                {
                memcpy (&tmp_mod_ri, &modlist_parr [i_ii], sizeof (module_t));
                memcpy (&modlist_parr [i_ii], &modlist_parr [index_ii], sizeof (module_t));
                memcpy (&modlist_parr [index_ii], &tmp_mod_ri, sizeof (module_t));
                }

        index_ii++;
        }
    }


static int mod_getmoddisk (int mod_type)
    {
           char  testfile_ti [MAX_FILENAME];
    static int   gotit_is = FALSE;

    sprintf (testfile_ti, "%s/NEEDMOD", config.module.dir);

    if (!gotit_is && util_check_exist (testfile_ti))
        {
        if (dia_message (txt_get (mod_type == MOD_TYPE_NET ? TXT_ENTER_MODDISK2 : TXT_ENTER_MODDISK), MSGTYPE_INFO) == -1)
            return (-1);
        else
            gotit_is = TRUE;
        }

    return (0);
    }



int mod_pcmcia_ok()
{
  file_t *f;
  int i, ok = 0;

  if(util_check_exist(pidfile)) {
    f = file_read_file(pidfile);

    if(f && (i = atoi(f->key_str))) {
      if(!strcmp(util_process_name(i), "cardmgr")) ok = 1;
    }

    file_free_file(f);
  }

  return ok;
}


int mod_load_pcmcia()
{
  int i, type, ok;
  char buf[256];
  window_t status, win;

  if(mod_pcmcia_ok()) {
    dia_message(txt_get(TXT_PCMCIA_ALREADY), MSGTYPE_INFO);
    return 0;
  }

  type = mod_pcmcia_chipset();

  if(type != 1 && type != 2) return -1;

  mod_param_text = buf;

  sprintf(buf, txt_get(TXT_FOUND_PCMCIA), type == 1 ? "tcic" : "i82365");
  ok = mod_load_modules("pcmcia_core", 1);

  if(ok) {
    sprintf(buf, txt_get(TXT_PCMCIA_PARAMS), type == 1 ? "tcic" : "i82365");
    ok = mod_load_modules(type == 1 ? "tcic" : "i82365", 1);
  }

  mod_param_text = NULL;

  if(ok) ok = mod_load_modules("ds", 0);

  if(ok) {
    dia_status_on(&status, txt_get(TXT_START_CARDMGR));
    system("cardmgr -v -m /modules");
    for(i = 0; i <= 100; i++) {
      dia_status(&status, i++);
      usleep(100000);
    }
    win_close(&status);
  }

  ok = mod_pcmcia_ok();

  if(ok) {
    pcmcia_chip_ig = type;

    if(!auto_ig) {
      dia_message(txt_get(TXT_PCMCIA_SUCCESS), MSGTYPE_INFO);
    }
    else {
      dia_info(&win, txt_get(TXT_PCMCIA_SUCCESS));
      sleep(2);
      win_close (&win);
    }
  }
  else {
    dia_message(txt_get(TXT_PCMCIA_FAIL), MSGTYPE_ERROR);
  }

  util_update_kernellog();

  if(!auto_ig) dia_show_file(txt_get(TXT_INFO_KERNEL), lastlog_tg, TRUE);

  return ok ? 0 : -1;
}


int mod_pcmcia_chipset()
{
  char *items[] = {
    "tcic",
    "i82365",
    NULL
  };
  static int last_item = 0;
  int type;

  type = system("probe");
  type >>= 8;

  if(type != 1 && type != 2) {
    type = dia_list(txt_get(TXT_NO_PCMCIA), 10, NULL, items, last_item, align_center);
    if(type) last_item = type;
  }

  return type;
}
