/*
 *
 * display.c     Low level display functions
 *
 * Copyright (c) 1996-2002  Hubert Mantel, SuSE Linux AG  (mantel@suse.de)
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include "global.h"
#include "display.h"

/*
 *
 * data on module level
 *
 */

#define IS_BRIGHT(x)     (x & 0x08)
#define IS_ALTERNATE(x)  (x & 0x80)
#define FOREGROUND(x)    ((x >> 3) & 0x0f)
#define BACKGROUND(x)    (x & 0x07)

static char disp_attr_cm;
static int  disp_x_im;
static int  disp_y_im;
static int  disp_state_im = DISP_ON;

static character_t **disp_screen_aprm;

colorset_t  disp_vgacolors_rm;
static colorset_t  disp_mono_rm;
static colorset_t  disp_alternate_rm;

graphics_t graphics_sg;

/*
 *
 * local function prototypes
 *
 */


/*
 *
 * exported functions
 *
 */

void disp_init (void)
    {
    int   i_ii;

    disp_vgacolors_rm.has_colors = TRUE;
    disp_vgacolors_rm.bg         = COL_BLUE;
    disp_vgacolors_rm.msg_win    = COL_CYAN;
    disp_vgacolors_rm.msg_fg     = COL_BLACK;
    disp_vgacolors_rm.choice_win = COL_WHITE;
    disp_vgacolors_rm.choice_fg  = COL_BLACK;
    disp_vgacolors_rm.menu_win   = COL_WHITE;
    disp_vgacolors_rm.menu_fg    = COL_BLACK;
    disp_vgacolors_rm.button_bg  = COL_BLUE;
    disp_vgacolors_rm.button_fg  = COL_BWHITE;
    disp_vgacolors_rm.input_win  = COL_WHITE;
    disp_vgacolors_rm.input_bg   = COL_BLUE;
    disp_vgacolors_rm.input_fg   = COL_BWHITE;
    disp_vgacolors_rm.error_win  = COL_RED;
    disp_vgacolors_rm.error_fg   = COL_BYELLOW;

    disp_alternate_rm.has_colors = TRUE;
    disp_alternate_rm.bg         = COL_GREEN;
    disp_alternate_rm.msg_win    = COL_MAGENTA;
    disp_alternate_rm.msg_fg     = COL_BLACK;
    disp_alternate_rm.choice_win = COL_YELLOW;
    disp_alternate_rm.choice_fg  = COL_BLACK;
    disp_alternate_rm.menu_win   = COL_YELLOW;
    disp_alternate_rm.menu_fg    = COL_BLACK;
    disp_alternate_rm.button_bg  = COL_CYAN;
    disp_alternate_rm.button_fg  = COL_BWHITE;
    disp_alternate_rm.input_win  = COL_YELLOW;
    disp_alternate_rm.input_bg   = COL_CYAN;
    disp_alternate_rm.input_fg   = COL_BWHITE;
    disp_alternate_rm.error_win  = COL_RED;
    disp_alternate_rm.error_fg   = COL_BLACK;

    disp_mono_rm.has_colors = FALSE;
    disp_mono_rm.bg         = COL_BLACK;
    disp_mono_rm.msg_win    = COL_BLACK;
    disp_mono_rm.msg_fg     = COL_WHITE;
    disp_mono_rm.choice_win = COL_BLACK;
    disp_mono_rm.choice_fg  = COL_WHITE;
    disp_mono_rm.menu_win   = COL_BLACK;
    disp_mono_rm.menu_fg    = COL_WHITE;
    disp_mono_rm.button_bg  = COL_WHITE;
    disp_mono_rm.button_fg  = COL_BLACK;
    disp_mono_rm.input_win  = COL_BLACK;
    disp_mono_rm.input_bg   = COL_WHITE;
    disp_mono_rm.input_fg   = COL_BLACK;
    disp_mono_rm.error_win  = COL_BLACK;
    disp_mono_rm.error_fg   = COL_WHITE;

    if (config.test)
        {
        disp_mono_rm.bg         = COL_WHITE;
        disp_mono_rm.msg_win    = COL_WHITE;
        disp_mono_rm.msg_fg     = COL_BLACK;
        disp_mono_rm.choice_win = COL_WHITE;
        disp_mono_rm.choice_fg  = COL_BLACK;
        disp_mono_rm.menu_win   = COL_WHITE;
        disp_mono_rm.menu_fg    = COL_BLACK;
        disp_mono_rm.button_bg  = COL_BLACK;
        disp_mono_rm.button_fg  = COL_WHITE;
        disp_mono_rm.input_win  = COL_WHITE;
        disp_mono_rm.input_bg   = COL_BLACK;
        disp_mono_rm.input_fg   = COL_WHITE;
        disp_mono_rm.error_win  = COL_WHITE;
        disp_mono_rm.error_fg   = COL_BLACK;
        }

    if (config.test || config.serial)
        {
        if(!config.had_segv) fprintf (stderr, "ACS activated\n");

        graphics_sg.lrcorner = 106;
        graphics_sg.urcorner = 107;
        graphics_sg.ulcorner = 108;
        graphics_sg.llcorner = 109;
        graphics_sg.ltee     = 116;
        graphics_sg.rtee     = 117;
        graphics_sg.dtee     = 118;
        graphics_sg.utee     = 119;
        graphics_sg.vline    = 120;
        graphics_sg.hline    = 113;
        graphics_sg.cross    = 110;
        graphics_sg.block    =  97;
        }
    else
        {
        graphics_sg.lrcorner = 217;
        graphics_sg.urcorner = 191;
        graphics_sg.ulcorner = 218;
        graphics_sg.llcorner = 192;
        graphics_sg.ltee     = 195;
        graphics_sg.rtee     = 180;
        graphics_sg.dtee     = 194;
        graphics_sg.utee     = 193;
        graphics_sg.vline    = 179;
        graphics_sg.hline    = 196;
        graphics_sg.cross    = 197;
        graphics_sg.block    = 177;
        }

    colors_prg = &disp_vgacolors_rm;

    disp_screen_aprm = malloc (sizeof (character_t *) * max_y_ig);
    for (i_ii = 0; i_ii < max_y_ig; i_ii++)
        disp_screen_aprm [i_ii] = malloc (sizeof (character_t) * max_x_ig);
    }


void disp_end (void)
    {
    int   i_ii;
    FILE *fd_pri;
    char  tty_ti [20];

    if (config.linemode)
      {
	printf("\n\n");
	return;
      }

    disp_set_color (COL_WHITE, COL_BLACK);
    printf ("\033[2J");
    disp_gotoxy (1, 1);
    disp_graph_off ();
    printf ("\033c");
    fflush (stdout);

    for (i_ii = 0; i_ii < max_y_ig; i_ii++)
        free (disp_screen_aprm [i_ii]);

    free (disp_screen_aprm);

    if(!config.test)
        {
        for (i_ii = 2; i_ii <= 6; i_ii++)
            {
            sprintf (tty_ti, "/dev/tty%d", i_ii);
            fd_pri = fopen (tty_ti, "a");
            if (fd_pri)
                {
                fprintf (fd_pri, "\033[2J\033[1;1f");
                fflush (fd_pri);
                fclose (fd_pri);
                }
            }
        }
    }


void disp_gotoxy (int x_iv, int y_iv)
    {
    if (x_iv != disp_x_im || y_iv != disp_y_im)
        {
        if (x_iv > 0 && x_iv <= max_x_ig && y_iv > 0 && y_iv <= max_y_ig)
            {
            printf ("\033[%d;%df", y_iv, x_iv);
            disp_x_im = x_iv;
            disp_y_im = y_iv;
            }
        }
    }


void disp_set_color (char fg_cv, char bg_cv)
    {
    char  attr_ci;

    attr_ci = (disp_attr_cm & 0x80) | (fg_cv << 3) | bg_cv;

    if (attr_ci != disp_attr_cm)
        {
        disp_attr_cm = attr_ci;

        if (IS_BRIGHT (fg_cv))
            attr_ci = ATTR_BRIGHT;
        else
            attr_ci = ATTR_NORMAL;

        if(!config.linemode)
        	printf ("\033[%d;%d;%dm", (int) attr_ci, (int) (fg_cv & 0x07) + 30, (int) bg_cv + 40);
        }
    }


void disp_set_attr (char attr_cv)
    {
    if (attr_cv != disp_attr_cm)
        {
        if (IS_ALTERNATE (attr_cv))
            disp_graph_on ();
        else
            disp_graph_off ();

        disp_set_color (FOREGROUND (attr_cv), BACKGROUND (attr_cv));
        }
    }


void disp_cursor_off (void)
    {
    if (config.linemode)
        return;
    printf ("\033[?25l");
    }


void disp_cursor_on (void)
    {
    if (config.linemode)
      return;
    printf ("\033[?25h");
    }


void disp_graph_on (void)
    {
    if (!IS_ALTERNATE (disp_attr_cm))
        {
        if (config.serial || config.test)
            printf ("%c", 14);
        else
            printf ("\033[11m");
        disp_attr_cm |= 0x80;
        }
    }


void disp_graph_off (void)
    {
    if (IS_ALTERNATE (disp_attr_cm))
        {
        if (config.serial || config.test)
            printf ("%c", 15);
        else
            printf ("\033[10m");
        disp_attr_cm &= 0x7f;
        }
    }


void disp_write_char (char character_cv)
    {
    if (disp_x_im > 0 && disp_x_im <= max_x_ig &&
        disp_y_im > 0 && disp_y_im <= max_y_ig)
        {
        if (disp_state_im == DISP_ON)
            printf ("%c", character_cv);

        disp_screen_aprm [disp_y_im - 1][disp_x_im - 1].attr = disp_attr_cm;
        disp_screen_aprm [disp_y_im - 1][disp_x_im - 1].c = character_cv;
        disp_x_im++;
        }
    }


void disp_write_string (char *string_tv)
    {
    int  i_ii;


    if (disp_x_im > 0 && disp_x_im <= max_x_ig &&
        disp_y_im > 0 && disp_y_im <= max_y_ig)
        {
        if (disp_state_im == DISP_ON)
            printf ("%s", string_tv);

        for (i_ii = 0; (unsigned) i_ii < strlen (string_tv); i_ii++)
            if (i_ii + disp_x_im <= max_x_ig)
                {
                disp_screen_aprm [disp_y_im - 1][i_ii + disp_x_im - 1].attr =
                                 disp_attr_cm;
                disp_screen_aprm [disp_y_im - 1][i_ii + disp_x_im - 1].c =
                                 string_tv [i_ii];
                }

        disp_x_im += strlen (string_tv);
        if (disp_x_im > max_x_ig)
            disp_gotoxy (1, 1);
        }
    }


void disp_toggle_output (int state_iv)
    {
    disp_state_im = state_iv;
    }


void disp_save_area (window_t *win_prr)
    {
    int  i_ii;
    int  save_x_ii;
    int  save_y_ii;


    save_x_ii = win_prr->x_right - win_prr->x_left + 1;
    save_y_ii = win_prr->y_right - win_prr->y_left + 1;
    if (win_prr->shadow)
        {
        save_x_ii += 2;
        save_y_ii++;
        }

    if (save_x_ii < 1 || save_y_ii < 1)
        return;

    if (save_x_ii + win_prr->x_left > max_x_ig)
        save_x_ii = max_x_ig - win_prr->x_left + 1;

    if (save_y_ii + win_prr->y_left > max_y_ig)
        save_y_ii = max_y_ig - win_prr->y_left + 1;

    win_prr->save_area = malloc (sizeof (character_t *) * save_y_ii);
    for (i_ii = 0; i_ii < save_y_ii; i_ii++)
        {
        win_prr->save_area [i_ii] = malloc (sizeof (character_t) * save_x_ii);
        memcpy (win_prr->save_area [i_ii],
                &disp_screen_aprm [win_prr->y_left+i_ii-1][win_prr->x_left-1],
                sizeof (character_t) * save_x_ii);
        }

    }


void disp_restore_area (window_t *win_prr, int mode_iv)
    {
    int  y_ii;
    int  x_ii;
    int  save_x_ii;
    int  save_y_ii;
    int  x_start_ii;
    int  y_start_ii;
    int  x_end_ii;
    int  y_end_ii;
    int  ready_ii;
    char save_attr_ci;


    disp_toggle_output (DISP_ON);
    save_attr_ci = disp_attr_cm;
    save_x_ii = win_prr->x_right - win_prr->x_left + 1;
    save_y_ii = win_prr->y_right - win_prr->y_left + 1;
    if (win_prr->shadow)
        {
        save_x_ii += 2;
        save_y_ii++;
        }

    if (save_x_ii < 1 || save_y_ii < 1)
        return;

    if (save_x_ii + win_prr->x_left > max_x_ig)
        save_x_ii = max_x_ig - win_prr->x_left + 1;

    if (save_y_ii + win_prr->y_left > max_y_ig)
        save_y_ii = max_y_ig - win_prr->y_left + 1;

    if (!config.explode_win || config.serial)
        mode_iv = DISP_RESTORE_NORMAL;

    switch (mode_iv)
        {
        case DISP_RESTORE_EXPLODE:
        case DISP_RESTORE_IMPLODE:
            if (mode_iv == DISP_RESTORE_EXPLODE)
                {
                if (save_x_ii > save_y_ii)
                    {
                    y_start_ii = (save_y_ii - 1) / 2;
                    y_end_ii = save_y_ii / 2;
                    x_start_ii = y_start_ii;
                    x_end_ii = save_x_ii - x_start_ii - 1;
                    }
                else
                    {
                    x_start_ii = (save_x_ii - 1) / 2;
                    x_end_ii = save_x_ii / 2;
                    y_start_ii = x_start_ii;
                    y_end_ii = save_y_ii - y_start_ii - 1;
                    }
                }
            else
                {
                x_start_ii = 0;
                x_end_ii = save_x_ii - 1;
                y_start_ii = 0;
                y_end_ii = save_y_ii - 1;
                }

            ready_ii = FALSE;

            do
                {
                disp_gotoxy (win_prr->x_left + x_start_ii,
                             win_prr->y_left + y_start_ii);
                for (x_ii = x_start_ii; x_ii <= x_end_ii; x_ii++)
                    {
                    disp_set_attr (win_prr->save_area [y_start_ii][x_ii].attr);
                    disp_write_char (win_prr->save_area [y_start_ii][x_ii].c);
                    }

                for (y_ii = y_start_ii; y_ii <= y_end_ii; y_ii++)
                    {
                    disp_gotoxy (win_prr->x_left + x_start_ii,
                                 win_prr->y_left + y_ii);
                    disp_set_attr (win_prr->save_area [y_ii][x_start_ii].attr);
                    disp_write_char (win_prr->save_area [y_ii][x_start_ii].c);

                    disp_gotoxy (win_prr->x_left + x_end_ii,
                                 win_prr->y_left + y_ii);
                    disp_set_attr (win_prr->save_area [y_ii][x_end_ii].attr);
                    disp_write_char (win_prr->save_area [y_ii][x_end_ii].c);
                    }

                if (y_start_ii != y_end_ii)
                    {
                    disp_gotoxy (win_prr->x_left + x_start_ii,
                                 win_prr->y_left + y_end_ii);
                    for (x_ii = x_start_ii; x_ii <= x_end_ii; x_ii++)
                        {
                        disp_set_attr (win_prr->save_area [y_end_ii][x_ii].attr);
                        disp_write_char (win_prr->save_area [y_end_ii][x_ii].c);
                        }
                    }

                if (mode_iv == DISP_RESTORE_EXPLODE)
                    {
                    x_start_ii--;
                    y_start_ii--;
                    x_end_ii++;
                    y_end_ii++;
                    if (x_start_ii < 0)
                        ready_ii = TRUE;
                    }
                else
                    {
                    x_start_ii++;
                    y_start_ii++;
                    x_end_ii--;
                    y_end_ii--;
                    if (x_end_ii - x_start_ii < 0 || y_end_ii - y_start_ii < 0)
                        ready_ii = TRUE;
                    }

                fflush (stdout);

                if (x_start_ii % 2)
                    usleep (10000);
                }
            while (!ready_ii);
            break;

        default:
            for (y_ii = 0; y_ii < save_y_ii; y_ii++)
                {
                disp_gotoxy (win_prr->x_left, win_prr->y_left + y_ii);
                for (x_ii = 0; x_ii < save_x_ii; x_ii++)
                    {
                    disp_set_attr (win_prr->save_area [y_ii][x_ii].attr);
                    disp_write_char (win_prr->save_area [y_ii][x_ii].c);
                    }
                }
            fflush (stdout);
            break;
        }


    for (y_ii = 0; y_ii < save_y_ii; y_ii++) free (win_prr->save_area [y_ii]);
    free (win_prr->save_area);
    disp_set_attr (save_attr_ci);
    }


void disp_flush_area (window_t *win_prr)
    {
    window_t tmp_win_ri;

    memcpy (&tmp_win_ri, win_prr, sizeof (window_t));
    disp_save_area (&tmp_win_ri);
    disp_restore_area (&tmp_win_ri, DISP_RESTORE_EXPLODE);
    }


void disp_refresh_char (int x_iv, int y_iv)
    {
    if (x_iv > 0 && x_iv <= max_x_ig &&
        y_iv > 0 && y_iv <= max_y_ig)
        {
        disp_gotoxy (x_iv, y_iv);
        if (IS_ALTERNATE (disp_screen_aprm [y_iv - 1][x_iv - 1].attr))
            disp_graph_on ();
        else
            disp_graph_off ();
        disp_write_char (disp_screen_aprm [y_iv - 1][x_iv - 1].c);
        }
    }


void disp_set_display()
{
  switch(config.color) {
    case 1:
      colors_prg = &disp_mono_rm;
      break;
    case 3:
      colors_prg = &disp_alternate_rm;
      break;
    default:
      colors_prg = &disp_vgacolors_rm;
      config.color = 2;
      break;
  }
}


void disp_restore_screen (void)
    {
    int  y_ii;
    int  x_ii;


    disp_x_im = 0;
    for (y_ii = 0; y_ii < max_y_ig; y_ii++)
        {
        disp_gotoxy (1, y_ii + 1);
        for (x_ii = 0; x_ii < max_x_ig; x_ii++)
            {
            disp_set_attr (disp_screen_aprm [y_ii][x_ii].attr);
            disp_write_char (disp_screen_aprm [y_ii][x_ii].c);
            }
        }

    fflush (stdout);
    }


void disp_clear_screen (void)
    {
    printf ("\033[H\033[J");
    }
