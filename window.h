/*
 *
 * window.h      Window definition and handling
 *
 * Copyright (c) 1996-2002  Hubert Mantel, SuSE Linux AG  (mantel@suse.de)
 *
 */

#define STYLE_NORMAL   0
#define STYLE_SUNKEN   1
#define STYLE_RAISED   2

#define BUTTON_TIME    150000

extern void win_open             (window_t *win_prr);
extern void win_close            (window_t *win_prv);
extern void win_clear            (window_t *win_prv);
extern void win_print            (window_t *win_prv, int x_iv, int y_iv,
                                  char *text_tv);
extern void win_add_button       (window_t *win_prv, button_t *button_prr,
                                  int pos_iv,        int size_iv);
extern void win_button_pressed   (button_t *button_prr, int stay_ii);
extern void win_button_unpressed (button_t *button_prr);
extern void win_button_select    (button_t *button_prv);
extern void win_button_unselect  (button_t *button_prv);
extern int  win_choose_button    (button_t *buttons_arr [], int nr_buttons_iv,
                                  int default_iv);
extern int  win_input            (int x_iv,   int y_iv, char *input_tr,
                                  int len_iv, int fieldlen_iv, int pw_mode);
