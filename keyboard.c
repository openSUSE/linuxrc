/*
 *
 * keyboard.c    Keyboard handling
 *
 * Copyright (c) 1996-1999  Hubert Mantel, SuSE GmbH  (mantel@suse.de)
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <termios.h>
#include <signal.h>
#include <string.h>
#include <linux/vt.h>

#include "global.h"
#include "keyboard.h"

/*
 *
 * data on module level
 *
 */

#define KBD_ESC_DELAY     25000
#define KBD_TIMEOUT       10000

static struct termios   kbd_norm_tio_rm;
static struct termios   kbd_tio_rm;
static int              kbd_tty_im;
static int              kbd_timeout_im;

/*
 *
 * local function prototypes
 *
 */

static void kbd_del_timeout (void);
static void kbd_set_timeout (long timeout_lv);
static void kbd_timeout     (int signal_iv);

/*
 *
 * exported functions
 *
 */

void kbd_init (void)
    {
    struct winsize  winsize_ri;
    char           *start_msg_pci = "Startup...\n";


    kbd_tty_im = open ("/dev/tty4", O_RDWR);
    write (kbd_tty_im, start_msg_pci, strlen (start_msg_pci));
    close (kbd_tty_im);

    kbd_tty_im = open (console_tg, O_RDWR);

    tcgetattr (kbd_tty_im, &kbd_norm_tio_rm);
    kbd_tio_rm = kbd_norm_tio_rm;
    kbd_tio_rm.c_cc [VMIN] = 1;
    kbd_tio_rm.c_cc [VTIME] = 0;
    kbd_tio_rm.c_lflag &= ~(ECHO | ICANON | ISIG);
    if (!ioctl (kbd_tty_im, TIOCGWINSZ, &winsize_ri))
        {
        if (winsize_ri.ws_col && winsize_ri.ws_row)
            {
            max_x_ig = winsize_ri.ws_col;
            max_y_ig = winsize_ri.ws_row;
            }

        fprintf (stderr, "Winsize (%d, %d)!\n", max_y_ig, max_x_ig);
        }

    tcsetattr (kbd_tty_im, TCSAFLUSH, &kbd_tio_rm);
    }


void kbd_reset (void)
    {
    tcsetattr (kbd_tty_im, TCSAFLUSH, &kbd_tio_rm);
    }

void kbd_end (void)
    {
    tcsetattr (kbd_tty_im, TCSAFLUSH, &kbd_norm_tio_rm);
    close (kbd_tty_im);
    }


void kbd_switch_tty (int  tty_iv)
    {
    ioctl (kbd_tty_im, VT_ACTIVATE, tty_iv);
    ioctl (kbd_tty_im, VT_WAITACTIVE, tty_iv);
    }


int kbd_getch (int wait_iv)
    {
    char  keypress_ci;
    char  tmp_ci;
    int   i_ii;


    keypress_ci = 0;
    do
        {
        kbd_set_timeout (KBD_TIMEOUT);
        read (kbd_tty_im, &keypress_ci, 1);
        kbd_del_timeout ();
        }
    while (!keypress_ci && wait_iv);

    if (!keypress_ci)
        return (0);

    if (keypress_ci != KEY_ESC)
        return ((int) keypress_ci);

    kbd_set_timeout (KBD_ESC_DELAY);

    read (kbd_tty_im, &keypress_ci, 1);
    if (kbd_timeout_im)
        return ((int) KEY_ESC);
    else
        kbd_del_timeout ();

    if (keypress_ci != 91)
        return (0);

    read (kbd_tty_im, &keypress_ci, 1);
    if (keypress_ci == (KEY_UP    & 0xff)  ||
        keypress_ci == (KEY_DOWN  & 0xff)  ||
        keypress_ci == (KEY_RIGHT & 0xff)  ||
        keypress_ci == (KEY_LEFT  & 0xff))
        return (((int) keypress_ci) | KEY_SPECIAL);
    
    i_ii = 0;
    kbd_set_timeout (KBD_ESC_DELAY);

    do
        {
        tmp_ci = keypress_ci;
        read (kbd_tty_im, &keypress_ci, 1);
        i_ii++;
        }
    while (!kbd_timeout_im && keypress_ci != 126);

    kbd_del_timeout ();

    if (keypress_ci != 126)
        return (0);

    if (i_ii == 2)
        return (((int) tmp_ci) | KEY_SPECIAL | KEY_FUNC);
    else
        return (((int) tmp_ci) | KEY_SPECIAL);
    }


void kbd_clear_buffer (void)
    {
    while (kbd_getch (FALSE));
    }


/*
 *
 *  local functions
 *
 */

static void kbd_del_timeout (void)
    {
    struct itimerval timer_ri;

    kbd_timeout_im = FALSE;
    memset (&timer_ri, 0, sizeof (timer_ri));
    setitimer (ITIMER_REAL, &timer_ri, 0);
    signal (SIGALRM, SIG_IGN);
    }


static void kbd_set_timeout (long timeout_lv)
    {
    struct itimerval timer_ri;

    kbd_timeout_im = FALSE;

    memset (&timer_ri, 0, sizeof (timer_ri));
    signal (SIGALRM, kbd_timeout);
    timer_ri.it_value.tv_usec = timeout_lv;
    setitimer (ITIMER_REAL, &timer_ri, 0);
    }


static void kbd_timeout (int signal_iv)
    {
    kbd_timeout_im = TRUE;
    }
