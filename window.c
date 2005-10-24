/*
 *
 * window.c      Window handling
 *
 * Copyright (c) 1996-2002  Hubert Mantel, SuSE Linux AG  (mantel@suse.de)
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
#include "utf8.h"


#define MAX_FIELD        40

static int is_printable(int key);

void win_open (window_t *win_prr)
    {
    int  i_ii;
    int line_aci [MAX_X];


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
    disp_write_utf32string (line_aci);

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
    disp_write_utf32string (line_aci);

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
        disp_write_utf32string (line_aci);

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
        disp_write_utf32string (line_aci);

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
    if (config.win && win_prr->save_bg)
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


void fill_string(int *str, int len)
{
  int i;

  for(len--, i = 0; str[i] && i < len; i++);

  while(i < len) str[i++] = ' ';

  str[i] = 0;
}


int win_input (int x, int y, char *buf, int buf_len, int field_len, int pw_mode)
{
  int key, *input;
  int input_len, end;
  int *undo_buf, *tmp_buf;
  int field[MAX_FIELD + 1];

  int i, cur, ofs, overwrite;

    int   old_end_ii;
    int   tmp_end_ii;

  void goto_end(void)
  {
    if(end < field_len) {
      ofs = 0;
      cur = end;
    }
    else {
      cur = field_len - 1;
      ofs = end - cur;
    }
  }

  disp_cursor_on();

  input_len = buf_len;

  input = malloc(input_len * sizeof *input);
  undo_buf = malloc(input_len * sizeof *undo_buf);
  tmp_buf = malloc(input_len * sizeof *tmp_buf);

  utf8_to_utf32(input, input_len, buf);

  if(field_len > input_len) field_len = input_len;

  if(field_len > MAX_FIELD) field_len = MAX_FIELD;

  end = utf32_len(input);

  if(end > input_len - 1) end = input_len - 1;

  fill_string(input, input_len);


  cur = ofs = 0;
  overwrite = 0;

  key = KEY_END;
  field[field_len] = 0;

  old_end_ii = end;

  do {
    memcpy(tmp_buf, input, input_len * sizeof *tmp_buf);

    tmp_end_ii = end;

    switch(key) {

      case KEY_NONE:
        break;

      case KEY_LEFT:
        if(cur) {
          cur--;
        }
        else if(ofs) {
          ofs--;
        }
        break;

      case KEY_RIGHT:
        if(ofs + cur < end) {
          if(cur < field_len - 1) {
            cur++;
          }
          else if(ofs + cur < input_len - 1) {
            ofs++;
          }
        }
        break;

      case KEY_HOME:
      case KEY_CTRL_A:
        ofs = cur = 0;
        break;

      case KEY_END:
      case KEY_CTRL_E:
        goto_end();
        break;

      case KEY_INSERT:
        overwrite = !overwrite;
        break;

      case KEY_BACKSPACE:
      case KEY_CTRL_D:
      case KEY_CTRL_H:
        if(ofs + cur) {
          if(ofs) {
            ofs--;
          }
          else if(cur) {
            cur--;
          }
          for(i = ofs + cur; i < end; i++) input[i] = input[i + 1];
          input[--end] = ' ';
        }
        break;

      case KEY_DEL:
        if(end && ofs + cur < end) {
          for(i = ofs + cur; i < end; i++) input[i] = input[i + 1];
          input[--end] = ' ';
        }
        break;

      case KEY_CTRL_K:
        cur = ofs = end = 0;
        input[0] = 0;
        fill_string(input, input_len);
        break;

      case KEY_CTRL_C:
        dia_handle_ctrlc();
        break;

      case KEY_CTRL_U:
        end = old_end_ii;
        memcpy(input, undo_buf, input_len * sizeof *input);
        goto_end();
        break;

      case KEY_CTRL_W:
        while(end && input[end - 1] == ' ') end--;
        while(end && input[end - 1] != ' ') input[--end] = ' ';
        goto_end();
        break;

      default:
        if(is_printable(key)) {
          if(!overwrite) {
            for(i = end; i > ofs + cur; i--) input[i] = input[i - 1];

            if(ofs + cur < input_len - 1) input [ofs + cur] = key;

            if(cur < field_len - 1) {
              cur++;
            }
            else if(ofs + cur < input_len - 1) {
              ofs++;
            }

            if(end < input_len - 1) end++;
          }
          else {
            if(ofs + cur < input_len - 1) input[ofs + cur] = key;

            if(ofs + cur == end && end < input_len - 1) end++;

            if(cur < field_len - 1) {
              cur++;
            }
            else if(ofs + cur < input_len - 1) {
              ofs++;
            }
          }
        }
        break;

    }

    input[input_len - 1] = ' ';
    for(i = 0; i < field_len; i++) {
      if(pw_mode && ofs + i < end) {
        field[i] = '*';
      }
      else {
        field[i] = input[ofs + i];
      }
    }

    if(memcmp(input, tmp_buf, input_len * sizeof *input)) {
      old_end_ii = tmp_end_ii;
      memcpy(undo_buf, tmp_buf, input_len * sizeof *undo_buf);
    }

    disp_gotoxy(x, y);
    disp_set_color(colors_prg->input_fg, colors_prg->input_bg);
    disp_write_utf32string(field);
    disp_gotoxy(x + cur, y);
    fflush(stdout);

    key = kbd_getch(TRUE);

    if(key == KEY_TAB) key = ' ';
  }
  while(key != KEY_ENTER && key != KEY_ESC);

  input[end] = 0;

  utf32_to_utf8(buf, buf_len, input);

  free(input);
  free(undo_buf);
  free(tmp_buf);

  disp_cursor_off();

  return key == KEY_ENTER ? 0 : -1;
}


int is_printable(int key)
{
  return key < 0x20 || key >= (1 << 21) ? 0 : 1;
}

