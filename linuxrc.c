/*
 *
 * linuxrc.c     Load modules and rootimage to ramdisk
 *
 * Copyright (c) 1996-2002  Hubert Mantel, SuSE Linux AG (mantel@suse.de)
 *
 */

#include "dietlibc.h"

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mount.h>
#include <sys/reboot.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <dirent.h>
#include <string.h>
#include <signal.h>
#include <stdarg.h>
#include <inttypes.h>
#include <ctype.h>

#ifdef DIET
#include <asm/sigcontext.h>
#endif

#include "global.h"
#include "text.h"
#include "info.h"
#include "util.h"
#include "display.h"
#include "keyboard.h"
#include "dialog.h"
#include "window.h"
#include "module.h"
#include "net.h"
#include "install.h"
#include "settings.h"
#include "file.h"
#include "linuxrc.h"
#include "auto2.h"
#include "lsh.h"
#include "multiple_info.h"

#define LINUXRC_DEFAULT_STDERR "/dev/tty3"

static void lxrc_main_menu     (void);
static void lxrc_init          (void);
static int  lxrc_main_cb       (dia_item_t di);
static void lxrc_memcheck      (void);
static void lxrc_check_console (void);
static void lxrc_set_bdflush   (int percent_iv);
static int do_not_kill         (char *name);
static void save_environment   (void);
static void lxrc_reboot        (void);
static void lxrc_halt          (void);

static pid_t lxrc_mempid_rm;
static int lxrc_sig11_im = FALSE;
static char **lxrc_argv;
const char *lxrc_new_root;
static char **saved_environment;
extern char **environ;
static void lxrc_movetotmpfs(void);
#if SWISS_ARMY_KNIFE 
static void lxrc_makelinks(void);
#endif

#if SWISS_ARMY_KNIFE
int cardmgr_main(int argc, char **argv);
int insmod_main(int argc, char **argv);
int loadkeys_main(int argc, char **argv);
int dhcpcd_main(int argc, char **argv);
int portmap_main(int argc, char **argv);
int probe_main(int argc, char **argv);
int rmmod_main(int argc, char **argv);
int setfont_main(int argc, char **argv);

static struct {
  char *name;
  int (*func)(int, char **);
} lxrc_internal[] = {
  { "sh",          util_sh_main          },
  { "lsh",         lsh_main              },
  { "insmod",      insmod_main           },
  { "rmmod",       rmmod_main            },
  { "lsmod",       util_lsmod_main       },
  { "loadkeys",    loadkeys_main         },
  { "dhcpcd",      dhcpcd_main           },
#if WITH_PCMCIA
  { "cardmgr",     cardmgr_main          },
  { "probe",       probe_main            },
#endif
  { "setfont",     setfont_main          },
  { "portmap",     portmap_main          },
  { "mount",       util_mount_main       },
  { "umount",      util_umount_main      },
  { "cat",         util_cat_main         },
  { "echo",        util_echo_main        },
  { "ps",          util_ps_main          },
  { "lsof",        util_lsof_main        },
  { "cp",          util_cp_main          },
  { "ls",          util_ls_main          },
  { "rm",          util_rm_main          },
  { "mv",          util_mv_main          },
  { "mkdir",       util_mkdir_main       },
  { "kill",        util_kill_main        },
  { "bootpc",      util_bootpc_main      },
  { "swapon",      util_swapon_main      },
  { "swapoff",     util_swapoff_main     },
  { "freeramdisk", util_freeramdisk_main },
  { "raidautorun", util_raidautorun_main },
  { "free",        util_free_main        },
  { "wget",        util_wget_main        },
  { "fstype",      util_fstype_main      },
  { "nothing",     util_nothing_main     }
};
#endif

typedef enum {
  lx_auto, lx_auto2, lx_noauto2, lx_y2autoinst, lx_demo, lx_eval, lx_reboot,
  lx_yast1, lx_yast2, lx_loadnet, lx_loaddisk, /* lx_french, */ /* lx_color, */
  lx_rescue, lx_nopcmcia, lx_debug, lx_nocmdline
} lx_param_t;

static struct {
  char *name;
  lx_param_t key;
} lxrc_params[] = {
  { "auto",       lx_auto       },
  { "auto2",      lx_auto2      },
  { "noauto2",    lx_noauto2    },
  { "y2autoinst", lx_y2autoinst },
  { "demo",       lx_demo       },
  { "eval",       lx_eval       },
  { "reboot",     lx_reboot     },
  { "yast1",      lx_yast1      },
  { "yast2",      lx_yast2      },
  { "loadnet",    lx_loadnet    },
  { "loaddisk",   lx_loaddisk   },
//  { "french",     lx_french     },
//  { "color",      lx_color      },
  { "rescue",     lx_rescue     },
  { "nopcmcia",   lx_nopcmcia   },
  { "debug",      lx_debug      },
  { "nocmdline",  lx_nocmdline  }
};

static dia_item_t di_lxrc_main_menu_last = di_main_start;


int main(int argc, char **argv, char **env)
{
  char *prog;
  int err;
#if SWISS_ARMY_KNIFE
  int i;
#endif

  prog = (prog = strrchr(*argv, '/')) ? prog + 1 : *argv;

#if SWISS_ARMY_KNIFE
  for(i = 0; i < sizeof lxrc_internal / sizeof *lxrc_internal; i++) {
    if(!strcmp(prog, lxrc_internal[i].name)) {
      return lxrc_internal[i].func(argc, argv);
    }
  }
#endif

  config.run_as_linuxrc = 1;

  lxrc_argv = argv;

  if(getpid() > 19 || util_check_exist("/opt")) {
    printf("Seems we are on a running system; activating testmode...\n");
    config.test = 1;
    config.tmpfs = 1;
    strcpy(console_tg, "/dev/tty");
  }

  if(!config.test && !getuid()) {
    if(!util_check_exist("/oldroot")) {
#if SWISS_ARMY_KNIFE 
      lxrc_makelinks();
#endif
      lxrc_movetotmpfs();	// does (normally) not return
    }
    else {
      config.tmpfs = 1;
      // umount and release /oldroot
      umount("/oldroot");
      util_free_ramdisk("/dev/ram0");
    }
  }

  save_environment();
  lxrc_init();

  if(auto_ig) {
    err = inst_auto_install();
  }
  else if(demo_ig) {
    err = inst_start_demo();
  }
#ifdef USE_LIBHD
  else if(auto2_ig) {
    printf("***** rescue 0x%x *****\n", action_ig); fflush(stdout);
    if((action_ig & ACT_RESCUE)) {
      util_manual_mode();
      util_disp_init();
      set_choose_keytable(1);
    }
    err = inst_auto2_install();
  }
#endif
  else {
    err = 99;
  }

  if(err) {
    util_disp_init();
    lxrc_main_menu();
  }

  lxrc_end();

  return err;
}


#if 0
int my_syslog (int type_iv, char *buffer_pci, ...)
    {
    va_list  args_ri;

    va_start (args_ri, buffer_pci);
    vfprintf (stderr, buffer_pci, args_ri);
    va_end (args_ri);
    fprintf (stderr, "\n");
    return (0);
    }
#endif


int my_logmessage (char *buffer_pci, ...)
    {
    va_list  args_ri;

    va_start (args_ri, buffer_pci);
    vfprintf (stderr, buffer_pci, args_ri);
    va_end (args_ri);
    fprintf (stderr, "\n");
    return (0);
    }


void lxrc_reboot()
{
  if(config.test) {
    fprintf(stderr, "*** reboot ***\n");
    return;
  }

  if(auto_ig || auto2_ig || dia_yesno(txt_get(TXT_ASK_REBOOT), 1) == YES) {
    reboot(RB_AUTOBOOT);
  }
}

void lxrc_halt()
{
  if(config.test) {
    fprintf(stderr, "*** power off ***\n");
    return;
  }

  if(auto_ig || auto2_ig || dia_yesno("Do you want to halt the system now?", 1) == YES) {
    reboot(RB_POWER_OFF);
  }
}

static void save_environment (void)
{
    int i;

    i = 0;
    while (environ[i++])
	;
    saved_environment = malloc (i * sizeof (char *));
    if (saved_environment)
	memcpy (saved_environment, environ, i * sizeof (char *));
}

#ifdef USE_LXRC_CHANGE_ROOT
void lxrc_change_root (void)
{
  char *new_mp;

  if(config.test) return;

  new_mp = (action_ig & ACT_DEMO) ? ".mnt" : "mnt";

#ifdef SYS_pivot_root
  umount ("/mnt");
  util_mount_ro(lxrc_new_root, "/mnt");
  chdir ("/mnt");
  if (
#ifndef DIET
      /* XXX Until glibc is fixed to provide pivot_root. */
      syscall (SYS_pivot_root, ".", new_mp)
#else
      pivot_root (".", new_mp)
#endif
      == 0)
    {
#if 0
      close (0); close (1); close (2);
      chroot (".");
      execve ("/sbin/init", lxrc_argv, saved_environment ? : environ);
#endif
      int i;

      for(i = 0; i < 20 ; i++) close(i);
      chroot(".");
      if((action_ig & ACT_DEMO)) {
        execve("/sbin/init", lxrc_argv, saved_environment ? : environ);
      }
      else {
        execl("/bin/umount", "umount", "/mnt", NULL);
      }
    }
    else {
      chdir ("/");
      umount ("/mnt");
    }
#endif
}
#endif

void lxrc_end (void)
    {
    int i;

    deb_msg("lxrc_end()");
    kill (lxrc_mempid_rm, 9);
    lxrc_killall (1);
    while(waitpid(-1, NULL, WNOHANG) == 0);
    i = waitpid(lxrc_mempid_rm, NULL, 0);
    fprintf(stderr, "lxrc_mempid_rm = %d, wait = %d\n", lxrc_mempid_rm, i);
    printf ("\033[9;15]");	/* screen saver on */
/*    reboot (RB_ENABLE_CAD); */
    mod_free_modules ();
    util_umount_driver_update ();
    (void) util_umount (mountpoint_tg);
    lxrc_set_modprobe ("/sbin/modprobe");
    lxrc_set_bdflush (40);
    if(!config.test) {
      (void) util_umount ("/proc/bus/usb");
      (void) util_umount ("/proc");
    }
    disp_cursor_on ();
    kbd_end ();
    disp_end ();
#ifdef USE_LXRC_CHANGE_ROOT
    if (lxrc_new_root)
      lxrc_change_root();
#endif
    }


/*
 * Check if pid is either portmap or rpciod.
 *
 */
int do_not_kill(char *name)
{
  static char *progs[] = {
    "portmap", "rpciod", "lockd", "lsh"
  };
  int i;

  for(i = 0; i < sizeof progs / sizeof *progs; i++) {
    if(!strcmp(name, progs[i])) return 1;
  }

  return 0;
}


/*
 * really_all = 0: kill everything except rpc progs
 * really_all = 1: kill really everything
 *
 */
void lxrc_killall(int really_all_iv)
{
  pid_t mypid;
  struct dirent *de;
  DIR *d;
  pid_t pid;
  char *s;
  slist_t *sl0 = NULL, *sl;
  int sig;

  if(config.test) return;

  mypid = getpid();

  if(!(d = opendir("/proc"))) return;

  /* make a (reversed) list of all process ids */
  while((de = readdir(d))) {
    pid = strtoul(de->d_name, &s, 10);
    if(!*s && *util_process_cmdline(pid)) {
      sl = slist_add(&sl0, slist_new());
      sl->key = strdup(de->d_name);
      sl->value = strdup(util_process_name(pid));
    }
  }

  closedir(d);

  for(sig = 15; sig >= 9; sig -= 6) {
    for(sl = sl0; sl; sl = sl->next) {
      pid = strtoul(sl->key, NULL, 10);
      if(
        pid > mypid &&
        pid != lxrc_mempid_rm &&
        (really_all_iv || !do_not_kill(sl->value))
      ) {
        if(sig == 15) fprintf(stderr, "killing %s (%d)\n", sl->value, pid);
        kill(pid, sig);
        usleep(20000);
      }
    }
  }

  slist_free(sl0);

  while(waitpid(-1, NULL, WNOHANG) > 0);
}


/*
 *
 * Local functions
 *
 */


#if defined(__i386__) || defined(__PPC__) || defined(__sparc__) || defined(__s390__) || defined(__s390x__)
static void lxrc_catch_signal_11(int signum, struct sigcontext scp)
#endif
#if defined(__alpha__) || defined(__ia64__)
static void lxrc_catch_signal_11(int signum, int x, struct sigcontext *scp)
#endif
{
  volatile static unsigned cnt = 0;
  uint64_t ip;

#ifdef __i386__
  ip = scp.eip;
#endif

#ifdef __PPC__
  ip = (scp.regs)->nip;
#endif

#ifdef __sparc__
  ip = scp.si_regs.pc;
#endif

#ifdef __alpha__
  ip = scp->sc_pc;
#endif

#ifdef __ia64__
  ip = scp->sc_ip;
#endif

#ifdef __s390__
  ip = (scp.sregs)->regs.psw.addr;
#endif

#ifdef __s390x__
  ip = (scp.sregs)->regs.psw.addr;
#endif

  if(!cnt++) fprintf(stderr, "Linuxrc segfault at 0x%08"PRIx64". :-((\n", ip);
  sleep(10);

  lxrc_sig11_im = TRUE;
}


static void lxrc_catch_signal (int sig_iv)
    {
    if (sig_iv)
        {
        fprintf (stderr, "Caught signal %d!\n", sig_iv);
        sleep (10);
        }

    if (sig_iv == SIGBUS || sig_iv == SIGSEGV)
        lxrc_sig11_im = TRUE;

    signal (SIGHUP,  lxrc_catch_signal);
    signal (SIGBUS,  lxrc_catch_signal);
    signal (SIGINT,  lxrc_catch_signal);
    signal (SIGTERM, lxrc_catch_signal);
    signal (SIGSEGV, (void (*)(int)) lxrc_catch_signal_11);
    signal (SIGPIPE, lxrc_catch_signal);
    }


void lxrc_init()
{
  int i, j;
  file_t *ft;
  char *s, *t0, *t, buf[256];
  url_t *url;

  printf(">>> SuSE installation program v" LXRC_VERSION " (c) 1996-2002 SuSE Linux AG <<<\n");
  fflush(stdout);

  if(txt_init()) {
    printf("Corrupted texts!\n");
    exit(-1);
  }

  siginterrupt(SIGALRM, 1);
  siginterrupt(SIGHUP, 1);
  siginterrupt(SIGBUS, 1);
  siginterrupt(SIGINT, 1);
  siginterrupt(SIGTERM, 1);
  siginterrupt(SIGSEGV, 1);
  siginterrupt(SIGPIPE, 1);
  lxrc_catch_signal(0);
/*  reboot (RB_DISABLE_CAD); */

  umask(022);

  /* add cmdline to info file */
  config.info.add_cmdline = 1;

  /* make it configurable? */
  config.module.dir = strdup("/modules");
  config.mountpoint.floppy = strdup("/mounts/floppy");
  config.mountpoint.ramdisk2 = strdup("/mounts/ramdisk2");

  /* just a default for manual mode */
  config.floppies = 1;
  config.floppy_dev[0] = strdup("/dev/fd0");

  config.net.bootp_timeout = 10;
  config.net.dhcp_timeout = 60;
  config.net.tftp_timeout = 10;

  config.color = 2;
  config.net.use_dhcp = 1;

#if defined(__s390__) || defined(__s390x__)
  config.initrd_has_ldso = 1;
#endif

  for(i = 0; i < sizeof config.ramdisk / sizeof *config.ramdisk; i++) {
    sprintf(buf, "/dev/ram%d", i + 2);
    str_copy(&config.ramdisk[i].dev, buf);
  }

  config.inst_ramdisk = -1;

  util_free_mem();
  config.memory.min_free = 10240;	// at least 10MB
  if(config.memory.free < config.memory.min_free) config.memory.min_free = config.memory.free;

  if((s = getenv("lang"))) {
    i = set_langidbyname(s);
    if(i) config.language = i;
  }

  ft = file_get_cmdline(key_info);
  s = ft ? ft->value : getenv("info");
  if(s && !*s) s = "default";
  str_copy(&config.info.file, s);

  ft = file_get_cmdline(key_autoyast);
  s = ft ? ft->value : getenv("autoyast");
  if(s && !*s) s = "default";
  str_copy(&config.autoyast, s);

  if(config.autoyast) {
    auto2_ig = TRUE;
    yast_version_ig = 2;
    action_ig |= ACT_YAST2_AUTO_INSTALL;
    url = parse_url(config.autoyast);
    if(url && url->scheme) set_instmode(url->scheme);
  }

  ft = file_get_cmdline(key_linuxrc);
  str_copy(&config.linuxrc, ft ? ft->value : getenv("linuxrc"));

  if(config.linuxrc) {
    s = strdup(config.linuxrc);

    for(t0 = s; (t = strsep(&t0, ",")); ) {
      for(i = 0; i < sizeof lxrc_params / sizeof *lxrc_params; i++) {
        if(!strcasecmp(lxrc_params[i].name, t)) {
          switch(lxrc_params[i].key) {
            case lx_auto:
              auto_ig = TRUE;
              break;

            case lx_auto2:
              auto2_ig = TRUE;
              break;

            case lx_noauto2:
              auto2_ig = FALSE;
              break;

            case lx_y2autoinst:
              auto2_ig = TRUE;
              yast_version_ig = 2;
              action_ig |= ACT_YAST2_AUTO_INSTALL;
              break;

            case lx_demo:
              demo_ig = TRUE;
              action_ig |= ACT_DEMO | ACT_DEMO_LANG_SEL | ACT_LOAD_DISK;
              break;

            case lx_eval:
              demo_ig = TRUE;
              action_ig |= ACT_DEMO | ACT_LOAD_DISK;
              break;

            case lx_reboot:
              reboot_ig = TRUE;
              break;

            case lx_yast1:
              yast_version_ig = 1;
              break;

            case lx_yast2:
              yast_version_ig = 2;
              break;

            case lx_loadnet:
              action_ig |= ACT_LOAD_NET;
              break;

            case lx_loaddisk:
              action_ig |= ACT_LOAD_DISK;
              break;

#if 0	/* obsolete */
            case lx_french:
              config.language = lang_fr;
              break;
#endif

#if 0	/* obsolete */
            case lx_color:
              config.color = 2;
              break;
#endif

            case lx_rescue:
              action_ig |= ACT_RESCUE;
              break;

            case lx_nopcmcia:
              action_ig |= ACT_NO_PCMCIA;
              break;

            case lx_debug:
              action_ig |= ACT_DEBUG;
              break;

            case lx_nocmdline:
              config.info.add_cmdline = 0;
              break;
          }
          break;
        }
      }
    }
    free(s);
  }

  config.stderr_name = getenv("LINUXRC_STDERR");
  config.stderr_name = strdup(config.stderr_name ?: LINUXRC_DEFAULT_STDERR);
  freopen(config.stderr_name, "a", stderr);
#ifndef DIET
  setlinebuf(stderr);
#else
  {
    static char buf[128];
    setvbuf(stderr, buf, _IOLBF, sizeof buf);
  }
#endif

  if(!config.test) {
    mount("proc", "/proc", "proc", 0, 0);

    fprintf(stderr, "Remount of / ");
    i = mount(0, "/", 0, MS_MGC_VAL | MS_REMOUNT, 0);
    fprintf(stderr, i ? "failed\n" : "ok\n");

    /* Check for special case with aborted installation */
    if(util_check_exist ("/.bin")) {
      unlink("/bin");
      rename("/.bin", "/bin");
    }
  }

  if(util_check_exist("/sbin/modprobe")) has_modprobe = 1;
  lxrc_set_modprobe("/etc/nothing");
  lxrc_set_bdflush(5);

  lxrc_check_console();
  freopen(console_tg, "r", stdin);
  freopen(console_tg, "a", stdout);

  util_get_splash_status();

  if(util_check_exist("/proc/iSeries")) config.is_iseries = 1;

  kbd_init();
  util_redirect_kmsg();
  disp_init();

  auto2_chk_expert();

  printf("\033[9;0]");		/* screen saver off */
  fflush(stdout);

  info_init();

  // ???
  if(memory_ig < MEM_LIMIT_YAST2) yast_version_ig = 1;

  if(memory_ig > (yast_version_ig == 1 ? MEM_LIMIT_RAMDISK_YAST1 : MEM_LIMIT_RAMDISK_YAST2)) {
    force_ri_ig = TRUE;
  }

  lxrc_memcheck();

  mod_init();

  if(!(config.test || serial_ig)) {
    util_start_shell("/dev/tty9", "/bin/lsh", 0);
  }

#ifdef USE_LIBHD
  if(auto2_ig) {
    if(auto2_init()) {
      auto2_ig = TRUE;
    } else {
      deb_msg("Automatic setup not possible.");

      util_manual_mode();
      disp_set_display();
      util_disp_init();

#ifdef __i386__
      i = 0;
      j = 1;
      if(cdrom_drives && !(action_ig & ACT_DEMO)) {
        sprintf(buf, txt_get(TXT_INSERT_CD), 1);
        j = dia_okcancel(buf, YES) == YES ? 1 : 0;
        if(j) {
          util_disp_done();
          i = auto2_find_install_medium();
        }
      }
      if(i) {
        auto2_ig = TRUE;
      }
      else {
        yast_version_ig = 0;
        util_disp_init();
        if(j) {
          sprintf(buf,
            "Could not find the SuSE Linux %s CD.\n\nActivating manual setup program.\n",
            (action_ig & ACT_DEMO) ? "LiveEval" : "Installation"
          );
          dia_message(buf, MSGTYPE_ERROR);
        }
      }
#endif

    }
  }
#endif

  /* note: for auto2, file_read_info() is called in auto2_init() */
  if(auto_ig) {
    rename_info_file();
    file_read_info();
  }

  /* for 'manual' */
  if(!config.info.loaded) file_read_info();
	
  if(!auto2_ig) {
    util_disp_init();
  }

  if(!config.language && config.win) {
    set_choose_language();
  } else {
    set_activate_language(config.language);
  }

  util_print_banner();

  if(!config.color) {
    int old_win = config.win;

    if(!old_win) util_disp_init();
    set_choose_display();
    if(old_win) util_print_banner(); else util_disp_done();
  }

  if(!(serial_ig || config.is_iseries)) {
    set_choose_keytable(0);
  }

  util_update_kernellog();

  net_setup_localhost();

#if !(defined(__PPC__) || defined(__sparc__))
  if(!auto_ig || reboot_wait_ig) {
    config.rebootmsg = 1;
  }
#endif
}

void lxrc_main_menu()
{
  dia_item_t di;
  dia_item_t items[] = {
    di_main_settings,
    di_main_info,
    di_main_modules,
    di_main_start, 
    di_main_reboot,
    di_main_halt,
    di_none
  };

  util_manual_mode();

  for(;;) {
    di = dia_menu2(txt_get(TXT_HDR_MAIN), 40, lxrc_main_cb, items, di_lxrc_main_menu_last);

    if(di == di_none) {
      if(dia_message(txt_get(TXT_NO_LXRC_END), MSGTYPE_INFO) == -42) break;
      continue;
    }

    if(di == di_main_settings) {
      util_print_banner();
      continue;
    }

    break;
  }
}


/*
 * return values:
 * -1    : abort (aka ESC)
 *  0    : ok
 *  other: stay in menu
 */
int lxrc_main_cb(dia_item_t di)
{
  int rc = 1;

  di_lxrc_main_menu_last = di;

  switch(di) {
    case di_main_settings:
      rc = set_settings();
      if(rc == di_set_lang || rc == di_set_display)
        rc = 0;
      else if(rc == di_none)
        rc = 1;
      break;

    case di_main_info:
      info_menu();
      break;

    case di_main_modules:
      mod_menu();
      break;

    case di_main_start:
      rc = inst_menu();
      break;

    case di_main_reboot:
      lxrc_reboot();
      break;

    case di_main_halt:
      lxrc_halt();
      break;

    default:
  }

  return rc;
}


static void lxrc_memcheck (void)
    {
    int   nr_pages_ii;
    int   max_pages_ii;
    char *data_pci;
    int   i_ii;


    lxrc_mempid_rm = fork ();
    if (lxrc_mempid_rm)
        return;

    lxrc_catch_signal (0);

    if (memory_ig < 8000000)
        max_pages_ii = 40;
    else
        max_pages_ii = 100;

    while (1)
        {
#if 0
  /* dietlibc's (v0.12) rand() gives values above RAND_MAX */
        nr_pages_ii = 1 + (int) ((double) max_pages_ii * rand () / (RAND_MAX + 1.0));
#endif
        nr_pages_ii = ((rand() & 0xfff) % max_pages_ii) + 3;
        data_pci = malloc (nr_pages_ii * 4096);
        if (data_pci)
            {
            for (i_ii = 0; i_ii < nr_pages_ii && !lxrc_sig11_im; i_ii++)
                {
                data_pci [i_ii * 4096] = 42;
                usleep (10000);
                }
            lxrc_sig11_im = FALSE;
            free (data_pci);
            }
        sleep (1);
        }
    }


void lxrc_set_modprobe (char *program_tv)
    {
    FILE  *proc_file_pri;

    /* do nothing if we have a modprobe */
    if (has_modprobe || config.test) return;

    proc_file_pri = fopen ("/proc/sys/kernel/modprobe", "w");
    if (proc_file_pri)
        {
        fprintf (proc_file_pri, "%s\n", program_tv);
        fclose (proc_file_pri);
        }
    }


/* Check if we start linuxrc on a serial console. On Intel and
   Alpha, we look if the "console" parameter was used on the
   commandline. On SPARC, we use the result from hardwareprobing. */
static void lxrc_check_console (void)
    {
#if !defined(__sparc__) && !defined(__PPC__)
    FILE  *fd_pri;
    char   buffer_ti [300];
    char  *tmp_pci = NULL;
    char  *found_pci = NULL;

    fd_pri = fopen ("/proc/cmdline", "r");
    if (!fd_pri)
        return;

    if (fgets (buffer_ti, sizeof buffer_ti - 1, fd_pri))
        {
        tmp_pci = strstr (buffer_ti, "console");
        while (tmp_pci)
            {
            found_pci = tmp_pci;
            tmp_pci = strstr (found_pci + 1, "console");
            }
        }

    if (found_pci)
        {
        while (*found_pci && *found_pci != '=')
            found_pci++;

        found_pci++;

	/* Find the whole console= entry for the install.inf file */
        tmp_pci = found_pci;
        while (*tmp_pci && !isspace(*tmp_pci)) tmp_pci++;
        *tmp_pci = 0;

	strncpy (console_parms_tg, found_pci, sizeof console_parms_tg);
	console_parms_tg[sizeof console_parms_tg - 1] = '\0';

	/* Now search only for the device name */
	tmp_pci = found_pci;
        while (*tmp_pci && *tmp_pci != ',') tmp_pci++;
        *tmp_pci = 0;

        sprintf (console_tg, "/dev/%s", found_pci);
        if (!strncmp (found_pci, "ttyS", 4)) {
            serial_ig = TRUE;
            text_mode_ig = TRUE;
          }

        fprintf (stderr, "Console: %s, serial=%d\n", console_tg, serial_ig);
        }

    fclose (fd_pri);
#else
#ifdef USE_LIBHD
    char *cp;

    cp = auto2_serial_console ();
    if (cp && strlen (cp) > 0)
      {
	serial_ig = TRUE;
        text_mode_ig = TRUE;

	strcpy (console_tg, cp);

        fprintf (stderr, "Console: %s, serial=%d\n", console_tg, serial_ig);
      }
#endif
#endif
    }


static void lxrc_set_bdflush (int percent_iv)
    {
    FILE  *fd_pri;

    if(config.test) return;

    fd_pri = fopen ("/proc/sys/vm/bdflush", "w");
    if (!fd_pri)
        return;

    fprintf (fd_pri, "%d", percent_iv);
    fclose (fd_pri);
    }


/*
 * Copy root tree into a tmpfs tree, pivot_root() there and
 * exec() the new linuxrc.
 */
void lxrc_movetotmpfs()
{
  int i;
  char *newroot = "/newroot";

  fprintf(stderr, "Moving into tmpfs...");
  i = mkdir(newroot, 0755);
  if(i) {
    perror(newroot);
    return;
  }

  i = mount("shmfs", newroot, "shm", 0, 0);
  if(i) {
    perror(newroot);
    return;
  }

  i = util_do_cp("/", newroot);
  if(i) {
    fprintf(stderr, "copy failed: %d\n", i);
    return;
  }

  fprintf(stderr, " done.\n");

  i = chdir(newroot);
  if(i) {
    perror(newroot);
    return;
  }

  i = mkdir("oldroot", 0755);
  if(i) {
    perror("oldroot");
    return;
  }

  if(
#ifndef DIET
    !syscall(SYS_pivot_root, ".", "oldroot")
#else
    !pivot_root(".", "oldroot")
#endif
  ) {
    for(i = 0; i < 20; i++) close(i);
    chroot(".");
    open("/dev/console", O_RDWR);
    dup(0);
    dup(0);
    execve("/linuxrc", lxrc_argv, environ);
    perror("/linuxrc");
  }
  else {
    fprintf(stderr, "Oops, pivot_root failed\n");
  }
}


#if SWISS_ARMY_KNIFE
void lxrc_makelinks()
{
  int i;
  char buf[64];

  if(!util_check_exist("/bin")) mkdir("/bin", 0755);

  for(i = 0; i < sizeof lxrc_internal / sizeof *lxrc_internal; i++) {
    sprintf(buf, "/bin/%s", lxrc_internal[i].name);
    if(!util_check_exist(buf)) link("/linuxrc", buf);
  }
}
#endif
