/*
 *
 * settings.c    Settings for linuxrc
 *
 * Copyright (c) 1996-2002  Hubert Mantel, SuSE Linux AG  (mantel@suse.de)
 *
 */

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "global.h"
#include "settings.h"
#include "text.h"
#include "util.h"
#include "display.h"
#include "keyboard.h"
#include "dialog.h"
#include "file.h"

#if 1	/* I think the line below would catch all anyway... */
// #if defined(__i386__) || defined(__PPC__) || defined(__ia64__) || defined(__s390__) || defined(__sparc__) || defined(__alpha__)

#define LANG_DEFAULT	1
static language_t set_languages_arm [] =
{
#ifdef TRANS_de
{ lang_de, "Deutsch", "de-lat1-nd", "lat1-16.psfu", "none", "lat1u.uni", 0, 0, "de_DE", "german" },
#endif

#ifdef TRANS_en
{ lang_en, "English", "us", "lat1-16.psfu", "none", "lat1u.uni", 0, 0, "en_US", "english" },
#endif

#ifdef TRANS_es
{ lang_es, "Español", "es", "lat1-16.psfu", "none", "lat1u.uni", 0, 1, "es_ES", "spanish" },
#endif

#ifdef TRANS_fr
{ lang_fr, "Français", "fr-latin1", "lat1-16.psfu", "none", "lat1u.uni", 0, 0, "fr_FR", "french" },
#endif

#ifdef TRANS_br
{ lang_br, "Brezhoneg", "fr-latin1", "lat1-16.psfu", "none", "lat1u.uni", 0, 0, "fr_FR", "breton" },
#endif

#ifdef TRANS_el
{ lang_el, "Hellenic", "gr", "lat7-16.psfu", "trivial", "lat7u.uni", 1, 1, "el_EL", "greek" },
#endif

#ifdef TRANS_id
{ lang_id, "Indonesia", "us", "lat1-16.psfu", "none", "lat1u.uni", 0, 1, "de_DE", "indonesian" },
#endif

#ifdef TRANS_it
{ lang_it, "Italiano", "it", "lat1-16.psfu", "none", "lat1u.uni", 0, 0, "it_IT", "italian" },
#endif

#ifdef TRANS_hu
{ lang_hu, "Magyar", "hu", "lat2-16.psfu", "trivial", "lat2u.uni", 1, 1, "hu_HU", "hungarian" },
#endif

#ifdef TRANS_nl
{ lang_nl, "Nederlands", "us", "lat1-16.psfu", "none", "lat1u.uni", 0, 1, "nl_NL", "dutch" },
#endif

#ifdef TRANS_pl
{ lang_pl, "Polski", "Pl02", "lat2-16.psfu", "trivial", "lat2u.uni", 1, 1, "pl_PL", "polish" },
#endif

#ifdef TRANS_pt
{ lang_pt, "Português", "pt-latin1", "lat1-16.psfu", "none", "lat1u.uni", 0, 1, "pt_PT", "portuguese" },
#endif

#ifdef TRANS_pt_BR
{ lang_pt_BR, "Português Brasileiro", "br-abnt2", "lat1-16.psfu", "none", "lat1u.uni", 0, 1, "pt_BR", "brazilian" },
#endif

#ifdef TRANS_ro
{ lang_ro, "Romania", "us", "lat2-16.psfu", "trivial", "lat2u.uni", 1, 1, "en_US", "romanian" },
#endif

#ifdef TRANS_ru
{ lang_ru, "Russian", "ru1", "Cyr_a8x16.psfu", "koi2alt", "cyralt.uni",1, 1, "ru_RU.KOI8-R", "russian" },
#endif

#ifdef TRANS_cs
{ lang_cs, "Cestina", "cz-us-qwertz", "lat2-16.psfu", "trivial", "lat2u.uni", 1, 1, "cs_CZ", "czech" },
#endif

#ifdef TRANS_sk
{ lang_sk, "Slovencina", "sk-qwerty", "lat2-16.psfu", "trivial", "lat2u.uni", 1, 1, "sk_SK", "slovak" },
#endif
};
#endif

#if defined(__i386__) || defined(__x86_64__) || defined(__alpha__) || defined(__PPC__) || defined(__ia64__) || defined(__s390__) || defined(__MIPSEB__)
#define KEYMAP_DEFAULT	1
static keymap_t set_keymaps_arm [] =
{
{ "Deutsch",              "de-lat1-nd"   },
{ "English (US)",         "us"           },
{ "English (UK)",         "uk"           },
{ "Español",              "es"           },
{ "Français",             "fr-latin1"    },
{ "Hellenic",             "gr"           },
{ "Italiano",             "it"           },
{ "Magyar",               "hu"           },
{ "Nederlands",           "nl"           },
{ "Norsk",                "no-latin1"    },
{ "Polski",               "Pl02"         },
{ "Português",            "pt-latin1"    },
{ "Português Brasileiro", "br-abnt2"     },
{ "Russian",              "ru1"          },
{ "Ceske",                "cz-us-qwertz" },
{ "Dansk",                "dk"           },
{ "Suomi/Svensk",         "fi"           },
{ "Slovak",               "sk-qwerty"    }
};
#endif

#if defined(__sparc__)
#define KEYMAP_DEFAULT 3
static keymap_t set_keymaps_arm [] =
{
{ "Deutsch (PS/2)",              "de-lat1-nd"      },
{ "Deutsch (Sun Type5)",         "sunt5-de-latin1" },
{ "English/US (PS/2)",           "us"              },
{ "English/US (Sun)",            "sunkeymap"       },
{ "English/UK (PS/2)",           "uk"              },
{ "English/UK (Sun)",            "sunt5-uk"        },
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
{ "Ceske (PS/2)",                "cz-us-qwertz"    },
{ "Dansk (PS/2)",                "dk"              },
{ "Suomi/Svensk (PS/2)",         "fi"              },
{ "Suomi/Svensk (Sun Type4)",    "sunt4-fi-latin1" },
{ "Suomi/Svensk (Sun Type5)",    "sunt5-fi-latin1" },
{ "Slovak",                      "sk-qwerty"       }
};
#endif

#define NR_LANGUAGES (sizeof(set_languages_arm)/sizeof(set_languages_arm[0]))
#define NR_KEYMAPS (sizeof(set_keymaps_arm)/sizeof(set_keymaps_arm[0]))

#if defined(__PPC__)
#define KEYMAP_DEFAULT	1
static keymap_t set_keymaps_arm_mac [] =
{
{ "Deutsch",              "mac-de-latin1-nodeadkeys" },
{ "English (US)",         "mac-us"                   },
{ "English (UK)",         "mac-uk"                   },
{ "Français",             "mac-fr-latin1"            },
{ "Deutsch (CH)",         "mac-de_CH"                },
{ "Français (CH)",        "mac-fr_CH"                },
{ "Dansk",                "mac-dk-latin1"            },
{ "Suomi/Svensk",         "mac-fi"                   },
{ "Italiano",             "mac-it"                   },
{ "Flamish",              "mac-be"                   },
{ "Español",              "mac-es"                   },
{ "Svenska",              "mac-se"                   },
{ "Português",            "mac-pt"                   }
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
static void set_expert               (void);
static int  set_expert_cb            (dia_item_t di);
static int  set_get_current_language (void);
#if 0
static void set_activate_font        (int usermap_iv);
#endif
static void set_font(char *font, char *map, char *unimap);

/*
 *
 * exported functions
 *
 */

enum langid_t set_langidbyname(char *name)
{
  int i, l;

  for(i = 0; i < NR_LANGUAGES; i++) {
    if(!strcasecmp(set_languages_arm[i].yastcode, name)) {
      return set_languages_arm[i].id;
    }
  }

  for(i = 0; i < NR_LANGUAGES; i++) {
    if(!strcasecmp(set_languages_arm[i].locale, name)) {
      return set_languages_arm[i].id;
    }
  }

  l = strlen(name);
  if(l) {
    for(i = 0; i < NR_LANGUAGES; i++) {
      if(
        !strncasecmp(set_languages_arm[i].locale, name, l) &&
        set_languages_arm[i].locale[l] == '_'
      ) {
        return set_languages_arm[i].id;
      }
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
    di_set_expert,
    di_none
  };

  return dia_menu2(txt_get(TXT_SETTINGS), 30, set_settings_cb, items, di_set_settings_last);
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

    case di_set_expert:
      set_expert();
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
    txt_get(TXT_COLOR_DISPLAY),
    txt_get(TXT_MONO_DISPLAY),
    NULL
  };

  last_item = dia_list(txt_get(TXT_CHOOSE_DISPLAY), 30, NULL, items, last_item, align_center);

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
  cur_lang = set_get_current_language();
  def_keymap = config.keymap ?: set_languages_arm[cur_lang - 1].keymap;

  def_keymap_idx = KEYMAP_DEFAULT;
  for(i = 0; i < keymaps; i++) {
    if(!strcmp(keymap[i].mapname, def_keymap)) {
      def_keymap_idx = i;
      break;
    }
  }

  if(config.keymap && !always_show) {
    set_activate_keymap(config.keymap);
    return;
  }

  if(!config.win && !always_show) return;

  for(i = cnt = default_idx = 0; i < keymaps; i++) {
    sprintf(buf, "/kbd/keymaps/%s.map", keymap[i].mapname);
    if(config.test || util_check_exist(buf)) {
      if(i == def_keymap_idx) default_idx = cnt;
      items[cnt++] = keymap[i].descr;
    }
  }
  items[cnt] = NULL;

  i = dia_list(txt_get(TXT_CHOOSE_KEYMAP), 24, NULL, items, default_idx + 1, align_left);

  if(i) set_activate_keymap(keymap[i - 1].mapname);
}


/*
 * Set language and activate font.
 */
void set_activate_language(enum langid_t lang_id)
{
  int i;
  language_t *lang;
//  char cmd[MAX_FILENAME];

  config.language = lang_id;
  i = set_get_current_language();

  if(i > 0) {
    lang = set_languages_arm + i - 1;

    if(!serial_ig && lang->font) {
      set_font(lang->font, lang->mapscreen, NULL);
    }

#if 0
    /* Maybe we should always load the keymap??? */
    if(demo_ig && !serial_ig) {
      set_activate_keymap(lang->keymap);
    }
#endif
  }
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
    sprintf(cmd, "loadkeys %s.map", keymap);
    system (cmd);
  }
}


/*
 * Select a language.
 */
void set_choose_language()
{
  char *items[NR_LANGUAGES + 1];
  int i;

  for(i = 0; i < NR_LANGUAGES; i++) {
    items[i] = set_languages_arm[i].descr;
  }
  items[i] = NULL;

  i = dia_list(txt_get(TXT_CHOOSE_LANGUAGE), 24, NULL, items, set_get_current_language(), align_left);

  if(i > 0) set_activate_language(set_languages_arm[i - 1].id);
}


void set_expert()
{
  char tmp[MAX_X];
  time_t t;
  struct tm *gm;

  dia_item_t di;
  dia_item_t items[] = {
    di_expert_animate,
    di_expert_forceroot,
    di_expert_rootimage,
    di_expert_instsys,
    di_expert_nfsport,
    di_expert_bootptimeout,
    di_expert_dhcp,
    di_expert_vnc,
    di_expert_usessh,
    di_none
  };

  t = time(NULL);
  gm = gmtime(&t);
  sprintf(tmp, "%s -- Time: %02d:%02d", txt_get(TXT_MENU_EXPERT), gm->tm_hour, gm->tm_min);

  di = dia_menu2(tmp, 32, set_expert_cb, items, di_set_expert_last);
}


/*
 * return values:
 * -1    : abort (aka ESC)
 *  0    : ok
 *  other: stay in menu
 */
int set_expert_cb(dia_item_t di)
{
  char tmp[MAX_FILENAME];
  int rc;

  di_set_expert_last = di;

  switch(di) {
    case di_expert_animate:
      rc = dia_yesno(txt_get(TXT_ASK_EXPLODE), explode_win_ig == TRUE ? YES : NO);
      if(rc == YES)
        explode_win_ig = TRUE;
      else if(rc == NO)
        explode_win_ig = FALSE;
      break;

    case di_expert_forceroot:
      rc = dia_yesno(txt_get(TXT_ASK_RI_FORCE), force_ri_ig == TRUE ? YES : NO);
      if(rc == YES)
        force_ri_ig = TRUE;
      else if(rc == NO)
        force_ri_ig = FALSE;
      break;

    case di_expert_rootimage:
      rc = dia_input2(txt_get(TXT_ENTER_ROOTIMAGE), &config.rootimage, 30, 0);
      break;

    case di_expert_instsys:
      rc = dia_input2(txt_get(TXT_ENTER_INST_SYS), &config.installdir, 30, 0);
      break;

    case di_expert_nfsport:
      if(config.net.nfs_port)
        sprintf(tmp, "%d", config.net.nfs_port);
      else
        *tmp = 0;
      rc = dia_input(txt_get(TXT_ENTER_NFSPORT), tmp, 6, 6, 0);
      if(!rc) config.net.nfs_port = atoi(tmp);
      break;

    case di_expert_bootptimeout:
      sprintf(tmp, "%d", config.net.bootp_timeout);
      rc = dia_input(txt_get(TXT_ENTER_BOOTP_TIMEOUT), tmp, 4, 4, 0);
      if(!rc) config.net.bootp_timeout = atoi(tmp);
      break;

    case di_expert_dhcp:
      rc = dia_yesno(txt_get(TXT_DHCP_VS_BOOTP), config.net.use_dhcp ? YES : NO);
      config.net.use_dhcp = rc == YES ? 1 : 0;
      break;

    case di_expert_vnc:
      rc = dia_yesno(txt_get(TXT_VNC_YES_NO), config.vnc ? YES : NO);
      config.vnc = rc == YES ? 1 : 0;
      break;

    case di_expert_usessh:
      rc = dia_yesno(txt_get(TXT_SSH_YES_NO), config.usessh ? YES : NO);
      config.usessh = rc == YES ? 1 : 0;
      break;

    default:
      break;
  }

  return 1;
}


void set_write_info(FILE *f)
{
  language_t *lang;
  char magic[3] = "( ";

  lang = set_languages_arm + set_get_current_language() - 1;

  file_write_str(f, key_language, lang->yastcode);

  if(lang->write_info) {
    file_write_str(f, key_font, lang->font);
    file_write_str(f, key_screenmap, lang->mapscreen);
    magic[1] = lang->usermap ? 'K' : 'B';
    file_write_str(f, key_fontmagic, magic);
  }

  file_write_str(f, key_locale, lang->locale);
}


/* Note: this *must* return a value in the range [1, NR_LANGUAGES]! */
int set_get_current_language()
{
  int i;

  for(i = 0; i < NR_LANGUAGES; i++) {
    if(set_languages_arm[i].id == config.language) return i + 1;
  }

  return LANG_DEFAULT + 1;
}


#if 0
static void set_activate_font (int usermap_iv)
    {
    char  text_ti [20];
    char  tmp_ti [20];
    FILE *tty_pri;
    int   i_ii;

    if (usermap_iv)
        strcpy (text_ti, "\033(K");
    else
        strcpy (text_ti, "\033(B");

    printf("%s", text_ti); fflush(stdout);

    for (i_ii = 1; i_ii <= 6; i_ii++)
        {
        sprintf (tmp_ti, "/dev/tty%d", i_ii);
        tty_pri = fopen (tmp_ti, "w");
        if (tty_pri)
            {
            fprintf (tty_pri, text_ti);
            fclose (tty_pri);
            }
        }
    }
#endif

/*
 * New setfont code.
 * setfont apparently works with /dev/tty. This breaks things on frame buffer
 * consoles for some reason. Moreover, fb consoles can have different settings
 * for every console.
 * Hence the workaround.
 */
static void set_font(char *font, char *map, char *unimap)
{
  char cmd[100], cmd_map[32], cmd_unimap[32];
  char dev[32];
  char usermap;
  int i, err = 0, max_cons;
  int has_fb;
  FILE *f;

  *cmd_map = *cmd_unimap = 0;
  usermap = !map || strcmp(map, "none") ? 'K' : 'B';
  sprintf(cmd, "setfont %s", font);
  if(map) sprintf(cmd_map, " -m %s", map);
  if(unimap) sprintf(cmd_map, " -u %s", unimap);
  strcat(strcat(cmd, cmd_map), cmd_unimap);

  fprintf(stderr, "setfont %s (map %s)\n", font, map);

  has_fb = 0;
  if((f = fopen("/dev/fb", "r"))) {
    has_fb = 1;
    fclose(f);
  }

//  deb_int(has_fb);

  max_cons = has_fb ? 6 : 1;

  err |= rename("/dev/tty", "/dev/tty.bak");
  for(i = 0; i < max_cons; i++) {
    sprintf(dev, "/dev/tty%d", i);
    err |= rename(dev, "/dev/tty");
    system(cmd);
    f = fopen("/dev/tty", "w");
    if(f) { fprintf(f, "\033(%c", usermap); fclose(f); }
    err |= rename("/dev/tty", dev);
  }
  err |= rename("/dev/tty.bak", "/dev/tty");

#if 0
  system(cmd);
  f = fopen("/dev/tty", "w");
  if(f) { fprintf(f, "\033(%c", usermap); fclose(f); }
#endif

  if(err) deb_int(err);
}


language_t *current_language()
{
  return set_languages_arm + set_get_current_language() - 1;
}

