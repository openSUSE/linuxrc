/*
 *
 * display.h     Header file for display.c
 *
 * Copyright (c) 1996-2000  Hubert Mantel, SuSE GmbH  (mantel@suse.de)
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

/* #define LXRC_TEST */
#undef LXRC_TEST

#ifdef LXRC_TEST
#define GRAPH_LRCORNER   106
#define GRAPH_URCORNER   107
#define GRAPH_ULCORNER   108
#define GRAPH_LLCORNER   109
#define GRAPH_LTEE       116
#define GRAPH_RTEE       117
#define GRAPH_DTEE       118
#define GRAPH_UTEE       119
#define GRAPH_VLINE      120
#define GRAPH_HLINE      113
#define GRAPH_CROSS      110
#define GRAPH_BLOCK       97
#else
#define GRAPH_LRCORNER   217
#define GRAPH_URCORNER   191
#define GRAPH_ULCORNER   218
#define GRAPH_LLCORNER   192
#define GRAPH_LTEE       195
#define GRAPH_RTEE       180
#define GRAPH_DTEE       194
#define GRAPH_UTEE       193
#define GRAPH_VLINE      179
#define GRAPH_HLINE      196
#define GRAPH_CROSS      197
#define GRAPH_BLOCK      177
#endif

#define DISP_OFF           0
#define DISP_ON            1

#define DISP_RESTORE_NORMAL     0
#define DISP_RESTORE_EXPLODE    1
#define DISP_RESTORE_IMPLODE    2

extern void disp_init           (void);
extern void disp_end            (void);
extern void disp_gotoxy         (int x_iv, int y_iv);
extern void disp_set_color      (char fg_cv, char bg_cv);
extern void disp_set_attr       (char attr_cv);
extern void disp_cursor_on      (void);
extern void disp_cursor_off     (void);
extern void disp_graph_on       (void);
extern void disp_graph_off      (void);
extern void disp_write_char     (char character_cv);
extern void disp_write_string   (char *string_tv);
extern void disp_toggle_output  (int state_iv);
extern void disp_save_area      (window_t *win_prr);
extern void disp_restore_area   (window_t *win_prr, int mode_iv);
extern void disp_flush_area     (window_t *win_prr);
extern void disp_refresh_char   (int x_iv, int y_iv);
extern void disp_set_display    (int type_iv);
extern void disp_restore_screen (void);
