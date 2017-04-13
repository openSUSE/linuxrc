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
#include "util.h"
#include "utf8.h"

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


    if(config.utf8) {
      graphics_sg.lrcorner = 0x2518;
      graphics_sg.urcorner = 0x2510;
      graphics_sg.ulcorner = 0x250C;
      graphics_sg.llcorner = 0x2514;
      graphics_sg.ltee     = 0x251C;
      graphics_sg.rtee     = 0x2524;
      graphics_sg.dtee     = 0x2534;
      graphics_sg.utee     = 0x252C;
      graphics_sg.vline    = 0x2502;
      graphics_sg.hline    = 0x2500;
      graphics_sg.cross    = 0x253C;
      graphics_sg.block    = 0x2592;
    }
    else {
      graphics_sg.lrcorner = 0x6a;
      graphics_sg.urcorner = 0x6b;
      graphics_sg.ulcorner = 0x6c;
      graphics_sg.llcorner = 0x6d;
      graphics_sg.ltee     = 0x74;
      graphics_sg.rtee     = 0x75;
      graphics_sg.dtee     = 0x76;
      graphics_sg.utee     = 0x77;
      graphics_sg.vline    = 0x78;
      graphics_sg.hline    = 0x71;
      graphics_sg.cross    = 0x6e;
      graphics_sg.block    = 0x61;
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

    if (disp_screen_aprm)
      {
        for (i_ii = 0; i_ii < max_y_ig; i_ii++)
            free (disp_screen_aprm [i_ii]);

        free (disp_screen_aprm);
      }

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


void disp_gotoxy(int x, int y)
{
  if(x == disp_x_im && y == disp_y_im) return;

//  log_info("gotoxy %d x %d\n", x, y);

  if(
    x > 0 && x <= max_x_ig &&
    y > 0 && y <= max_y_ig
  ) {
    printf("\033[%d;%df", y, x);
    disp_x_im = x;
    disp_y_im = y;
  }
}


void disp_set_color(char fg, char bg)
{
  char attr;

  attr = (disp_attr_cm & 0x80) | (fg << 3) | bg;

  if(attr != disp_attr_cm) {
    disp_attr_cm = attr;

    if(IS_BRIGHT(fg)) {
      attr = ATTR_BRIGHT;
    }
    else {
      attr = ATTR_NORMAL;
    }

    if(!config.linemode) {
      printf("\033[%d;%d;%dm", (int) attr, (int) (fg & 0x07) + 30, (int) bg + 40);
    }
  }
}


void disp_set_attr(char attr)
{
  if(attr != disp_attr_cm) {
    if(IS_ALTERNATE(attr)) {
      disp_graph_on();
    }
    else {
      disp_graph_off();
    }

    disp_set_color(FOREGROUND(attr), BACKGROUND(attr));
  }
}


void disp_cursor_off()
{
  if(config.linemode) return;

  printf("\033[?25l");
}


void disp_cursor_on()
{
  if(config.linemode) return;

  printf("\033[?25h");
}


void disp_graph_on()
{
  if(config.utf8) return;

  if(!IS_ALTERNATE(disp_attr_cm)) {
    if(config.serial || config.test) {
      printf("%c", 14);
    }
    else {
      printf("\033[11m");
    }
    disp_attr_cm |= 0x80;
  }
}


void disp_graph_off()
{
  if(config.utf8) return;

  if(IS_ALTERNATE(disp_attr_cm)) {
    if(config.serial || config.test) {
      printf("%c", 15);
    }
    else {
      printf("\033[10m");
    }
    disp_attr_cm &= 0x7f;
  }
}


void disp_toggle_output(int state)
{
  disp_state_im = state;
}


/*
 * Save window area.
 *
 * You can restore the window area later by calling disp_restore_area().
 *
 * Note: it is a programming error to call disp_save_area() two times in a
 * row without disp_restore_area() in between.
 */
void disp_save_area(window_t *win)
{
  int i, x_len, y_len;

  x_len = win->x_right - win->x_left + 1;
  y_len = win->y_right - win->y_left + 1;

  if(win->shadow) {
    x_len += 2;
    y_len++;
  }

  if(x_len < 1 || y_len < 1) return;

  if(x_len + win->x_left > max_x_ig) x_len = max_x_ig - win->x_left + 1;

  if(y_len + win->y_left > max_y_ig) y_len = max_y_ig - win->y_left + 1;

  log_debug("save area %p at %d x %d (size %d x %d)\n", win, win->x_left, win->y_left, x_len, y_len);

//  dump_screen("save area, start");

  if(win->save_area) {
    log_info("Warning: save area already in use!");
  }

  win->save_area = malloc(sizeof (character_t *) * y_len);

  for(i = 0; i < y_len; i++) {
    win->save_area[i] = malloc(sizeof (character_t) * x_len);
    memcpy(
      win->save_area[i],
      &disp_screen_aprm[win->y_left + i - 1][win->x_left - 1],
      sizeof (character_t) * x_len
    );
  }

//  dump_screen("save area, end");
}


/*
 * Restore saved window area.
 *
 * Note: it is actually allowed to call this function without a prior
 * disp_save_area(). It will do nothing in this case.
 *
 */
void disp_restore_area(window_t *win)
{
  int x, x_len;
  int y, y_len;
  char save_attr;

  disp_toggle_output(DISP_ON);
  save_attr = disp_attr_cm;

  x_len = win->x_right - win->x_left + 1;
  y_len = win->y_right - win->y_left + 1;

  if(win->shadow) {
    x_len += 2;
    y_len++;
  }

  if(x_len < 1 || y_len < 1) return;

  if(x_len + win->x_left > max_x_ig) x_len = max_x_ig - win->x_left + 1;
  if(y_len + win->y_left > max_y_ig) y_len = max_y_ig - win->y_left + 1;

  log_debug("restore area %p at %d x %d (size %d x %d)\n", win, win->x_left, win->y_left, x_len, y_len);

  if(!win->save_area) {
    log_debug("Warning: restore area not initialized!");
    return;
  }

  for(y = 0; y < y_len; y++) {
    disp_gotoxy(win->x_left, win->y_left + y);
    for(x = 0; x < x_len;) {
      disp_set_attr(win->save_area[y][x].attr);
      x += disp_write_char(win->save_area[y][x].c);
    }
  }
  fflush(stdout);

  for(y = 0; y < y_len; y++) free(win->save_area[y]);
  free(win->save_area);

  win->save_area = NULL;

  disp_set_attr(save_attr);
}


void disp_flush_area(window_t *win)
{
  window_t tmp_win;

  tmp_win = *win;
  /* we must not copy .save_area as it's modified by disp_{save,restore}_area */
  tmp_win.save_area = NULL;

  disp_save_area(&tmp_win);
  disp_restore_area(&tmp_win);
}


void disp_refresh_char(int x, int y)
{
  if(
    x > 0 && x <= max_x_ig &&
    y > 0 && y <= max_y_ig
  ) {
    disp_gotoxy(x, y);

    if(IS_ALTERNATE(disp_screen_aprm[y - 1][x - 1].attr)) {
      disp_graph_on();
    }
    else {
      disp_graph_off();
    }

    disp_write_char(disp_screen_aprm[y - 1][x - 1].c);
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

  if((config.serial || config.test) && !config.linemode) {
    printf("\033)0");
  }
}


void disp_restore_screen()
{
  int x, y;

  log_info("restore screen\n");

  disp_x_im = 0;

  for(y = 0; y < max_y_ig; y++) {
    disp_gotoxy(1, y + 1);
    for(x = 0; x < max_x_ig; x++) {
      disp_set_attr(disp_screen_aprm[y][x].attr);
      disp_write_char(disp_screen_aprm[y][x].c);
    }
  }

  fflush(stdout);
}


void disp_clear_screen()
{
  printf("\033[H\033[J");
}


/*
 * Write utf32 string.
 */
void disp_write_utf32string(int *str)
{
  int i, j, len, buf_len, width;
  unsigned char *buf;

  if(
    disp_x_im > 0 &&
    disp_x_im <= max_x_ig &&
    disp_y_im > 0 &&
    disp_y_im <= max_y_ig
  ) {
    len = utf32_len(str);

    if(disp_state_im == DISP_ON) {
      buf = malloc(buf_len = len * 6 + 1);
      utf32_to_utf8(buf, buf_len, str);
      printf("%s", buf);
      free(buf);
    }

    for(i = 0; i < len; i++) {

      width = 1;

      width = utf32_char_width(str[i]);
      if(!width) width = 1;

      if(disp_x_im <= max_x_ig) {
        disp_screen_aprm[disp_y_im - 1][disp_x_im - 1].attr = disp_attr_cm;
        disp_screen_aprm[disp_y_im - 1][disp_x_im - 1].c = str[i];
        for(j = 1; j < width; j++) {
          disp_screen_aprm[disp_y_im - 1][disp_x_im - 1 + j].attr = disp_attr_cm;
          disp_screen_aprm[disp_y_im - 1][disp_x_im - 1 + j].c = 0;
        }
      }

      disp_x_im += width;
    }

    if(disp_x_im > max_x_ig) disp_gotoxy(1, 1);
  }
}


/*
 * Write utf8 string.
 */
void disp_write_string(char *str)
{
  int len, *buf;

//  log_info("[* <%s>", str);
//  getchar();

  len = strlen(str) + 1;

  buf = malloc(len * sizeof *buf);
  utf8_to_utf32(buf, len, str);
  disp_write_utf32string(buf);

//  log_info("#]\n");

  free(buf);
}


/*
 * Write utf32 char. Returns char width.
 */
int disp_write_char(int c)
{
  int i, width = 1;

  if(
    disp_x_im > 0 &&
    disp_x_im <= max_x_ig &&
    disp_y_im > 0 &&
    disp_y_im <= max_y_ig
  ) {
    if(disp_state_im == DISP_ON && c) printf("%s", utf8_encode(c));

    width = utf32_char_width(c);
    if(!width) width = 1;

    disp_screen_aprm[disp_y_im - 1][disp_x_im - 1].attr = disp_attr_cm;
    disp_screen_aprm[disp_y_im - 1][disp_x_im - 1].c = c;

    for(i = 1; i < width; i++) {
      disp_screen_aprm[disp_y_im - 1][disp_x_im - 1 + i].attr = disp_attr_cm;
      disp_screen_aprm[disp_y_im - 1][disp_x_im - 1 + i].c = 0;
    }

    disp_x_im += width;
  }

  return width;
}
