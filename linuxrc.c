/*
 *
 * linuxrc.c     Load modules and rootimage to ramdisk
 *
 * Copyright (c) 1996-2004  Hubert Mantel, SuSE Linux AG (mantel@suse.de)
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
#include "multiple_info.h"
#include "mkdevs.h"
#include "scsi_rename.h"
#include "hotplug.h"
#include "checkmd5.h"

#if defined(__alpha__) || defined(__ia64__)
#define SIGNAL_ARGS	int signum, int x, struct sigcontext *scp
#else	// __i386__ __x86_64__ __PPC__ __sparc__ __s390__ __s390x__ __MIPSEB__
#define SIGNAL_ARGS	int signum, struct sigcontext scp
#endif

#define pivot_root(a, b) syscall(SYS_pivot_root, a, b)

#ifndef MS_MOVE
#define MS_MOVE		(1 << 13)
#endif

#ifndef MNT_DETACH
#define MNT_DETACH	(1 << 1)
#endif

static void lxrc_main_menu     (void);
static void lxrc_catch_signal_11(SIGNAL_ARGS);
// static void sig_chld(int signum);
static void lxrc_catch_signal  (int signum);
static void lxrc_init          (void);
static int  lxrc_main_cb       (dia_item_t di);
static void lxrc_check_console (void);
static void lxrc_set_bdflush   (int percent_iv);
static int do_not_kill         (char *name);
static void lxrc_change_root   (void);
static void lxrc_change_root2  (void);
static void lxrc_reboot        (void);
static void lxrc_halt          (void);

extern char **environ;
static void lxrc_movetotmpfs(void);
static void lxrc_movetotmpfs2(void);
#if SWISS_ARMY_KNIFE 
static void lxrc_makelinks(char *name);
#endif

#if SWISS_ARMY_KNIFE
int probe_main(int argc, char **argv);
int rmmod_main(int argc, char **argv);
int smbmnt_main(int argc, char **argv);

static struct {
  char *name;
  int (*func)(int, char **);
} lxrc_internal[] = {
  { "sh",          util_sh_main          },
  { "lsh",         lsh_main              },
  { "mkdevs",      mkdevs_main           },
  { "rmmod",       rmmod_main            },
  { "lsmod",       util_lsmod_main       },
  { "smbmnt",      smbmnt_main           },
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
  { "swapon",      util_swapon_main      },
  { "swapoff",     util_swapoff_main     },
  { "freeramdisk", util_freeramdisk_main },
  { "raidautorun", util_raidautorun_main },
  { "free",        util_free_main        },
  { "wget",        util_wget_main        },
  { "fstype",      util_fstype_main      },
  { "scsi_rename", scsi_rename_main      },
  { "lndir",       util_lndir_main       },
  { "hotplug",     hotplug_main		 },
  { "nothing",     util_nothing_main     }
};
#endif

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

  if(!strcmp(prog, "init")) config.initramfs = 1;

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
      ft = file_get_cmdline(key_tmpfs);
      if(ft && ft->is.numeric) {
        config.tmpfs = ft->nvalue;
        tmpfs_opt = 1;
      }
      ft = file_get_cmdline(key_debugwait);
      if(ft && ft->is.numeric) config.debugwait = ft->nvalue;
      util_free_mem();
      umount("/proc");

      // fprintf(stderr, "free: %d, %d, %d\n", config.memory.free, config.tmpfs, tmpfs_opt);

      if(!config.initramfs) {
        if(config.tmpfs && (config.memory.free > 24 * 1024 || tmpfs_opt)) {
          lxrc_movetotmpfs();	/* does not return if successful */
        }
        config.tmpfs = 0;
      }
      else {
        if(config.tmpfs && (config.memory.free > 24 * 1024 || tmpfs_opt)) {
          lxrc_movetotmpfs2();	/* does not return if successful */
        }
        config.tmpfs = 0;
      }

      if(!config.serial && config.debugwait) {
        util_start_shell("/dev/tty9", "/bin/bash", 1);
        config.shell_started = 1;
      }
      deb_wait;
    }
    else {
      // umount and release /oldroot
      if(!config.initramfs) {
        umount("/oldroot");
        util_free_ramdisk("/dev/ram0");
      }
    }
  }

  setenv("PATH", "/lbin:/bin:/sbin:/usr/bin:/usr/sbin", 1);

  lxrc_init();

  if(config.rootpassword && !strcmp(config.rootpassword, "ask")) {
    int win_old;

    if(!(win_old = config.win)) util_disp_init();
    str_copy(&config.rootpassword, NULL);
    dia_input2(txt_get(TXT_ROOT_PASSWORD), &config.rootpassword, 20, 1);
    if(!win_old) util_disp_done();
  }

  if(config.demo) {
    err = inst_start_demo();
  }
  else if(!config.manual) {
    if(config.rescue) {
      int win_old = 1;

      if(
        config.language == lang_undef &&
        !(win_old = config.win)
      ) util_disp_init();
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


void lxrc_change_root2()
{
  int fd;

  if(config.test) return;

  fprintf(stderr, "starting %s\n", config.new_root);

  umount("/mnt");
  if(config.rescue) {
    if(util_mount_rw(config.new_root, "/mnt")) {
      util_mount_ro(config.new_root, "/mnt");
    }
    fd = open("/mnt/tmp/xxx", O_WRONLY|O_CREAT, 0644);
    if(fd >= 0) {
      close(fd);
      unlink("/mnt/tmp/xxx");
    }
    else {
      util_mount_ro(config.new_root, "/mnt/mnt");

      mount("tmpfs", "/mnt/tmp", "tmpfs", 0, "size=0,nr_inodes=0");
      chmod("/mnt/tmp", 01777);

      mount("tmpfs", "/mnt/var", "tmpfs", 0, "size=0,nr_inodes=0");
      util_do_cp("/mnt/mnt/var", "/mnt/var");
      chmod("/mnt/var", 0755);

      mount("tmpfs", "/mnt/etc", "tmpfs", 0, "size=0,nr_inodes=0");
      util_do_cp("/mnt/mnt/etc", "/mnt/etc");
      chmod("/mnt/etc", 0755);

      umount("/mnt/mnt");
    }
  }
  else{
    util_mount_ro(config.new_root, "/mnt");
  }
  chdir("/mnt");

  if(config.rescue && util_check_exist("/mnt/lib/modules") == 'd') {
    mount("/lib/modules", "/mnt/lib/modules", "none", MS_BIND, 0);
  }

  mount(".", "/", NULL, MS_MOVE, NULL);
  chroot(".");

  execl("/sbin/init", "init", NULL);

  perror("init failed\n");

  chdir("/");
  umount("/mnt");
  fprintf(stderr, "system start failed\n");

  deb_wait;
}


/*
 * Copy root tree into a tmpfs tree, pivot_root() there and
 * exec() the new linuxrc.
 */
void lxrc_movetotmpfs2()
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
  FILE *f;

  if(config.netstop) {
    util_debugwait("shut down network");
    net_stop();
  }

  util_debugwait("kill remaining procs");

  lxrc_killall(1);

  fprintf(stderr, "all killed\n");

//  while(waitpid(-1, NULL, WNOHANG) == 0);

  fprintf(stderr, "all done\n");

  /* screen saver on */
  if(!config.linemode) printf("\033[9;15]");

/*    reboot (RB_ENABLE_CAD); */
  mod_free_modules();

  util_umount(config.mountpoint.extra);
  util_umount(config.mountpoint.instdata);

  lxrc_set_modprobe("/sbin/modprobe");
  lxrc_set_bdflush(40);

  deb_str(config.new_root);

  util_debugwait("leaving now");

  if(!config.test) {
    if(config.new_root && (config.pivotroot || config.initramfs)) {
      // tell kernel root is /dev/ram0, prevents remount after initrd
      if(!(f = fopen ("/proc/sys/kernel/real-root-dev", "w"))) return;
      fprintf(f, "256\n");
      fclose(f);
    }

    util_umount("/dev/pts");
    util_umount("/sys");
    util_umount("/proc/bus/usb");
    util_umount("/proc");
  }

  disp_cursor_on ();
  kbd_end (1);
  disp_end ();

  if(config.new_root && config.initramfs) lxrc_change_root2();
  if(config.new_root && config.pivotroot) lxrc_change_root();
}

/*
 * Check if pid is either portmap or rpciod.
 *
 */
int do_not_kill(char *name)
{
  static char *progs[] = {
    "portmap", "rpciod", "lockd", "lsh", "dhcpcd", "cifsd", "mount.smbfs"
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
    execl(*config.argv, config.initramfs ? "init" : "linuxrc", "segv", addr, state, NULL);
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


#if 0
void sig_chld(int signum)
{
  while(waitpid(-1, NULL, WNOHANG) && errno != ECHILD);
}
#endif

void lxrc_init()
{
  int i, j;
  char buf[256];
//  pid_t lxrc_pid;

  if(txt_init()) {
    printf("Linuxrc error: Corrupted texts!\n");
    exit(-1);
  }

#if 0
  if(!config.test && (lxrc_pid = fork())) {
    if(lxrc_pid != -1) {
      signal(SIGCHLD, sig_chld);

      waitpid(lxrc_pid, NULL, 0);
    }

    exit(0);
  }
#endif

  siginterrupt(SIGALRM, 1);
  signal(SIGHUP, SIG_IGN);
  siginterrupt(SIGBUS, 1);
  siginterrupt(SIGINT, 1);
  siginterrupt(SIGTERM, 1);
  siginterrupt(SIGSEGV, 1);
  siginterrupt(SIGPIPE, 1);
  lxrc_catch_signal(0);
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

  /* make it configurable? */
  config.module.dir = strdup("/modules");
  config.mountpoint.floppy = strdup("/mounts/floppy");
  config.mountpoint.ramdisk2 = strdup("/mounts/ramdisk2");
  config.mountpoint.extra = strdup("/mounts/extra");
  config.mountpoint.instsys = strdup("/mounts/instsys");
  config.mountpoint.instsys2 = strdup("/mounts/instsys2");
  config.mountpoint.live = strdup("/mounts/live");
  config.mountpoint.update = strdup("/mounts/update");
  config.mountpoint.instdata = strdup("/var/adm/mount");

  config.setupcmd = strdup("setctsid `showconsole` inst_setup yast");
  config.update.dst = strdup("/update");

  config.update.map = calloc(1, MAX_UPDATES);

  config.zenconfig = strdup("settings.txt");

  util_set_product_dir("suse");

  config.net.bootp_timeout = 10;
  config.net.dhcp_timeout = 60;
  config.net.tftp_timeout = 10;
  config.net.ifconfig = 1;
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

  config.hwdetect = 1;

  config.scsi_rename = 1;
  config.activate_storage = 1;		/* together with scsi_rename */

  config.module.disks = 1;

  // default memory limits for i386 version
  config.memory.min_free =       12 * 1024;
  config.memory.min_yast_text =  32 * 1024;
  config.memory.min_yast =       40 * 1024;
  config.memory.min_modules =    64 * 1024;
  config.memory.load_image =    200 * 1024;

  if(util_check_exist("/sbin/mount.smbfs")) {
    str_copy(&config.net.cifs.binary, "/sbin/mount.smbfs");
    str_copy(&config.net.cifs.module, "smbfs");
  }

  if(util_check_exist("/sbin/mount.cifs")) {
    str_copy(&config.net.cifs.binary, "/sbin/mount.cifs");
    str_copy(&config.net.cifs.module, "cifs");
  }

  /* make auto mode default */
  if(config.test || config.had_segv) {
    config.manual = 1;
  }

#if defined(__s390__) || defined(__s390x__)
  config.initrd_has_ldso = 1;
#endif

  if(config.tmpfs && util_check_exist("/download") == 'd') {
    for(i = 0; i < sizeof config.ramdisk / sizeof *config.ramdisk; i++) {
      sprintf(buf, "/download/image%d", i);
      str_copy(&config.ramdisk[i].dev, buf);
    }
  }
  else {
    for(i = 0; i < sizeof config.ramdisk / sizeof *config.ramdisk; i++) {
      sprintf(buf, "/dev/ram%d", i + 2);
      str_copy(&config.ramdisk[i].dev, buf);
    }
  }

  config.inst_ramdisk = -1;
  config.module.ramdisk = -1;

  file_read_info_file("file:/linuxrc.config", kf_cfg);

  if(!config.had_segv) {
    if (config.linemode)
      putchar('\n');
    printf(
      ">>> %s installation program v" LXRC_FULL_VERSION " (c) 1996-2006 SUSE Linux Products GmbH <<<\n",
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

  get_ide_options();
  file_read_info_file("cmdline", kf_cmd_early);

  if(config.staticdevices) {
    util_mkdevs();
  }
  else {
    fprintf(stderr, "Starting udev ...\n");
    system("/bin/myudevstart >/dev/null 2>&1");
    fprintf(stderr, "... udev running\n");
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

  force_ri_ig = config.memory.free > config.memory.load_image ? 1 : 0;

  if(util_check_exist("/sbin/modprobe")) has_modprobe = 1;
  lxrc_set_modprobe("/etc/nothing");
  lxrc_set_bdflush(5);

  lxrc_check_console();
  freopen(config.console, "r", stdin);
  freopen(config.console, "a", stdout);

  util_get_splash_status();

  util_splash_bar(10, SPLASH_10);

  if(util_check_exist("/proc/iSeries")) {
    config.is_iseries = 1;
    config.linemode = 1;
  }

  kbd_init(1);
  util_redirect_kmsg();
  disp_init();

  set_activate_language(config.language);

  // auto2_chk_expert();

  if (!config.linemode)
    printf("\033[9;0]");		/* screen saver off */
  fflush(stdout);

  info_init();

  mod_init(1);
  util_update_disk_list(NULL, 1);
  util_update_cdrom_list();

  if(!(config.test || config.serial || config.shell_started || config.noshell)) {
    util_start_shell("/dev/tty9", "/bin/bash", 1);
    config.shell_started = 1;
  }

  if(config.had_segv) config.manual = 1;

  /* get usb keyboard working */
  if(config.manual == 1 && !config.had_segv) util_load_usb();

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

  if(!config.manual) {
    if(auto2_init()) {
      config.manual = 0;	/* ###### does it make sense? */
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
        config.manual |= 1;
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

  /* file_read_info() is called in auto2_init(), too */
  if(!config.info.loaded && !config.hwcheck && !config.had_segv) file_read_info();

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

  if(!config.language && config.win) set_choose_language();

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
  if(config.manual || reboot_wait_ig) {
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
    di_main_verify,
    di_main_eject,
//    di_main_hwcheck,
    di_main_reboot,
    di_main_halt,
    di_none
  };

  config.manual |= 1;

  di_lxrc_main_menu_last = di_main_start;

#if 0
  di_lxrc_main_menu_last = config.hwcheck ? di_main_hwcheck : di_main_start;

  if(config.hwcheck) {
    items[0] = items[1] = items[2] = items[3] = items[4] = items[5] = di_skip;
  }
  else {
    items[6] = di_skip;
  }
#endif

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

    case di_main_verify:
      md5_verify();
      rc = 1;
      break;

    case di_main_eject:
      util_eject_cdrom(config.cdrom);
      rc = 1;
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

  i = mount("tmpfs", newroot, "tmpfs", 0, "nr_inodes=30720");
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

  if(!syscall(SYS_pivot_root, ".", "oldroot")) {
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


