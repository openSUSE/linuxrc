/*
 *
 * global.h      Global defines for linuxrc
 *
 * Copyright (c) 1996-2002  Hubert Mantel, SuSE Linux AG  (mantel@suse.de)
 *
 */


#include <stdio.h>
#include <netinet/in.h>
#include <inttypes.h>

#include "tftp.h"
#include "po/text_langids.h"
#include "text.h"
#include "settings.h"

#include "version.h"

#if defined(__sparc__) || defined(__PPC__) || defined(__s390__) || defined(__s390x__) || defined(__MIPSEB__)
#define WITH_PCMCIA	0
#else
#define WITH_PCMCIA	1
#endif

// linuxrc includes a lot of other
// functionality like 'mount/umount', 'loadkeys', ...

#define SWISS_ARMY_KNIFE 1

#define SP_FILE "servicepack.tar.gz"
#define TEXTS_FILE "nextmedia"
#define INSTALL_FILE_LIST "installfiles"

#ifndef TRUE
#define TRUE			1
#endif
#ifndef FALSE
#define FALSE			0
#endif

#define YES			1
#define NO			0
#define ESCAPE			-1

/* max bytes needed for utf8 string of length a */
#define UTF8_SIZE(a)		((a) * 6 + 1)

#define MAX_X			250
#define MAX_Y			150

#define BUTTON_SIZE_NORMAL	8
#define BUTTON_SIZE_LARGE	10
#define STATUS_SIZE		50

#define  LXRC_DEBUG

#ifdef LXRC_DEBUG
# define deb_wait if(config.debugwait) printf("%s:%d: Press a key...\n", __func__, __LINE__), getchar()
# define deb_msg(a) fprintf(stderr, "%s:%u %s\n", __func__, __LINE__, a)
# define deb_str(a) fprintf(stderr, "%s:%u " #a " = \"%s\"\n", __func__, __LINE__, a)
# define deb_int(a) fprintf(stderr, "%s:%u " #a " = %d\n", __func__, __LINE__, a)
#else
# define deb_wait
# define deb_msg(a)
# define deb_str(a)
# define deb_int(a)
#endif

#define RAMDISK_2  "/dev/ram2"

#define MAX_FILENAME     300

#define MODULE_SUFFIX	".ko"

typedef struct {
               int c;
               char attr;
               }
        character_t;

typedef struct {
               int           x_left;
               int           y_left;
               int           x_right;
               int           y_right;
               int           head;
               int           foot;
               char          bg_color;
               char          fg_color;
               char          style;
               char          shadow;
               char          save_bg;
               character_t **save_area;
               }
        window_t;

typedef struct {
               window_t  win;
               char      text [UTF8_SIZE(BUTTON_SIZE_LARGE)];
               }
        button_t;

typedef struct {
               int       has_colors;
               char      bg;
               char      msg_win;
               char      msg_fg;
               char      choice_win;
               char      choice_fg;
               char      menu_win;
               char      menu_fg;
               char      button_bg;
               char      button_fg;
               char      input_win;
               char      input_bg;
               char      input_fg;
               char      error_win;
               char      error_fg;
               }
        colorset_t;

/*
 * check sym_constants[] in file.c before rearranging things
 */
typedef enum {
  inst_none = 0, inst_file, inst_nfs, inst_ftp, inst_smb,
  inst_http, inst_tftp, inst_cdrom, inst_floppy, inst_hd,
  inst_dvd, inst_cdwithnet, inst_net, inst_slp
} instmode_t;


typedef struct {
  char *text;
  int (*func) (int);
  int di;
  struct {
    unsigned head:1;
  } tag;
} item_t;


typedef struct slist_s {
  struct slist_s *next;
  char *key, *value;
} slist_t;


typedef struct {
  unsigned ok:1;		/* ip field is valid */
  struct in_addr ip;
  struct in_addr net;
  char *name;
} inet_t;


typedef struct {
  instmode_t scheme;
  char *server;
  char *share;
  char *dir;
  char *user;
  char *password;
  char *domain;
  unsigned port;
} url_t;


typedef struct module2_s {
  struct module2_s *next;
  char *name;		/* module name */
  char *descr;		/* a description */
  char *param;		/* sample module params */
  char *pre_inst;	/* load these before */
  char *post_inst;	/* load these after */
  int type;		/* category, e.g. scsi, cdrom, network... */
  unsigned initrd:1;	/* add it to initrd */
  unsigned autoload:1;	/* can be autoloaded */
  unsigned exists:1;	/* module really exists */
  unsigned dontask:1;	/* don't ask for module params */
  unsigned detected:1;	/* auto-detected */
} module_t;


typedef struct driver_s {
  struct driver_s *next;
  char *name;		/* module name */
  char *sysfs_name;	/* sysfs directory name */
  unsigned vendor;
  unsigned device;
  unsigned subvendor;
  unsigned subdevice;
  unsigned class;
  unsigned class_mask;
  unsigned long driver_data;
  unsigned used;
} driver_t;


typedef enum {
  nc_none, nc_static, nc_bootp, nc_dhcp
} net_config_t;

/* > 100 and <= 1000 */
#define MAX_UPDATES		1000

#define MAX_MODULE_TYPES	10

/* config.net.do_setup bitmasks */

#define DS_SETUP		(1 << 0)	/* NetSetup option has been used */
#define DS_INSTALL		(1 << 1)
#define DS_VNC			(1 << 2)
#define DS_SSH			(1 << 3)

/* config.net.setup bitmasks */

/* reserved 			(1 << 0) */
#define NS_DHCP			(1 << 1)
#define NS_HOSTIP		(1 << 2)
#define NS_NETMASK		(1 << 3)
#define NS_GATEWAY		(1 << 4)
#define NS_NAMESERVER		(1 << 5)

#define NS_DEFAULT		(NS_DHCP | NS_HOSTIP | NS_NETMASK | NS_GATEWAY | NS_NAMESERVER)

#define SPLASH_10	NULL
#define SPLASH_20	"rlchange B"
#define SPLASH_30	NULL
#define SPLASH_40	NULL
#define SPLASH_50	"rlchange 3"
#define SPLASH_60	"splash_early start"

typedef struct {
  unsigned rebootmsg:1;		/* show reboot message */
  unsigned redraw_menu:1;	/* we need a better solution for this */
  unsigned initrd_has_ldso:1;	/* instsys contains a dynamic linker */
  unsigned suppress_warnings:1;	/* show less warning dialogs */
  unsigned noerrors:1;		/* no error messages */
  unsigned is_iseries:1;	/* set if we run on an iSeries machine */
  unsigned win:1;		/* set if we are drawing windows */
  unsigned forceinsmod:1;	/* use 'insmod -f' if set */
  unsigned tmpfs:1;		/* we're using tmpfs for / */
  unsigned run_as_linuxrc:1;	/* set if we really are linuxrc */
  unsigned initramfs:1;		/* initramfs mode */
  unsigned test:1;		/* we are in test mode */
  unsigned rescue:1;		/* start rescue system */
  unsigned demo:1;		/* start live cd */
  unsigned hwcheck:1;		/* do hardware check */
  unsigned shell_started:1;	/* there is a shell running on /dev/tty9 */
  unsigned extramount:1;	/* mountpoints.extra is in use */
  unsigned instdata_mounted:1;	/* install data are mounted */
  unsigned textmode:1;		/* start yast2 in text mode */
  unsigned debugwait:1;		/* pop up dialogs at some critical points */
  unsigned linemode:1;		/* line mode */
  unsigned ask_language:1;	/* let use choose language  */
  unsigned ask_keytable:1;	/* let user choose keytable */
  unsigned activate_storage:1;	/* load all storage modules */
  unsigned activate_network:1;	/* load all network modules */
  unsigned nopcmcia:1;		/* don't start pcmcia automatically */
  unsigned use_ramdisk:1;	/* used internally */
  unsigned vnc:1;		/* vnc mode */
  unsigned usessh:1;		/* ssh mode */
  unsigned pivotroot:1;		/* use pivotroot system call */
  unsigned testpivotroot:1;	/* test pivotroot */
  unsigned addswap:1;		/* offer to add swap if yast needs it */
  unsigned aborted:1;		/* yast did abort the installation */
  unsigned splash:1;		/* splash active */
  unsigned netstop:1;		/* shut down network iface at end */
  unsigned noshell:1;		/* don't start any shells */
  volatile unsigned restart_on_segv:1;	/* restart linuxrc after segfault */
  unsigned had_segv:1;		/* last linuxrc run ended with segv */
  unsigned hwdetect:1;		/* do automatic hardware detection */
  unsigned explode_win:1;	/* animated windows */
  unsigned scsi_before_usb:1;	/* load storage controller modules before usb/ieee1394 */
  unsigned scsi_rename:1;	/* ensure hotplug scsi devs are last */
  unsigned kernel_pcmcia:1;	/* use kernel pcmcia modules */
  unsigned debug;		/* debug */
  unsigned idescsi;		/* use ide-scsi module */
  unsigned floppy_probed:1;	/* tried to detect floppy device */
  unsigned linebreak:1;		/* internal: print a newline first */
  unsigned manual;		/* manual mode */
  unsigned utf8:1;		/* in utf8 mode */
  unsigned fb:1;		/* has frame buffer */
  unsigned instsys_complain:2;	/* check instsys id */
  unsigned do_pcmcia_startup:1;	/* run pcmcia-socket-startup */
  unsigned update_complain:2;	/* check for certain updates */
  unsigned staticdevices:1;	/* use static /dev tree (not udev) */
  unsigned startshell:1;	/* start shell before & after yast */
  unsigned listen:1;		/* listen on port */
  unsigned zombies:1;		/* keep zombies around */
  unsigned installfilesread:1;	/* already got install files */
  unsigned zen;			/* zenworks mode */
  char *zenconfig;		/* zenworks config file */
  unsigned xxx;			/* xxx */
  unsigned withiscsi;		/* iSCSI parameter */
  char *instsys_id;		/* instsys id */
  char *initrd_id;		/* initrd id */
  int floppies;			/* number of floppy drives */
  int floppy;			/* floppy drive recently used */
  char *floppy_dev[4];		/* list of floppy devices */
  slist_t *disks;		/* list of harddisk, without '/dev/' */
  slist_t *partitions;		/* list of partitions, without '/dev/' */
  char *partition;		/* currently used partition (hd install), without '/dev/' */
  slist_t *cdroms;		/* list of cdroms, without '/dev/' */
  char *cdrom;			/* currently used cdrom, without '/dev/' */
  slist_t *swaps;		/* swap partitions, without '/dev/' */
  char *floppydev;		/* floppy device specified via config file (no '/dev/') */
  char *cdromdev;		/* cdrom device specified via config file (no '/dev/') */
  char *instsys;		/* installation system mount point */
  char *instsys2;		/* extra installation system mount point */
  struct {
    char *file;			/* 'info' file name */
    char *loaded;		/* actual 'info' file that was loaded */
    unsigned add_cmdline:1;	/* parse cmdline, too */
  } info;
  char *autoyast;		/* yast autoinstall parameter */
  slist_t *linuxrc;		/* 'linuxrc' parameters */
  char *stderr_name;		/* stderr device name */
  int color;			/* color scheme: 0-3: undef, mono, color, alternate */
  enum langid_t language;	/* currently selected language */
  char *keymap;			/* current keymap */
  char *serverdir;		/* install base directory on server */
  unsigned sourcetype:1;	/* 0: directory, 1: file */
  char *serverfile;		/* file below serverdir (eg. some ISO) */
  char *serverpath;		/* path to serverfile */
  instmode_t insttype;		/* install type (cdrom, network, etc. )*/
  instmode_t instmode;		/* ftp, nfs, smb, etc. */
  instmode_t instmode_extra;	/* for the stranger things... */
  int inst_ramdisk;		/* ramdisk with instsys */
  int inst2_ramdisk;		/* ramdisk with extra instsys */
  char *new_root;		/* root device to boot */
  char *installdir;		/* "/boot/inst-sys" */
  char *rootimage;		/* "/boot/root" */
  char *rescueimage;		/* "/boot/rescue" */
  char *rootimage2;		/* additional root image */
  char *term;			/* TERM var */
  char *cdid;			/* set if we found a install CD */
  int usbwait;			/* sleep this much after loading usb modules */
  char *setupcmd;		/* command used to start the install program */
  char **argv;			/* store argv here */
  uint64_t segv_addr;		/* segfault addr if last linuxrc run */
  char *console;		/* console device */
  char *serial;			/* serial console parameters, e.g. ttyS0,38400 or ttyS1,9600n8 */
  char *product;		/* product name */
  char *product_dir;		/* product specific dir component (e.g. 'suse') */
  int kbdtimeout;		/* keyboard timeout (in s) */
  int escdelay;			/* timeout to differ esc from function keys */
  int loglevel;			/* set kernel log level */
  char *loghost;		/* syslog host */
  char *rootpassword;
  int kbd_fd;			/* fd for console */
  slist_t *ethtool;		/* ethtool options */
  slist_t *cd1texts;		/* text for requesting next product cd */

  struct {
    char *dir;			/* driver update source dir */
    char *dst;			/* driver update destination dir */
    char *dev;			/* device recently used for updates (if any) */
    unsigned count;		/* driver update count */
    unsigned next;		/* next driver update to do */
    unsigned style:1;		/* 0: new style, 1: old style */
    unsigned ask:1;		/* 1: ask for update disk */
    unsigned shown:1;		/* 1: update dialog has been shown at least once */
    unsigned name_added:1;	/* set if driver update has a name */
    char *id;			/* current id, if any */
    unsigned prio;		/* priority */
    unsigned char *map;		/* track updates */
    slist_t *id_list;		/* list of updates */
    slist_t *name_list;		/* list of update names */
    slist_t **next_name;	/* points into name_list */
    slist_t *expected_name_list;	/* updates we must have */
  } update;

  struct {
    char *image;		/* "/boot/liveeval" */
    char *cfg;			/* live config file */
    slist_t *args;		/* 'live' cmdline args, splitted */
    slist_t *useswap;		/* swap partitions to use */
    slist_t *swaps;		/* swap partitions found */
    slist_t *partitions;	/* live eval partitions */
    unsigned newconfig:1;	/* ignore existing config */
    unsigned nodisk:1;		/* don't save to disk */
    unsigned swapfile:1;	/* use swap file */
    unsigned autopart:1;	/* use first suitable partition, if any */
    unsigned autoswap:1;	/* use first suitable swap, if any */
  } live;

  struct {
    char *buf;
    unsigned size;
    unsigned cnt;
  } cache;			/* used internally to buffer reads in some cases */

  struct {
    char *dir;				/* modules directory */
    char *type_name[MAX_MODULE_TYPES];	/* module type names */
    char *more_file[MAX_MODULE_TYPES];	/* file name of module archive */
    int disk[MAX_MODULE_TYPES];		/* number of module disk */
    module_t *list;			/* list of all modules */
    int scsi_type;		/* for some reasons... */
    int cdrom_type;		/* dto. */
    int network_type;		/* dto. */
    int pcmcia_type;		/* dto. */
    int fs_type;		/* dto. */
    slist_t *input_params;	/* history for module loading dialog */
    slist_t *used_params;	/* parameters that were used for insmod */
    int ramdisk;		/* ramdisk used for modules */
    slist_t *broken;		/* list of modules that must not be loaded */
    slist_t *initrd;		/* extra modules for initrd */
    unsigned keep_usb_storage:1;	/* don't unload usb-storage */
    int delay;			/* wait this much after insmod */
    driver_t *drivers;		/* list of extra drive info */
    unsigned disks:1;		/* automatically ask for module disks */
    slist_t *options;		/* potential module parameters */
  } module;

  struct {
    int total;			/* memory size (in kB) */
    int free;			/* free memory (in kB) when linuxrc starts */
    int free_swap;		/* free swap */
    int current;		/* currently free memory */
    int min_free;		/* don't let it drop below this */
    int min_modules;		/* remove modules before starting yast, if it drops below this */
    int min_yast;		/* minimum for yast */
    int min_yast_text;		/* minimum for yast in text mode */
    int load_image;		/* _load_ rootimage, if we have at least that much */
    int ram;			/* ram size in MB */
    int ram_min;		/* min required memory (ram size) needed for install in MB */
  } memory;

  struct {
    char *dev;			/* device name */
    char *mountpoint;		/* mountpoint, if any */
    int inuse:1;		/* currently in use */
    int fd;			/* file descriptor while it is open */
    int size;			/* current size in kB */
  } ramdisk[6];			/* /dev/ram2 .. /dev/ram7 */

  struct {			/* mountpoints */
    char *floppy;
    char *ramdisk2;
    char *extra;
    char *instdata;
    char *instsys;
    char *instsys2;
    char *live;
    char *update;
  } mountpoint;

  struct {
    unsigned use_dhcp:1;	/* use dhcp instead of bootp */
    unsigned dhcp_active:1;	/* dhcpd is running */
    unsigned device_given:1;	/* netdevice explicity set in info file */
    unsigned ifconfig:1;	/* setup network interface */
    unsigned is_configured:1;	/* set if network is configured */
    unsigned nfs_tcp:1;		/* use TCP for NFS */
    unsigned keep:1;		/* keep network interface up */
    unsigned do_setup;		/* do network setup */
    unsigned setup;		/* bitmask: do these network setup things */
    char *device;		/* currently used device */
    slist_t *devices;		/* list of active network devs */
    slist_t *dns_cache;		/* cache dns lookups here */
    int ftp_sock;		/* used internally by ftp code */
    struct tftp tftp;		/* used by tftp code */
    int file_length;		/* length of currently retrieved file */
    char *error;		/* ftp/http/tftp error message, if any */
    char *domain;		/* domain name */
    char *nisdomain;		/* NIS domain name */
    unsigned proxyport;		/* proxy port */
    unsigned port;		/* port */
    instmode_t proxyproto;	/* http or ftp */
    int nfs_port;		/* nfs port */
    int bootp_timeout;		/* various timeout values (in s) */
    int dhcp_timeout;
    int tftp_timeout;
    int bootp_wait;		/* wait this time (in s) after network setup before starting bootp */
    int ifup_wait;		/* wait this time (in s) after network setup */
    int nfs_rsize;		/* nfs rsize mount option */
    int nfs_wsize;		/* nfs wsize mount option */
    inet_t netmask;
    inet_t network;
    inet_t broadcast;
    inet_t gateway;
    inet_t nameserver[4];	/* up to 4 nameservers */
    unsigned nameservers;	/* actual number of nameservers */
    inet_t proxy;
    inet_t hostname;
    inet_t server;
    inet_t ptphost;
    char *realhostname;		/* hostname, if explicitly set */
    char *workgroup;		/* SMB */
    char *share;		/* SMB */
    char *user;			/* if this is NULL, perform guest login */
    char *password;
    char *vncpassword;
    inet_t displayip;		/* IP of remote X server */
    char *sshpassword;
    net_config_t configured;	/* how we configured the network device */
    char *unique_id;		/* unique id of network card */
    char *hwaddr;		/* hardware addr of network card */
    char *ethtool_used;		/* ethtool options used for active card */
    struct {
      char *binary;		/* cifs/smb mount binary */
      char *module;		/* cifs/smb kernel module */
    } cifs;
    char *dhcpcd;		/* dhcpcd parameters (if any) */
  } net;

  struct {
    char *proto;		/* protocol we want (ftp, nfs, ...) */
    char *key;			/* key to match against description */
  } slp;

#if defined(__s390__) || defined(__s390x__)
  /* hwcfg file parameters */
  struct {
    char* userid;
    char* startmode;
    char* module;
    char* module_options;
    char* module_unload;
    char* scriptup;
    char* scriptup_ccw;
    char* scriptup_ccwgroup;
    char* scriptdown;
    char* readchan;
    char* writechan;
    char* datachan;
    char* ccw_chan_ids;
    int ccw_chan_num;
    int protocol;
    char* portname;
    int type;
    int interface;
    int medium;
    int layer2;
    char* osahwaddr;
  } hwp;
  
#endif

} config_t;

config_t config;

extern int             max_x_ig;
extern int             max_y_ig;
extern colorset_t     *colors_prg;
extern char           *kernellog_tg;
extern char           *lastlog_tg;
extern char           *bootmsg_tg;
extern int             pcmcia_chip_ig;
extern int             cpu_ig;
extern int             force_ri_ig;
extern char            ppcd_tg [10];
extern int             has_floppy_ig;
extern int             has_kbd_ig;
extern unsigned        frame_buffer_mode_ig;
extern int             reboot_ig;
extern int             usb_ig;
extern char            *usb_mods_ig;
extern int             reboot_ig;
extern char            xkbmodel_tg [20];
extern unsigned        yast2_color_ig;
extern int             reboot_wait_ig;
extern char            livesrc_tg[16];
extern int             cdrom_drives;
extern int             has_modprobe;
