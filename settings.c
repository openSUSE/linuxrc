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


#define KM_L1 "iso-8859-15"
#define KM_L2 "iso-8859-2"
#define KM_L7 "iso-8859-7"
#define KM_KOI "koi8-r"


/* keymap encodings */
struct {
  char *map;
  char *enc;
} km_enc[] = {
  { "Pl02",         KM_L2  },
  { "be-latin1",    KM_L1  },
  { "br-abnt2",     KM_L1  },
  { "cz-us-qwertz", KM_L2  },
  { "de-latin1-nodeadkeys",   KM_L1  },
  { "es",           KM_L1  },
  { "fr-latin1",    KM_L1  },
  { "gr",           KM_L7  },
  { "hu",           KM_L2  },
  { "it",           KM_L1  },
  { "no-latin1",    KM_L1  },
  { "pt-latin1",    KM_L1  },
  { "fi-latin1",    KM_L1  },
  { "sv-latin1",    KM_L1  },
  { "ru1",          KM_KOI },
  { "sk-qwerty",    KM_L2  },
  { "slovene",      KM_L2  }
};


#define LANG_DEFAULT lang_en
static language_t set_languages_arm[] = {
  { lang_af, "Afrikaans", "us", "af_ZA", "af" },
  { lang_bg, "Bulgarian", "us", "bg_BG", "bg" },
  { lang_ca, "Catala", "us", "ca_ES", "ca" },
  { lang_cs, "Cestina", "cz-us-qwertz", "cs_CZ", "cs" },
  { lang_da, "Dansk", "dk", "da_DK", "da" },
  { lang_de, "Deutsch", "de-latin1-nodeadkeys", "de_DE", "de" },
  { lang_en, "English", "us", "en_US", "en_US" },
  { lang_es, "Espanol", "es", "es_ES", "es" },
  { lang_fr, "Francais", "fr-latin1", "fr_FR", "fr" },
  { lang_el, "Greek", "gr", "el_GR", "el" },
  { lang_it, "Italiano", "it", "it_IT", "it" },
  { lang_ja, "Japanese", "jp106", "ja_JP", "ja" },
  { lang_hu, "Magyar", "hu", "hu_HU", "hu" },
  { lang_nl, "Nederlands", "us", "nl_NL", "nl" },
  { lang_nb, "Norsk", "no-latin1", "nb_NO", "nb" },
  { lang_pl, "Polski", "Pl02", "pl_PL", "pl" },
  { lang_pt, "Portugues", "pt-latin1", "pt_PT", "pt" },
  { lang_pt_BR, "Portugues brasileiro", "br-abnt2", "pt_BR", "pt_BR" },
  { lang_ru, "Russian", "ru1", "ru_RU", "ru" },
  { lang_zh_CN, "Simplified Chinese", "us", "zh_CN", "zh_CN" },
  { lang_sk, "Slovencina", "sk-qwerty", "sk_SK", "sk" },
  { lang_sl, "Slovenscina", "slovene", "sl_SI", "sl" },
  { lang_fi, "Suomi", "fi-latin1", "fi_FI", "fi" },
  { lang_sv, "Svenska", "sv-latin1", "sv_SE", "sv" },
  { lang_zh_TW, "Traditional Chinese", "us", "zh_TW", "zh_TW" },
  { lang_uk, "Ukrainian", "us", "uk_UA", "uk" },
  { lang_xh, "isiXhosa", "us", "xh_ZA", "xh" },
  { lang_zu, "isiZulu", "us", "zu_ZA", "zu" },
  // entry for unknown language
  { lang_dummy, "", "us", NULL, NULL },
};

#define KEYMAP_DEFAULT	"us"

#if !defined(__sparc__)
static keymap_t set_keymaps_arm [] =
{
{ "Belgian",              "be-latin1"    },
{ "Ceske",                "cz-us-qwertz" },
{ "Dansk",                "dk"           },
{ "Deutsch",              "de-latin1-nodeadkeys"   },
{ "English (UK)",         "uk"           },
{ "English (US)",         "us"           },
{ "Español",              "es"           },
{ "Français",             "fr-latin1"    },
{ "Hellenic",             "gr"           },
{ "Italiano",             "it"           },
{ "Japanese",             "jp106"        },
{ "Magyar",               "hu"           },
{ "Nederlands",           "nl"           },
{ "Norsk",                "no-latin1"    },
{ "Polski",               "Pl02"         },
{ "Português Brasileiro", "br-abnt2"     },
{ "Português",            "pt-latin1"    },
{ "Russian",              "ru1"          },
{ "Slovak",               "sk-qwerty"    },
{ "Slovene",              "slovene"      },
{ "Suomi",                "fi-latin1"    },
{ "Svensk",               "sv-latin1"    }
};
#else		/* defined(__sparc__) */
static keymap_t set_keymaps_arm [] =
{
{ "Ceske (PS/2)",                "cz-us-qwertz"    },
{ "Dansk (PS/2)",                "dk"              },
{ "Deutsch (PS/2)",              "de-latin1-nodeadkeys"      },
{ "Deutsch (Sun Type5)",         "sunt5-de-latin1" },
{ "English/UK (PS/2)",           "uk"              },
{ "English/UK (Sun)",            "sunt5-uk"        },
{ "English/US (PS/2)",           "us"              },
{ "English/US (Sun)",            "sunkeymap"       },
{ "Español (PS/2)",              "es"              },
{ "Español (Sun Type4)",         "sunt4-es"        },
{ "Español (Sun Type5)",         "sunt5-es"        },
{ "Français (PS/2)",             "fr-latin1"       },
{ "Français (Sun Type5)",        "sunt5-fr-latin1" },
{ "Hellenic (PS/2)",             "gr"              },
{ "Italiano (PS/2)",             "it"              },
{ "Magyar (PS/2)",               "hu"              },
{ "Nederlands (PS/2)",           "nl"              },
{ "Norsk (PS/2)",                "no-latin1"       },
{ "Norsk (Sun Type5)",           "sunt4-no-latin1" },
{ "Polski (PS/2)",               "Pl02"            },
{ "Português (PS/2)",            "pt-latin1"       },
{ "Português Brasileiro (PS/2)", "br-abnt2"        },
{ "Russian (PS/2)",              "ru1"             },
{ "Russian (Sun Type5)",         "sunt5-ru"        },
{ "Slovak",                      "sk-qwerty"       },
{ "Suomi/Svensk (PS/2)",         "fi"              },
{ "Suomi/Svensk (Sun Type4)",    "sunt4-fi-latin1" },
{ "Suomi/Svensk (Sun Type5)",    "sunt5-fi-latin1" }
};
#endif

#define NR_LANGUAGES (sizeof(set_languages_arm)/sizeof(set_languages_arm[0]))
#define NR_KEYMAPS (sizeof(set_keymaps_arm)/sizeof(set_keymaps_arm[0]))

#if defined(__PPC__)
#define KEYMAP_DEFAULT	"mac-us"
static keymap_t set_keymaps_arm_mac [] =
{
{ "Dansk",                "mac-dk-latin1"            },
{ "Deutsch (CH)",         "mac-de_CH"                },
{ "Deutsch",              "mac-de-latin1-nodeadkeys" },
{ "English (UK)",         "mac-uk"                   },
{ "English (US)",         "mac-us"                   },
{ "Español",              "mac-es"                   },
{ "Flamish",              "mac-be"                   },
{ "Français (CH)",        "mac-fr_CH"                },
{ "Français",             "mac-fr-latin1"            },
{ "Italiano",             "mac-it"                   },
{ "Português",            "mac-pt"                   },
{ "Suomi/Svensk",         "mac-fi"                   },
{ "Svenska",              "mac-se"                   }
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
static char *keymap_encoding(char *map);

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
  char *s, enc[64];

  /* keymap might be config.keymap, so be careful... */
  keymap = keymap ? strdup(keymap) : NULL;

  if(config.keymap) free(config.keymap);

  if((config.keymap = keymap)) {
    kbd_unimode();
    *enc = 0;
    if((s = keymap_encoding(config.keymap))) {
      sprintf(enc, " -c %s", s);
    }
    sprintf(cmd,
      "loadkeys -q %s ; dumpkeys%s >/tmp/dk ; loadkeys -q --unicode </tmp/dk",
      keymap, enc
    );
    if(!config.test) {
      if(config.debug) fprintf(stderr, "%s\n", cmd);
      system(cmd);
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
    di_expert_verify,
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
  char *dev = NULL;
  file_t *f;

  di_set_expert_last = di;

  switch(di) {
    case di_expert_info:
      info_menu();
      break;

    case di_expert_modules:
      mod_menu();
      break;

    case di_expert_verify:
      util_choose_disk_device(&dev, 2, "Please choose the device to check.", "Enter the device to check.");
      if(dev) digest_media_verify(dev);
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
        kbd_end(0);
        if(config.win) {
          disp_cursor_on();
        }
        if(!config.linemode) {
          printf("\033c");
          if(config.utf8) printf("\033%%G");
          fflush(stdout);
        }

        char *cmd = NULL;
        strprintf(&cmd, "PS1='\\w # ' %s 2>&1", config.debugshell ?: "/bin/sh");
        system(cmd);
        free(cmd);

        kbd_init(0);
        if(config.win) {
          disp_cursor_off();
          if(!config.linemode) disp_restore_screen();
        }
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


/* look up keymap encoding */
char *keymap_encoding(char *map)
{
  int i;

  if(map) {
    for(i = 0; i < sizeof km_enc / sizeof *km_enc; i++) {
      if(!strcmp(km_enc[i].map, map)) return km_enc[i].enc;
    }
 }

  return NULL;
}


