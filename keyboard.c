/*
 *
 * keyboard.c    Keyboard handling
 *
 * Copyright (c) 1996-2002  Hubert Mantel, SuSE Linux AG  (mantel@suse.de)
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
#include <errno.h>

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

static int kbd_getch_raw    (int wait_iv);

static void get_screen_size(int fd);


/*
 *
 * exported functions
 *
 */

void kbd_init (int first)
    {
    struct winsize  winsize_ri;
    char           *start_msg_pci = "Startup...\n";

    if(!config.test) {
      kbd_tty_im = open ("/dev/tty4", O_RDWR);
      write (kbd_tty_im, start_msg_pci, strlen (start_msg_pci));
      close (kbd_tty_im);
    }

    kbd_tty_im = open (config.console, O_RDWR);

    tcgetattr (kbd_tty_im, &kbd_norm_tio_rm);
    kbd_tio_rm = kbd_norm_tio_rm;
    if (config.linemode)
        {
	kbd_tio_rm.c_lflag &= ~ISIG;
	tcsetattr (kbd_tty_im, TCSAFLUSH, &kbd_tio_rm);
	if (first)
	    {
	    max_x_ig = 80;
	    max_y_ig = 24;
	    }
	return;
	}
    kbd_tio_rm.c_cc [VMIN] = 1;
    kbd_tio_rm.c_cc [VTIME] = 0;
    kbd_tio_rm.c_lflag &= ~(ECHO | ICANON | ISIG);
    kbd_tio_rm.c_iflag &= ~(INLCR | IGNCR | ICRNL);
    if (first && !ioctl (kbd_tty_im, TIOCGWINSZ, &winsize_ri))
        {
        if (winsize_ri.ws_col && winsize_ri.ws_row)
            {
            max_x_ig = winsize_ri.ws_col;
            max_y_ig = winsize_ri.ws_row;
            }

        }

    tcsetattr (kbd_tty_im, TCSAFLUSH, &kbd_tio_rm);

    write(kbd_tty_im, "\033[?1l", sizeof "\033[?1l" - 1);
    fsync(kbd_tty_im);

    if(first) {
      get_screen_size(kbd_tty_im);

      if(max_x_ig > MAX_X) max_x_ig = MAX_X;
      if(max_y_ig > MAX_Y) max_y_ig = MAX_Y;

      if(!config.had_segv) fprintf(stderr, "Window size: %d x %d\n", max_x_ig, max_y_ig);

      memset(&winsize_ri, 0, sizeof winsize_ri);

      winsize_ri.ws_col = max_x_ig;
      winsize_ri.ws_row = max_y_ig;

      ioctl(kbd_tty_im, TIOCSWINSZ, &winsize_ri);
    }

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


/*
 * Do some tricks to work around broken terminals that send CR + LF.
 */
int kbd_getch(int wait_iv)
{
  int key;
  static int last_was_nl = 0;

  do {
    key = kbd_getch_raw(wait_iv);
//    fprintf(stderr, "rawkey(%d): %d\n", wait_iv, key);

    if(key == KEY_ENTER || key == KEY_CTRL_M) {
      if(!last_was_nl) {
        last_was_nl = key;
        break;
      }
      if(last_was_nl && key != last_was_nl) {
        last_was_nl = key = 0;
      }
    }
    else {
      if(key) last_was_nl = 0;
    }
  }
  while(!key && wait_iv);

  if(key == KEY_CTRL_M) key = KEY_ENTER;

//  fprintf(stderr, "key(%d): %d\n", wait_iv, key);

  return key;
}


int kbd_getch_raw (int wait_iv)
    {
    char  keypress_ci;
    char  tmp_ci;
    int   i_ii;
    int   time_to_wait;
    int   esc_delay = KBD_ESC_DELAY;

    if(config.escdelay) esc_delay = config.escdelay * 1000;

    keypress_ci = 0;

    time_to_wait = config.kbdtimeout * 1000000;

    do
        {
        kbd_set_timeout (KBD_TIMEOUT);
        read (kbd_tty_im, &keypress_ci, 1);
        kbd_del_timeout ();
        if(wait_iv && config.kbdtimeout && (time_to_wait -= KBD_TIMEOUT) <= 0)
            {
            return KEY_ENTER;
            }
        }
    while (!keypress_ci && wait_iv);

    if (!keypress_ci)
        return (0);

    config.kbdtimeout = 0;

    if (keypress_ci != KEY_ESC)
        return ((int) keypress_ci);

    kbd_set_timeout (esc_delay);

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
    kbd_set_timeout (esc_delay);

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

void kbd_echo_off (void)
    {
	struct termios tios;
	tios = kbd_tio_rm;
	tios.c_lflag &= ~ECHO;
	tcsetattr (kbd_tty_im, TCSAFLUSH, &tios);
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


void get_screen_size(int fd)
{
  static const char *term_init = "\0337\033[r\033[999;999H\033[6n\0338";
  char buf[64];
  int i, buf_len;
  struct timeval to;
  fd_set set;
  unsigned u1, u2;

  write(fd, term_init, strlen(term_init));
  fsync(fd);

  buf[buf_len = 0];

  FD_ZERO(&set);
  FD_SET(fd, &set);
  to.tv_sec = 0; to.tv_usec = 500000;

  while(select(fd + 1, &set, NULL, NULL, &to) > 0) {
    if((i = read(fd, buf + buf_len, sizeof buf - 1 - buf_len)) < 0) break;
    buf_len += i;
    buf[buf_len] = 0;
    to.tv_sec = 0; to.tv_usec = 500000;
    if(strchr(buf, 'R')) break;
  }

  if(sscanf(buf, "\033[%u;%uR", &u1, &u2) == 2) {
    max_x_ig = u2;
    max_y_ig = u1;
  }
  else if(*buf) {
    fprintf(stderr, "Unexpected response: >>%s<<\n", buf + 1);
  }
}

