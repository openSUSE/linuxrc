/*
 *
 * dialog.c      User dialogs
 *
 * Copyright (c) 1996-2001  Hubert Mantel, SuSE GmbH  (mantel@suse.de)
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>

#include <sys/types.h>
#include <sys/wait.h>

#include "global.h"
#include "window.h"
#include "keyboard.h"
#include "text.h"
#include "util.h"
#include "display.h"
#include "dialog.h"
#include "linuxrc.h"


#define MIN_WIN_SIZE    40
#define MAX_LINES     1000

struct {
  dia_item_t item;
  int text_id;
  char *text;
} dia_texts[] = {
  { di_main_settings, TXT_SETTINGS,      },
  { di_main_info,     TXT_MENU_INFO,     },
  { di_main_modules,  TXT_MENU_MODULES,  },
  { di_main_start,    TXT_MENU_START,    },
  { di_main_reboot,   TXT_END_REBOOT,    },
  { di_main_halt,     0, "Power off"     },

  { di_set_lang,    TXT_MENU_LANG,    },
  { di_set_display, TXT_MENU_DISPLAY, },
  { di_set_keymap,  TXT_MENU_KEYMAP,  },
  { di_set_expert,  TXT_MENU_EXPERT,  },

  { di_expert_animate,      TXT_ASK_ANIMATE,     },
  { di_expert_forceroot,    TXT_FORCE_ROOTIMAGE, },
  { di_expert_rootimage,    TXT_NEW_ROOTIMAGE,   },
  { di_expert_instsys,      TXT_NEW_INST_SYS,    },
  { di_expert_nfsport,      TXT_NFSPORT,         },
  { di_expert_bootptimeout, TXT_BOOTP_TIMEOUT,   },
  { di_expert_dhcp,         0, "DHCP/BOOTP"      },

  { di_inst_install, TXT_START_INSTALL,    },
  { di_inst_demo,    TXT_START_DEMO,       },
  { di_inst_system,  TXT_BOOT_SYSTEM,      },
  { di_inst_rescue,  TXT_START_RESCUE,     },
  { di_inst_eject,   TXT_EJECT_CD,         },
  { di_inst_update,  0, "Driver Update CD" },

  { di_source_cdrom,  TXT_CDROM,    },
  { di_source_nfs,    TXT_NFS,      },
  { di_source_ftp,    TXT_FTP,      },
  { di_source_smb,    TXT_SMB,      },
  { di_source_hd,     TXT_HARDDISK, },
  { di_source_floppy, TXT_FLOPPY,   },

  { di_info_kernel,     TXT_INFO_KERNEL  },
  { di_info_drives,     TXT_DRIVES       },
  { di_info_modules,    TXT_INFO_MODULES },
  { di_info_pci,        TXT_INFO_PCI     },
  { di_info_cpu,        TXT_INFO_CPU     },
  { di_info_mem,        TXT_INFO_MEM     },
  { di_info_ioports,    TXT_INFO_IOPORTS },
  { di_info_interrupts, TXT_INFO_IRQS    },
  { di_info_devices,    TXT_INFO_DEVICES },
  { di_info_netdev,     TXT_INFO_NETDEV  },
  { di_info_dma,        TXT_INFO_DMA     }
};


/*
 *
 * local function prototypes
 *
 */

static int  dia_win_open (window_t *win_prr, char *txt_tv);

/*
 *
 * exported functions
 *
 */

int dia_yesno (char *txt_tv, int default_iv)
    {
    window_t  win_ri;
    button_t  yes_button_ri;
    button_t  no_button_ri;
    button_t *buttons_ari [2] = { &yes_button_ri, &no_button_ri };
    int       width_ii;
    int       answer_ii;


    disp_toggle_output (DISP_OFF);
    memset (&win_ri, 0, sizeof (window_t));
    win_ri.bg_color = colors_prg->choice_win;
    win_ri.fg_color = colors_prg->choice_fg;
    width_ii = dia_win_open (&win_ri, txt_tv);

    util_generate_button (&yes_button_ri, txt_get (TXT_YES));
    util_generate_button (&no_button_ri, txt_get (TXT_NO));

    win_add_button (&win_ri, &yes_button_ri,
                    width_ii / 3 - strlen (yes_button_ri.text) / 2 - 3,
                    strlen (yes_button_ri.text));
    win_add_button (&win_ri, &no_button_ri, width_ii - width_ii / 3 -
                    strlen (yes_button_ri.text) / 2 + 2,
                    strlen (no_button_ri.text));

    disp_flush_area (&win_ri);
    answer_ii = win_choose_button (buttons_ari, 2, default_iv == NO ? 2 : 1);
    win_close (&win_ri);

    if (answer_ii == 1)
        return (YES);
    else if (answer_ii == 2)
        return (NO);
    else
        return (ESCAPE);
    }


int dia_okcancel (char *txt_tv, int default_iv)
    {
    window_t  win_ri;
    button_t  yes_button_ri;
    button_t  no_button_ri;
    button_t *buttons_ari [2] = { &yes_button_ri, &no_button_ri };
    int       width_ii;
    int       answer_ii;


    disp_toggle_output (DISP_OFF);
    memset (&win_ri, 0, sizeof (window_t));
    win_ri.bg_color = colors_prg->choice_win;
    win_ri.fg_color = colors_prg->choice_fg;
    width_ii = dia_win_open (&win_ri, txt_tv);

    util_generate_button (&yes_button_ri, "Ok");
    util_generate_button (&no_button_ri, "Cancel");

    win_add_button (&win_ri, &yes_button_ri,
                    width_ii / 3 - strlen (yes_button_ri.text) / 2 - 3,
                    strlen (yes_button_ri.text));
    win_add_button (&win_ri, &no_button_ri, width_ii - width_ii / 3 -
                    strlen (yes_button_ri.text) / 2 + 2,
                    strlen (no_button_ri.text));

    disp_flush_area (&win_ri);
    answer_ii = win_choose_button (buttons_ari, 2, default_iv == NO ? 2 : 1);
    win_close (&win_ri);

    if (answer_ii == 1)
        return (YES);
    else if (answer_ii == 2)
        return (NO);
    else
        return (ESCAPE);
    }


int dia_message (char *txt_tv, int msgtype_iv)
    {
    window_t  win_ri;
    int       width_ii;
    button_t  button_ri;
    int       key_ii;


    disp_toggle_output (DISP_OFF);
    kbd_clear_buffer ();
    memset (&win_ri, 0, sizeof (window_t));
    if (msgtype_iv == MSGTYPE_ERROR)
        {
        win_ri.bg_color = colors_prg->error_win;
        win_ri.fg_color = colors_prg->error_fg;
        }
    else
        {
        win_ri.bg_color = colors_prg->msg_win;
        win_ri.fg_color = colors_prg->msg_fg;
        }
    width_ii = dia_win_open (&win_ri, txt_tv);
    util_generate_button (&button_ri, txt_get (TXT_OK));
    win_add_button (&win_ri, &button_ri,
                    width_ii / 2 - strlen (button_ri.text) / 2 - 1,
                    strlen (button_ri.text));
/*    win_button_select (&button_ri); */

    disp_flush_area (&win_ri);
    do
        {
        key_ii = kbd_getch (TRUE);
        if (key_ii == KEY_CTRL_C)
            dia_handle_ctrlc ();

        if (key_ii == KEY_ENTER)
            win_button_pressed (&button_ri, FALSE);
        }
    while (key_ii != KEY_ENTER && key_ii != KEY_ESC && key_ii != 'q' && key_ii != 'r' && key_ii != 'i');

    win_close (&win_ri);

    if (key_ii == KEY_ENTER)
        return (0);
    else if (key_ii == KEY_ESC)
        return (-1);
    else if (key_ii == 'r')
        return (-69);
    else if (key_ii == 'i')
        return (-71);
    else
        return (-42);
    }


int dia_menu (char *head_tv,     item_t  items_arv [],
              int   nr_items_iv, int     default_iv)
    {
    int       width_ii;
    int       key_ii;
    int       current_ii;
    window_t  menu_win_ri;
    window_t  tmp_win_ri;
    button_t  yes_button_ri;
    button_t  no_button_ri;
    button_t *curr_button_pri;
    int       nr_lines_ii;
    char     *lines_ati [MAX_Y];
    int       i_ii;
    int       phys_items_ii;
    int       offset_ii;
    int       need_redraw_ii;
    int       scrollbar_ii;
    int       rc_ii;


    disp_toggle_output (DISP_OFF);
    kbd_clear_buffer ();
    width_ii = strlen (head_tv) + 6;
    if (width_ii < strlen (items_arv [0].text) + 6)
        width_ii = strlen (items_arv [0].text) + 6;

    if (width_ii < MIN_WIN_SIZE)
        width_ii = MIN_WIN_SIZE;

    if (width_ii > max_x_ig - 16)
        width_ii = max_x_ig - 16;

    nr_lines_ii = util_format_txt (head_tv, lines_ati, width_ii - 4);

    if (nr_items_iv < max_y_ig - 17)
        phys_items_ii = nr_items_iv;
    else
        phys_items_ii = max_y_ig - 17;

    if (default_iv > nr_items_iv)
        default_iv = nr_items_iv;
    if (default_iv < 1)
        default_iv = 1;

    memset (&menu_win_ri, 0, sizeof (window_t));
    menu_win_ri.x_left = max_x_ig / 2 - width_ii / 2;
    menu_win_ri.y_left = max_y_ig / 2 - 3 - (nr_lines_ii + phys_items_ii + 1) / 2;
    menu_win_ri.x_right = max_x_ig / 2 + width_ii / 2;
    menu_win_ri.y_right = max_y_ig / 2 + 7 + (nr_lines_ii + phys_items_ii) / 2;
    menu_win_ri.head = 2 + nr_lines_ii;
    menu_win_ri.foot = 3;
    menu_win_ri.shadow = TRUE;
    menu_win_ri.style = STYLE_RAISED;
    menu_win_ri.bg_color = colors_prg->menu_win;
    menu_win_ri.fg_color = colors_prg->menu_fg;
    menu_win_ri.save_bg = TRUE;
    win_open (&menu_win_ri);
    win_clear (&menu_win_ri);

    memset (&tmp_win_ri, 0, sizeof (window_t));
    tmp_win_ri.x_left = menu_win_ri.x_left + 1;
    tmp_win_ri.y_left = menu_win_ri.y_left + 1;
    tmp_win_ri.x_right = menu_win_ri.x_right - 1;
    tmp_win_ri.y_right = menu_win_ri.y_left + 2 + nr_lines_ii;
    tmp_win_ri.style = STYLE_RAISED;
    tmp_win_ri.bg_color = menu_win_ri.bg_color;
    tmp_win_ri.fg_color = menu_win_ri.fg_color;
    win_open (&tmp_win_ri);

    for (i_ii = 1; i_ii <= nr_lines_ii; i_ii++)
        {
        win_print (&tmp_win_ri, 2, i_ii, lines_ati [i_ii - 1]);
        free (lines_ati [i_ii - 1]);
        }
    

    util_generate_button (&yes_button_ri, txt_get (TXT_OK));
    util_generate_button (&no_button_ri, txt_get (TXT_CANCEL));

    win_add_button (&menu_win_ri, &yes_button_ri,
                    width_ii / 3 - strlen (yes_button_ri.text) / 2 - 3,
                    strlen (yes_button_ri.text));
    win_add_button (&menu_win_ri, &no_button_ri,
                    width_ii - width_ii / 3 - strlen (yes_button_ri.text) / 2 + 1,
                    strlen (no_button_ri.text));
    curr_button_pri = &yes_button_ri;
    win_button_select (curr_button_pri);

    if (width_ii > strlen (items_arv [0].text) + 4)
        width_ii = strlen (items_arv [0].text) + 4;
    memset (&tmp_win_ri, 0, sizeof (window_t));
    tmp_win_ri.x_left = max_x_ig / 2 - width_ii / 2;
    tmp_win_ri.y_left = menu_win_ri.y_left + menu_win_ri.head + 2;
    tmp_win_ri.x_right = max_x_ig / 2 + width_ii / 2;
    tmp_win_ri.y_right = menu_win_ri.y_right - menu_win_ri.foot - 2;
    tmp_win_ri.style = STYLE_SUNKEN;
    tmp_win_ri.bg_color = menu_win_ri.bg_color;
    tmp_win_ri.fg_color = menu_win_ri.fg_color;
    win_open (&tmp_win_ri);

    if (default_iv <= phys_items_ii)
        {
        offset_ii = 0;
        current_ii = default_iv - 1;
        }
    else
        {
        offset_ii = default_iv - phys_items_ii;
        current_ii = phys_items_ii - 1;
        }


    if (phys_items_ii < nr_items_iv)
        {
        disp_graph_on ();
        scrollbar_ii = (offset_ii + current_ii) * phys_items_ii / nr_items_iv;
        if (scrollbar_ii == 0 && offset_ii + current_ii != 0)
            scrollbar_ii++;
        if (scrollbar_ii == phys_items_ii - 1 && offset_ii + current_ii < nr_items_iv - 1)
            scrollbar_ii--;

        for (i_ii = 0; i_ii < phys_items_ii; i_ii++)
            {
            disp_gotoxy (tmp_win_ri.x_right - 1, tmp_win_ri.y_left + 1 + i_ii);
            if (i_ii == scrollbar_ii)
                {
                disp_set_color (tmp_win_ri.bg_color, tmp_win_ri.fg_color);
                disp_write_char (' ');
                }
            else
                {
                disp_set_color (tmp_win_ri.fg_color, tmp_win_ri.bg_color);
                disp_write_char (GRAPH_BLOCK);
                }
            }
        disp_graph_off ();
        }

    disp_set_color (tmp_win_ri.fg_color, tmp_win_ri.bg_color);
    for (i_ii = 0; i_ii < phys_items_ii; i_ii++)
        win_print (&tmp_win_ri, 2, i_ii + 1, items_arv [offset_ii + i_ii].text);

    disp_set_color (colors_prg->input_fg, colors_prg->input_bg);
    win_print (&tmp_win_ri, 2, current_ii + 1,
               items_arv [offset_ii + current_ii].text);

    disp_flush_area (&menu_win_ri);
    do
        {
        key_ii = kbd_getch (TRUE);
        if (key_ii == KEY_CTRL_C)
            dia_handle_ctrlc ();

        if (key_ii != KEY_LEFT  && key_ii != KEY_RIGHT &&
            key_ii != KEY_TAB   && key_ii != KEY_BTAB  &&
            key_ii != KEY_ENTER && key_ii != KEY_ESC)
            {
            disp_set_color (tmp_win_ri.fg_color, tmp_win_ri.bg_color);
            win_print (&tmp_win_ri, 2, current_ii + 1,
                       items_arv [offset_ii + current_ii].text);
            }

        need_redraw_ii = FALSE;
        switch (key_ii)
            {
            case KEY_DOWN:
                if (current_ii < phys_items_ii - 1)
                    current_ii++;
                else if (offset_ii + current_ii < nr_items_iv - 1)
                    {
                    offset_ii++;
                    need_redraw_ii = TRUE;
                    }
                else if (phys_items_ii == nr_items_iv)
                    current_ii = (current_ii + 1) % phys_items_ii;
                break;
            case KEY_UP:
                if (current_ii > 0)
                    current_ii--;
                else if (offset_ii > 0)
                    {
                    offset_ii--;
                    need_redraw_ii = TRUE;
                    }
                else if (phys_items_ii == nr_items_iv)
                    current_ii = (current_ii + phys_items_ii - 1) %
                                         phys_items_ii;
                break;
            case KEY_HOME:
                offset_ii = 0;
                current_ii = 0;
                need_redraw_ii = TRUE;
                break;
            case KEY_PGUP:
                if (offset_ii == 0)
                    current_ii = 0;
                else
                    {
                    offset_ii -= phys_items_ii;
                    if (offset_ii < 0)
                        offset_ii = 0;
                    }
                need_redraw_ii = TRUE;
                break;
            case KEY_END:
                offset_ii = nr_items_iv - phys_items_ii;
                current_ii = phys_items_ii - 1;
                need_redraw_ii = TRUE;
                break;
            case KEY_PGDOWN:
                if (offset_ii + phys_items_ii >= nr_items_iv - 1)
                    current_ii = phys_items_ii - 1;
                else
                    {
                    offset_ii += phys_items_ii;
                    if (offset_ii > nr_items_iv - phys_items_ii)
                        offset_ii = nr_items_iv - phys_items_ii;
                    }
                need_redraw_ii = TRUE;
                break;
            case KEY_LEFT:
            case KEY_RIGHT:
            case KEY_TAB:
            case KEY_BTAB:
                win_button_unselect (curr_button_pri);
                if (key_ii == KEY_LEFT ||
                    (key_ii != KEY_RIGHT && curr_button_pri == &no_button_ri))
                    curr_button_pri = &yes_button_ri;
                else if (key_ii == KEY_RIGHT ||
                    (key_ii != KEY_LEFT && curr_button_pri == &yes_button_ri))
                    curr_button_pri = &no_button_ri;
                win_button_select (curr_button_pri);
                break;
            case KEY_ENTER:
                win_button_pressed (curr_button_pri, TRUE);
                if (curr_button_pri == &no_button_ri)
                    key_ii = KEY_ESC;
                else
                    {
                    if (items_arv [offset_ii + current_ii].func)
                        {
                        i_ii = items_arv[offset_ii + current_ii].di ?: offset_ii + current_ii + 1;
                        rc_ii = items_arv[offset_ii + current_ii].func(i_ii);
                        if (rc_ii == -1)
                            key_ii = KEY_ESC;
                        else if (rc_ii)
                            key_ii = 0;
                        }
                    else
                        usleep (BUTTON_TIME);
                    }
                win_button_unpressed (curr_button_pri);
                if (!key_ii)
                    win_button_select (curr_button_pri);
                kbd_clear_buffer ();
                break;
            case KEY_ESC:
                win_button_unselect (curr_button_pri);
                curr_button_pri = &no_button_ri;
                win_button_pressed (curr_button_pri, FALSE);
                break;
            default:
                break;
            }

        if (need_redraw_ii)
            {
            disp_set_color (tmp_win_ri.fg_color, tmp_win_ri.bg_color);
            for (i_ii = 0; i_ii < phys_items_ii; i_ii++)
                win_print (&tmp_win_ri, 2, i_ii + 1,
                           items_arv [offset_ii + i_ii].text);
            }

        if (phys_items_ii < nr_items_iv)
            {
            disp_graph_on ();
            scrollbar_ii = (offset_ii + current_ii) * phys_items_ii / nr_items_iv;
            if (scrollbar_ii == 0 && offset_ii + current_ii != 0)
                scrollbar_ii++;
            if (scrollbar_ii == phys_items_ii - 1 && offset_ii + current_ii < nr_items_iv - 1)
                scrollbar_ii--;

            for (i_ii = 0; i_ii < phys_items_ii; i_ii++)
                {
                disp_gotoxy (tmp_win_ri.x_right - 1, tmp_win_ri.y_left + 1 + i_ii);
                if (i_ii == scrollbar_ii)
                    {
                    disp_set_color (tmp_win_ri.bg_color, tmp_win_ri.fg_color);
                    disp_write_char (' ');
                    }
                else
                    {
                    disp_set_color (tmp_win_ri.fg_color, tmp_win_ri.bg_color);
                    disp_write_char (GRAPH_BLOCK);
                    }
                }
            disp_graph_off ();
            }

        if (key_ii != KEY_LEFT  && key_ii != KEY_RIGHT &&
            key_ii != KEY_TAB   && key_ii != KEY_BTAB  &&
            key_ii != KEY_ENTER && key_ii != KEY_ESC)
            {
            disp_set_color (colors_prg->input_fg, colors_prg->input_bg);
            win_print (&tmp_win_ri, 2, current_ii + 1,
                       items_arv [offset_ii + current_ii].text);
            }

        fflush (stdout);
        }
    while (key_ii != KEY_ENTER && key_ii != KEY_ESC);

    win_close (&menu_win_ri);
    if (key_ii == KEY_ESC)
        return (0);
    else
        return (offset_ii + current_ii + 1);
    }


void dia_status_on (window_t *win_prr, char *txt_tv)
    {
    window_t  tmp_win_ri;
    char      tmp_txt_ti [STATUS_SIZE + 1];

    if(auto2_ig) { printf("%s", txt_tv); return; }

    disp_toggle_output (DISP_OFF);
    strncpy (tmp_txt_ti, txt_tv, STATUS_SIZE);
    tmp_txt_ti [STATUS_SIZE] = 0;
    util_center_text (tmp_txt_ti, STATUS_SIZE);

    memset (win_prr, 0, sizeof (window_t));
    win_prr->x_left = max_x_ig / 2 - (STATUS_SIZE + 6) / 2;
    win_prr->y_left = max_y_ig / 2 - 4;
    win_prr->x_right = max_x_ig / 2 + (STATUS_SIZE + 6) / 2 - 1;
    win_prr->y_right = max_y_ig / 2 + 4;
    win_prr->style = STYLE_RAISED;
    win_prr->shadow = TRUE;
    win_prr->save_bg = TRUE;
    win_prr->bg_color = colors_prg->msg_win;
    win_prr->fg_color = colors_prg->msg_fg;
    win_prr->foot = 3;
    win_open (win_prr);

    memset (&tmp_win_ri, 0, sizeof (window_t));
    tmp_win_ri.x_left = win_prr->x_left + 1;
    tmp_win_ri.y_left = win_prr->y_left + 1;
    tmp_win_ri.x_right = win_prr->x_right - 1;
    tmp_win_ri.y_right = win_prr->y_left + 3;
    tmp_win_ri.style = STYLE_SUNKEN;
    tmp_win_ri.bg_color = win_prr->bg_color;
    tmp_win_ri.fg_color = win_prr->fg_color;
    win_open (&tmp_win_ri);
    win_clear (&tmp_win_ri);
    disp_set_color (tmp_win_ri.fg_color, tmp_win_ri.bg_color);
    win_print (&tmp_win_ri, 2, 1, tmp_txt_ti);

    tmp_win_ri.x_left = win_prr->x_left + 1;
    tmp_win_ri.y_left = win_prr->y_right - 3;
    tmp_win_ri.x_right = win_prr->x_right - 1;
    tmp_win_ri.y_right = win_prr->y_right - 1;
    tmp_win_ri.style = STYLE_RAISED;
    win_open (&tmp_win_ri);
    win_clear (&tmp_win_ri);

    memset (tmp_txt_ti, GRAPH_BLOCK, STATUS_SIZE);
    tmp_txt_ti [STATUS_SIZE] = 0;
    disp_gotoxy (win_prr->x_left + 3, win_prr->y_right - 2);
    disp_graph_on ();
    disp_write_string (tmp_txt_ti);
    disp_graph_off ();

    disp_flush_area (win_prr);
    }


void dia_status (window_t *win_prv, int percent_iv)
    {
    char line_ti [STATUS_SIZE + 1];
    int  i_ii;
    static unsigned count = 0;

    if(auto2_ig) {
      if(percent_iv >= 100) {
        printf(".");
      }
      else {
        if(!(count & 3)) printf(".");
      }
      count++;
      fflush(stdout);
      return;
    }

    if (percent_iv > 100)
        percent_iv = 100;

    for (i_ii = 0; i_ii < percent_iv * STATUS_SIZE / 100; i_ii++)
        line_ti [i_ii] = ' ';
    line_ti [i_ii] = 0;
    disp_set_color (win_prv->bg_color, win_prv->fg_color);
    disp_gotoxy (win_prv->x_left + 3, win_prv->y_right - 2);
    disp_write_string (line_ti);
    fflush (stdout);
    }


int dia_input (char *txt_tv, char *input_tr, int len_iv, int fieldlen_iv)
    {
    window_t  win_ri;
    int       width_ii;
    window_t  tmp_win_ri;
    int       rc_ii;


    disp_toggle_output (DISP_OFF);
    memset (&win_ri, 0, sizeof (window_t));
    win_ri.bg_color = colors_prg->input_win;
    win_ri.fg_color = colors_prg->msg_fg;
    width_ii = dia_win_open (&win_ri, txt_tv);

    memset (&tmp_win_ri, 0, sizeof (window_t));
    tmp_win_ri.x_left = win_ri.x_left + 1;
    tmp_win_ri.y_left = win_ri.y_right - 3;
    tmp_win_ri.x_right = win_ri.x_right - 1;
    tmp_win_ri.y_right = win_ri.y_right - 1;
    tmp_win_ri.style = STYLE_RAISED;
    tmp_win_ri.bg_color = win_ri.bg_color;
    tmp_win_ri.fg_color = win_ri.fg_color;
    win_open (&tmp_win_ri);
    win_clear (&tmp_win_ri);
    disp_flush_area (&win_ri);

    rc_ii = win_input (max_x_ig / 2 - fieldlen_iv / 2, win_ri.y_right - 2,
                       input_tr, len_iv, fieldlen_iv);

    win_close (&win_ri);
    return (rc_ii);
    }


int dia_show_lines (char *head_tv, char *lines_atv [], int nr_lines_iv,
                     int   width_iv, int eof_iv)
    {
    window_t  file_win_ri;
    window_t  tmp_win_ri;
    button_t  button_ri;
    int       key_ii;
    int       offset_ii;
    int       i_ii;
    int       textlines_ii;
    int       need_redraw_ii;
    char     *lines_ati [MAX_Y];
    int       line_length_ii;
    int       h_offset_ii;
    char      tmp_ti [MAX_X];
    int       visible_lines_ii;
    int       sb_len_ii;
    int       sb_start_ii;
    int       needflush_ii;


    if (!nr_lines_iv || width_iv < 8)
        return (-1);

    disp_toggle_output (DISP_OFF);
    line_length_ii = width_iv;
    if (width_iv > max_x_ig - 6)
        width_iv = max_x_ig - 6;
    if (nr_lines_iv > max_y_ig - 14)
        visible_lines_ii = max_y_ig - 14;
    else
        visible_lines_ii = nr_lines_iv;

    textlines_ii = util_format_txt (head_tv, lines_ati, width_iv - 4);

    memset (&file_win_ri, 0, sizeof (window_t));
    file_win_ri.x_left = max_x_ig / 2 - width_iv / 2;
    file_win_ri.y_left = max_y_ig / 2 - 4 - (textlines_ii + visible_lines_ii + 1) / 2;
    file_win_ri.x_right = max_x_ig / 2 + (width_iv + 1) / 2;
    file_win_ri.y_right = max_y_ig / 2 + 6 + (textlines_ii + visible_lines_ii) / 2;
    file_win_ri.head = 2 + textlines_ii;
    file_win_ri.foot = 3;
    file_win_ri.shadow = TRUE;
    file_win_ri.style = STYLE_RAISED;
    file_win_ri.bg_color = colors_prg->msg_win;
    file_win_ri.fg_color = colors_prg->msg_fg;
    file_win_ri.save_bg = TRUE;
    win_open (&file_win_ri);
    win_clear (&file_win_ri);

    memset (&tmp_win_ri, 0, sizeof (window_t));
    tmp_win_ri.x_left = file_win_ri.x_left + 1;
    tmp_win_ri.y_left = file_win_ri.y_left + 1;
    tmp_win_ri.x_right = file_win_ri.x_right - 1;
    tmp_win_ri.y_right = file_win_ri.y_left + 2 + textlines_ii;
    tmp_win_ri.style = STYLE_RAISED;
    tmp_win_ri.bg_color = file_win_ri.bg_color;
    tmp_win_ri.fg_color = file_win_ri.fg_color;
    win_open (&tmp_win_ri);

    for (i_ii = 1; i_ii <= textlines_ii; i_ii++)
        {
        win_print (&tmp_win_ri, 2, i_ii, lines_ati [i_ii - 1]);
        free (lines_ati [i_ii - 1]);
        }
    
    util_generate_button (&button_ri, txt_get (TXT_OK));

    win_add_button (&file_win_ri, &button_ri,
                    width_iv / 2 - strlen (button_ri.text) / 2 - 1,
                    strlen (button_ri.text));

    memset (&tmp_win_ri, 0, sizeof (window_t));
    tmp_win_ri.x_left = file_win_ri.x_left + 1;
    tmp_win_ri.y_left = file_win_ri.y_left + file_win_ri.head + 2;
    tmp_win_ri.x_right = file_win_ri.x_right - 1;
    tmp_win_ri.y_right = file_win_ri.y_right - file_win_ri.foot - 2;
    tmp_win_ri.style = STYLE_SUNKEN;
    tmp_win_ri.bg_color = file_win_ri.bg_color;
    tmp_win_ri.fg_color = file_win_ri.fg_color;
    win_open (&tmp_win_ri);
    win_clear (&tmp_win_ri);


    if (eof_iv == TRUE)
        key_ii = KEY_END;
    else
        key_ii = KEY_HOME;

    offset_ii = 0;
    h_offset_ii = 0;
    needflush_ii = TRUE;
    kbd_clear_buffer ();
    do
        {
        need_redraw_ii = FALSE;

        switch (key_ii)
            {
            case KEY_HOME:
                offset_ii = 0;
                need_redraw_ii = TRUE;
                break;
            case KEY_END:
                if (visible_lines_ii < nr_lines_iv)
                    offset_ii = nr_lines_iv - visible_lines_ii;
                need_redraw_ii = TRUE;
                break;
            case KEY_DOWN:
                if (offset_ii + visible_lines_ii < nr_lines_iv)
                    {
                    offset_ii++;
                    need_redraw_ii = TRUE;
                    }
                break;
            case KEY_UP:
                if (offset_ii)
                    {
                    offset_ii--;
                    need_redraw_ii = TRUE;
                    }
                break;
            case KEY_PGDOWN:
                if (visible_lines_ii < nr_lines_iv)
                    {
                    offset_ii += visible_lines_ii;
                    if (offset_ii + visible_lines_ii >= nr_lines_iv)
                        offset_ii = nr_lines_iv - visible_lines_ii;
                    need_redraw_ii = TRUE;
                    }
                break;
            case KEY_PGUP:
                if (visible_lines_ii < nr_lines_iv)
                    {
                    offset_ii -= visible_lines_ii;
                    if (offset_ii < 0)
                        offset_ii = 0;
                    need_redraw_ii = TRUE;
                    }
                break;
            case KEY_RIGHT:
                if (h_offset_ii + width_iv < line_length_ii)
                    {
                    h_offset_ii++;
                    need_redraw_ii = TRUE;
                    }
                break;
            case KEY_LEFT:
                if (h_offset_ii)
                    {
                    h_offset_ii--;
                    need_redraw_ii = TRUE;
                    }
                break;
            default:
                break;
            }

        if (need_redraw_ii)
            {
            disp_set_color (tmp_win_ri.fg_color, tmp_win_ri.bg_color);
            for (i_ii = 0; i_ii < visible_lines_ii; i_ii++)
                if (i_ii + offset_ii < nr_lines_iv)
                    {
                    strncpy (tmp_ti, &lines_atv [offset_ii + i_ii][h_offset_ii],
                             width_iv - 4);
                    tmp_ti [width_iv - 5] = 0;
                    win_print (&tmp_win_ri, 2, i_ii + 1, tmp_ti);
                    }

            if (visible_lines_ii < nr_lines_iv)
                {
                disp_graph_on ();
                sb_len_ii = (visible_lines_ii * visible_lines_ii + 1) / nr_lines_iv;
                if (sb_len_ii >= visible_lines_ii - 1)
                    sb_len_ii--;
                sb_start_ii = (offset_ii * visible_lines_ii) / nr_lines_iv;
                if (offset_ii + visible_lines_ii < nr_lines_iv &&
                    sb_start_ii + sb_len_ii == visible_lines_ii - 1)
                    sb_start_ii--;
                if (offset_ii && !sb_start_ii)
                    sb_start_ii++;

                for (i_ii = 0; i_ii < visible_lines_ii; i_ii++)
                    {
                    disp_gotoxy (tmp_win_ri.x_right - 1, tmp_win_ri.y_left + 1 + i_ii);
                    if (i_ii >= sb_start_ii && i_ii <= sb_start_ii + sb_len_ii)
                        {
                        disp_set_color (tmp_win_ri.bg_color, tmp_win_ri.fg_color);
                        disp_write_char (' ');
                        }
                    else
                        {
                        disp_set_color (tmp_win_ri.fg_color, tmp_win_ri.bg_color);
                        disp_write_char (GRAPH_BLOCK);
                        }
                    }
                disp_graph_off ();
                }

            if (needflush_ii)
                {
                needflush_ii = FALSE;
                disp_flush_area (&file_win_ri);
                }

            fflush (stdout);
            }

        key_ii = kbd_getch (TRUE);
        if (key_ii == KEY_CTRL_C)
            dia_handle_ctrlc ();
        }
    while (key_ii != KEY_ENTER && key_ii != KEY_ESC);

    win_button_pressed (&button_ri, FALSE);
    win_close (&file_win_ri);
    return (0);
    }


int dia_show_file (char *head_tv, char *file_tv, int eof_iv)
    {
    FILE     *fd_pri;
    char     *lines_ati [MAX_LINES];
    int       nr_lines_ii;
    int       i_ii;
    char      buffer_ti [MAX_X];
    int       width_ii;
    int       rc_ii = 0;

    fd_pri = fopen (file_tv, "r");
    if (!fd_pri)
        return (-1);

    nr_lines_ii = 0;
    width_ii = 0;
    while (fgets (buffer_ti, MAX_X - 1, fd_pri) &&
           nr_lines_ii < MAX_LINES)
        {
        lines_ati [nr_lines_ii] = malloc (MAX_X);
        if (strrchr (buffer_ti, '\n'))
            *strrchr (buffer_ti, '\n') = 0;
        for (i_ii = 0; buffer_ti [i_ii]; i_ii++)
            if (buffer_ti [i_ii] == '\t' || buffer_ti [i_ii] == 0x0d)
                buffer_ti [i_ii] = ' ';
        strncpy (lines_ati [nr_lines_ii], buffer_ti, MAX_X - 1);
        lines_ati [nr_lines_ii][MAX_X - 1] = 0;
        if (strlen (lines_ati [nr_lines_ii]) + 6 > width_ii)
            width_ii = strlen (lines_ati [nr_lines_ii]) + 6;
        util_fill_string (lines_ati [nr_lines_ii], MAX_X);
        nr_lines_ii++;
        }
    fclose (fd_pri);

    rc_ii = dia_show_lines (head_tv, lines_ati, nr_lines_ii, width_ii, eof_iv);

    for (i_ii = 0; i_ii < nr_lines_ii; i_ii++)
        free (lines_ati [i_ii]);

    return (rc_ii);
    }


void dia_info (window_t *win_prr, char *txt_tv)
    {
    int        width_ii;
    window_t   tmp_win_ri;
    char      *lines_ati [MAX_Y];
    int        nr_lines_ii;
    int        i_ii;


    disp_toggle_output (DISP_OFF);
    width_ii = strlen (txt_tv) + 6;
    if (width_ii < MIN_WIN_SIZE)
        width_ii = MIN_WIN_SIZE;

    if (width_ii > max_x_ig - 16)
        width_ii = max_x_ig - 16;

    nr_lines_ii = util_format_txt (txt_tv, lines_ati, width_ii - 4);

    memset (win_prr, 0, sizeof (window_t));
    win_prr->x_left = max_x_ig / 2 - width_ii / 2;
    win_prr->y_left = max_y_ig / 2 - nr_lines_ii / 2 - 1;
    win_prr->x_right = max_x_ig / 2 + width_ii / 2;
    win_prr->y_right = max_y_ig / 2 + (nr_lines_ii + 1) / 2 + 2;
    win_prr->shadow = TRUE;
    win_prr->style = STYLE_RAISED;
    win_prr->save_bg = TRUE;
    win_prr->bg_color = colors_prg->msg_win;
    win_prr->fg_color = colors_prg->msg_fg;
    win_open (win_prr);
    win_clear (win_prr);

    memset (&tmp_win_ri, 0, sizeof (window_t));
    tmp_win_ri.x_left = win_prr->x_left + 1;
    tmp_win_ri.y_left = win_prr->y_left + 1;
    tmp_win_ri.x_right = win_prr->x_right - 1;
    tmp_win_ri.y_right = win_prr->y_right - 1;
    tmp_win_ri.style = STYLE_SUNKEN;
    tmp_win_ri.bg_color = win_prr->bg_color;
    tmp_win_ri.fg_color = win_prr->fg_color;
    win_open (&tmp_win_ri);
    win_clear (&tmp_win_ri);

    for (i_ii = 1; i_ii <= nr_lines_ii; i_ii++)
        {
        win_print (&tmp_win_ri, 2, i_ii, lines_ati [i_ii - 1]);
        free (lines_ati [i_ii - 1]);
        }

    disp_flush_area (win_prr);
    }

/*
 *
 *  local functions
 *
 */

static int dia_win_open (window_t *win_prr, char *txt_tv)
    {
    int        width_ii;
    window_t   tmp_win_ri;
    char      *lines_ati [MAX_Y];
    int        nr_lines_ii;
    int        i_ii;


    width_ii = strlen (txt_tv) + 6;
    if (width_ii < MIN_WIN_SIZE)
        width_ii = MIN_WIN_SIZE;

    if (width_ii > max_x_ig - 16)
        width_ii = max_x_ig - 16;

    nr_lines_ii = util_format_txt (txt_tv, lines_ati, width_ii - 4);

    win_prr->x_left = max_x_ig / 2 - width_ii / 2;
    win_prr->y_left = max_y_ig / 2 - 3 - nr_lines_ii / 2;
    win_prr->x_right = max_x_ig / 2 + width_ii / 2;
    win_prr->y_right = max_y_ig / 2 + 4 + (nr_lines_ii + 1) / 2;
    win_prr->foot = 3;
    win_prr->shadow = TRUE;
    win_prr->style = STYLE_RAISED;
    win_prr->save_bg = TRUE;
    win_open (win_prr);
    win_clear (win_prr);

    memset (&tmp_win_ri, 0, sizeof (window_t));
    tmp_win_ri.x_left = max_x_ig / 2 - width_ii / 2 + 1;
    tmp_win_ri.y_left = win_prr->y_left + 1;
    tmp_win_ri.x_right = max_x_ig / 2 + width_ii / 2 - 1;
    tmp_win_ri.y_right = win_prr->y_right - 5;
    tmp_win_ri.style = STYLE_SUNKEN;
    tmp_win_ri.bg_color = win_prr->bg_color;
    tmp_win_ri.fg_color = win_prr->fg_color;
    win_open (&tmp_win_ri);
    disp_set_color (tmp_win_ri.fg_color, tmp_win_ri.bg_color);

    for (i_ii = 1; i_ii <= nr_lines_ii; i_ii++)
        {
        win_print (&tmp_win_ri, 2, i_ii, lines_ati [i_ii - 1]);
        free (lines_ati [i_ii - 1]);
        }
    
    return (width_ii);
    }


void dia_handle_ctrlc (void)
    {
    int i, j;
    static int is_in_ctrlc_is = FALSE;
    static char s[100] = { };
    char *t;

    if (is_in_ctrlc_is)
        return;

    is_in_ctrlc_is = TRUE;

    i = dia_message(txt_get(TXT_NO_CTRLC), MSGTYPE_ERROR);
    if(i == -42) {
      lxrc_end();
      exit(0);
    }
    else if(i == -69) {
      i = dia_input("run command", s, sizeof s - 1, 35);
      if(!i) {
        if(strstr(s, "exec ") == s) {
          t = s + 5;
          while(isspace(*t)) t++;
          kbd_end();	/* restore terminal settings */
          j = execlp(t, t, NULL);
          kbd_init();
        }
        else {
          j = system(s);
        }
        if(j) fprintf(stderr, "  exit code: %d\n", WIFEXITED(j) ? WEXITSTATUS(j) : -1);
      }
    }

    else if(i == -71) {
      util_status_info();
    }

    is_in_ctrlc_is = FALSE;
    }


char *dia_get_text(dia_item_t di)
{
  int i;
  char *s = "";

  for(i = 0; i < sizeof dia_texts / sizeof *dia_texts; i++) {
    if(dia_texts[i].item == di) {
      s = dia_texts[i].text ?: txt_get(dia_texts[i].text_id);
      break;
    }
  }

  return s;
}


/*
 * returns selected menu item, or di_none (ESC pressed)
 */
dia_item_t dia_menu2(char *title, int width, int (*func)(dia_item_t), dia_item_t *items, dia_item_t default_item)
{
  int item_cnt, default_idx, i;
  dia_item_t *it, di;
  item_t *item_list;

  for(item_cnt = 0, it = items; *it != di_none; it++) {
    if(*it != di_skip) item_cnt++;
  }

  if(!item_cnt) return di_none;

  item_list = calloc(item_cnt, sizeof *item_list);

  util_create_items(item_list, item_cnt, width);

  default_idx = 1;
  for(i = 0, it = items; *it != di_none; it++) {
    if(*it != di_skip) {
      if(*it == default_item) default_idx = i + 1;
      strncpy(item_list[i].text, dia_get_text(*it), width);
      item_list[i].text[width] = 0;
      item_list[i].di = *it;
      item_list[i].func = (int (*)(int)) func;
      util_center_text(item_list[i].text, width);
      i++;
    }
  }

  i = dia_menu(title, item_list, item_cnt, default_idx);

  if(i > 0 && i <= item_cnt) {
    di = item_list[i - 1].di;
  }
  else {
    di = di_none;
  }

  util_free_items(item_list, item_cnt);

  return di;
}


/*
 * returns selected item (1 based), or 0 (ESC pressed)
 */
int dia_list(char *title, int width, int (*func)(int), char **items, int default_item, dia_align_t align)
{
  int item_cnt, i;
  char **it;
  item_t *item_list;

  for(item_cnt = 0, it = items; *it; it++) item_cnt++;

  if(!item_cnt) return 0;

  item_list = calloc(item_cnt, sizeof *item_list);

  util_create_items(item_list, item_cnt, width);

  if(default_item < 1 || default_item > item_cnt) default_item = 1;

  for(i = 0, it = items; *it; it++, i++) {
    strncpy(item_list[i].text, *it, width);
    item_list[i].text[width] = 0;
    item_list[i].func = func;
    if(align == align_center) {
      util_center_text(item_list[i].text, width);
    }
    else {
      util_fill_string(item_list[i].text, width);
    }
  }

  i = dia_menu(title, item_list, item_cnt, default_item);

  util_free_items(item_list, item_cnt);

  return i;
}


