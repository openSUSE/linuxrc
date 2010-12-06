/*
 *
 * keyboard.h    Header file for keyboard.c
 *
 * Copyright (c) 1996-2008  Hubert Mantel, SuSE Linux AG  (mantel@suse.de)
 *
 */

#define KEY_SPECIAL (1 << 30)

#define KEY_NONE         0

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

#define KEY_UP        ( 1 | KEY_SPECIAL)
#define KEY_DOWN      ( 2 | KEY_SPECIAL)
#define KEY_RIGHT     ( 3 | KEY_SPECIAL)
#define KEY_LEFT      ( 4 | KEY_SPECIAL)
#define KEY_HOME      ( 5 | KEY_SPECIAL)
#define KEY_END       ( 6 | KEY_SPECIAL)
#define KEY_PGUP      ( 7 | KEY_SPECIAL)
#define KEY_PGDOWN    ( 8 | KEY_SPECIAL)
#define KEY_INSERT    ( 9 | KEY_SPECIAL)
#define KEY_DEL       (10 | KEY_SPECIAL)
#define KEY_F1        (11 | KEY_SPECIAL)
#define KEY_F2        (12 | KEY_SPECIAL)
#define KEY_F3        (13 | KEY_SPECIAL)
#define KEY_F4        (14 | KEY_SPECIAL)

extern void  kbd_init         (int first);
extern void  kbd_reset        (void);
extern void  kbd_end          (int close_fd);
extern int   kbd_getch        (int wait_iv);
extern void  kbd_clear_buffer (void);
extern void  kbd_switch_tty   (int tty_iv);
extern void  kbd_echo_off     (void);
extern int   kbd_getch_old    (int);
void kbd_unimode(void);
extern int kbd_getch_raw(int do_wait);
