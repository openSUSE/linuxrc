/*
 *
 * text.c        Handling of messages
 *
 * Copyright (c) 1996-2001  Hubert Mantel, SuSE GmbH  (mantel@suse.de)
 *
 */

#include <stdio.h>
#include <stdlib.h>

#include "global.h"
#include "text.h"

typedef struct
    {
    enum textid_t  id;
    char          *text;
    } text_t;


typedef struct
    {
    enum langid_t  language;
    text_t        *texts;
    int            nr_texts;
    } alltexts_t;

#ifndef __alpha__
#include "lang/brasil.txt"
#include "lang/dutch.txt"
#endif
#include "lang/english.txt"
#include "lang/french.txt"
#include "lang/german.txt"
#ifndef __alpha__
#include "lang/greek.txt"
#include "lang/hungarian.txt"
#include "lang/indonesia.txt"
#include "lang/italian.txt"
#include "lang/polish.txt"
#include "lang/portuguese.txt"
#include "lang/russian.txt"
#include "lang/slovak.txt"
#include "lang/spanish.txt"
#include "lang/romanian.txt"
#include "lang/czech.txt"
#include "lang/breton.txt"
#endif
#define LANG_ENTRY(lang) lang, sizeof (lang) / sizeof (lang [0])

static alltexts_t alltexts_arm [] =
    {
    { LANG_ENGLISH,    LANG_ENTRY (txt_english_atm)    },
    { LANG_GERMAN,     LANG_ENTRY (txt_german_atm)     },
#ifndef __alpha__
    { LANG_ITALIAN,    LANG_ENTRY (txt_italian_atm)    },
#endif
    { LANG_FRENCH,     LANG_ENTRY (txt_french_atm)     },
#ifndef __alpha__
    { LANG_BRETON,     LANG_ENTRY (txt_breton_atm)     },
    { LANG_SPANISH,    LANG_ENTRY (txt_spanish_atm)    },
    { LANG_BRAZIL,     LANG_ENTRY (txt_brasil_atm)     },
    { LANG_GREEK,      LANG_ENTRY (txt_greek_atm)      },
    { LANG_DUTCH,      LANG_ENTRY (txt_dutch_atm)      },
    { LANG_RUSSIA,     LANG_ENTRY (txt_russian_atm)    },
    { LANG_SLOVAK,     LANG_ENTRY (txt_slovak_atm)     },
    { LANG_POLISH,     LANG_ENTRY (txt_polish_atm)     },
    { LANG_INDONESIA,  LANG_ENTRY (txt_indonesia_atm)  },
    { LANG_PORTUGUESE, LANG_ENTRY (txt_portuguese_atm) },
    { LANG_ROMANIAN,   LANG_ENTRY (txt_romanian_atm)   },
    { LANG_CZECH,      LANG_ENTRY (txt_czech_atm)   },
    { LANG_HUNGARIA,   LANG_ENTRY (txt_hungarian_atm)  }
#endif
    };

#define NR_LANGUAGES (sizeof(alltexts_arm)/sizeof(alltexts_arm[0]))
#define NR_TEXTS     (sizeof(txt_german_atm)/sizeof(txt_german_atm[0]))


char *txt_get (enum textid_t id_iv)
    {
    text_t  *txt_pci;
    int      found_ii = FALSE;
    int      i_ii = 0;


    while (i_ii < NR_LANGUAGES && !found_ii)
        if (alltexts_arm [i_ii].language == language_ig)
            found_ii = TRUE;
        else
            i_ii++;

    if (found_ii)
        txt_pci = alltexts_arm [i_ii].texts;
    else
        txt_pci = alltexts_arm [0].texts;

    i_ii = 0;
    found_ii = FALSE;
    while (!found_ii && i_ii < NR_TEXTS)
        if (txt_pci [i_ii].id == id_iv)
            found_ii = TRUE;
        else
            i_ii++;

    if (found_ii)
        return (txt_pci [i_ii].text);
    else
        return ("*** Unknown text! ***");
    }


int txt_init (void)
    {
    int  i_ii;

    for (i_ii = 0; i_ii < NR_LANGUAGES; i_ii++)
        if (alltexts_arm [i_ii].nr_texts != NR_TEXTS)
            {
            fprintf (stderr, "Nr of texts in language %d: %d != %d\n",
                     i_ii, alltexts_arm [i_ii].nr_texts, (int) NR_TEXTS);
            return (-1);
            }

    return (0);
    }
