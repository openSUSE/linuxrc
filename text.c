/*
 *
 * text.c        Handling of messages
 *
 * Copyright (c) 1996-2002  Hubert Mantel, SuSE Linux AG  (mantel@suse.de)
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

#include "po/text_inc.h"

#define LANG_ENTRY(lang) lang, sizeof lang / sizeof *lang

#include "po/text_array.h"

#define NR_LANGUAGES (sizeof alltexts_arm /sizeof *alltexts_arm)
#define NR_TEXTS     (sizeof txt_en_atm / sizeof *txt_en_atm)


char *txt_get (enum textid_t id_iv)
    {
    text_t  *txt_pci;
    int      found_ii = FALSE;
    int      i_ii = 0;


    while ((unsigned) i_ii < NR_LANGUAGES && !found_ii)
        if (alltexts_arm [i_ii].language == config.language)
            found_ii = TRUE;
        else
            i_ii++;

    /* deserves a proper solution */
    if(
      !config.serial &&
      !config.test && (
        config.language == lang_ja ||
        config.language == lang_zh_CN ||
        config.language == lang_zh_TW
      )
    ) found_ii = FALSE;

    if (found_ii)
        txt_pci = alltexts_arm [i_ii].texts;
    else
        txt_pci = alltexts_arm [0].texts;

    i_ii = 0;
    found_ii = FALSE;
    while (!found_ii && (unsigned) i_ii < NR_TEXTS)
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

    for (i_ii = 0; (unsigned) i_ii < NR_LANGUAGES; i_ii++)
        if (alltexts_arm [i_ii].nr_texts != NR_TEXTS)
            {
            fprintf (stderr, "Nr of texts in language %d: %d != %d\n",
                     i_ii, alltexts_arm [i_ii].nr_texts, (int) NR_TEXTS);
            return (-1);
            }

    return (0);
    }
