/*
 *
 * modparms.c    Module parameters
 *
 * Copyright (c) 1996-2000  Hubert Mantel, SuSE GmbH  (mantel@suse.de)
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "global.h"
#include "text.h"
#include "dialog.h"
#include "module.h"
#include "modparms.h"

#define NR_MODPARAMS 20

static char *mpar_modparams_atm [NR_MODPARAMS] = { 0, };
static char *mpar_modnames_atm [NR_MODPARAMS] = { 0, };

static void  mpar_save_modname   (char *module_tv);
static void  mpar_delete_modname (char *module_tv);

int mpar_get_params (module_t *module_prv, char *params_pcr)
    {
    char  txt_ti [MAX_PARAM_LEN + 100];


    sprintf (txt_ti, txt_get (TXT_ENTER_PARAMS), module_prv->module_name);
    if (params_pcr && params_pcr [0])
        {
        strcat (txt_ti, txt_get (TXT_EXAMPLE_PAR));
        strcat (txt_ti, params_pcr);
        params_pcr [0] = 0;
        }

    switch (module_prv->id)
        {
        default:
            if (dia_input (txt_ti, params_pcr, MAX_PARAM_LEN, 30))
                return (-1);
            break;
        }

    return (0);
    }


void mpar_save_modparams (char *module_tv, char *params_tv)
    {
    int  i_ii;


    if (!module_tv || !module_tv [0])
        return;

    mpar_delete_modparams (module_tv);
    mpar_save_modname (module_tv);

    if (!params_tv || !params_tv [0])
        return;

    i_ii = 0;
    while (i_ii < NR_MODPARAMS && mpar_modparams_atm [i_ii])
        i_ii++;

    if (i_ii == NR_MODPARAMS)
        return;

    mpar_modparams_atm [i_ii] = malloc (strlen (module_tv) + strlen (params_tv) + 5);
    if (mpar_modparams_atm [i_ii])
        sprintf (mpar_modparams_atm [i_ii], "%s %s", module_tv, params_tv);
    }


void mpar_delete_modparams (char *module_tv)
    {
    int  i_ii;
    int  found_ii;


    if (!module_tv || !module_tv [0])
        return;

    mpar_delete_modname (module_tv);

    i_ii = 0;
    found_ii = FALSE;

    while (i_ii < NR_MODPARAMS && !found_ii)
        if (mpar_modparams_atm [i_ii] &&
            !strncmp (module_tv, mpar_modparams_atm [i_ii], strlen (module_tv)))
            found_ii = TRUE;
        else
            i_ii++;

    if (!found_ii)
        return;

    free (mpar_modparams_atm [i_ii]);
    mpar_modparams_atm [i_ii] = 0;
    }


void mpar_write_modparms (FILE *file_prv)
    {
    int  i_ii;


    if (mpar_modnames_atm [0] || usb_mods_ig)
        {
        fprintf (file_prv, "INITRD_MODULES=\"%s%s%s",
            usb_mods_ig ? usb_mods_ig : "",
            *mpar_modnames_atm [0] || usb_mods_ig ? " " : "",
            mpar_modnames_atm [0] ? mpar_modnames_atm [0] : "");

        for (i_ii = 1; i_ii < NR_MODPARAMS; i_ii++)
            if (mpar_modnames_atm [i_ii] &&
                mod_get_mod_type (mpar_modnames_atm [i_ii]) == MOD_TYPE_SCSI)
                fprintf (file_prv, " %s", mpar_modnames_atm [i_ii]);

        fprintf (file_prv, "\"\n");
        }

    for (i_ii = 0; i_ii < NR_MODPARAMS; i_ii++)
        if (mpar_modparams_atm [i_ii])
            fprintf (file_prv, "options %s\n", mpar_modparams_atm [i_ii]);
    }


static void mpar_save_modname (char *module_tv)
    {
    int  i_ii;


    i_ii = 0;
    while (i_ii < NR_MODPARAMS && mpar_modnames_atm [i_ii])
        i_ii++;

    if (i_ii == NR_MODPARAMS)
        {
        fprintf (stderr, "Too many modules!\n");
        return;
        }

    mpar_modnames_atm [i_ii] = malloc (strlen (module_tv) + 5);
    if (mpar_modnames_atm [i_ii])
        strcpy (mpar_modnames_atm [i_ii], module_tv);
    }


static void mpar_delete_modname (char *module_tv)
    {
    int  i_ii;
    int  found_ii;


    i_ii = 0;
    found_ii = FALSE;

    while (i_ii < NR_MODPARAMS && !found_ii)
        if (mpar_modnames_atm [i_ii] &&
            !strncmp (module_tv, mpar_modnames_atm [i_ii], strlen (module_tv)))
            found_ii = TRUE;
        else
            i_ii++;

    if (!found_ii)
        return;

    free (mpar_modnames_atm [i_ii]);
    while (i_ii < NR_MODPARAMS - 1)
        {
        mpar_modnames_atm [i_ii] = mpar_modnames_atm [i_ii + 1];
        i_ii++;
        }

    mpar_modnames_atm [NR_MODPARAMS - 1] = 0;
    }

