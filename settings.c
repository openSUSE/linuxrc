/*
 *
 * settings.c    Settings for linuxrc
 *
 * Copyright (c) 1996-2000  Hubert Mantel, SuSE GmbH  (mantel@suse.de)
 *
 */

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "global.h"
#include "text.h"
#include "util.h"
#include "display.h"
#include "keyboard.h"
#include "dialog.h"


typedef struct
      {
      enum langid_t  id;
      char          *descr;
      char          *keymap;
      char          *font;
      char          *mapscreen;
      char          *unimap;
      int            usermap;
      int            write_info;
      char          *locale;
      char          *yastcode;
      } language_t;

typedef struct
      {
      char *descr;
      char *mapname;
      } keymap_t;


#define LANG_DEFAULT	1
static language_t set_languages_arm [] =
{
{ LANG_GERMAN,       "Deutsch",              "de-lat1-nd",   "lat1u-16.psf",
  "none",            "lat1u.uni", 0, 0,      "de_DE",        "german"        },
{ LANG_ENGLISH,      "English",              "us",           "lat1u-16.psf",
  "none",            "lat1u.uni", 0, 0,      "en_US",        "english"       },
{ LANG_SPANISH,      "Español",              "es",           "lat1u-16.psf",
  "none",            "lat1u.uni", 0, 1,      "es_ES",        "spanish"       },
{ LANG_FRENCH,       "Français",             "fr-latin1",    "lat1u-16.psf",
  "none",            "lat1u.uni", 0, 0,      "fr_FR",        "french"        },
{ LANG_BRETON,       "Brezhoneg",            "fr-latin1",    "lat1u-16.psf",
  "none",            "lat1u.uni", 0, 0,      "fr_FR",        "breton"        },
{ LANG_GREEK,        "Hellenic",             "gr",           "lat7-16.psfu",
  "trivial",         "lat7u.uni", 1, 1,      "el_EL",        "greek"         },
{ LANG_INDONESIA,    "Indonesia",            "us",           "lat1u-16.psf",
  "none",            "lat1u.uni", 0, 1,      "de_DE",        "indonesian"    },
{ LANG_ITALIAN,      "Italiano",             "it",           "lat1u-16.psf",
  "none",            "lat1u.uni", 0, 0,      "it_IT",        "italian"       },
{ LANG_HUNGARIA,     "Magyar",               "hu",           "lat2u-16.psf",
  "latin2u.scrnmap", "lat2u.uni", 1, 1,      "hu_HU",        "hungarian"     },
{ LANG_DUTCH,        "Nederlands",           "us",           "lat1u-16.psf",
  "none",            "lat1u.uni", 0, 1,      "nl_NL",        "dutch"         },
{ LANG_POLISH,       "Polski",               "Pl02",         "lat2u-16.psf",
  "latin2u.scrnmap", "lat2u.uni", 1, 1,      "pl_PL",        "polish"        },
{ LANG_PORTUGUESE,   "Português",            "pt-latin1",    "lat1u-16.psf",
  "none",            "lat1u.uni", 0, 1,      "pt_PT",        "portuguese"    },
{ LANG_BRAZIL,       "Português Brasileiro", "br-abnt2",     "lat1u-16.psf",
  "none",            "lat1u.uni", 0, 1,      "pt_BR",        "brazilian"     },
{ LANG_ROMANIAN,     "Romania",              "us",           "lat2u-16.psf",
  "latin2u.scrnmap", "lat2u.uni", 1, 1,      "en_US",        "romanian"      },
{ LANG_RUSSIA,       "Russian",              "ru1",          "Cyr_a8x16",
  "koi2alt",         "cyralt.uni",1, 1,      "ru_RU.KOI8-R", "russian"       },
{ LANG_CZECH,        "Czech",                "cz-us-qwertz", "lat2u-16.psf",
  "latin2u.scrnmap", "lat2u.uni", 1, 1,      "cs_CZ",        "czech"         },
{ LANG_SLOVAK,       "Slovensky",            "sk-qwerty",    "lat2u-16.psf",
  "latin2u.scrnmap", "lat2u.uni", 1, 1,      "sk_SK",        "slovak"        },
};

#if defined(__i386__) || defined(__alpha__) || defined(__PPC__) || defined(__ia64__)
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
{ "Norway",               "no-latin1"    },
{ "Polski",               "Pl02"         },
{ "Português",            "pt-latin1"    },
{ "Português Brasileiro", "br-abnt2"     },
{ "Russian",              "ru1"          },
{ "Czech",                "cz-us-qwertz" },
{ "Dansk",                "dk"           },
{ "Suomi/Svensk",         "fi"           },
{ "Slovak",               "sk-qwerty"    }
};
#endif

#if defined(__sparc__)
#define KEYMAP_DEFAULT	0
static keymap_t set_keymaps_arm [] =
{
{ "sunkeymap",       "sunkeymap"       },
{ "sundvorak",       "sundvorak"       },
{ "sun-pl-altgraph", "sun-pl-altgraph" },
{ "sun-pl",          "sun-pl"          },
{ "sunt4-es",        "sunt4-es"        },
{ "sunt4-no-lotin1", "sunt4-no-lotin1" },
{ "sunt5-de-latin1", "sunt5-de-latin1" },
{ "sunt5-es",        "sunt5-es"        },
{ "sunt5-fi-latin1", "sunt5-fi-latin1" },
{ "sunt5-fr-latin1", "sunt5-fr-latin1" },
{ "sunt5-ru",        "sunt5-ru"        }
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
{ "English (UK)",         "mac-us"                   },
{ "Français",             "mac-fr2-ext"              }
};
/* !!! ***MUST NOT*** be bigger than NR_KEYMAPS !!! */
#define NR_KEYMAPS_MAC (sizeof set_keymaps_arm_mac / sizeof *set_keymaps_arm_mac)
#endif

static const char  *set_txt_language_tm       = "Language:";
static const char  *set_txt_font_tm           = "Font:";
static const char  *set_txt_screenmap_tm      = "Screenmap:";
static const char  *set_txt_unicode_tm        = "Unicodemap:";
static const char  *set_txt_fontmagic_tm      = "Fontmagic:";
static const char  *set_txt_locale_tm         = "Locale:";

/*
 *
 * local function prototypes
 *
 */

static int  set_settings_cb          (int what_iv);
static void set_expert               (void);
static int  set_expert_cb            (int what_iv);
static int  set_get_current_language (void);
static void set_activate_font        (int usermap_iv);

/*
 *
 * exported functions
 *
 */

enum langid_t set_langidbyname(char *name)
{
  int i;

  for(i = 0; i < NR_LANGUAGES; i++) {
    if(!strcasecmp(set_languages_arm[i].yastcode, name))
      return set_languages_arm[i].id;
  }

  return LANG_UNDEF;
}


int set_settings (void)
    {
    item_t items_ari [4];
    int    nr_items_ii = sizeof (items_ari) / sizeof (items_ari [0]);
    int    i_ii;
    int    width_ii = 30;
    int    choice_ii;


    util_create_items (items_ari, nr_items_ii, width_ii);

    strncpy (items_ari [0].text, txt_get (TXT_MENU_LANG), width_ii);
    strncpy (items_ari [1].text, txt_get (TXT_MENU_DISPLAY), width_ii);
    strncpy (items_ari [2].text, txt_get (TXT_MENU_KEYMAP), width_ii);
    strncpy (items_ari [3].text, txt_get (TXT_MENU_EXPERT), width_ii);
    for (i_ii = 0; i_ii < nr_items_ii; i_ii++)
        {
        util_center_text (items_ari [i_ii].text, width_ii);
        items_ari [i_ii].func = set_settings_cb;
        }

    choice_ii = dia_menu (txt_get (TXT_SETTINGS), items_ari, nr_items_ii, 1);

    util_free_items (items_ari, nr_items_ii);

    return (choice_ii);
    }


void set_choose_display (void)
    {
    item_t   items_ari [2];
    int      choice_ii;
    int      width_ii = 30;


    if (auto_ig || auto2_ig)
        {
        disp_set_display (color_ig ? 1 : 2);
        return;
        }

    util_create_items (items_ari, 2, width_ii);
    strcpy (items_ari [0].text, txt_get (TXT_COLOR_DISPLAY));
    strcpy (items_ari [1].text, txt_get (TXT_MONO_DISPLAY));
    util_center_text (items_ari [0].text, width_ii);
    util_center_text (items_ari [1].text, width_ii);

    choice_ii = dia_menu (txt_get (TXT_CHOOSE_DISPLAY), items_ari, 2, 1);
    disp_set_display (choice_ii);

    util_free_items (items_ari, 2);
    }


void set_choose_keytable (void)
    {
    item_t  items_ari [NR_KEYMAPS];
    int     i_ii;
    int     width_ii = 24;
    char    command_ti [MAX_FILENAME];
    int     keymaps = NR_KEYMAPS;
    keymap_t *keymap = set_keymaps_arm;

#ifdef __PPC__
    if(!strcmp(xkbmodel_tg, "macintosh")) {
      keymaps = NR_KEYMAPS_MAC;
      keymap = set_keymaps_arm_mac;
    }
#endif

    if (auto_ig || auto2_ig || keymap_tg[0] != 0)
        {
        if (!*keymap_tg)
            strcpy (keymap_tg, keymap [KEYMAP_DEFAULT].mapname);

        sprintf (command_ti, "loadkeys %s.map", keymap_tg);
        system (command_ti);
        return;
        }

    util_create_items (items_ari, keymaps, width_ii);
    for (i_ii = 0; i_ii < keymaps; i_ii++)
        {
        strcpy (items_ari [i_ii].text, keymap [i_ii].descr);
        util_fill_string (items_ari [i_ii].text, width_ii);
        }

    i_ii = dia_menu (txt_get (TXT_CHOOSE_KEYMAP), items_ari, keymaps, 1 + KEYMAP_DEFAULT);
    util_free_items (items_ari, keymaps);

    if (i_ii)
        {
        sprintf (command_ti, "loadkeys %s.map",
                 keymap [i_ii - 1].mapname);
        system (command_ti);
        strcpy (keymap_tg, keymap [i_ii - 1].mapname);
        }
    }


void set_choose_language (void)
    {
    item_t  items_ari [NR_LANGUAGES];
    int     current_ii;
    int     rc_ii;
    int     width_ii = 24;
    int     i_ii;
    char    command_ti [MAX_FILENAME];


    current_ii = set_get_current_language ();

    util_create_items (items_ari, NR_LANGUAGES, width_ii);
    for (i_ii = 0; i_ii < NR_LANGUAGES; i_ii++)
        {
        strcpy (items_ari [i_ii].text, set_languages_arm [i_ii].descr);
        util_fill_string (items_ari [i_ii].text, width_ii);
        }

    if (auto_ig)
        rc_ii = current_ii;
    else
        rc_ii = dia_menu (txt_get (TXT_CHOOSE_LANGUAGE), items_ari,
                          NR_LANGUAGES, current_ii);

    if (rc_ii > 0)
        {
        language_ig = set_languages_arm [rc_ii - 1].id;
        if (!serial_ig && set_languages_arm [rc_ii - 1].font)
            {
            sprintf (command_ti, "setfont %s -m %s -u %s",
                     set_languages_arm [rc_ii - 1].font,
                     set_languages_arm [rc_ii - 1].mapscreen,
                     set_languages_arm [rc_ii - 1].unimap);

            system (command_ti);
            set_activate_font (set_languages_arm [rc_ii - 1].usermap);
            }
        /* Maybe we should always load the keymap??? */
        if (demo_ig && !serial_ig)
            {
            sprintf (command_ti, "loadkeys %s.map", set_languages_arm [rc_ii - 1].keymap);
            system (command_ti);
            strcpy (keymap_tg, set_languages_arm [rc_ii - 1].keymap);
            }
        }

    util_free_items (items_ari, NR_LANGUAGES);
    }


static int set_settings_cb (int what_iv)
    {
    int  rc_ii = 0;

    switch (what_iv)
        {
        case 1:
            set_choose_language ();
            break;
        case 2:
            set_choose_display ();
            util_print_banner ();
            break;
        case 3:
            set_choose_keytable ();
            rc_ii = 1;
            break;
        case 4:
            set_expert ();
            rc_ii = 1;
            break;
        default:
            break;
        }

    return (rc_ii);
    }


static void set_expert (void)
    {
    item_t     items_ari [5];
    int        nr_items_ii = sizeof (items_ari) / sizeof (items_ari [0]);
    int        i_ii;
    int        width_ii = 32;
    char       tmp_ti [MAX_X];
    time_t     time_ri;
    struct tm *broken_time_ri;


    util_create_items (items_ari, nr_items_ii, width_ii);

    strncpy (items_ari [0].text, txt_get (TXT_ASK_ANIMATE), width_ii);
    strncpy (items_ari [1].text, txt_get (TXT_FORCE_ROOTIMAGE), width_ii);
    strncpy (items_ari [2].text, txt_get (TXT_NEW_ROOTIMAGE), width_ii);
    strncpy (items_ari [3].text, txt_get (TXT_NEW_INST_SYS), width_ii);
    strncpy (items_ari [4].text, txt_get (TXT_NFSPORT), width_ii);
    for (i_ii = 0; i_ii < nr_items_ii; i_ii++)
        {
        util_center_text (items_ari [i_ii].text, width_ii);
        items_ari [i_ii].func = set_expert_cb;
        }

    time_ri = time (0);
    broken_time_ri = gmtime (&time_ri);
    sprintf (tmp_ti, "%s -- Time: %02d:%02d", txt_get (TXT_MENU_EXPERT),
             broken_time_ri->tm_hour, broken_time_ri->tm_min);
    (void) dia_menu (tmp_ti, items_ari, nr_items_ii, 1);

    util_free_items (items_ari, nr_items_ii);
    }


static int set_expert_cb (int what_iv)
    {
    char  tmp_ti [MAX_FILENAME];
    int   rc_ii;


    switch (what_iv)
        {
        case 1:
            rc_ii = dia_yesno (txt_get (TXT_ASK_EXPLODE),
                               explode_win_ig == TRUE ? YES : NO);
            if (rc_ii == YES)
                explode_win_ig = TRUE;
            else if (rc_ii == NO)
                explode_win_ig = FALSE;
            break;
        case 2:
            rc_ii = dia_yesno (txt_get (TXT_ASK_RI_FORCE),
                               force_ri_ig == TRUE ? YES : NO);
            if (rc_ii == YES)
                force_ri_ig = TRUE;
            else if (rc_ii == NO)
                force_ri_ig = FALSE;
            break;
        case 3:
            strcpy (tmp_ti, rootimage_tg);
            rc_ii = dia_input (txt_get (TXT_ENTER_ROOTIMAGE), tmp_ti,
                               MAX_FILENAME - 1, 30);
            if (!rc_ii)
                strcpy (rootimage_tg, tmp_ti);
            break;
        case 4:
            strcpy (tmp_ti, installdir_tg);
            rc_ii = dia_input (txt_get (TXT_ENTER_INST_SYS), tmp_ti,
                               MAX_FILENAME - 1, 30);
            if (!rc_ii)
                strcpy (installdir_tg, tmp_ti);
            break;
        case 5:
            if (nfsport_ig)
                sprintf (tmp_ti, "%d", nfsport_ig);
            else
                tmp_ti [0] = 0;
            rc_ii = dia_input (txt_get (TXT_ENTER_NFSPORT), tmp_ti, 6, 6);
            if (!rc_ii)
                nfsport_ig = atoi (tmp_ti);
            break;
        default:
            dia_message (txt_get (TXT_NOT_IMPLEMENTED), MSGTYPE_ERROR);
            break;
        }

    return (what_iv);
    }


void set_write_info (FILE *file_prv)
    {
    char   line_ti [200];
    int    lang_idx_ii;

    lang_idx_ii = set_get_current_language () - 1;

    sprintf (line_ti, "%s %s\n", set_txt_language_tm,
                      set_languages_arm [lang_idx_ii].yastcode);
    fprintf (file_prv, line_ti);

    if (set_languages_arm [lang_idx_ii].write_info)
        {
        sprintf (line_ti, "%s %s\n", set_txt_font_tm,
                          set_languages_arm [lang_idx_ii].font);
        fprintf (file_prv, line_ti);

        sprintf (line_ti, "%s %s\n", set_txt_unicode_tm,
                          set_languages_arm [lang_idx_ii].unimap);
        fprintf (file_prv, line_ti);

        sprintf (line_ti, "%s %s\n", set_txt_screenmap_tm,
                          set_languages_arm [lang_idx_ii].mapscreen);
        fprintf (file_prv, line_ti);

        sprintf (line_ti, "%s (%c\n", set_txt_fontmagic_tm,
                          set_languages_arm [lang_idx_ii].usermap ? 'K' : 'B');
        fprintf (file_prv, line_ti);
        }

    sprintf (line_ti, "%s %s\n", set_txt_locale_tm,
                      set_languages_arm [lang_idx_ii].locale);
    fprintf (file_prv, line_ti);
    }


static int set_get_current_language (void)
    {
    int     found_ii = FALSE;
    int     current_ii = 0;


    while (current_ii < NR_LANGUAGES && !found_ii)
        if (set_languages_arm [current_ii].id == language_ig)
            found_ii = TRUE;
        else
            current_ii++;

    if (!found_ii)
        current_ii = LANG_DEFAULT + 1;
    else
        current_ii++;

    return (current_ii);
    }


static void set_activate_font (int usermap_iv)
    {
    char  text_ti [20];
    char  tmp_ti [20];
    FILE *tty_pri;
    int   i_ii;

    if (usermap_iv)
        strcpy (text_ti, "(K");
    else
        strcpy (text_ti, "(B");

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
