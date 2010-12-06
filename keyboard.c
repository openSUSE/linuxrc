/*
 *
 * keyboard.c    Keyboard handling
 *
 * Copyright (c) 1996-2008  Hubert Mantel, SuSE Linux AG  (mantel@suse.de)
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
#include <linux/kd.h>
#include <errno.h>

#include "global.h"
#include "keyboard.h"
#include "utf8.h"

/*
 *
 * data on module level
 *
 */

#define KBD_TIMEOUT       10000

static struct termios   kbd_norm_tio_rm;
static struct termios   kbd_tio_rm;
static int              kbd_timeout_im;

typedef struct {
  unsigned char buffer[16];
  unsigned pos;
  int key;
} kbd_buffer_t;

static kbd_buffer_t kbd;

struct {
  unsigned char *bytes;
  int key;
} key_list[] = {
  { "\x1b[[A", KEY_F1     },
  { "\x1b[[B", KEY_F2     },
  { "\x1b[[C", KEY_F3     },
  { "\x1b[[D", KEY_F4     },
  { "\x1b[1~", KEY_HOME   },
  { "\x1b[2~", KEY_INSERT },
  { "\x1b[3~", KEY_DEL    },
  { "\x1b[4~", KEY_END    },
  { "\x1b[5~", KEY_PGUP   },
  { "\x1b[6~", KEY_PGDOWN },
  { "\x1b[A" , KEY_UP     },
  { "\x1b[B" , KEY_DOWN   },
  { "\x1b[D" , KEY_LEFT   },
  { "\x1b[C" , KEY_RIGHT  },
  { "\x1b[H" , KEY_HOME   },
  { "\x1b[F" , KEY_END    },
  { "\x1bOP" , KEY_F1     },
  { "\x1bOQ" , KEY_F2     },
  { "\x1bOR" , KEY_F3     },
  { "\x1bOS" , KEY_F4     },
};


/*
 *
 * local function prototypes
 *
 */

static void kbd_del_timeout (void);
static void kbd_set_timeout (long timeout_lv);
static void kbd_timeout     (int signal_iv);

static void get_screen_size(int fd);


/*
 *
 * exported functions
 *
 */

void kbd_init (int first)
    {
    struct winsize winsize_ri;
    int fd;

    if(!config.test) {
      fd = open("/dev/tty4", O_RDWR);
      write(fd, "Startup...\n", sizeof "Startup...\n" - 1);
      close(fd);
    }

    kbd_unimode();

    if(config.kbd_fd == -1) config.kbd_fd = open(config.console, O_RDWR);
    tcgetattr (config.kbd_fd, &kbd_norm_tio_rm);
    kbd_tio_rm = kbd_norm_tio_rm;
    if (config.linemode)
        {
	kbd_tio_rm.c_lflag &= ~ISIG;
	tcsetattr (config.kbd_fd, TCSAFLUSH, &kbd_tio_rm);
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
    if (first && !ioctl (config.kbd_fd, TIOCGWINSZ, &winsize_ri))
        {
        if (winsize_ri.ws_col && winsize_ri.ws_row)
            {
            max_x_ig = winsize_ri.ws_col;
            max_y_ig = winsize_ri.ws_row;
            }

        }

    tcsetattr (config.kbd_fd, TCSAFLUSH, &kbd_tio_rm);

    if (config.utf8)
    {
      write(config.kbd_fd, "\033[?1l", sizeof "\033[?1l" - 1);
      fsync(config.kbd_fd);

      write(config.kbd_fd, "\033%G", sizeof "\033%G" - 1);
      fsync(config.kbd_fd);
    }

    if(first && config.serial) {
      get_screen_size(config.kbd_fd);

      if(max_x_ig > MAX_X) max_x_ig = MAX_X;
      if(max_y_ig > MAX_Y) max_y_ig = MAX_Y;

      if(!config.had_segv) fprintf(stderr, "Window size: %d x %d\n", max_x_ig, max_y_ig);

      memset(&winsize_ri, 0, sizeof winsize_ri);

      winsize_ri.ws_col = max_x_ig;
      winsize_ri.ws_row = max_y_ig;

      ioctl(config.kbd_fd, TIOCSWINSZ, &winsize_ri);
    }

    }


void kbd_reset()
{
  tcsetattr(config.kbd_fd, TCSAFLUSH, &kbd_tio_rm);
}


void kbd_end(int close_fd)
{
  tcsetattr (config.kbd_fd, TCSAFLUSH, &kbd_norm_tio_rm);
  if(close_fd) {
    close(config.kbd_fd);
    config.kbd_fd = -1;
  }
}


void kbd_switch_tty(int tty)
{
  ioctl(config.kbd_fd, VT_ACTIVATE, tty);
  ioctl(config.kbd_fd, VT_WAITACTIVE, tty);
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

  if(config.debug >= 3) {
    fprintf(stderr, "key(%d): 0x%02x\n", wait_iv, key);
  }

  return key;
}


/*
 * Remove n keycodes from keyboard buffer.
 */
void del_keys(unsigned n)
{
  if(n >= kbd.pos) {
    kbd.pos = 0;

    return;
  }

  memmove(kbd.buffer, kbd.buffer + n, kbd.pos -= n);
}


/*
 * Check keyboard buffer for key sequences.
 */
void check_for_key(int del_garbage)
{
  unsigned u, len;

  kbd.key = 0;

  if(!kbd.pos) return;

  if(config.debug >= 4) {
    fprintf(stderr, "kbd buffer(%d):", del_garbage);
    for(u = 0; u < kbd.pos; u++) {
      fprintf(stderr, " 0x%02x", kbd.buffer[u]);
    }
    fprintf(stderr, "\n");
  }

  /* look for esc sequences */
  for(u = 0; u < sizeof key_list / sizeof *key_list; u++) {
    len = strlen(key_list[u].bytes);
    if(len && len <= kbd.pos && !memcmp(kbd.buffer, key_list[u].bytes, len)) {
      kbd.key = key_list[u].key;
      del_keys(len);
//      fprintf(stderr, "-> key = 0x%02x\n", kbd.key);

      return;
    }
  }

  /* kill unknown esc sequences */
  if(del_garbage && kbd.buffer[0] == '\x1b' && kbd.pos > 1) {
    kbd.key = 0;
    del_keys(kbd.pos);

    return;
  }

  /* utf8 sequence */
  if(kbd.buffer[0] != '\x1b') {
    u = utf8_enc_len(kbd.buffer[0]);
    if(u && u <= kbd.pos) {
      kbd.key = utf8_decode(kbd.buffer);
      del_keys(u);

//      fprintf(stderr, "utf8(%u) = 0x%02x\n", u, kbd.key);

      return;
    }
  }

  /* kill remaining stuff */
  if(del_garbage) {
    /* basically for 'Esc' */
    if(kbd.pos == 1) kbd.key = kbd.buffer[0];

    del_keys(kbd.pos);

    return;
  }
}


/*
 * Read keyboard input until some key sequence has been recognized.
 */
int kbd_getch_raw(do_wait)
{
  unsigned char key;
  int i, esc_delay, time_to_wait, esc_count, delay;

  esc_delay = config.escdelay ? config.escdelay * 1000 : 25000;

  time_to_wait = config.kbdtimeout * 1000000;

  check_for_key(1);

  if(kbd.key || !do_wait) return kbd.key;

  esc_count = 0;

  for(;;) {
    delay = esc_count ? esc_delay : KBD_TIMEOUT;
    if(esc_count) esc_count--;

    kbd_set_timeout(delay);
    key = 0;
    read(config.kbd_fd, &key, 1);
    kbd_del_timeout();
    if(do_wait && config.kbdtimeout && (time_to_wait -= delay) <= 0) {
      /* fake 'Enter' */
      kbd.pos = 0;
      kbd.key = KEY_ENTER;

      return kbd.key;
    }

    /* next 3 keys might be part of an esc sequence */
    if(key == '\x1b') esc_count = 3;

    if((i = utf8_enc_len(key)) > 1) esc_count = i - 1;

    if(key) {
      if(kbd.pos < sizeof kbd.buffer) kbd.buffer[kbd.pos++] = key;

      check_for_key(0);
    }
    else {
      check_for_key(1);
    }

    if(kbd.key || !do_wait) return kbd.key;
  }
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
	tcsetattr (config.kbd_fd, TCSAFLUSH, &tios);
    }

int kbd_getch_old (int wait_iv)
    {
#define KBD_ESC_DELAY     25000
#define KEY_FUNC         0x0800
    char  keypress_ci;
    char  tmp_ci;
    int   i_ii;


    keypress_ci = 0;
    do
        {
        kbd_set_timeout (KBD_TIMEOUT);
        read (config.kbd_fd, &keypress_ci, 1);
        kbd_del_timeout ();
        }
    while (!keypress_ci && wait_iv);

    if (!keypress_ci)
        return (0);

    if (keypress_ci != KEY_ESC)
        return ((int) keypress_ci);

    kbd_set_timeout (KBD_ESC_DELAY);

    read (config.kbd_fd, &keypress_ci, 1);
    if (kbd_timeout_im)
        return ((int) KEY_ESC);
    else
        kbd_del_timeout ();

    if (keypress_ci != 91)
        return (0);

    read (config.kbd_fd, &keypress_ci, 1);
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
        read (config.kbd_fd, &keypress_ci, 1);
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

  if(fd < 0) return;

  write(fd, term_init, strlen(term_init));
  fsync(fd);

  buf[buf_len = 0] = 0;

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


void kbd_unimode()
{
  int fd, kbd_mode = K_UNICODE;

  if(config.test) return;

  fd = open(config.console, O_RDWR);

  if(fd < 0 || ioctl(fd, KDSKBMODE, kbd_mode) == -1) {
    perror("error setting kbd mode");
  }

  if(fd >= 0) close(fd);
}

