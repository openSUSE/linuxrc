#define _GNU_SOURCE     /* getline, strchrnul */

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
#include <sys/sysinfo.h>
#include <fcntl.h>
#include <dirent.h>
#include <string.h>
#include <signal.h>
#include <stdarg.h>
#include <inttypes.h>
#include <ctype.h>
#include <errno.h>
#include <time.h>

#include <hd.h>

#include "global.h"
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
#include "scsi_rename.h"
#include "checkmedia.h"
#include "url.h"
#include <sys/utsname.h>
#ifdef __s390x__
#include <query_capacity.h>
#endif

#if defined(__alpha__) || defined(__ia64__)
#define SIGNAL_ARGS	int signum, int x, struct sigcontext *scp
#else	// all others
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
static void lxrc_umount_parts(char *basedir);
#if SWISS_ARMY_KNIFE 
static void lxrc_makelinks(char *name);
#endif
static void select_repo_url(char *msg, char **repo);
static char * get_platform_name();
static char *get_console_device();

#if SWISS_ARMY_KNIFE
int probe_main(int argc, char **argv);

static struct {
  char *name;
  int (*func)(int, char **);
} lxrc_internal[] = {
  { "cp",          util_cp_main          },
//  { "swapon",      util_swapon_main      },
  { "scsi_rename", scsi_rename_main      },
  { "lndir",       util_lndir_main       },
  { "extend",      util_extend_main      },
  { "fstype",      util_fstype_main      },
};
#endif

static dia_item_t di_lxrc_main_menu_last;

int main(int argc, char **argv, char **env)
{
  char *prog, *s;
  int err, i, j;
  struct sysinfo si;
  uint64_t totalram = 0;

  prog = (prog = strrchr(*argv, '/')) ? prog + 1 : *argv;

#if SWISS_ARMY_KNIFE
  for(i = 0; (unsigned) i < sizeof lxrc_internal / sizeof *lxrc_internal; i++) {
    if(!strcmp(prog, lxrc_internal[i].name)) {
      config.log.dest[0].level = LOG_LEVEL_SHOW;
      config.log.dest[0].f = stdout;

      config.log.dest[1].level = LOG_LEVEL_INFO;
      config.log.dest[1].f = stderr;

      return lxrc_internal[i].func(argc, argv);
    }
  }
#endif

  config.argv = argv;

  config.run_as_linuxrc = 1;
  config.tmpfs = 1;

  if(!sysinfo(&si)) totalram = si.totalram * si.mem_unit;

  // do not use zram if there's more than 64 GiB free memory (bsc#1197253)
  if(totalram < (64ull << 30)) {
    str_copy(&config.zram.root_size, "1G");
    str_copy(&config.zram.swap_size, "1G");
  }

  str_copy(&config.console, "/dev/console");

  // define logging destinations for the various log levels:

  // standard linuxrc console
  config.log.dest[0].level = LOG_LEVEL_SHOW;
  config.log.dest[0].f = stdout;

  // linuxrc error console (tty3)
  config.log.dest[1].level = LOG_LEVEL_INFO;
  str_copy(&config.log.dest[1].name, "/dev/tty3");

  // linuxrc log file
  config.log.dest[2].level = LOG_LEVEL_SHOW | LOG_LEVEL_INFO | LOG_LEVEL_DEBUG | LOG_TIMESTAMP;
  str_copy(&config.log.dest[2].name, "/var/log/linuxrc.log");

  str_copy(&config.product, "SUSE Linux");

  config.update.next_name = &config.update.name_list;

  log_info("totalram: %"PRIu64"\n", totalram);

  /* maybe we had a segfault recently... */
  if(argc == 4 && !strcmp(argv[1], "segv")) {
    unsigned state = argv[3][0] - '0';

    for(i = 0; i < 16 && argv[2][i]; i++) {
      config.segv_addr <<= 4;
      j = argv[2][i] - '0';
      if(j > 9) j -= 'a' - '9' - 1;
      config.segv_addr += j & 0xf;
    }
    config.had_segv = 1;

    config.linemode = (state >> 1) & 3;

    if((state & 1) == 0) {	/* was not in window mode */
      log_show("\n\nLinuxrc crashed. :-((\nPress ENTER to continue.\n");
      getchar();
    }
  }

  if((s = getenv("restarted")) && !strcmp(s, "42")) {
    config.restarted = 1;
    unsetenv("restarted");
    log_show("\n\nLinuxrc has been restarted\n");
  }

  if(!config.had_segv) config.restart_on_segv = 1;

  if(util_check_exist("/usr/src/packages") || getuid()) {
    log_show("Seems we are on a running system; activating testmode...\n");
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
      file_do_info(file_get_cmdline(key_linuxrcstderr), kf_cmd + kf_cmd_early);
      file_do_info(file_get_cmdline(key_lxrcdebug), kf_cmd + kf_cmd_early);
      file_do_info(file_get_cmdline(key_linuxrc_core), kf_cmd + kf_cmd_early);
      file_do_info(file_get_cmdline(key_zram), kf_cmd_early);
      file_do_info(file_get_cmdline(key_zram_root), kf_cmd_early);
      file_do_info(file_get_cmdline(key_zram_swap), kf_cmd_early);
      util_setup_coredumps();
      util_free_mem();
      umount("/proc");

      // log_info("free: %d, %d\n", config.memory.free, config.tmpfs);

      if(config.tmpfs && config.memoryXXX.free > (24 << 20)) {
        lxrc_movetotmpfs();	/* does not return if successful */
      }

      config.tmpfs = 0;

      if(!config.serial && config.debugwait) {
        util_start_shell("/dev/tty9", "/bin/sh", 3);
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
    dia_input2("Enter temporary root password.\n"
               "You will be required to change this password on initial root user login.",
               &config.rootpassword, 20, 1);
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
    lxrc_main_menu();
  }

  lxrc_end();

  return err;
}


void lxrc_reboot()
{
  util_splash_mode(PLY_MODE_REBOOT);

  if(config.test) {
    log_info("*** reboot ***\n");
    return;
  }

  if(dia_yesno("Reboot the system now?", 1) == YES) {
    reboot(RB_AUTOBOOT);
  }
}

void lxrc_halt()
{
  util_splash_mode(PLY_MODE_SHUTDOWN);

  if(config.test) {
    log_info("*** power off ***\n");
    return;
  }

  if(dia_yesno("Do you want to halt the system now?", 1) == YES) {
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
    "boot", "etc", "home", "run",
    "media", "mounts", "mounts/initrd", "mnt", "parts", "parts/mp_0000", "proc",
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
    log_info("starting rescue\n");

    mount("tmpfs", mp, "tmpfs", 0, "size=100%,nr_inodes=0");

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
      log_info("mkdir %s\n", buf);
      if(!strcmp(*s, "tmp")) chmod(buf, 01777);
    }

    // mount 'parts/00_lib' (kernel parts)
    strprintf(&buf, "%s/parts/mp_0000", mp);
    util_mount_ro("/parts/00_lib", buf, NULL);

    // unmount filesystems below /parts
    lxrc_umount_parts("");

    // add devices
    strprintf(&buf, "%s/dev", mp);
    if(config.devtmpfs) {
      umount("/dev/pts");
      umount("/dev");
      mkdir(buf, 0755);
      mount("devtmpfs", buf, "devtmpfs", 0, 0);
    }
    else {
      rename("/dev", buf);
    }

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
    log_info("starting %s\n", config.new_root);

    util_mount_ro(config.new_root, mp, NULL);
  }

  chdir(mp);

  mount(".", "/", NULL, MS_MOVE, NULL);
  chroot(".");

  if(config.rescue) {
    if(config.usessh) setenv("SSH", "1", 1);
    if(config.net.sshpassword) setenv("SSHPASSWORD", config.net.sshpassword, 1);
    if(config.net.sshpassword_enc) setenv("SSHPASSWORDENC", config.net.sshpassword_enc, 1);

    /* change hostname from 'install' to 'rescue' unless we've had something better */
    if(!config.net.realhostname) util_set_hostname("rescue");

    lxrc_run_console("/mounts/initrd/scripts/prepare_rescue");

    LXRC_WAIT

    if(!config.debug) {
      umount("/mounts/initrd");
      rmdir("/mounts/initrd");
    }
  }

  execl("/sbin/init", "init", NULL);

  perror_info("init failed\n");

  chdir("/");
  umount(mp);
  log_info("system start failed\n");

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

  log_info("Moving into %s...", config.zram.root_size ? "zram" : "tmpfs");

  i = mkdir(newroot, 0755);
  if(i) {
    perror(newroot);
    return;
  }

  if(config.zram.root_size) {
    mount("proc", "/proc", "proc", 0, 0);
    mount("sysfs", "/sys", "sysfs", 0, 0);
    mount("devtmpfs", "/dev", "devtmpfs", 0, 0);
    char *buf = NULL;
    strprintf(&buf, "/scripts/zram_setup %s", config.zram.root_size);
    i = system(buf);
    free(buf);
    if(!i) {
      i = util_mount_rw("/dev/zram0", newroot, NULL);
    }
    umount("/dev");
    umount("/sys");
    umount("/proc");
    if(i) log_info("zram setup failed, falling back to tmpfs...");
  }
  else {
    i = 1;
  }

  if(i) {
    i = mount("tmpfs", newroot, "tmpfs", 0, "size=100%,nr_inodes=0");
  }

  if(i) {
    perror(newroot);
    return;
  }

  i = util_do_cp("/", newroot);
  if(i) {
    log_info("copy failed: %d\n", i);
    return;
  }

  log_info(" done.\n");

  lxrc_run("/bin/rm -r /lib /dev /bin /sbin /usr /etc /init /lbin");

  if(chdir(newroot)) perror_info(newroot);

  if(mkdir("oldroot", 0755)) perror_info("oldroot");

  mount(".", "/", NULL, MS_MOVE, NULL);
  chroot(".");

  for(i = 0; i < 20; i++) close(i);

  open("/dev/console", O_RDWR);
  dup(0);
  dup(0);
  execve("/init", config.argv, environ);

  perror_info("/init");
}


void lxrc_end()
{
  unsigned netstop = config.netstop;

  if(netstop == 3) netstop = config.rescue ? 0 : 1;

  util_plymouth_off();

  if(netstop || config.restarting) {
    LXRC_WAIT

    net_stop();
  }

  LXRC_WAIT

  lxrc_killall(1);

  log_info("all killed\n");

//  while(waitpid(-1, NULL, WNOHANG) == 0);

  log_info("all done\n");

  /* screen saver on */
  if(!config.linemode) printf("\033[9;15]");

  lxrc_set_bdflush(40);

  LXRC_WAIT

  if(!config.test) {
    util_umount("/dev/pts");
    #if defined(__s390__) || defined(__s390x__)
    util_umount("/sys/hypervisor/s390");
    #endif
    util_umount("/sys");
    util_umount("/proc/bus/usb");
    if (!config.rescue)
        util_umount_all_devices ();
    util_umount("/proc");
  }

  disp_cursor_on();
  kbd_end(1);
  disp_end();

  if(!config.restarting) lxrc_change_root();
}

/*
 * Check if pid is either portmap or rpciod.
 *
 */
int do_not_kill(char *name)
{
  static char *progs[] = {
    "portmap", "rpciod", "lockd", "cifsd", "mount.smbfs", "udevd",
    "mount.ntfs-3g", "brld", "sbl", "wickedd", "wickedd-auto4", "wickedd-dhcp4",
    "wickedd-dhcp6", "wickedd-nanny", "dbus-daemon", "rpc.idmapd", "sh", "haveged",
    "wpa_supplicant", "rsyslogd"
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
        if(sig == 15) log_info("killing %s (%d)\n", sl->value, pid);
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

  config.error_trace = 1;
  util_error_trace("***  signal 11 ***\n");

  log_info("Linuxrc segfault at 0x%08"PRIx64". :-((\n", ip);
  if(config.restart_on_segv) {
    config.restart_on_segv = 0;
    for(i = 15; i >= 0; i--, ip >>= 4) {
      j = ip & 0xf;
      if(j > 9) j += 'a' - '9' - 1;
      addr[i] = j + '0';
    }
    addr[16] = 0;
    state[0] = (config.win + (config.linemode << 1)) + '0';
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
    log_info("Caught signal %d!\n", signum);
    sleep(10);
  }

  signal(SIGBUS,  lxrc_catch_signal);
  signal(SIGTERM, lxrc_catch_signal);
  signal(SIGSEGV, (void (*)(int)) lxrc_catch_signal_11);
  signal(SIGPIPE, lxrc_catch_signal);
}


void lxrc_init()
{
  slist_t *sl;

  siginterrupt(SIGALRM, 1);
  signal(SIGHUP, SIG_IGN);
  siginterrupt(SIGBUS, 1);
  siginterrupt(SIGINT, 1);
  siginterrupt(SIGTERM, 1);
  siginterrupt(SIGSEGV, 1);
  siginterrupt(SIGPIPE, 1);
  if(!config.core) lxrc_catch_signal(0);
  signal(SIGINT,  SIG_IGN);
  signal(SIGUSR1, lxrc_usr1);

  {
    struct sigaction sa = { };

    sa.sa_handler = util_restart;
    sa.sa_flags = SA_NODEFER;
    sigaction(SIGUSR2, &sa, NULL);
  }

  umask(022);

  if(!config.test) {
    if(config.had_segv) {
      umount("/dev/pts");
      #if defined(__s390__) || defined(__s390x__)
      umount("/sys/hypervisor/s390");
      #endif
      umount("/sys");
      umount("/proc");
    }
    mount("proc", "/proc", "proc", 0, 0);
    mount("sysfs", "/sys", "sysfs", 0, 0);
    mount("devpts", "/dev/pts", "devpts", 0, 0);
  }

  util_setup_coredumps();

  #if defined(__s390__) || defined(__s390x__)
  if(util_check_exist("/sys/hypervisor/s390")) {
    char *type;

    mount("s390_hypfs", "/sys/hypervisor/s390", "s390_hypfs", 0, 0);

    type = util_get_attr("/sys/hypervisor/s390/hyp/type");

    if(!strncmp(type, "z/VM", sizeof "z/VM" - 1)) {
      config.hwp.hypervisor = "z/VM";
    }
    else if(!strncmp(type, "LPAR", sizeof "LPAR" - 1)) {
      config.hwp.hypervisor = "LPAR";
    }
    else {
      config.hwp.hypervisor = "Unknown";
    }
  }
  else {
    struct utsname utsinfo;

    uname(&utsinfo);
    if(!strncmp(utsinfo.machine, "s390x", sizeof "s390x" - 1 )) config.hwp.hypervisor="KVM";
    else config.hwp.hypervisor="Reallyunknown";
  }
  #endif

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

  config.setupcmd = strdup("setsid -wc inst_setup yast");

  config.debugshell = strdup("setsid -wc /bin/bash -l");

  config.update.map = calloc(1, MAX_UPDATES);

  config.zenconfig = strdup("settings.txt");

  util_set_product_dir("suse");

  str_copy(&config.net.dhcpfail, "show");

  config.net.dhcp_timeout = 60;
  config.net.tftp_timeout = 10;
  config.net.ifconfig = 1;
  config.net.ipv4 = 1;
  config.net.ipv6 = 1;
  config.net.setup = NS_DHCP;	/* unless we are told otherwise just go for dhcp */
  config.net.nameservers = 1;
  config.net.sethostname = 1;	/* let wicked set hostname */

  config.explode_win = 1;
  config.color = 2;
  config.addswap = 1;
  config.netstop = 3;
  config.usbwait = 4;		/* 4 seconds */
  config.escdelay = 100;	/* 100 ms */
  config.utf8 = 1;
  config.kbd_fd = -1;
  config.ntfs_3g = 1;
  config.secure = 1;
  config.sslcerts = 1;
  config.squash = 1;
  config.kexec_reboot = 1;
  config.efi = -1;
  config.udev_mods = 1;
  config.devtmpfs = 1;
  config.kexec = 2;		/* kexec if necessary, with user dialog */
  config.auto_assembly = 0;	/* default to disable MD/RAID auto-assembly (bsc#1132688) */
  config.autoyast_parse = 1;	/* analyse autoyast option and read autoyast file */
#if defined(__s390x__)
  config.device_auto_config = 2;	/* ask before doing s390 device auto config */
#endif
  config.switch_to_fb = 1;

  // defaults for self-update feature
  config.self_update_url = NULL;
  config.self_update = 1;

  config.scsi_rename = 0;
  config.scsi_before_usb = 1;

  // default memory limits
  config.memoryXXX.min_free =       12 << 20;
  config.memoryXXX.min_yast =      224 << 20;
  config.memoryXXX.load_image =    750 << 20;

  config.swap_file_size = 1024;		/* 1024 MB */

  str_copy(&config.namescheme, "by-id");

  slist_assign_values(&config.digests.supported, "sha1,sha256");

  config.plymouth = 1;

  config.ifcfg.manual = calloc(1, sizeof *config.ifcfg.manual);
  config.ifcfg.manual->dhcp = 1;

  // config.nanny = 0;	/* see config.nanny_set */

  #if defined(__s390__) || defined(__s390x__)
  config.linemode = 1;
  #endif

  // a config for this interface always exists
  slist_append_str(&config.ifcfg.initial, "lo");

  config.defaultrepo = slist_split(',', "cd:/,hd:/");

  file_do_info(file_get_cmdline(key_lxrcdebug), kf_cmd + kf_cmd_early);

  LXRC_WAIT

  if(!config.had_segv) {
    lxrc_add_parts();
  }

  // modprobe config files exist from here on
  config.module.modprobe_ok = 1;

  config.platform_name=get_platform_name();

  util_get_releasever();

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

  util_set_hostname("install");

  // read config from initrd:
  //   - /linuxrc.config
  //   - /etc/linuxrc.d/*
  file_read_info_file("file:/linuxrc.config", kf_cfg);
  DIR *d;
  if((d = opendir("/etc/linuxrc.d"))) {
    struct dirent *de;
    slist_t *sl0 = NULL, *sl;
    while((de = readdir(d))) {
      if(util_check_exist2("/etc/linuxrc.d", de->d_name) == 'r') {
        sl = slist_append(&sl0, slist_new());
        strprintf(&sl->key, "file:/etc/linuxrc.d/%s", de->d_name);
      }
    }
    closedir(d);
    sl0 = slist_sort(sl0, cmp_entry_s);
    for (sl = sl0; sl; sl = sl->next) {
      file_read_info_file(sl->key, kf_cfg);
    }
    slist_free(sl0);
  }

  util_setup_coredumps();

  if(!config.had_segv) {
    if (config.linemode)
      putchar('\n');
    printf(
      "\n>>> %s installation program v" LXRC_FULL_VERSION " (c) 1996-2021 SUSE LLC %s <<<\n",
      config.product,
      config.platform_name
    );
    if (config.linemode)
      putchar('\n');
    fflush(stdout);
  }

  get_ide_options();
  file_read_info_file("cmdline", kf_cmd_early);

  util_redirect_kmsg();

  LXRC_WAIT

  util_setup_udevrules();

  if(!config.udev_mods) {
    mkdir("/run/udev", 0755);
    mkdir("/run/udev/rules.d", 0755);

    lxrc_run("cp /usr/lib/udev/80-drivers.rules.no_modprobe /run/udev/rules.d/80-drivers.rules");

    LXRC_WAIT
  }

  config.plymouth &= util_check_exist("/usr/sbin/plymouthd") == 'r' ? 1 : 0;
  config.plymouth &= !(config.linemode || config.manual);

  if(config.early_bash) {
    util_start_shell("/dev/tty8", "/bin/bash", 3);
  }

  LXRC_WAIT

  if(config.devtmpfs) {
    umount("/dev/pts");
    mount("devtmpfs", "/dev", "devtmpfs", 0, 0);
    mkdir("/dev/pts", 0755);
    mount("devpts", "/dev/pts", "devpts", 0, 0);
  }

  time_t t = time(NULL);
  struct tm *gm = gmtime(&t);
  if(gm) log_info(
    "===  linuxrc " LXRC_FULL_VERSION " - %04d-%02d-%02d %02d:%02d:%02d  ===\n",
    gm->tm_year + 1900, gm->tm_mon + 1, gm->tm_mday, gm->tm_hour, gm->tm_min, gm->tm_sec
  );

  /*
   * Do what has to be done before udevd starts.
   */
  file_read_info_file("cmdline", kf_cmd0);

  if(!config.test) {
    log_show("Starting udev... ");
    util_run_script("udev_setup");
    log_show("ok\n");
  }

  util_setup_coredumps();

  if(config.had_segv) config.manual = 1;

  if(!config.test && !config.had_segv) {
    /* Check for special case with aborted installation */
    if(util_check_exist ("/.bin")) {
      unlink("/bin");
      rename("/.bin", "/bin");
    }
  }

  // prepare wicked for nanny
  net_nanny();

  // set up config key list
  net_wicked_get_config_keys();

  util_run_script("early_setup");

  file_read_info_file("file:/etc/ibft_devices", kf_cfg);

  // ibft interfaces are handled by wicked
  for(sl = config.ifcfg.ibft; sl; sl = sl->next) {
    slist_append_str(&config.ifcfg.initial, sl->key);
  }

  // now that udev is up and running, some URLs might be parsed differently
  util_reparse_blockdev_urls();

  util_free_mem();

  if(config.memoryXXX.free < config.memoryXXX.min_free) {
    config.memoryXXX.min_free = config.memoryXXX.free;
  }

  if(!config.download.instsys_set) {
    config.download.instsys = config.memoryXXX.free > config.memoryXXX.load_image ? 1 : 0;
  }

  lxrc_set_bdflush(5);

  lxrc_check_console();
  if(!config.test) {
    freopen(config.console, "r", stdin);
    freopen(config.console, "a", stdout);
  }

  if(util_check_exist("/proc/iSeries")) {
    config.is_iseries = 1;
    config.linemode = 1;
  }

  kbd_init(1);
  util_redirect_kmsg();
  disp_init();

  // clear keyboard queue
  while(kbd_getch_old(0));

  if(config.plymouth) util_run_script("plymouth_setup");

  util_splash_mode(PLY_MODE_UPGRADE);

  util_splash_bar(10);

  set_activate_language(config.language);

  // auto2_chk_expert();

  if (!config.linemode)
    printf("\033[9;0]");		/* screen saver off */
  fflush(stdout);

  info_init();

  LXRC_WAIT

  log_show("Loading basic drivers...");
  util_splash_msg("Loading basic drivers");
  mod_init(1);
  log_show(" ok\n");

  LXRC_WAIT

  util_device_auto_config();

  /* look for driver updates in initrd */
  util_chk_driver_update("/", "/");

  url_register_schemes();

  util_update_disk_list(NULL, 1);
  util_update_cdrom_list();

  if(!(config.test || config.serial || config.shell_started || config.noshell)) {
    util_start_shell("/dev/tty9", "/bin/sh", 3);
    config.shell_started = 1;
  }

  file_read_info_file("cmdline", kf_cmd1);

  if(config.had_segv) config.manual = 1;

  /* check efi status */
  if(util_check_exist("/sys/firmware/efi/vars") == 'd' || util_check_exist("/sys/firmware/efi/efivars") == 'd') {
    config.efi_vars = 1;
  }
  log_debug("efi = %d\n", config.efi_vars);

  if(iscsi_check()) config.withiscsi = 1;
  if(fcoe_check()) config.withfcoe = 1;

  LXRC_WAIT

  /* get usb keyboard working */
  if(config.manual == 1 && !config.had_segv) util_load_usb();

  /* load ip over infiniband modules */
  if(config.withipoib) {
    mod_modprobe("ib_cm", NULL);
    mod_modprobe("ib_ipoib", NULL);
  }

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
          sprintf(cmd,"/sbin/chzdev -e zfcp-host --no-root-update %s",device);
          util_write_active_devices("%s\n", device);
          if(!config.test) lxrc_run(cmd);
          if(util_read_and_chop("/sys/firmware/ipl/wwpn", wwpn, sizeof wwpn))
          {
            if(util_read_and_chop("/sys/firmware/ipl/lun", lun, sizeof lun))
            {
              sprintf(cmd,"/sbin/chzdev -e zfcp-lun --no-root-update %s:%s:%s",device,wwpn,lun);
              if(!config.test) lxrc_run(cmd);
            }
          }
        }
      }
      else log_info("not booted via FCP\n");
    }
    else log_info("could not read /sys/firmware/ipl/ipl_type\n");
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
        log_info("loading ps3vram, mtdblock and ps3_gelic\n");
        mod_modprobe("ps3vram","");
        mod_modprobe("mtdblock","");
        mod_modprobe("ps3_gelic","");
        if(!config.test && util_check_exist("/sys/block/mtdblock0"))
        {
          lxrc_run(cmd);
	}
      } else if(strcmp(buf,"Pegasos2")==0)
      {
        log_info("preloading via-rhine, loading mv643xx_eth\n");
        mod_modprobe("via-rhine","");
        mod_modprobe("mv643xx_eth","");
      }
    }
  }
#endif

  if(config.memoryXXX.ram_min && !config.had_segv) {
    int window = config.win;
    int64_t ram;
    char *msg = NULL;

    util_get_ram_size();

    ram = config.memoryXXX.ram_min - config.memoryXXX.ram_min / 8;

    if(config.memoryXXX.ram < ram) {
      if(!window) util_disp_init();
      strprintf(&msg, "Your computer does not have enough RAM for the installation of %s. You need at least %d MB.", config.product, (int) (config.memoryXXX.ram_min >> 20));
      dia_message(msg, MSGTYPE_REBOOT);
      free(msg);
      if(!window) util_disp_done();
      if(config.memoryXXX.ram_min) {
        config.manual = 1;
        if(config.test) {
          log_info("*** reboot ***\n");
        }
        else {
          reboot(RB_AUTOBOOT);
        }
      }
    }
  }

  net_update_ifcfg(0);

  net_wicked_up("all");

  if(config.manual) file_read_info_file("cmdline", kf_cmd);

  if(config.braille.check) run_braille();

  if(!config.manual && !auto2_init()) {
    char *buf = NULL, *repo = NULL;

    log_info("Automatic setup not possible.\n");

    // ok, we failed to find a suitable repo
    // do something about it

    // If the file '/nextmedia' exists, get the message to show from there
    // and retry the default repo settings if the user is ready.
    // Otherwise, present the user the list of repos we have tried so far and
    // let her choose / edit one and try again.

    util_disp_init();

    if(util_check_exist("/nextmedia") == 'r') {
      config.cd1texts = file_parse_xmllike("/nextmedia", "text");
    }

    char *next_msg = get_translation(config.cd1texts, current_language()->locale);

    strprintf(&buf,
      "%s\n\n%s",
      next_msg ?: "Please make sure your installation medium is available.",
      config.cd1texts ? "Continue?" : "Choose the URL to retry."
    );

    if(config.cd1texts) {
      slist_t *sl;

      if(dia_okcancel(buf, YES) != YES) {
        config.rescue = 0;
        config.manual |= 1;
      }
      else {
        for(sl = config.defaultrepo; sl; sl = sl->next) {
          config.manual = 0;

          url_free(config.url.install);
          config.url.install = url_set(sl->key);

          if(auto2_find_repo()) break;
        }
      }
    }
    else {
      do {
        config.manual = 0;

        select_repo_url(buf, &repo);

        if(!repo || !*repo) {
          config.rescue = 0;
          config.manual |= 1;

          break;
        }

        url_free(config.url.install);
        config.url.install = url_set(repo);
      } while(!auto2_find_repo());
    }

    str_copy(&buf, NULL);
    str_copy(&repo, NULL);

    if(!config.manual) util_disp_done();
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

  if(!(config.serial || config.is_iseries || config.linemode) && config.manual) {
    set_choose_keytable(0);
  }

  util_update_kernellog();

  if(config.loghost) util_run_script("remote_log_setup");

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
    di = dia_menu2("Main Menu", 38, lxrc_main_cb, items, di_lxrc_main_menu_last);

    if(di == di_none) {
      if(dia_message("You can leave Linuxrc only via \n"
                     "\n"
                     "\"Start Installation or System\"\n"
                     "\n"
                     "You may need to load some drivers (modules) to support your hardware.",
                     MSGTYPE_INFO) == -42)
        break;
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

return dia_menu2("Exit or Reboot", 30, lxrc_exit_cb, items, 1);
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


#define LXRC_CONSOLE_DEV "/dev/tty1"

/*
 * Set console device linuxrc is going to use.
 *
 * This is usually just /dev/console or something specified via the
 * 'console' boot option.
 *
 * But if a framebuffer device exists (after udev loads some drivers) and
 * the user hasn't specified any 'console' option, switch to
 * LXRC_CONSOLE_DEV (that is, use the framebuffer).
 *
 * This console switching can be prevented using the 'switch_to_fb=0' boot
 * option or enforced using 'switch_to_fb=2'. (The default setting is 1.)
 */
void lxrc_check_console()
{
  char *current_console = get_console_device();

  util_set_serial_console(auto2_serial_console());

  /*
   * Switch to tty1 if there is a framebuffer device and the user hasn't
   * specified something else explicitly.
   *
   * The idea here is to catch cases where udev loads graphics drivers and a
   * local graphical terminal becomes available. In this case, switch to
   * that terminal.
   */
  if(
    !config.test &&
    (config.switch_to_fb == 2 || (config.switch_to_fb == 1 && !config.console_option)) &&
    util_check_exist("/dev/fb0") == 'c' &&
    util_check_exist(LXRC_CONSOLE_DEV) == 'c'
  ) {
    if(strcmp(current_console, LXRC_CONSOLE_DEV)) {
      str_copy(&config.console, LXRC_CONSOLE_DEV);
      log_show(
        "\nFramebuffer device detected - continuing installation on console %s.\n"
        "Use boot option 'switch_to_fb=0' to prevent this.\n\n",
        config.console
      );
      kbd_switch_tty(0, 1);
    }
  }

  log_debug("going for console device: %s\n", config.console);

  if(config.serial) {
    log_info(
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

  for(i = 0; (unsigned) i < sizeof lxrc_internal / sizeof *lxrc_internal; i++) {
    sprintf(buf, "/lbin/%s", lxrc_internal[i].name);
    if(!util_check_exist(buf)) link(name, buf);
  }
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


void lxrc_usr1(int signum)
{
  static unsigned extend_cnt = 0;
  int err = 0;
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

      pid_t child_pid = fork();

      if(!child_pid) {
        config.extend_running = 1;

        log_debug("=== extend started ===\n");


        // get us a copy of the logs
        unlink("/tmp/extend.log");
        str_copy(&config.log.dest[1].name, "/tmp/extend.log");
        config.log.dest[1].f = fopen(config.log.dest[1].name, "a");
        config.log.dest[1].level = LOG_LEVEL_SHOW | LOG_LEVEL_INFO | LOG_LEVEL_DEBUG | LOG_TIMESTAMP;

        // close stdin; stdout and stderr point to log file
        close(0);
        if(config.log.dest[1].f) {
          int fd = fileno(config.log.dest[1].f);
          dup2(fd, 1);
          dup2(fd, 2);
        }

        config.download.cnt = 1000 + extend_cnt;
        config.mountpoint.cnt = 1000 + extend_cnt;
        if(!config.debug) config.debug = 1;

        config.keepinstsysconfig = 1;
        config.linemode = 1;
        config.secure_always_fail = 1;

        if(task == 'a' && sl) {
          log_info("instsys extend: add %s\n", ext);
          log_info("%s: already added\n", ext, ext);
          err = 0;
        }
        else if(task == 'r' && !sl) {
          log_info("instsys extend: remove %s\n", ext);
          log_info("%s: not there\n", ext);
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

        config.log.dest[1].level = 0;
        log_debug("=== extend finished ===\n");

        exit(0);
      }
      else {
        // wait for child to finish, then signal extend command to go on
        waitpid(child_pid, NULL, 0);
        if(extend_pid > 0) kill(extend_pid, SIGUSR1);
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

static void insmod_basics(void)
{
  static const struct {
    char *name;
    char *param;
    int mandatory;
  } *i, basics[] = {
    { "loop", "max_loop=64", 1 },
    { "lz4_decompress" },
    { "xxhash" },
    { "zstd_decompress" },
    { }
  };

  for(i = basics; i->name; i++) {
    char *buf = mod_find_module("/modules", i->name);

    if(!buf) {
      if(i->mandatory) {
	log_show("Cannot find module %s!\n", i->name);
      }
      continue;
    }

    strprintf(&buf, "/sbin/insmod %s", buf);
    if(i->param) strprintf(&buf, "%s %s", buf, i->param);

    lxrc_run(buf);

    free(buf);
  }
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
    log_info("Integrating %s\n", sl->key);
    if(!config.test) {
      if(!insmod_done) {
        insmod_done = 1;
        insmod_basics();
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


/*
 * Unmount initrd parts when no longer needed.
 *
 * This is called when moving control to the rescue system.
 */
void lxrc_umount_parts(char *basedir)
{
  char *mp = NULL;

  if(config.test) return;

  for(unsigned u = 0; u < config.mountpoint.initrd_parts; u++) {
    strprintf(&mp, "%s/parts/mp_%04u", basedir, u);
    umount2(mp, MNT_DETACH);
  }

  free(mp);
}


/*
 * Offer the user a list of URLs to choose from based in the current install
 * URL and the default repo setting.
 *
 * In addition she can also edit the URL.
 *
 * Return NULL in repo if the user cancelled the dialogs, else repo contains
 * the chosen repo URL.
 */
void select_repo_url(char *msg, char **repo)
{
  slist_t *sl;
  char **item_list;
  int i, with_install_url, items, default_item;

  // if config.url.install is a duplicate of some default repo entry, skip it
  with_install_url = config.url.install && !slist_getentry(config.defaultrepo, config.url.install->str);

  for(items = 1, sl = config.defaultrepo; sl; sl = sl->next) items++;

  if(with_install_url) items++;

  item_list = calloc(items + 1, sizeof *item_list);

  i = 0;

  if(with_install_url) {
    item_list[i++] = config.url.install->str;
  }

  for(sl = config.defaultrepo; sl; sl = sl->next, i++) {
    item_list[i] = sl->key;
  }

  item_list[i] = "Enter another URL";

  default_item = 1;

  // if we skipped config.url.install because it's a duplicate of some
  // defaultrepo entry, make it the default entry
  if(config.url.install && !with_install_url) {
    for(i = 1, sl = config.defaultrepo; sl && strcmp(sl->key, config.url.install->str); sl = sl->next) i++;
    if(sl) default_item = i;
  }

  i = dia_list(msg, 64, NULL, item_list, default_item, align_left);

  if(i > 0) {
    if(i == items) {
      if(!*repo) str_copy(repo, item_list[default_item - 1]);
      if(dia_input2("Enter the repository URL.", repo, 64, 0)) {
        str_copy(repo, NULL);
      }
    }
    else {
      str_copy(repo, item_list[i - 1]);
    }
  }
  else {
    str_copy(repo, NULL);
  }

  free(item_list);
  item_list = NULL;

  log_debug("repo: %s\n", *repo ?: "");
}

char * get_platform_name()
{
  char *platform = NULL;
#if defined(__s390__) || defined(__s390x__)
  void *qc_configuration_handle = NULL;
  const char *qc_result_string = NULL;
  int qc_return_code = 0;
  int qc_get_return_code = 0;

  qc_configuration_handle = qc_open(&qc_return_code);
  if (qc_return_code==0)
    qc_get_return_code = qc_get_attribute_string(qc_configuration_handle, qc_type_name,
                                                    0, &qc_result_string);
  else log_show("The call to qc_open failed.\n");

  if (qc_get_return_code<=0) {
    log_show("Unable to retrieve machine type.\n");
    strprintf(&platform, "%s", "on unknown machine type");
  }
  else strprintf(&platform, "on %s", qc_result_string);

  qc_close(qc_configuration_handle);
#else
  str_copy(&platform, "");
#endif
return platform;
}


/*
 * Get current console device name.
 *
 * Do not free() the returned string.
 */
char *get_console_device()
{
  FILE *f;
  static char *buf = NULL;
  size_t len;

  str_copy(&buf, NULL);

  if((f = popen("showconsole 2>/dev/null", "r"))) {
    if(getline(&buf, &len, f) > 0) {
      *strchrnul(buf, '\n') = 0;
    }
    pclose(f);
  }

  if(!buf) str_copy(&buf, "/dev/console");

  log_info("get_console_device: %s\n", buf);

  return buf;
}
