/*
 *
 * settings.c    Settings for linuxrc
 *
 * Copyright (c) 1996-2008  Hubert Mantel, SuSE Linux AG  (mantel@suse.de)
 *
 */

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "global.h"
#include "settings.h"
#include "util.h"
#include "info.h"
#include "module.h"
#include "checkmedia.h"
#include "display.h"
#include "keyboard.h"
#include "dialog.h"
#include "file.h"
#include "slp.h"
#include "url.h"
#include "net.h"

#define LANG_DEFAULT lang_en
static language_t set_languages_arm[] = {
  { lang_af, "Afrikaans", "us", "af_ZA", "af" },
  { lang_bg, "Bulgarian", "us", "bg_BG", "bg" },
  { lang_ca, "Catala", "us", "ca_ES", "ca" },
  { lang_cs, "Cestina", "cz", "cs_CZ", "cs" },
  { lang_da, "Dansk", "dk", "da_DK", "da" },
  { lang_de, "Deutsch", "de-nodeadkeys", "de_DE", "de" },
  { lang_en, "English", "us", "en_US", "en_US" },
  { lang_es, "Espanol", "es", "es_ES", "es" },
  { lang_fr, "Francais", "fr", "fr_FR", "fr" },
  { lang_el, "Greek", "gr", "el_GR", "el" },
  { lang_it, "Italiano", "it", "it_IT", "it" },
  { lang_ja, "Japanese", "jp", "ja_JP", "ja" },
  { lang_hu, "Magyar", "hu", "hu_HU", "hu" },
  { lang_nl, "Nederlands", "nl", "nl_NL", "nl" },
  { lang_nb, "Norsk", "no", "nb_NO", "nb" },
  { lang_pl, "Polski", "pl", "pl_PL", "pl" },
  { lang_pt, "Portugues", "pt", "pt_PT", "pt" },
  { lang_pt_BR, "Portugues brasileiro", "br", "pt_BR", "pt_BR" },
  { lang_ru, "Russian", "ruwin_alt-UTF-8", "ru_RU", "ru" },
  { lang_zh_CN, "Simplified Chinese", "us", "zh_CN", "zh_CN" },
  { lang_sk, "Slovencina", "sk", "sk_SK", "sk" },
  { lang_sl, "Slovenscina", "si", "sl_SI", "sl" },
  { lang_fi, "Suomi", "fi-kotoistus", "fi_FI", "fi" },
  { lang_sv, "Svenska", "se", "sv_SE", "sv" },
  { lang_zh_TW, "Traditional Chinese", "us", "zh_TW", "zh_TW" },
  { lang_uk, "Ukrainian", "ua-utf", "uk_UA", "uk" },
  { lang_xh, "isiXhosa", "us", "xh_ZA", "xh" },
  { lang_zu, "isiZulu", "us", "zu_ZA", "zu" },
  // entry for unknown language
  { lang_dummy, "", "us", NULL, NULL },
};

#define KEYMAP_DEFAULT	"us"

static keymap_t set_keymaps_arm [] =
{
{ "Belgian",              "be"           },
{ "Ceske",                "cz"           },
{ "Dansk",                "dk"           },
{ "Deutsch",              "de-nodeadkeys"},
{ "English (UK)",         "gb"           },
{ "English (US)",         "us"           },
{ "Español",              "es"           },
{ "Français",             "fr"           },
{ "Hellenic",             "gr"           },
{ "Italiano",             "it"           },
{ "Japanese",             "jp"           },
{ "Magyar",               "hu"           },
{ "Nederlands",           "nl"           },
{ "Norsk",                "no"           },
{ "Polski",               "pl"           },
{ "Português Brasileiro", "br"           },
{ "Português",            "pt"           },
{ "Russian",              "ruwin_alt-UTF-8" },
{ "Slovak",               "sk"           },
{ "Slovene",              "si"           },
{ "Suomi",                "fi-kotoistus" },
{ "Svensk",               "se"           }
};

#define NR_LANGUAGES (sizeof(set_languages_arm)/sizeof(set_languages_arm[0]))
#define NR_KEYMAPS (sizeof(set_keymaps_arm)/sizeof(set_keymaps_arm[0]))

#if defined(__PPC__)
#define KEYMAP_DEFAULT	"mac-us"
static keymap_t set_keymaps_arm_mac [] =
{
{ "Dansk",                "dk-mac"                   },
{ "Deutsch (CH)",         "ch-de_mac"                },
{ "Deutsch",              "de-mac"                   },
{ "English (UK)",         "gb-mac"                   },
{ "English (US)",         "us-mac"                   },
{ "Español",              "es-mac"                   },
{ "Flamish",              "us-mac"                   },
{ "Français (CH)",        "ch-fr_mac"                },
{ "Français",             "fr-mac"                   },
{ "Italiano",             "it-mac"                   },
{ "Português",            "pt-mac"                   },
{ "Suomi/Svensk",         "fi-mac"                   },
{ "Svenska",              "se-mac"                   }
};
/* !!! ***MUST NOT*** be bigger than NR_KEYMAPS !!! */
#define NR_KEYMAPS_MAC (sizeof set_keymaps_arm_mac / sizeof *set_keymaps_arm_mac)
#endif

static dia_item_t di_set_settings_last = di_none;
static dia_item_t di_set_expert_last = di_none;


/*
 *
 * local function prototypes
 *
 */

static int  set_settings_cb          (dia_item_t di);
static int  set_expert_cb            (dia_item_t di);
static int  set_get_current_language (enum langid_t lang);

/*
 *
 * exported functions
 *
 */

enum langid_t set_langidbyname(char *name)
{
  int i, l;

  for(i = 0; (unsigned) i < NR_LANGUAGES; i++) {
    if(set_languages_arm[i].locale && !strcasecmp(set_languages_arm[i].locale, name)) {
      return set_languages_arm[i].id;
    }
  }

  l = strlen(name);
  if(l) {
    for(i = 0; (unsigned) i < NR_LANGUAGES; i++) {
      if(
        set_languages_arm[i].locale &&
        !strncasecmp(set_languages_arm[i].locale, name, l) &&
        set_languages_arm[i].locale[l] == '_'
      ) {
        return set_languages_arm[i].id;
      }
    }
  }

  for(i = 0; i < NR_LANGUAGES; i++) {
    if(set_languages_arm[i].id == lang_dummy) {
      str_copy(&set_languages_arm[i].locale, name);
      str_copy(&set_languages_arm[i].trans_id, name);
      return lang_dummy;
    }
  }

  return lang_undef;
}

int set_settings()
{
  dia_item_t items[] = {
    di_set_lang,
    di_set_display,
    di_set_keymap,
    di_set_animate,
    di_set_forceroot,
    di_set_rootimage,
    di_set_vnc,
    di_set_usessh,
    di_set_startshell,
    di_set_slp,
    di_inst_net_config,
#if defined(__s390x__)
    di_set_auto_config,
#endif
    di_none
  };

  return dia_menu2("Settings", 40, set_settings_cb, items, di_set_settings_last);
}


/*
 * return values:
 * -1    : abort (aka ESC)
 *  0    : ok
 *  other: stay in menu
 */
int set_settings_cb (dia_item_t di)
{
  int rc = 0;
  char *s;
  url_t *url;

  di_set_settings_last = di;

  switch(di) {
    case di_set_lang:
      set_choose_language();
      break;

    case di_set_display:
      set_choose_display();
      util_print_banner();
      break;

    case di_set_keymap:
      set_choose_keytable(1);
      rc = 1;
      break;

    case di_set_animate:
      rc = dia_yesno("Use animated windows?", config.explode_win ? YES : NO);
      if(rc == YES)
        config.explode_win = 1;
      else if(rc == NO)
        config.explode_win = 0;
      rc = 1;
      break;

    case di_set_forceroot:
      rc = dia_yesno("Should the root image be loaded into the RAM disk?", config.download.instsys ? YES : NO);
      config.download.instsys_set = 1;
      if(rc == YES)
        config.download.instsys = 1;
      else if(rc == NO)
        config.download.instsys = 0;
      rc = 1;
      break;

    case di_set_rootimage:
      (void) dia_input2("Enter the path and name of the file to load into the RAM disk as the root file system.", &config.rootimage, 30, 0);
      rc = 1;
      break;

    case di_set_vnc:
      rc = dia_yesno("Use VNC for install?", config.vnc ? YES : NO);
      if(rc != ESCAPE) {
        if((config.vnc = rc == YES ? 1 : 0)) {
          config.net.do_setup |= DS_VNC;
        }
        else {
          config.net.do_setup &= ~DS_VNC;
        }
      }
      rc = 1;
      break;

    case di_set_usessh:
      rc = dia_yesno("Start SSH for Text Install?", config.usessh ? YES : NO);
      if(rc != ESCAPE) {
        if((config.usessh = rc == YES ? 1 : 0)) {
          config.net.do_setup |= DS_SSH;
        }
        else {
          config.net.do_setup &= ~DS_SSH;
        }
      }
      rc = 1;
      break;

    case di_set_startshell:
      rc = dia_yesno("Start shell before and after YaST?", config.startshell ? YES : NO);
      if(rc != ESCAPE) config.startshell = rc == YES ? 1 : 0;
      rc = 1;
      break;

    case di_set_slp:
      if(net_config_needed(1) && net_config()) {
        rc = 1;
        break;
      }
      url = url_set("slp:");
      s = slp_get_install(url);
      url_free(url);
      rc = 1;
      if(s) {
        url = url_set(s);
        if(url->scheme) {
          url_free(config.url.install);
          config.url.install = url;
          rc = 0;
        }
        else {
          url_free(url);
        }
      }
      if(rc) dia_message("SLP failed", MSGTYPE_ERROR);
      rc = 1;
      break;

    case di_inst_net_config:
      net_config();
      rc = 1;
      break;

    case di_set_auto_config:
      config.device_auto_config = 2;
      util_device_auto_config();
      rc = 1;
      break;

    default:
      break;
  }

  return rc;
}

void set_choose_display()
{
  static int last_item = 0;
  char *items[] = {
    "Color display",
    "Monochrome display",
    NULL
  };

  last_item = dia_list("Select the display type.", 30, NULL, items, last_item, align_center);

  config.color = 3 - last_item;

  disp_set_display();
}


void set_choose_keytable(int always_show)
{
  char *items[NR_KEYMAPS + 1];
  int keymaps = NR_KEYMAPS;
  keymap_t *keymap = set_keymaps_arm;
  int i, cur_lang, def_keymap_idx, cnt, default_idx;
  char *def_keymap;
  char buf[256];

#ifdef __PPC__
  if(!strcmp(xkbmodel_tg, "macintosh")) {
    keymaps = NR_KEYMAPS_MAC;
    keymap = set_keymaps_arm_mac;
  }
#endif

  /* note that this works only for iaxx, axp and non-mac ppc */
  cur_lang = set_get_current_language(lang_undef);
  def_keymap = config.keymap ?: set_languages_arm[cur_lang - 1].keymap;

  def_keymap_idx = 0;

  for(i = 0; i < keymaps; i++) {
    if(!strcmp(keymap[i].mapname, def_keymap)) {
      def_keymap_idx = i;
      break;
    }
  }

  if(i == keymaps) {
    for(i = 0; i < keymaps; i++) {
      if(!strcmp(keymap[i].mapname, KEYMAP_DEFAULT)) {
        def_keymap_idx = i;
        break;
      }
    }
  }

  if(!config.win || (config.keymap && !always_show)) {
    set_activate_keymap(def_keymap);
    return;
  }

  for(i = cnt = default_idx = 0; i < keymaps; i++) {
    sprintf(buf, "/usr/share/kbd/keymaps/%s.map", keymap[i].mapname);
    if(config.test || 1 /* util_check_exist(buf) */) {
      if(i == def_keymap_idx) default_idx = cnt;
      items[cnt++] = keymap[i].descr;
    }
  }
  items[cnt] = NULL;

  i = dia_list("Choose a keyboard map.\n"
               "YaST will offer additional keyboard tables later.",
               24, NULL, items, default_idx + 1, align_left);

  if(i) set_activate_keymap(keymap[i - 1].mapname);
}


/*
 * Set language and activate font.
 */
void set_activate_language(enum langid_t lang_id)
{
  config.language = lang_id;
}


/*
 * Load keymap.
 */
void set_activate_keymap(char *keymap)
{
  char cmd[MAX_FILENAME];

  /* keymap might be config.keymap, so be careful... */
  keymap = keymap ? strdup(keymap) : NULL;

  if(config.keymap) free(config.keymap);

  if((config.keymap = keymap)) {
    kbd_unimode();
    sprintf(cmd, "loadkeys -q %s", keymap);
    if(!config.test) {
      lxrc_run_console(cmd);
    }
  }
}


/*
 * Select a language.
 */
void set_choose_language()
{
  char *items[NR_LANGUAGES + 1];
  int i;

  for(i = 0; (unsigned) i < NR_LANGUAGES; i++) {
    if(set_languages_arm[i].id == lang_dummy) break;
    items[i] = set_languages_arm[i].descr;
  }
  items[i] = NULL;

  i = set_get_current_language(lang_undef);
  if(set_languages_arm[i - 1].id == lang_dummy) i = set_get_current_language(LANG_DEFAULT);

  i = dia_list("Select the language.", 29, NULL, items, i, align_left);

  if(i > 0) set_activate_language(set_languages_arm[i - 1].id);
}


void set_expert_menu()
{
  char tmp[MAX_X];
  time_t t;
  struct tm *gm;

  dia_item_t items[] = {
    di_expert_info,
    di_expert_modules,
    di_expert_eject,
    di_extras_info,
    di_extras_change,
    di_extras_shell,
    di_none
  };

  t = time(NULL);
  gm = gmtime(&t);
  sprintf(tmp, "%s -- Time: %02d:%02d", "Expert", gm->tm_hour, gm->tm_min);

  dia_menu2(tmp, 42, set_expert_cb, items, di_set_expert_last);
}


/*
 * return values:
 * -1    : abort (aka ESC)
 *  0    : ok
 *  other: stay in menu
 */
int set_expert_cb(dia_item_t di)
{
  int i;
  file_t *f;

  di_set_expert_last = di;

  switch(di) {
    case di_expert_info:
      info_menu();
      break;

    case di_expert_modules:
      mod_menu();
      break;

    case di_expert_eject:
      util_eject_cdrom(config.cdrom);
      break;

    case di_extras_info:
      util_status_info(0);
      break;

    case di_extras_change:
        i = dia_input2("Change config", &config.change_config, 35, 0);
        if(!i) {
          f = file_parse_buffer(config.change_config, kf_cfg + kf_cmd + kf_cmd_early);
          file_do_info(f, kf_cfg + kf_cmd + kf_cmd_early);
          file_free_file(f);
          net_update_ifcfg(IFCFG_IFUP);
        }
       break;

    case di_extras_shell:
        util_run_debugshell();
      break;

    default:
      break;
  }

  return 1;
}


void set_write_info(FILE *f)
{
  language_t *lang;

  lang = set_languages_arm + set_get_current_language(lang_undef) - 1;

  file_write_str(f, key_locale, lang->locale);
}


/* Note: this *must* return a value in the range [1, NR_LANGUAGES]! */
int set_get_current_language(enum langid_t lang)
{
  unsigned u;

  if(lang == lang_undef) lang = config.language;

  for(u = 0; u < NR_LANGUAGES; u++) {
    if(set_languages_arm[u].id == lang) return u + 1;
  }

  for(u = 0; u < NR_LANGUAGES; u++) {
    if(set_languages_arm[u].id == LANG_DEFAULT) return u + 1;
  }

  return 1;
}


language_t *current_language()
{
  return set_languages_arm + set_get_current_language(lang_undef) - 1;
}

