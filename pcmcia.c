/*
 *
 * pcmcia.c      PCMCIA handling
 *
 * Copyright (c) 1996-2001  Hubert Mantel, SuSE GmbH  (mantel@suse.de)
 *
 */

#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <signal.h>

#include "global.h"
#include "module.h"
#include "window.h"
#include "dialog.h"
#include "text.h"
#include "util.h"
#include "modparms.h"

int pcmcia_core_loaded_im = FALSE;

static int pcmcia_find_chipset (void);

extern char *pidfile;

int pcmcia_load_core (void)
    {
    int       type_ii;
    int       rc_ii;
    char      text_ti [200];
    int       error_ii = FALSE;
    char      params_ti [MAX_PARAM_LEN];
    window_t  status_ri;
    window_t  win_ri;
    int       i_ii;


    if (pcmcia_core_loaded_im)
        {
        (void) dia_message (txt_get (TXT_PCMCIA_ALREADY), MSGTYPE_INFO);
        return (0);
        }

    type_ii = pcmcia_find_chipset ();
    if (type_ii != 1 && type_ii != 2)
        return (-1);

    rc_ii = mod_get_ram_modules (MOD_TYPE_OTHER);
    if (rc_ii)
        return (rc_ii);

    params_ti [0] = 0;
    sprintf (text_ti, txt_get (TXT_FOUND_PCMCIA), type_ii == 1 ? "tcic" : "i82365");
    if (!auto_ig && dia_input (text_ti, params_ti, MAX_PARAM_LEN, 30))
        return (-1);

    if (mod_load_module ("pcmcia_core", params_ti))
        error_ii = TRUE;

    if (!error_ii)
        {
        if(*params_ti) mpar_save_modparams("pcmcia_core", params_ti);
        sprintf (text_ti, txt_get (TXT_PCMCIA_PARAMS), type_ii == 1 ? "tcic" : "i82365");
        params_ti [0] = 0;
        if (!auto_ig && dia_input (text_ti, params_ti, MAX_PARAM_LEN, 30))
            return (-1);

        if (type_ii == 2)
            rc_ii = mod_load_module ("i82365", params_ti);
        else
            rc_ii = mod_load_module ("tcic", params_ti);
        }

    if (rc_ii)
        error_ii = TRUE;

    if (error_ii || mod_load_module ("ds", 0))
        error_ii = TRUE;

    if (!error_ii)
        {
        if(*params_ti) mpar_save_modparams(type_ii == 2 ? "i82365" : "tcic", params_ti);
        dia_status_on (&status_ri, txt_get (TXT_START_CARDMGR));
        (void) system ("cardmgr -v -m /modules");
        for (i_ii = 0; i_ii <= 100; i_ii++)
            {
            dia_status (&status_ri, i_ii++);
            usleep (100000);
            }
        win_close (&status_ri);
        }

    if (!util_check_exist (pidfile))
        error_ii = TRUE;

    if (!error_ii)
        {
        pcmcia_core_loaded_im = TRUE;
        pcmcia_chip_ig = type_ii;

        if (!auto_ig)
            (void) dia_message (txt_get (TXT_PCMCIA_SUCCESS), MSGTYPE_INFO);
        else
            {
            dia_info (&win_ri, txt_get (TXT_PCMCIA_SUCCESS));
            sleep (2);
            win_close (&win_ri);
            }
        }
    else
        (void) dia_message (txt_get (TXT_PCMCIA_FAIL), MSGTYPE_ERROR);

    util_update_kernellog ();
    if (!auto_ig)
        (void) dia_show_file (txt_get (TXT_INFO_KERNEL), lastlog_tg, TRUE);

    if (error_ii)
        return (-1);
    else
        return (0);
    }


static int pcmcia_find_chipset (void)
    {
    int      type_ii;
    item_t   items_ari [2];
    int      width_ii = 10;


    type_ii = system ("probe");
    type_ii >>= 8;
    if (type_ii != 1 && type_ii != 2)
        {
        util_create_items (items_ari, 2, width_ii);
        strcpy (items_ari [0].text, "tcic");
        strcpy (items_ari [1].text, "i82365");
        util_center_text (items_ari [0].text, width_ii);
        util_center_text (items_ari [1].text, width_ii);
        type_ii = dia_menu (txt_get (TXT_NO_PCMCIA), items_ari, 2, 1);
        util_free_items (items_ari, 2);
        }

    return (type_ii);
    }
