/*
 *
 * linuxrc.c     Load modules and rootimage to ramdisk
 *
 * Copyright (c) 1996-2003  Hubert Mantel, SuSE Linux AG (mantel@suse.de)
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
#include <sys/select.h>
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

#if defined(__alpha__) || defined(__ia64__)
#define SIGNAL_ARGS	int signum, int x, struct sigcontext *scp
#else	// __i386__ __x86_64__ __PPC__ __sparc__ __s390__ __s390x__ __MIPSEB__
#define SIGNAL_ARGS	int signum, struct sigcontext scp
#endif

#ifndef DIET
#define pivot_root(a, b) syscall(SYS_pivot_root, a, b)
#endif

static void lxrc_main_menu     (void);
static void lxrc_catch_signal_11(SIGNAL_ARGS);
static void lxrc_catch_signal  (int signum);
static void lxrc_init          (void);
static int  lxrc_main_cb       (dia_item_t di);
static void lxrc_check_console (void);
static void lxrc_set_bdflush   (int percent_iv);
static int do_not_kill         (char *name);
static void save_environment   (void);
static void lxrc_change_root   (void);
static void lxrc_reboot        (void);
static void lxrc_halt          (void);

static char **saved_environment;
extern char **environ;
static void lxrc_movetotmpfs(void);
#if SWISS_ARMY_KNIFE 
static void lxrc_makelinks(char *name);
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
int smbmnt_main(int argc, char **argv);

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
  { "smbmnt",      smbmnt_main           },
  { "portmap",     portmap_main          },
  { "mount",       util_mount_main       },
  { "umount",      util_umount_main      },
  { "cat",         util_cat_main         },
  { "hex",         util_hex_main         },
  { "echo",        util_echo_main        },
  { "ps",          util_ps_main          },
  { "lsof",        util_lsof_main        },
  { "cp",          util_cp_main          },
  { "ls",          util_ls_main          },
  { "rm",          util_rm_main          },
  { "mv",          util_mv_main          },
  { "ln",          util_ln_main          },
  { "mkdir",       util_mkdir_main       },
  { "chroot",      util_chroot_main      },
  { "kill",        util_kill_main        },
  { "bootpc",      util_bootpc_main      },
  { "swapon",      util_swapon_main      },
  { "swapoff",     util_swapoff_main     },
  { "freeramdisk", util_freeramdisk_main },
  { "raidautorun", util_raidautorun_main },
  { "free",        util_free_main        },
  { "wget",        util_wget_main        },
  { "fstype",      util_fstype_main      },
  { "modprobe",    util_modprobe_main    },
  { "usbscsi",     util_usbscsi_main     },
  { "nothing",     util_nothing_main     }
};
#endif

typedef enum {
  lx_auto, lx_auto2, lx_reboot, lx_loadnet, lx_loaddisk, lx_nocmdline
} lx_param_t;

static struct {
  char *name;
  lx_param_t key;
} lxrc_params[] = {
  { "auto",       lx_auto       },
  { "auto2",      lx_auto2      },
  { "reboot",     lx_reboot     },
  { "loadnet",    lx_loadnet    },
  { "loaddisk",   lx_loaddisk   },
  { "nocmdline",  lx_nocmdline  }
};

static dia_item_t di_lxrc_main_menu_last;


int main(int argc, char **argv, char **env)
{
  char *prog;
  int err, i, j;
  file_t *ft;
  int tmpfs_opt = 0;

  prog = (prog = strrchr(*argv, '/')) ? prog + 1 : *argv;

#if SWISS_ARMY_KNIFE
  for(i = 0; (unsigned) i < sizeof lxrc_internal / sizeof *lxrc_internal; i++) {
    if(!strcmp(prog, lxrc_internal[i].name)) {
      return lxrc_internal[i].func(argc, argv);
    }
  }
#endif

  config.argv = argv;

  config.run_as_linuxrc = 1;
  config.tmpfs = 1;

  str_copy(&config.console, "/dev/console");
  str_copy(&config.stderr_name, "/dev/tty3");

  str_copy(&config.product, "SuSE Linux");

  config.update.next_name = &config.update.name_list;

  /* maybe we had a segfault recently... */
  if(argc == 4 && !strcmp(argv[1], "segv")) {
    for(i = 0; i < 16 && argv[2][i]; i++) {
      config.segv_addr <<= 4;
      j = argv[2][i] - '0';
      if(j > 9) j -= 'a' - '9' - 1;
      config.segv_addr += j & 0xf;
    }
    config.had_segv = 1;

    if(argv[3][0] == '0') {	/* was not in window mode */
      fprintf(stderr, "\n\nLinuxrc crashed. :-((\nPress ENTER to continue.\n");
      printf("\n\nLinuxrc crashed. :-((\nPress ENTER to continue.\n");
      fflush(stdout);
      getchar();
    }
  }

  if(!config.had_segv) config.restart_on_segv = 1;

  if(util_check_exist("/opt") || getuid()) {
    printf("Seems we are on a running system; activating testmode...\n");
    config.test = 1;
    str_copy(&config.console, "/dev/tty");
  }

  if(!config.test && !config.had_segv) {
    if(!util_check_exist("/oldroot")) {
#if SWISS_ARMY_KNIFE 
      lxrc_makelinks(*argv);
#endif
      mount("proc", "/proc", "proc", 0, 0);
      ft = file_get_cmdline(key_tmpfs);
      if(ft && ft->is.numeric) {
        config.tmpfs = ft->nvalue;
        tmpfs_opt = 1;
      }
      ft = file_get_cmdline(key_debugwait);
      if(ft && ft->is.numeric) config.debugwait = ft->nvalue;
      util_free_mem();
      umount("/proc");

//      fprintf(stderr, "free: %d, %d, %d\n", config.memory.free, config.tmpfs, tmpfs_opt);

      if(config.tmpfs && (config.memory.free > 24 * 1024 || tmpfs_opt)) {
        lxrc_movetotmpfs();	/* does not return if successful */
      }
      config.tmpfs = 0;

      if(!config.serial && config.debugwait) {
        util_start_shell("/dev/tty9", "/bin/lsh", 0);
        config.shell_started = 1;
      }
      deb_wait;
    }
    else {
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
  else if(config.demo) {
    err = inst_start_demo();
  }
#ifdef USE_LIBHD
  else if(auto2_ig) {
    if(config.rescue) {
      int win_old;
      if(!(win_old = config.win)) util_disp_init();
      set_choose_keytable(0);
      if(!win_old) util_disp_done();
      
    }
    if(config.hwcheck) {
      // util_hwcheck();
      err = 11;
    }
    else {
      err = inst_start_install();
    }
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


void lxrc_change_root()
{
  int i;
  char *new_mp;

  if(config.test) return;

  new_mp = config.demo ? ".mnt" : "mnt";

  umount("/mnt");
  util_mount_ro(config.new_root, "/mnt");
  chdir("/mnt");

  if(!pivot_root(".", new_mp)) {
    fprintf(stderr, "pivot_root ok\n");
    for(i = config.testpivotroot ? 3 : 0; i < 20 ; i++) close(i);
    chroot(".");
    if(config.testpivotroot) {
      freopen(config.console, "r", stdin);
      freopen(config.console, "a", stdout);
      freopen(config.console, "a", stderr);
      execl("/bin/sh", "sh", NULL);
    }
    else {
      execl("/bin/umount", "umount", "/mnt", NULL);
    }
  }
  else {
    chdir("/");
    umount("/mnt");
    fprintf(stderr, "pivot_root failed\n");
    deb_wait;
  }
}


void lxrc_end()
{
  FILE *f;

    if(config.netstop) {
      util_debugwait("shut down network");
      net_stop();
    }

    util_debugwait("kill remaining procs");

    lxrc_killall(1);

    while(waitpid(-1, NULL, WNOHANG) == 0);

    if (!config.linemode)
      printf ("\033[9;15]");		/* screen saver on */

/*    reboot (RB_ENABLE_CAD); */
    mod_free_modules();

//    util_umount_driver_update();
    util_umount(mountpoint_tg);

    lxrc_set_modprobe("/sbin/modprobe");
    lxrc_set_bdflush(40);

    deb_str(config.new_root);

    util_debugwait("leaving now");

  if(!config.test) {
    if(config.new_root && config.pivotroot) {
      // tell kernel root is /dev/ram0, prevents remount after initrd
      if(!(f = fopen ("/proc/sys/kernel/real-root-dev", "w"))) return;
      fprintf(f, "256\n");
      fclose(f);
    }

    util_umount("/proc/bus/usb");
    util_umount("/proc");
  }

  disp_cursor_on ();
  kbd_end ();
  disp_end ();

  if(config.new_root && config.pivotroot) lxrc_change_root();
}

/*
 * Check if pid is either portmap or rpciod.
 *
 */
int do_not_kill(char *name)
{
  static char *progs[] = {
    "portmap", "rpciod", "lockd", "lsh", "dhcpcd", "smbmount", "cardmgr"
  };
  int i;

  for(i = 0; (unsigned) i < sizeof progs / sizeof *progs; i++) {
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


void lxrc_catch_signal_11(SIGNAL_ARGS)
{
  uint64_t ip;
  char addr[17];
  char state[2];
  int i, j;

#ifdef __i386__
  ip = scp.eip;
#endif

#ifdef __x86_64__
  ip = scp.rip;
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

#ifdef __MIPSEB__
  ip = scp.sc_fpc_eir;
#endif

  fprintf(stderr, "Linuxrc segfault at 0x%08"PRIx64". :-((\n", ip);
  if(config.restart_on_segv) {
    config.restart_on_segv = 0;
    for(i = 15; i >= 0; i--, ip >>= 4) {
      j = ip & 0xf;
      if(j > 9) j += 'a' - '9' - 1;
      addr[i] = j + '0';
    }
    addr[16] = 0;
    state[0] = config.win ? '1' : '0';
    state[1] = 0;
    kbd_end();		/* restore terminal settings */
    execl(*config.argv, "linuxrc", "segv", addr, state, NULL);
  }

  /* stop here */
  for(;;) select(0, NULL, NULL, NULL, NULL);
}


void lxrc_catch_signal(int signum)
{
  if(signum) {
    fprintf(stderr, "Caught signal %d!\n", signum);
    sleep(10);
  }

  signal(SIGHUP,  lxrc_catch_signal);
  signal(SIGBUS,  lxrc_catch_signal);

  if(!config.test) {
    signal(SIGINT,  lxrc_catch_signal);
    signal(SIGTERM, lxrc_catch_signal);
  }

  signal(SIGSEGV, (void (*)(int)) lxrc_catch_signal_11);
  signal(SIGPIPE, lxrc_catch_signal);
}


void lxrc_init()
{
  int i, j;
  file_t *ft;
  char *s, *t0, *t, buf[256];
  url_t *url;

  if(txt_init()) {
    printf("Linuxrc error: Corrupted texts!\n");
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

  if(!config.test) {
    if(config.had_segv) umount("/proc");
    mount("proc", "/proc", "proc", 0, 0);
  }

  /* add cmdline to info file */
  config.info.add_cmdline = 1;

  /* make it configurable? */
  config.module.dir = strdup("/modules");
  config.mountpoint.floppy = strdup("/mounts/floppy");
  config.mountpoint.ramdisk2 = strdup("/mounts/ramdisk2");
  config.mountpoint.extra = strdup("/mounts/extra");
  config.mountpoint.instsys = strdup("/mounts/instsys");
  config.mountpoint.live = strdup("/mounts/live");
  config.mountpoint.update = strdup("/mounts/update");
  config.mountpoint.instdata = strdup("/var/adm/mount");

  config.setupcmd = strdup("/sbin/inst_setup yast");
  config.update.dir = strdup("/linux/suse/" LX_ARCH "-" LX_REL);
  config.update.dst = strdup("/update");

  util_set_product_dir("suse");

  config.net.bootp_timeout = 10;
  config.net.dhcp_timeout = 60;
  config.net.tftp_timeout = 10;
  config.net.ifconfig = 1;

  config.explode_win = 1;
  config.color = 2;
  config.net.use_dhcp = 1;
  config.addswap = 1;
  config.netstop = 1;
  config.usbwait = 4;		/* 4 seconds */

  config.hwdetect = 1;

  // default memory limits for i386 version
  config.memory.min_free =       12 * 1024;
  config.memory.min_yast_text =  32 * 1024;
  config.memory.min_yast =       40 * 1024;
  config.memory.min_modules =    64 * 1024;
  config.memory.load_image =    200 * 1024;

  /* make auto mode default */
  if(config.test || config.had_segv) {
    config.manual = 1;
  }
  else {
    auto2_ig = 1;
  }

#if defined(__s390__) || defined(__s390x__)
  config.initrd_has_ldso = 1;
#endif

  for(i = 0; (unsigned) i < sizeof config.ramdisk / sizeof *config.ramdisk; i++) {
    sprintf(buf, "/dev/ram%d", i + 2);
    str_copy(&config.ramdisk[i].dev, buf);
  }

  config.inst_ramdisk = -1;
  config.module.ramdisk = -1;

  file_read_info_file("file:/linuxrc.config", NULL, kf_cfg);

  if(!config.had_segv) {
    if (config.linemode)
      putchar('\n');
    printf(
      ">>> %s installation program v" LXRC_VERSION " (c) 1996-2003 SuSE Linux AG <<<\n",
      config.product
    );
    if (config.linemode)
      putchar('\n');
    fflush(stdout);
  }

  /* just a default for manual mode */
  config.floppies = 1;
  config.floppy_dev[0] = strdup("/dev/fd0");
  if(config.floppydev) {
    sprintf(buf, "/dev/%s", config.floppydev);
    str_copy(&config.floppy_dev[0], buf);
  }

  if((s = getenv("lang"))) {
    i = set_langidbyname(s);
    if(i) config.language = i;
  }

  if(!config.had_segv) {
    ft = file_get_cmdline(key_manual);
    if(ft && ft->is.numeric) {
      config.manual = ft->nvalue;
      auto_ig = 0;
      auto2_ig = config.manual ^ 1;
    }
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
    auto_ig = 0;
    auto2_ig = 1;
    config.manual = 0;
    url = parse_url(config.autoyast);
    if(url && url->scheme) set_instmode(url->scheme);
  }

  ft = file_get_cmdline(key_linemode);
  if(ft && ft->is.numeric) config.linemode = ft->nvalue;

  ft = file_get_cmdline(key_usbwait);
  if(ft && ft->is.numeric) config.usbwait = ft->nvalue;

  ft = file_get_cmdline(key_moduledelay);
  if(ft && ft->is.numeric) config.module.delay = ft->nvalue;

  ft = file_get_cmdline(key_scsibeforeusb);
  if(ft && ft->is.numeric) config.scsi_before_usb = ft->nvalue;

  ft = file_get_cmdline(key_useusbscsi);
  if(ft && ft->is.numeric) config.use_usbscsi = ft->nvalue;

  ft = file_get_cmdline(key_useidescsi);
  if(ft && ft->is.numeric) config.idescsi = ft->nvalue;

  ft = file_get_cmdline(key_lxrcdebug);
  if(ft && ft->is.numeric) config.debug = ft->nvalue;

  ft = file_get_cmdline(key_linuxrc);
  str_copy(&config.linuxrc, ft ? ft->value : getenv("linuxrc"));

  if(config.linuxrc) {
    s = strdup(config.linuxrc);

    for(t0 = s; (t = strsep(&t0, ",")); ) {
      for(i = 0; (unsigned) i < sizeof lxrc_params / sizeof *lxrc_params; i++) {
        if(!strcasecmp(lxrc_params[i].name, t)) {
          switch(lxrc_params[i].key) {
            case lx_auto:
              auto_ig = 1;
              config.manual = 0;
              break;

            case lx_auto2:
              auto2_ig = 1;
              config.manual = 0;
              break;

            case lx_reboot:
              reboot_ig = TRUE;
              break;

            case lx_loadnet:
              config.activate_network = 1;
              break;

            case lx_loaddisk:
              config.activate_storage = 1;
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

  if((s = getenv("LINUXRC_STDERR"))) str_copy(&config.stderr_name, s);
  util_set_stderr(config.stderr_name);

  if(!config.test && !config.had_segv) {
    fprintf(stderr, "Remount of / ");
    i = mount(0, "/", 0, MS_MGC_VAL | MS_REMOUNT, 0);
    fprintf(stderr, i ? "failed\n" : "ok\n");

    /* Check for special case with aborted installation */
    if(util_check_exist ("/.bin")) {
      unlink("/bin");
      rename("/.bin", "/bin");
    }
  }

  util_free_mem();

  if(config.memory.free < config.memory.min_free) {
    config.memory.min_free = config.memory.free;
  }

  force_ri_ig = config.memory.free > config.memory.load_image ? 1 : 0;

  if(util_check_exist("/sbin/modprobe")) has_modprobe = 1;
  lxrc_set_modprobe("/etc/nothing");
  lxrc_set_bdflush(5);

  lxrc_check_console();
  freopen(config.console, "r", stdin);
  freopen(config.console, "a", stdout);

  util_get_splash_status();

  if(util_check_exist("/proc/iSeries")) config.is_iseries = 1;

  kbd_init();
  util_redirect_kmsg();
  disp_init();

  // auto2_chk_expert();

  if (!config.linemode)
    printf("\033[9;0]");		/* screen saver off */
  fflush(stdout);

  info_init();

  ft = file_get_cmdline(key_brokenmodules);
  if(ft && *ft->value) {
    slist_free(config.module.broken);
    config.module.broken = slist_split(',', ft->value);
  }

  mod_init(1);
  util_update_disk_list(NULL, 1);
  util_update_cdrom_list();

  if(!(config.test || config.serial || config.shell_started || config.noshell)) {
    util_start_shell("/dev/tty9", "/bin/lsh", 0);
    config.shell_started = 1;
  }

  if(config.had_segv) util_manual_mode();

#ifdef USE_LIBHD
  if(auto2_ig) {
    if(auto2_init()) {
      auto_ig = 0;
      auto2_ig = 1;
      config.manual = 0;
    } else {
      fprintf(stderr, "Automatic setup not possible.\n");

      util_disp_init();

      i = 0;
      j = 1;
      if(config.insttype == inst_cdrom && cdrom_drives && !config.demo) {
        sprintf(buf, txt_get(TXT_INSERT_CD), 1);
        j = dia_okcancel(buf, YES) == YES ? 1 : 0;
        if(j) {
          util_disp_done();
          i = auto2_find_install_medium();
        }
      }

      if(!i) {
        config.rescue = 0;
        util_manual_mode();
        util_disp_init();
        if(j) {
          sprintf(buf, "Could not find the %s ", config.product);
          if(config.insttype == inst_cdrom) {
            sprintf(buf + strlen(buf),
              "%s CD.", config.demo ? "LiveEval" : "Installation"
            );
          }
          else {
            strcat(buf, "Installation Source.");
          }
          strcat(buf, "\n\nActivating manual setup program.\n");
          dia_message(buf, MSGTYPE_ERROR);
        }
      }
    }
  }
#endif

  /* note: for auto2, file_read_info() is called in auto2_init() */
  if(auto_ig) {
    rename_info_file();
    file_read_info();
  }

  /* for 'manual' */
  if(!config.info.loaded && !config.hwcheck && !config.had_segv) file_read_info();

  if(config.had_segv) {
    char buf[256];

    util_disp_init();
    sprintf(buf,
      "Sorry, linuxrc crashed at address 0x%08"PRIx64".\n\n"
      "Linuxrc has been restarted in manual mode.",
      config.segv_addr
    );
    dia_message(buf, MSGTYPE_ERROR);
  }
  else if(!auto2_ig) {
    util_disp_init();
  }

  /* after we've told the user about the last segv, turn it on... */
  config.restart_on_segv = 1;

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

  if(!(config.serial || config.is_iseries)) {
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
    di_main_hwcheck,
    di_main_reboot,
    di_main_halt,
    di_none
  };

  util_manual_mode();

  di_lxrc_main_menu_last = config.hwcheck ? di_main_hwcheck : di_main_start;

  if(config.hwcheck) {
    items[0] = items[1] = items[2] = items[3] = di_skip;
  }
  else {
    items[4] = di_skip;
  }

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

#if 0
    case di_main_hwcheck:
      util_hwcheck();
      rc = 1;
      break;
#endif

    case di_main_reboot:
      lxrc_reboot();
      break;

    case di_main_halt:
      lxrc_halt();
      break;

    default:
      break;
  }

  return rc;
}


void lxrc_set_modprobe(char *prog)
{
  FILE *f;

  /* do nothing if we have a modprobe */
  if (has_modprobe || config.test) return;

  if((f = fopen("/proc/sys/kernel/modprobe", "w"))) {
    fprintf(f, "%s\n", prog);
    fclose(f);
  }
}


/* Check if we start linuxrc on a serial console. On Intel and
   Alpha, we look if the "console" parameter was used on the
   commandline. On SPARC, we use the result from hardwareprobing. */
void lxrc_check_console()
{
#if !defined(__sparc__) && !defined(__PPC__)

  file_t *ft;

  if(!(ft = file_get_cmdline(key_console)) || !*ft->value) return;

  util_set_serial_console(ft->value);

#else
#ifdef USE_LIBHD

  util_set_serial_console(auto2_serial_console());

#endif
#endif

  if(config.serial) {
    fprintf(stderr,
      "Console: %s, serial line params \"%s\"\n",
      config.console, config.serial
    );
  }
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

  i = mount("shmfs", newroot, "shm", 0, "nr_inodes=10240");
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
    execve("/linuxrc", config.argv, environ);
    perror("/linuxrc");
  }
  else {
    fprintf(stderr, "Oops, pivot_root failed\n");
  }
}


#if SWISS_ARMY_KNIFE
void lxrc_makelinks(char *name)
{
  int i;
  char buf[64];

  if(!util_check_exist("/bin")) mkdir("/bin", 0755);

  if(!util_check_exist("/etc/nothing")) link(name, "/etc/nothing");

  for(i = 0; (unsigned) i < sizeof lxrc_internal / sizeof *lxrc_internal; i++) {
    sprintf(buf, "/bin/%s", lxrc_internal[i].name);
    if(!util_check_exist(buf)) link(name, buf);
  }
}
#endif
