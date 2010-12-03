/*
 *
 * linuxrc.c     Load modules and rootimage to ramdisk
 *
 * Copyright (c) 1996-2008  Hubert Mantel, SuSE Linux AG (mantel@suse.de)
 *
 */

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
#include <errno.h>

#include <hd.h>

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
#include "mkdevs.h"
#include "scsi_rename.h"
#include "hotplug.h"
#include "checkmd5.h"

#if defined(__alpha__) || defined(__ia64__)
#define SIGNAL_ARGS	int signum, int x, struct sigcontext *scp
#else	// __i386__ __x86_64__ __PPC__ __sparc__ __s390__ __s390x__ __MIPSEB__
#define SIGNAL_ARGS	int signum, struct sigcontext scp
#endif

#ifndef MS_MOVE
#define MS_MOVE		(1 << 13)
#endif

#ifndef MNT_DETACH
#define MNT_DETACH	(1 << 1)
#endif

static void lxrc_main_menu     (void);
static void lxrc_catch_signal_11(SIGNAL_ARGS);
static void lxrc_catch_signal  (int signum);
static void lxrc_init          (void);
static int  lxrc_main_cb       (dia_item_t di);
static void lxrc_check_console (void);
static void lxrc_set_bdflush   (int percent_iv);
static int do_not_kill         (char *name);
static void lxrc_change_root   (void);
static void lxrc_reboot        (void);
static void lxrc_halt          (void);
static void lxrc_usr1(int signum);
static int  lxrc_exit_menu     (void);
static int  lxrc_exit_cb       (dia_item_t di);

extern char **environ;
static void lxrc_movetotmpfs(void);
static int cmp_entry(slist_t *sl0, slist_t *sl1);
static int cmp_entry_s(const void *p0, const void *p1);
static void lxrc_add_parts(void);
#if SWISS_ARMY_KNIFE 
static void lxrc_makelinks(char *name);
#endif
// static void config_rescue(char *mp);

#if SWISS_ARMY_KNIFE
int probe_main(int argc, char **argv);
int rmmod_main(int argc, char **argv);

static struct {
  char *name;
  int (*func)(int, char **);
} lxrc_internal[] = {
  { "sh",          util_sh_main          },
  { "lsh",         lsh_main              },
  { "mkdevs",      mkdevs_main           },
  { "rmmod",       rmmod_main            },
  { "lsmod",       util_lsmod_main       },
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
  { "killall",     util_killall_main     },
  { "bootpc",      util_bootpc_main      },
//  { "swapon",      util_swapon_main      },
  { "swapoff",     util_swapoff_main     },
  { "raidautorun", util_raidautorun_main },
  { "free",        util_free_main        },
  { "wget",        util_wget_main        },
  { "fstype",      util_fstype_main      },
  { "scsi_rename", scsi_rename_main      },
  { "lndir",       util_lndir_main       },
  { "extend",      util_extend_main      },
  { "hotplug",     hotplug_main          },
  { "nothing",     util_nothing_main     }
};
#endif

static dia_item_t di_lxrc_main_menu_last;

int main(int argc, char **argv, char **env)
{
  char *prog;
  int err, i, j;

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

  str_copy(&config.product, "SUSE Linux");

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

  if(util_check_exist("/usr/src/packages") || getuid()) {
    printf("Seems we are on a running system; activating testmode...\n");
    config.test = 1;
    str_copy(&config.console, "/dev/tty");
  }

  if(!config.test && !config.had_segv) {
#if SWISS_ARMY_KNIFE 
    lxrc_makelinks(*argv);
#endif
    if(!util_check_exist("/oldroot")) {
      find_shell();
      mount("proc", "/proc", "proc", 0, 0);
      file_do_info(file_get_cmdline(key_tmpfs), kf_cmd + kf_cmd_early);
      file_do_info(file_get_cmdline(key_lxrcdebug), kf_cmd + kf_cmd_early);
      util_free_mem();
      umount("/proc");

      // fprintf(stderr, "free: %d, %d\n", config.memory.free, config.tmpfs);

      if(config.tmpfs && config.memory.free > 24 * 1024) {
        lxrc_movetotmpfs();	/* does not return if successful */
      }

      config.tmpfs = 0;

      if(!config.serial && config.debugwait) {
        util_start_shell("/dev/tty9", "/bin/lsh", 1);
        config.shell_started = 1;
      }
      LXRC_WAIT
    }
  }

  setenv("PATH", "/lbin:/bin:/sbin:/usr/bin:/usr/sbin:/usr/local/bin", 1);

  lxrc_init();

  if(config.rootpassword && !strcmp(config.rootpassword, "ask")) {
    int win_old;

    if(!(win_old = config.win)) util_disp_init();
    str_copy(&config.rootpassword, NULL);
    dia_input2(txt_get(TXT_ROOT_PASSWORD), &config.rootpassword, 20, 1);
    if(!win_old) util_disp_done();
  }

  if(!config.manual) {
    if(config.rescue && !config.serial) {
      int win_old = 1;

      if(
        config.language == lang_undef &&
        !(win_old = config.win)
      ) util_disp_init();
      set_choose_keytable(0);
      if(!win_old) util_disp_done();
      
    }
    err = inst_start_install();
  }
  else {
    err = 99;
  }

  if(err) {
    util_disp_init();

#if 0
    extern int ask_for_swap(int size, char *msg);
    ask_for_swap(-1, "Foo Bar");
#endif

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

  if(dia_yesno(txt_get(TXT_ASK_REBOOT), 1) == YES) {
    reboot(RB_AUTOBOOT);
  }
}

void lxrc_halt()
{
  if(config.test) {
    fprintf(stderr, "*** power off ***\n");
    return;
  }

  if(dia_yesno(txt_get(TXT_HALT), 1) == YES) {
    reboot(RB_POWER_OFF);
  }
}


void lxrc_change_root()
{
  int i;
  char *buf = NULL, *mp = "/mnt", **s;
  slist_t *sl;
  char *argv[3] = { };
  char *dirs[] = {
    "bin", "boot", "etc", "home", "lib", "lib/firmware",
    "media", "mounts", "mounts/initrd", "mnt", "proc", "sbin",
    "sys", "tmp", "usr", "usr/lib", "usr/lib/microcode", "var",
    NULL
  };

  if(config.test) return;

  umount(mp);

  if(
    config.rescue &&
    config.url.instsys &&
    (mp = config.url.instsys->mount)
  ) {
    fprintf(stderr, "starting rescue\n");

    // add dud images
    for(i = 0; i < config.update.ext_count; i++) {
      sl = slist_add(&config.url.instsys_list, slist_new());
      str_copy(&sl->value, new_mountpoint());
      strprintf(&sl->key, "%s/dud_%04u", config.download.base, i);
      util_mount_ro(sl->key, sl->value, NULL);
    }

    // first, some directories
    for(s = dirs; *s; s++) {
      strprintf(&buf, "%s/%s", mp, *s);
      mkdir(buf, 0755);
      fprintf(stderr, "mkdir %s\n", buf);
      if(!strcmp(*s, "tmp")) chmod(buf, 01777);
    }

    // move module tree
    strprintf(&buf, "%s/lib/modules", mp);
    rename("/lib/modules", buf);

    // move firmware tree
    strprintf(&buf, "%s/lib/firmware", mp);
    rename("/lib/firmware", buf);

    // move 'parts' tree
    strprintf(&buf, "%s/parts", mp);
    rename("/parts", buf);

    // add devices
    strprintf(&buf, "%s/dev", mp);
    rename("/dev", buf);

    // keep initrd available
    strprintf(&buf, "%s/mounts/initrd", mp);
    mount("/", buf, "none", MS_BIND, 0);

    // add rescue images
    for(sl = config.url.instsys_list; sl; sl = sl->next) {
      argv[1] = sl->value;
      argv[2] = mp;
      util_lndir_main(3, argv);
    }

    // move image mountpoints
    for(sl = config.url.instsys_list; sl; sl = sl->next) {
      strprintf(&buf, "%s%s", mp, sl->value);
      mkdir(buf, 0755);
      mount(sl->value, buf, "none", MS_BIND, 0);
      umount(sl->value);
    }

    // config_rescue(mp);
  }
  else if(config.new_root) {
    fprintf(stderr, "starting %s\n", config.new_root);

    util_mount_ro(config.new_root, mp, NULL);
  }

  chdir(mp);

  mount(".", "/", NULL, MS_MOVE, NULL);
  chroot(".");

  if(config.rescue) {
    system("/mounts/initrd/bin/prepare_rescue");

    // system("PS1='\\w # ' /bin/bash 2>&1");

    if(!config.debug) {
      umount("/mounts/initrd");
      rmdir("/mounts/initrd");
    }
  }

  execl("/sbin/init", "init", NULL);

  perror("init failed\n");

  chdir("/");
  umount(mp);
  fprintf(stderr, "system start failed\n");

  LXRC_WAIT
}


/*
 * Copy root tree into a tmpfs tree, make it '/' and exec() the new
 * linuxrc.
 */
void lxrc_movetotmpfs()
{
  int i;
  char *newroot = "/.newroot";

  fprintf(stderr, "Moving into tmpfs...");
  i = mkdir(newroot, 0755);
  if(i) {
    perror(newroot);
    return;
  }

  i = mount("tmpfs", newroot, "tmpfs", 0, "size=0,nr_inodes=0");
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

  system("/bin/rm -r /lib /dev /bin /sbin /usr /etc /init /lbin");

  if(chdir(newroot)) perror(newroot);

  if(mkdir("oldroot", 0755)) perror("oldroot");

  mount(".", "/", NULL, MS_MOVE, NULL);
  chroot(".");

  /* put / entry back into /proc/mounts */
  mount("/", "/", "none", MS_BIND, 0);

  for(i = 0; i < 20; i++) close(i);

  open("/dev/console", O_RDWR);
  dup(0);
  dup(0);
  execve("/init", config.argv, environ);

  perror("/init");
}


void lxrc_end()
{
  if(config.netstop) {
    LXRC_WAIT

    net_stop();
  }

  LXRC_WAIT

  lxrc_killall(1);

  fprintf(stderr, "all killed\n");

//  while(waitpid(-1, NULL, WNOHANG) == 0);

  fprintf(stderr, "all done\n");

  /* screen saver on */
  if(!config.linemode) printf("\033[9;15]");

  lxrc_set_modprobe("/sbin/modprobe");
  lxrc_set_bdflush(40);

  LXRC_WAIT

  if(!config.test) {
    util_umount("/dev/pts");
    util_umount("/sys");
    util_umount("/proc/bus/usb");
    if (!config.rescue)
        util_umount_all_devices ();
    util_umount("/proc");
  }

  disp_cursor_on();
  kbd_end(1);
  disp_end();

  lxrc_change_root();
}

/*
 * Check if pid is either portmap or rpciod.
 *
 */
int do_not_kill(char *name)
{
  static char *progs[] = {
    "portmap", "rpciod", "lockd", "lsh", "dhcpcd", "cifsd", "mount.smbfs", "udevd",
    "mount.ntfs-3g", "brld", "sbl"
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
    kbd_end(1);		/* restore terminal settings */
    execl(*config.argv, "init", "segv", addr, state, NULL);
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
  char buf[256];

  if(txt_init()) {
    printf("Linuxrc error: Corrupted texts!\n");
    exit(-1);
  }

  siginterrupt(SIGALRM, 1);
  signal(SIGHUP, SIG_IGN);
  siginterrupt(SIGBUS, 1);
  siginterrupt(SIGINT, 1);
  siginterrupt(SIGTERM, 1);
  siginterrupt(SIGSEGV, 1);
  siginterrupt(SIGPIPE, 1);
  lxrc_catch_signal(0);
  signal(SIGUSR1, lxrc_usr1);

/*  reboot (RB_DISABLE_CAD); */

  umask(022);

  if(!config.test) {
    if(config.had_segv) {
      umount("/dev/pts");
      umount("/sys");
      umount("/proc");
    }
    mount("proc", "/proc", "proc", 0, 0);
    mount("sysfs", "/sys", "sysfs", 0, 0);
    mount("devpts", "/dev/pts", "devpts", 0, 0);
  }

  /* add cmdline to info file */
  config.info.add_cmdline = 1;

  config.module.dir = strdup(config.test ? "/tmp/modules" : "/modules");
  config.update.dst = strdup(config.test ? "/tmp/update" : "/update");

  config.download.base = strdup(config.test ? "/tmp/download" : "/download");
  mkdir(config.download.base, 0755);

  /* must end with '/' */
  config.mountpoint.base = strdup(config.test ? "/tmp/mounts/" : "/mounts/");

  strprintf(&config.mountpoint.instsys, "%sinstsys", config.mountpoint.base);
  strprintf(&config.mountpoint.swap, "%sswap", config.mountpoint.base);
  strprintf(&config.mountpoint.update, "%supdate", config.mountpoint.base);

  config.mountpoint.instdata = strdup("/var/adm/mount");

  config.setupcmd = strdup("setctsid `showconsole` inst_setup yast");

  config.update.map = calloc(1, MAX_UPDATES);

  config.zenconfig = strdup("settings.txt");

  util_set_product_dir("suse");

  str_copy(&config.net.dhcpfail, "show");

  config.net.bootp_timeout = 10;
  config.net.dhcp_timeout = 60;
  config.net.tftp_timeout = 10;
  config.net.ifconfig = 1;
  config.net.ipv4 = 1;
  config.net.setup = NS_DEFAULT;
  config.net.nameservers = 1;

  config.explode_win = 1;
  config.color = 2;
  config.net.use_dhcp = 1;
  config.addswap = 1;
  config.netstop = 1;
  config.usbwait = 4;		/* 4 seconds */
  config.escdelay = 100;	/* 100 ms */
  config.utf8 = 1;
  config.kbd_fd = -1;
  config.ntfs_3g = 1;
  config.secure = 1;
  config.squash = 1;
  config.kexec_reboot = 1;
  config.efi = -1;
  config.udev_mods = 1;

  config.scsi_rename = 0;
  config.scsi_before_usb = 1;

  // default memory limits
  config.memory.min_free =       12 * 1024;
  config.memory.min_yast =      224 * 1024;
  config.memory.load_image =    350 * 1024;

  config.swap_file_size = 1024;		/* 1024 MB */

  str_copy(&config.namescheme, "by-id");

  file_do_info(file_get_cmdline(key_lxrcdebug), kf_cmd + kf_cmd_early);

  LXRC_WAIT

  if(!config.had_segv) {
    lxrc_add_parts();
    // we need edd for udev
    if(util_check_exist("/modules/edd.ko")) {
      system("/sbin/insmod /modules/edd.ko");
    }
  }

  LXRC_WAIT

  if(util_check_exist("/sbin/mount.smbfs")) {
    str_copy(&config.net.cifs.binary, "/sbin/mount.smbfs");
    str_copy(&config.net.cifs.module, "smbfs");
  }

  if(util_check_exist("/sbin/mount.cifs")) {
    str_copy(&config.net.cifs.binary, "/sbin/mount.cifs");
    str_copy(&config.net.cifs.module, "cifs");
  }

  /* make auto mode default */
  if(config.had_segv) {
    config.manual = 1;
  }

#if defined(__s390__) || defined(__s390x__)
  config.initrd_has_ldso = 1;
#endif

  file_read_info_file("file:/linuxrc.config", kf_cfg);

  if(!config.had_segv) {
    if (config.linemode)
      putchar('\n');
    printf(
      "\n>>> %s installation program v" LXRC_FULL_VERSION " (c) 1996-2010 SUSE Linux Products GmbH <<<\n",
      config.product
    );
    if (config.linemode)
      putchar('\n');
    fflush(stdout);
  }

  get_ide_options();
  file_read_info_file("cmdline", kf_cmd_early);

  util_redirect_kmsg();

  util_setup_udevrules();

  if(!config.udev_mods) {
    system("cp /lib/udev/80-drivers.rules.no_modprobe /lib/udev/rules.d/80-drivers.rules");
  }

  if(config.staticdevices) {
    util_mkdevs();
  }
  else if(!config.test) {
    fprintf(stderr, "Starting udev... ");
    fflush(stderr);
    system("/bin/myudevstart >/dev/null 2>&1");
    fprintf(stderr, "ok\n");
    unlink("/devz");	/* cf. util_mkdevs() */
  }

  util_set_stderr(config.stderr_name);

  if(config.had_segv) config.manual = 1;

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

  if(!config.download.instsys_set) {
    config.download.instsys = config.memory.free > config.memory.load_image ? 1 : 0;
  }

  lxrc_set_modprobe("/etc/nothing");
  lxrc_set_bdflush(5);

  lxrc_check_console();
  if(!config.test) {
    freopen(config.console, "r", stdin);
    freopen(config.console, "a", stdout);
  }

  util_get_splash_status();

  util_splash_bar(10, SPLASH_10);

  if(util_check_exist("/proc/iSeries")) {
    config.is_iseries = 1;
    config.linemode = 1;
  }

  kbd_init(1);
  util_redirect_kmsg();
  disp_init();

  // clear keyboard queue
  while(kbd_getch_old(0));

  set_activate_language(config.language);

  // auto2_chk_expert();

  if (!config.linemode)
    printf("\033[9;0]");		/* screen saver off */
  fflush(stdout);

  info_init();

  read_iscsi_ibft();

  printf("Loading basic drivers...");
  fflush(stdout);
  mod_init(1);
  printf(" ok\n");
  fflush(stdout);

  /* look for driver updates in initrd */
  util_chk_driver_update("/", "/");

  util_update_disk_list(NULL, 1);
  util_update_cdrom_list();

  if(!(config.test || config.serial || config.shell_started || config.noshell)) {
    util_start_shell("/dev/tty9", "/bin/lsh", 1);
    config.shell_started = 1;
  }

  file_read_info_file("cmdline", kf_cmd1);

  if(config.had_segv) config.manual = 1;

  /* check efi status */
  if(util_check_exist("/sys/firmware/efi/vars") == 'd') {
    config.efi_vars = 1;
  }

  /* get usb keyboard working */
  if(config.manual == 1 && !config.had_segv) util_load_usb();

#if defined(__s390__) || defined(__s390x__)
  /* activate boot FCP adapter */
  {
    char ipl_type[40];
    char device[40];
    char wwpn[40];
    char lun[40];
    char cmd[200];
    
    if(util_read_and_chop("/sys/firmware/ipl/ipl_type", ipl_type, sizeof ipl_type))
    {
      if(strcmp(ipl_type,"fcp")==0)
      {
        mod_modprobe("zfcp","");
        if(util_read_and_chop("/sys/firmware/ipl/device", device, sizeof device))
        {
          sprintf(cmd,"/sbin/zfcp_host_configure %s 1",device);
          fprintf(stderr,"executing %s\n",cmd);
          if(!config.test) system(cmd);
          if(util_read_and_chop("/sys/firmware/ipl/wwpn", wwpn, sizeof wwpn))
          {
            if(util_read_and_chop("/sys/firmware/ipl/lun", lun, sizeof lun))
            {
              sprintf(cmd,"/sbin/zfcp_disk_configure %s %s %s 1",device,wwpn,lun);
              fprintf(stderr,"executing %s\n",cmd);
              if(!config.test) system(cmd);
            }
          }
        }
      }
      else fprintf(stderr,"not booted via FCP\n");
    }
    else fprintf(stderr,"could not read /sys/firmware/ipl/ipl_type\n");
  }
#endif

#if defined(__powerpc__)
  /*
   * The network interface on the PS3 uses the same MAC address for both
   * eth0 and wlan0. hwinfo needs to know the kernel device name to distinguish
   * between the two interfaces. Because the sysfs directory are only created
   * if the driver is loaded, we force loading ps3_gelic because linuxrc would
   * load it anyway if hwinfo would find a network card.
   * Also enable swap to videoram on PS3.
   *
   * On Pegasos2, mv643xx_eth will create several files in sysfs. Only one of
   * them points to the real network interface. To keep the interface ordering
   * stable, load 100MBit before 1000MBit.
   */
  {
    const char cmd[] = "s=/dev/mtdblock0;udevsettle --timeout=3;mkswap -L ps3_vram_swap $s&&swapon -p 42 $s";
    char buf[16];

    if(util_read_and_chop("/proc/device-tree/model", buf, sizeof buf))
    {
      if(strcmp(buf,"SonyPS3")==0)
      {
        fprintf(stderr,"loading ps3vram, mtdblock and ps3_gelic\n");
        mod_modprobe("ps3vram","");
        mod_modprobe("mtdblock","");
        mod_modprobe("ps3_gelic","");
        if(!config.test && util_check_exist("/sys/block/mtdblock0"))
        {
          fprintf(stderr,"executing %s\n",cmd);
          system(cmd);
	}
      } else if(strcmp(buf,"Pegasos2")==0)
      {
        fprintf(stderr,"preloading via-rhine, loading mv643xx_eth\n");
        mod_modprobe("via-rhine","");
        mod_modprobe("mv643xx_eth","");
      }
    }
  }
#endif

  if(config.memory.ram_min && !config.had_segv) {
    int window = config.win, ram;
    char *msg = NULL;

    util_get_ram_size();

    ram = config.memory.ram_min - config.memory.ram_min / 8;

    if(config.memory.ram < ram) {
      if(!window) util_disp_init();
      strprintf(&msg, txt_get(TXT_NO_RAM), config.product, config.memory.ram_min);
      dia_message(msg, MSGTYPE_REBOOT);
      free(msg);
      if(!window) util_disp_done();
      if(config.memory.ram_min) {
        config.manual = 1;
        if(config.test) {
          fprintf(stderr, "*** reboot ***\n");
        }
        else {
          reboot(RB_AUTOBOOT);
        }
      }
    }
  }

  net_setup_localhost();

  if(config.manual) file_read_info_file("cmdline", kf_cmd);

  if(config.braille.check) run_braille();

  if(!config.manual && !auto2_init()) {
    fprintf(stderr, "Automatic setup not possible.\n");

    util_disp_init();

    i = 0;
    j = 1;
#if 1
    if(util_check_exist("/nextmedia") == 'r') {
      config.cd1texts = file_parse_xmllike("/nextmedia", "text");  
    }

    if(config.url.install && config.url.install->is.cdrom) {
      char *s = get_translation(config.cd1texts, current_language()->locale);
      char *buf = NULL;

      strprintf(&buf, s ?: txt_get(TXT_INSERT_CD), 1);
      do {
        j = dia_okcancel(buf, YES) == YES ? 1 : 0;
        if(j) {
          config.manual = 0;
          i = auto2_find_repo();
        }
      } while(!i && j);

      free(buf);
    }
#endif

    if(!i) {
      config.rescue = 0;
      config.manual |= 1;
      if(j) {
        sprintf(buf, "Could not find the %s ", config.product);
        strcat(buf, "Repository.");
        strcat(buf, "\n\nActivating manual setup program.\n");
        dia_message(buf, MSGTYPE_ERROR);
      }
    }
    else {
      util_disp_done();
    }
  }

  set_activate_language(config.language);

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
  else if(config.manual) {
    util_disp_init();
  }

  /* after we've told the user about the last segv, turn it on... */
  config.restart_on_segv = 1;

  if(!config.language && config.win && !config.linemode) set_choose_language();

  util_print_banner();

  if(!config.color) {
    int old_win = config.win;

    if(!old_win) util_disp_init();
    set_choose_display();
    if(old_win) util_print_banner(); else util_disp_done();
  }

  if(!(config.serial || config.is_iseries || config.linemode)) {
    set_choose_keytable(0);
  }

  util_update_kernellog();

#if !(defined(__PPC__) || defined(__sparc__))
  if(config.manual || reboot_wait_ig) {
    config.rebootmsg = 1;
  }
#endif
}

void lxrc_main_menu()
{
  dia_item_t di;
  dia_item_t items[] = {
    di_main_start,
    di_main_settings,
    di_main_expert,
    di_main_exit,
    di_none
  };

  config.manual |= 1;

  di_lxrc_main_menu_last = di_main_start;

  for(;;) {
    di = dia_menu2(txt_get(TXT_HDR_MAIN), 38, lxrc_main_cb, items, di_lxrc_main_menu_last);

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
    case di_main_start:
      rc = inst_menu();
      break;

    case di_main_settings:
      rc = set_settings();
      if(rc == di_set_lang || rc == di_set_display)
        rc = 0;
      else if(rc == di_none)
        rc = 1;
      break;

    case di_main_expert:
      set_expert_menu();
      break;

    case di_main_exit:
      lxrc_exit_menu();
      break;

    default:
      break;
  }

  return rc;
}

int lxrc_exit_menu()
{
  dia_item_t items[] = {
    di_exit_reboot,
    di_exit_halt,
    di_none
  };

return dia_menu2(txt_get(TXT_END_REBOOT), 30, lxrc_exit_cb, items, 1);
}

int lxrc_exit_cb (dia_item_t di)
{
  switch(di) {
    case di_exit_reboot:
      lxrc_reboot();
      break;

    case di_exit_halt:
      lxrc_halt();
      break;

    default:
      break;
    }

return (1);
}

void lxrc_set_modprobe(char *prog)
{
  FILE *f;

  /* do nothing if we have a modprobe */
  if(config.test || !config.nomodprobe) return;

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
  util_set_serial_console(auto2_serial_console());

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


#if SWISS_ARMY_KNIFE
void lxrc_makelinks(char *name)
{
  int i;
  char buf[64];

  if(!util_check_exist("/lbin")) mkdir("/lbin", 0755);

  if(!util_check_exist("/etc/nothing")) link(name, "/etc/nothing");

  for(i = 0; (unsigned) i < sizeof lxrc_internal / sizeof *lxrc_internal; i++) {
    sprintf(buf, "/lbin/%s", lxrc_internal[i].name);
    if(!util_check_exist(buf)) link(name, buf);
  }
  if(!util_check_exist("/sbin/hotplug")) link(name, "/sbin/hotplug");
}
#endif


/*
 * Ensure /bin/sh points to some shell.
 */
void find_shell()
{
  if(util_check_exist("/bin/sh") == 'r') return;
  if(!util_check_exist("/bin")) mkdir("/bin", 0755);

  if(util_check_exist("/bin") == 'd') {
    unlink("/bin/sh");
    symlink("/lbin/sh", "/bin/sh");
  }
}


#if 0
/*
 * Configure rescue system mounted at mp.
 */
void config_rescue(char *mp)
{
  char *s = NULL;
  FILE *f;

  /* add getty entry for /dev/console */
  if(config.serial) {
    strprintf(&s, "%s/etc/securetty", mp);
    if((f = fopen(s, "a"))) {
      fprintf(f, "console\n");
      fclose(f);
    }

    strprintf(&s, "%s/etc/inittab", mp);
    if((f = fopen(s, "a"))) {
      fprintf(f, "c:2345:respawn:/sbin/mingetty --noclear console\n");
      fclose(f);
    }
  }

}

#endif


void lxrc_usr1(int signum)
{
  static unsigned extend_cnt = 0;
  int i, err = 0;
  char *s, buf[1024];
  FILE *f;
  slist_t *sl = NULL, *sl_task = NULL;
  char task = 0, *ext = NULL;
  int extend_pid = 0;

  if(!rename("/tmp/extend.job", s = new_download())) {
    *buf = 0;
    f = fopen(s, "r");
    if(f) {
      if(!fgets(buf, sizeof buf, f)) *buf = 0;
      if(*buf) {
        sl_task = slist_split(' ', buf);
        extend_pid = atoi(sl_task->key);
        if(sl_task->next) {
          task = *sl_task->next->key;
          if(sl_task->next->next) ext = sl_task->next->next->key;
        }
      }
      fclose(f);
    }
    unlink(s);
    if((task == 'a' || task == 'r') && ext) {
      sl = slist_getentry(config.extend_list, ext);
      if(task == 'a' && !sl) {
        slist_append_str(&config.extend_list, ext);
      }
      else if(task == 'r' && sl) {
        str_copy(&sl->key, NULL);
      }
      if(!fork()) {
        for(i = 0; i < 256; i++) close(i);
        open("/tmp/extend.log", O_RDWR | O_CREAT | O_TRUNC, 0644);
        dup(0);
        dup(0);
        setlinebuf(stderr);
        config.download.cnt = 1000 + extend_cnt;
        config.mountpoint.cnt = 1000 + extend_cnt;
        if(!config.debug) config.debug = 1;

        config.keepinstsysconfig = 1;

        if(task == 'a' && sl) {
          fprintf(stderr, "instsys extend: add %s\n%s: already added\n", ext, ext);
          err = 0;
        }
        else if(task == 'r' && !sl) {
          fprintf(stderr, "instsys extend: remove %s\n%s: not there\n", ext, ext);
          err = 0;
        }
        else if(task == 'a') {
          err = auto2_add_extension(ext);
        }
        else if(task == 'r') {
          err = auto2_remove_extension(ext);
        }
        f = fopen("/tmp/extend.result", "w");
        if(f) fprintf(f, "%d\n", err);
        fclose(f);
        if(extend_pid > 0) kill(extend_pid, SIGUSR1);
        exit(0);
      }
    }
  }

  slist_free(sl_task);

  extend_cnt += 10;

  signal(SIGUSR1, lxrc_usr1);
}


int cmp_entry(slist_t *sl0, slist_t *sl1)
{
  return strcmp(sl0->key, sl1->key);
}


/* wrapper for qsort */
int cmp_entry_s(const void *p0, const void *p1)
{
  slist_t **sl0, **sl1;

  sl0 = (slist_t **) p0;
  sl1 = (slist_t **) p1;

  return cmp_entry(*sl0, *sl1);
}


void lxrc_add_parts()
{
  struct dirent *de;
  DIR *d;
  slist_t *sl0 = NULL, *sl;
  char *mp = NULL, *argv[3] = { };
  int insmod_done = 0;

  if((d = opendir("/parts"))) {
    while((de = readdir(d))) {
      if(util_check_exist2("/parts", de->d_name) == 'r') {
        sl = slist_append(&sl0, slist_new());
        strprintf(&sl->key, "/parts/%s", de->d_name);
      }
    }
    closedir(d);
  }

  sl0 = slist_sort(sl0, cmp_entry_s);

  for(sl = sl0; sl; sl = sl->next) {
    fprintf(stderr, "Integrating %s\n", sl->key);
    if(!config.test) {
      if(!insmod_done) {
        insmod_done = 1;
        system("/sbin/insmod /modules/loop.ko max_loop=64");
      }
      strprintf(&mp, "/parts/mp_%04u", config.mountpoint.initrd_parts++);
      mkdir(mp, 0755);
      util_mount_ro(sl->key, mp, NULL);
      argv[1] = mp;
      argv[2] = "/";
      util_lndir_main(3, argv);
    }
  }

  slist_free(sl0);
  free(mp);
}


void lxrc_readd_parts()
{
  char *mp = NULL, *argv[3] = { };
  unsigned u;

  if(config.test) return;

  for(u = 0; u < config.mountpoint.initrd_parts; u++) {
    strprintf(&mp, "/parts/mp_%04u", u);
    argv[1] = mp;
    argv[2] = "/";
    util_lndir_main(3, argv);
  }

  free(mp);
}


