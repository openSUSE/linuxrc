/*
 *
 * window.c      Window handling
 *
 * Copyright (c) 1996-1998  Hubert Mantel, S.u.S.E. GmbH  (mantel@suse.de)
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>

#include "global.h"
#include "display.h"
#include "window.h"
#include "keyboard.h"
#include "util.h"
#include "dialog.h"

#define MAX_FIELD        40

void win_open (window_t *win_prr)
    {
    int  i_ii;
    char line_aci [MAX_X];


    if (win_prr->x_left < 1 || win_prr->x_left > max_x_ig)
        win_prr->x_left = 1;
    if (win_prr->y_left < 1 || win_prr->y_left > max_y_ig)
        win_prr->y_left = 1;
    if (win_prr->x_right > max_x_ig || win_prr->x_right <= win_prr->x_left)
        win_prr->x_right = max_x_ig;
    if (win_prr->y_right > max_y_ig || win_prr->y_right <= win_prr->y_left)
        win_prr->y_right = max_y_ig;

    if (win_prr->save_bg)
        disp_save_area (win_prr);

    disp_graph_on ();

    line_aci [0] = GRAPH_ULCORNER;
    for (i_ii = 1; i_ii < win_prr->x_right - win_prr->x_left; i_ii++)
        line_aci [i_ii] = GRAPH_HLINE;
    line_aci [i_ii] = 0;
    disp_gotoxy (win_prr->x_left, win_prr->y_left);
    if (win_prr->style == STYLE_RAISED && colors_prg->has_colors)
        disp_set_color (win_prr->bg_color | 0x08, win_prr->bg_color);
    else if (colors_prg->has_colors)
        disp_set_color (COL_BLACK, win_prr->bg_color);
    else
        disp_set_color (win_prr->fg_color, win_prr->bg_color);
    disp_write_string (line_aci);

    line_aci [0] = GRAPH_LLCORNER;
    disp_gotoxy (win_prr->x_left, win_prr->y_right);
    disp_write_char (line_aci [0]);

    line_aci [0] = GRAPH_VLINE;
    for (i_ii = win_prr->y_left + 1; i_ii < win_prr->y_right; i_ii++)
        {
        disp_gotoxy (win_prr->x_left, i_ii);
        disp_write_char (line_aci [0]);
        }

    if (win_prr->style == STYLE_SUNKEN && colors_prg->has_colors)
        disp_set_color (win_prr->bg_color | 0x08, win_prr->bg_color);
    else if (colors_prg->has_colors)
        disp_set_color (COL_BLACK, win_prr->bg_color);
    else
        disp_set_color (win_prr->fg_color, win_prr->bg_color);
    for (i_ii = win_prr->y_left + 1; i_ii < win_prr->y_right; i_ii++)
        {
        disp_gotoxy (win_prr->x_right, i_ii);
        disp_write_char (line_aci [0]);
        }

    for (i_ii = 0; i_ii < win_prr->x_right - win_prr->x_left - 1; i_ii++)
        line_aci [i_ii] = GRAPH_HLINE;
    line_aci [i_ii] = GRAPH_LRCORNER;
    disp_gotoxy (win_prr->x_left + 1, win_prr->y_right);
    disp_write_string (line_aci);

    line_aci [0] = GRAPH_URCORNER;
    disp_gotoxy (win_prr->x_right, win_prr->y_left);
    disp_write_char (line_aci [0]);

    if (win_prr->head)
        {
        if (win_prr->head < 0 || win_prr->head > 6)
            win_prr->head = 1;

        for (i_ii = 0; i_ii < win_prr->x_right - win_prr->x_left - 1; i_ii++)
            line_aci [i_ii] = GRAPH_HLINE;
        line_aci [i_ii] = GRAPH_RTEE;
        line_aci [++i_ii] = 0;
        if (win_prr->style == STYLE_SUNKEN && colors_prg->has_colors)
            disp_set_color (win_prr->bg_color | 0x08, win_prr->bg_color);
        else if (colors_prg->has_colors)
            disp_set_color (COL_BLACK, win_prr->bg_color);
        else
            disp_set_color (win_prr->fg_color, win_prr->bg_color);

        disp_gotoxy (win_prr->x_left + 1, win_prr->y_left + win_prr->head + 1);
        disp_write_string (line_aci);

        line_aci [0] = GRAPH_LTEE;
        if (win_prr->style == STYLE_RAISED && colors_prg->has_colors)
            disp_set_color (win_prr->bg_color | 0x08, win_prr->bg_color);
        else if (colors_prg->has_colors)
            disp_set_color (COL_BLACK, win_prr->bg_color);
        else
            disp_set_color (win_prr->fg_color, win_prr->bg_color);
        
        disp_gotoxy (win_prr->x_left, win_prr->y_left + win_prr->head + 1);
        disp_write_char (line_aci [0]);
        }

    if (win_prr->foot)
        {
        if (win_prr->foot < 0 || win_prr->foot > 5)
            win_prr->foot = 1;

        line_aci [0] = GRAPH_LTEE;
        for (i_ii = 1; i_ii < win_prr->x_right - win_prr->x_left; i_ii++)
            line_aci [i_ii] = GRAPH_HLINE;
        line_aci [i_ii] = 0;
        if (win_prr->style == STYLE_RAISED && colors_prg->has_colors)
            disp_set_color (win_prr->bg_color | 0x08, win_prr->bg_color);
        else if (colors_prg->has_colors)
            disp_set_color (COL_BLACK, win_prr->bg_color);
        else
            disp_set_color (win_prr->fg_color, win_prr->bg_color);

        disp_gotoxy (win_prr->x_left, win_prr->y_right - win_prr->foot - 1);
        disp_write_string (line_aci);

        line_aci [0] = GRAPH_RTEE;
        if (win_prr->style == STYLE_SUNKEN && colors_prg->has_colors)
            disp_set_color (win_prr->bg_color | 0x08, win_prr->bg_color);
        else if (colors_prg->has_colors)
            disp_set_color (COL_BLACK, win_prr->bg_color);
        else
            disp_set_color (win_prr->fg_color, win_prr->bg_color);
        
        disp_gotoxy (win_prr->x_right, win_prr->y_right - win_prr->foot - 1);
        disp_write_char (line_aci [0]);
        }

    if (win_prr->shadow)
        {
        disp_set_color (COL_WHITE, COL_BLACK);
        for (i_ii = win_prr->y_left + 1; i_ii <= win_prr->y_right + 1; i_ii++)
            {
            disp_refresh_char (win_prr->x_right + 1, i_ii);
            disp_refresh_char (win_prr->x_right + 2, i_ii);
            }

        for (i_ii = win_prr->x_left + 2; i_ii < win_prr->x_right + 2; i_ii++)
            disp_refresh_char (i_ii, win_prr->y_right + 1);
        }

    disp_graph_off ();
    }


void win_close (window_t *win_prr)
    {
    if (auto2_ig)
        {
        printf ("\n");
        fflush (stdout);
        return;
        }

    if (win_prr->save_bg)
        disp_restore_area (win_prr, DISP_RESTORE_IMPLODE);
    }


void win_clear (window_t *win_prv)
    {
    char line_aci [MAX_X];
    int  i_ii;


    disp_set_color (win_prv->fg_color, win_prv->bg_color);
    for (i_ii = 0; i_ii < win_prv->x_right - win_prv->x_left - 1; i_ii++)
        line_aci [i_ii] = ' ';

    line_aci [i_ii] = 0;

    for (i_ii = win_prv->y_left + 1; i_ii < win_prv->y_right; i_ii++)
        if ((i_ii != win_prv->y_left + win_prv->head + 1  || !win_prv->head) &&
            (i_ii != win_prv->y_right - win_prv->foot - 1 || !win_prv->foot))
            {
            disp_gotoxy (win_prv->x_left + 1, i_ii);
            disp_write_string (line_aci);
            }
    }


void win_print (window_t *win_prv, int x_iv, int y_iv, char *text_tv)
    {
    int  max_y;
    int  y_pos;


    if (win_prv->head)
        y_pos = win_prv->y_left + win_prv->head + y_iv + 1;
    else
        y_pos = win_prv->y_left + y_iv;

    if (win_prv->foot)
        max_y = win_prv->y_right - win_prv->foot - 2;
    else
        max_y = win_prv->y_right;

    if (y_pos <= max_y)
        {
        disp_gotoxy (win_prv->x_left + x_iv, y_pos);
        disp_write_string (text_tv);
        }
    }


void win_add_button (window_t *win_prv, button_t *button_prr,
                     int pos_iv,        int size_iv)
    {
    button_prr->win.x_left = win_prv->x_left + pos_iv;
    button_prr->win.y_left = win_prv->y_right - 3;
    button_prr->win.x_right = win_prv->x_left + pos_iv + size_iv + 1;
    button_prr->win.y_right = win_prv->y_right -1;
    button_prr->win.shadow = FALSE;
    button_prr->win.bg_color = win_prv->bg_color;
    if (colors_prg->has_colors)
        button_prr->win.fg_color = COL_BLACK;
    else
        button_prr->win.fg_color = win_prv->fg_color;
    button_prr->win.style = STYLE_RAISED;

    win_open (&button_prr->win);
    disp_set_color (button_prr->win.fg_color, button_prr->win.bg_color);
    win_print (&button_prr->win, 1, 1, button_prr->text);
    }


void win_button_pressed (button_t *button_prr, int stay_iv)
    {
    button_prr->win.style = STYLE_SUNKEN;
    win_open (&button_prr->win);
    disp_set_color (button_prr->win.fg_color, button_prr->win.bg_color);
    win_print (&button_prr->win, 1, 1, button_prr->text);
    fflush (stdout);

    if (!stay_iv)
        {
        usleep (BUTTON_TIME);
        win_button_unpressed (button_prr);
        }
    }


void win_button_unpressed (button_t *button_prr)
    {
    button_prr->win.style = STYLE_RAISED;
    win_open (&button_prr->win);
    disp_set_color (button_prr->win.fg_color, button_prr->win.bg_color);
    win_print (&button_prr->win, 1, 1, button_prr->text);
    fflush (stdout);
    }


void win_button_select (button_t *button_prr)
    {
    disp_set_color (colors_prg->button_fg, colors_prg->button_bg);
    win_print (&button_prr->win, 1, 1, button_prr->text);
    fflush (stdout);
    }


void win_button_unselect (button_t *button_prr)
    {
    disp_set_color (button_prr->win.fg_color, button_prr->win.bg_color);
    win_print (&button_prr->win, 1, 1, button_prr->text);
    fflush (stdout);
    }


int win_choose_button (button_t *buttons_arr [], int nr_buttons_iv,
                                                 int default_iv)
    {
    int key_ii = 0;
    int current_ii;

    kbd_clear_buffer ();
    if (default_iv <= nr_buttons_iv && default_iv > 0)
        current_ii = default_iv - 1;
    else
        current_ii = 0;

    win_button_select (buttons_arr [current_ii]);

    do
        {
        key_ii = kbd_getch (TRUE);
        switch (key_ii)
            {
            case KEY_RIGHT:
            case KEY_TAB:
                win_button_unselect (buttons_arr [current_ii]);
                current_ii = (current_ii + 1) % nr_buttons_iv;
                win_button_select (buttons_arr [current_ii]);
                break;

            case KEY_LEFT:
            case KEY_BTAB:
                win_button_unselect (buttons_arr [current_ii]);
                current_ii = (current_ii + nr_buttons_iv - 1) % nr_buttons_iv;
                win_button_select (buttons_arr [current_ii]);
                break;
            
            case KEY_ENTER:
                win_button_pressed (buttons_arr [current_ii], FALSE);
                break;

            case KEY_CTRL_C:
                dia_handle_ctrlc ();
                break;

            default:
                break;
            }
        }
    while (key_ii != KEY_ENTER && key_ii != KEY_ESC);

    if (key_ii == KEY_ENTER)
        return (current_ii + 1);
    else
        return (0);
    }
    

int win_input (int x_iv, int y_iv, char *input_tr, int len_iv, int fieldlen_iv)
    {
    int   key_ii;
    char  field_ti [MAX_FIELD + 1];
    int   current_ii;
    int   offset_ii;
    int   i_ii;
    int   end_ii;
    int   overwrite_ii;
    char *undo_pci;
    char *tmp_pci;
    int   old_end_ii;
    int   tmp_end_ii;


    disp_cursor_on ();
    if (fieldlen_iv > len_iv)
        fieldlen_iv = len_iv;
    if (fieldlen_iv > MAX_FIELD)
        fieldlen_iv = MAX_FIELD;
    end_ii = strlen (input_tr);
    if (end_ii > len_iv - 1)
        end_ii = len_iv - 1;
    util_fill_string (input_tr, len_iv);
    current_ii = 0;
    offset_ii = 0;
    key_ii = KEY_END;
    field_ti [fieldlen_iv] = 0;
    overwrite_ii = FALSE;
    undo_pci = malloc (len_iv);
    tmp_pci = malloc (len_iv);
    old_end_ii = end_ii;

    do
        {
        void goto_end (void)
            {
            if (end_ii < fieldlen_iv)
                {
                offset_ii = 0;
                current_ii = end_ii;
                }
            else
                {
                current_ii = fieldlen_iv - 1;
                offset_ii = end_ii - current_ii;
                }
            }

        memcpy (tmp_pci, input_tr, len_iv);
        tmp_end_ii = end_ii;

        switch (key_ii)
            {
            case KEY_LEFT:
                if (current_ii)
                    current_ii--;
                else if (offset_ii)
                    offset_ii--;
                break;
            case KEY_RIGHT:
                if (offset_ii + current_ii < end_ii)
                    {
                    if (current_ii < fieldlen_iv - 1)
                        current_ii++;
                    else if (offset_ii + current_ii < len_iv - 1)
                        offset_ii++;
                    }
                break;
            case KEY_HOME:
            case KEY_CTRL_A:
                offset_ii = 0;
                current_ii = 0;
                break;
            case KEY_END:
            case KEY_CTRL_E:
                goto_end ();
                break;
            case KEY_INSERT:
                overwrite_ii = !overwrite_ii;
                break;
            case KEY_BACKSPACE:
            case KEY_CTRL_D:
            case KEY_CTRL_H:
                if (offset_ii + current_ii)
                    {
                    if (offset_ii)
                        offset_ii--;
                    else if (current_ii)
                        current_ii--;
                    for (i_ii = offset_ii + current_ii; i_ii < end_ii; i_ii++)
                        input_tr [i_ii] = input_tr [i_ii + 1];
                    input_tr [--end_ii] = ' ';
                    }
                break;
            case KEY_DEL:
                if (end_ii && offset_ii + current_ii < end_ii)
                    {
                    for (i_ii = offset_ii + current_ii; i_ii < end_ii; i_ii++)
                        input_tr [i_ii] = input_tr [i_ii + 1];
                    input_tr [--end_ii] = ' ';
                    }
                break;
            case KEY_CTRL_K:
                current_ii = 0;
                offset_ii = 0;
                end_ii = 0;
                input_tr [0] = 0;
                util_fill_string (input_tr, len_iv);
                break;
            case KEY_CTRL_C:
                dia_handle_ctrlc ();
                break;
            case KEY_CTRL_U:
                end_ii = old_end_ii;
                memcpy (input_tr, undo_pci, len_iv);
                goto_end ();
                break;
            case KEY_CTRL_W:
                while (end_ii && input_tr [end_ii - 1] == ' ')
                    end_ii--;
                while (end_ii && input_tr [end_ii - 1] != ' ')
                    input_tr [--end_ii] = ' ';
                goto_end ();
                break;
            default:
/*                if (isprint (key_ii)) */
                    {
                    if (!overwrite_ii)
                        {
                        for (i_ii = end_ii; i_ii > offset_ii + current_ii; i_ii--)
                            input_tr [i_ii] = input_tr [i_ii - 1];

                        if (offset_ii + current_ii < len_iv - 1)
                            input_tr [offset_ii + current_ii] = key_ii;

                        if (current_ii < fieldlen_iv - 1)
                            current_ii++;
                        else if (offset_ii + current_ii < len_iv - 1)
                            offset_ii++;

                        if (end_ii < len_iv - 1)
                            end_ii++;
                        }
                    else
                        {
                        if (offset_ii + current_ii < len_iv - 1)
                            input_tr [offset_ii + current_ii] = key_ii;

                        if (offset_ii + current_ii == end_ii && end_ii < len_iv - 1)
                            end_ii++;

                        if (current_ii < fieldlen_iv - 1)
                            current_ii++;
                        else if (offset_ii + current_ii < len_iv - 1)
                            offset_ii++;
                        }
                    }
                break;
            }

        input_tr [len_iv - 1] = ' ';
        for (i_ii = 0; i_ii < fieldlen_iv; i_ii++)
            if (passwd_mode_ig && offset_ii + i_ii < end_ii)
                field_ti [i_ii] = '*';
            else
                field_ti [i_ii] = input_tr [offset_ii + i_ii];

        if (memcmp (input_tr, tmp_pci, len_iv))
            {
            old_end_ii = tmp_end_ii;
            memcpy (undo_pci, tmp_pci, len_iv);
            }

        disp_gotoxy (x_iv, y_iv);
        disp_set_color (colors_prg->input_fg, colors_prg->input_bg);
        disp_write_string (field_ti);
        disp_gotoxy (x_iv + current_ii, y_iv);
        fflush (stdout);

        key_ii = kbd_getch (TRUE);
        }
    while (key_ii != KEY_ENTER && key_ii != KEY_ESC);

    free (undo_pci);
    free (tmp_pci);
    disp_cursor_off ();
    input_tr [end_ii] = 0;

    if (key_ii == KEY_ENTER)
        return (0);
    else
        return (-1);
    }

