/*
 *
 * keyboard.h    Header file for keyboard.c
 *
 * Copyright (c) 1996-2002  Hubert Mantel, SuSE Linux AG  (mantel@suse.de)
 *
 */

#define KEY_SPECIAL 0x0400
#define KEY_FUNC    0x0800

#define KEY_ESC         27
#define KEY_ENTER       10
#define KEY_TAB          9
#define KEY_BTAB         8
#define KEY_BACKSPACE  127

#define KEY_CTRL_A       1
#define KEY_CTRL_B       2
#define KEY_CTRL_C       3
#define KEY_CTRL_D       4
#define KEY_CTRL_E       5
#define KEY_CTRL_F       6
#define KEY_CTRL_G       7
#define KEY_CTRL_H       8
#define KEY_CTRL_I       9
#define KEY_CTRL_J      10
#define KEY_CTRL_K      11
#define KEY_CTRL_L      12
#define KEY_CTRL_M      13
#define KEY_CTRL_N      14
#define KEY_CTRL_O      15
#define KEY_CTRL_P      16
#define KEY_CTRL_Q      17
#define KEY_CTRL_R      18
#define KEY_CTRL_S      19
#define KEY_CTRL_T      20
#define KEY_CTRL_U      21
#define KEY_CTRL_V      22
#define KEY_CTRL_W      23
#define KEY_CTRL_X      24
#define KEY_CTRL_Y      25
#define KEY_CTRL_Z      26

#define KEY_UP        (65 | KEY_SPECIAL)
#define KEY_DOWN      (66 | KEY_SPECIAL)
#define KEY_RIGHT     (67 | KEY_SPECIAL)
#define KEY_LEFT      (68 | KEY_SPECIAL)
#define KEY_HOME      (49 | KEY_SPECIAL)
#define KEY_END       (52 | KEY_SPECIAL)
#define KEY_PGUP      (53 | KEY_SPECIAL)
#define KEY_PGDOWN    (54 | KEY_SPECIAL)
#define KEY_INSERT    (50 | KEY_SPECIAL)
#define KEY_DEL       (51 | KEY_SPECIAL)
#define KEY_F1        (49 | KEY_SPECIAL | KEY_FUNC)
#define KEY_F2        (50 | KEY_SPECIAL | KEY_FUNC)
#define KEY_F3        (51 | KEY_SPECIAL | KEY_FUNC)
#define KEY_F4        (52 | KEY_SPECIAL | KEY_FUNC)

extern void  kbd_init         (void);
extern void  kbd_reset        (void);
extern void  kbd_end          (void);
extern int   kbd_getch        (int wait_iv);
extern void  kbd_clear_buffer (void);
extern void  kbd_switch_tty   (int tty_iv);
