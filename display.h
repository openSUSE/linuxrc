/*
 *
 * display.h     Header file for display.c
 *
 * Copyright (c) 1996-2002  Hubert Mantel, SuSE Linux AG  (mantel@suse.de)
 *
 */

#define COL_BLACK          0
#define COL_RED            1
#define COL_GREEN          2
#define COL_YELLOW         3
#define COL_BLUE           4
#define COL_MAGENTA        5
#define COL_CYAN           6
#define COL_WHITE          7

#define COL_BBLACK         8
#define COL_BRED           9
#define COL_BGREEN        10
#define COL_BYELLOW       11
#define COL_BBLUE         12
#define COL_BMAGENTA      13
#define COL_BCYAN         14
#define COL_BWHITE        15

#define ATTR_NORMAL        0
#define ATTR_BRIGHT        1

#define GRAPH_LRCORNER   graphics_sg.lrcorner
#define GRAPH_URCORNER   graphics_sg.urcorner
#define GRAPH_ULCORNER   graphics_sg.ulcorner
#define GRAPH_LLCORNER   graphics_sg.llcorner
#define GRAPH_LTEE       graphics_sg.ltee
#define GRAPH_RTEE       graphics_sg.rtee
#define GRAPH_DTEE       graphics_sg.dtee
#define GRAPH_UTEE       graphics_sg.utee
#define GRAPH_VLINE      graphics_sg.vline
#define GRAPH_HLINE      graphics_sg.hline
#define GRAPH_CROSS      graphics_sg.cross
#define GRAPH_BLOCK      graphics_sg.block

#define DISP_OFF           0
#define DISP_ON            1

#define DISP_RESTORE_NORMAL     0
#define DISP_RESTORE_EXPLODE    1
#define DISP_RESTORE_IMPLODE    2

typedef struct {
  int
    lrcorner,
    urcorner,
    ulcorner,
    llcorner,
    ltee,
    rtee,
    dtee,
    utee,
    vline,
    hline,
    cross,
    block;
} graphics_t;

extern graphics_t graphics_sg;
extern colorset_t  disp_vgacolors_rm;

extern void disp_init           (void);
extern void disp_end            (void);
extern void disp_gotoxy         (int x_iv, int y_iv);
extern void disp_set_color      (char fg_cv, char bg_cv);
extern void disp_set_attr       (char attr_cv);
extern void disp_cursor_on      (void);
extern void disp_cursor_off     (void);
extern void disp_graph_on       (void);
extern void disp_graph_off      (void);
extern void disp_toggle_output  (int state_iv);
extern void disp_save_area      (window_t *win_prr);
extern void disp_restore_area   (window_t *win_prr, int mode_iv);
extern void disp_flush_area     (window_t *win_prr);
extern void disp_refresh_char   (int x_iv, int y_iv);
extern void disp_set_display    (void);
extern void disp_restore_screen (void);
extern void disp_clear_screen   (void);

int disp_write_char(int c);
void disp_write_string(char *str);
void disp_write_utf32string(int *str);
