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

#if defined (LXRC_TINY) || defined(__sparc__) || defined(__PPC__) || defined(__s390__) || defined(__s390x__) || defined(__MIPSEB__)
#define WITH_PCMCIA	0
#else
#define WITH_PCMCIA	1
#endif

// on all architectures but s390, linuxrc includes a lot of other
// functionality like 'mount/umount', 'loadkeys', ...

// on s/390 need much more than this, so we include the original tools
// instead of duplicating their functionality within linuxrc.
#if defined (__s390__) || defined (__s390x__)
#  define SWISS_ARMY_KNIFE 0
#else
#  define SWISS_ARMY_KNIFE 1
#endif

#ifndef TRUE
#define TRUE			1
#endif
#ifndef FALSE
#define FALSE			0
#endif

#define YES			1
#define NO			0
#define ESCAPE			-1

#define MAX_X			250
#define MAX_Y			150

#define BUTTON_SIZE_NORMAL	9
#define BUTTON_SIZE_LARGE	11
#define STATUS_SIZE		50

#define  LXRC_DEBUG

#ifdef LXRC_DEBUG
# define deb_wait if(config.debugwait) printf(__FUNCTION__ ":%d: Press a key...\n", __LINE__), getchar()
# define deb_msg(a) fprintf(stderr, __FUNCTION__ ":%u %s\n", __LINE__, a)
# define deb_str(a) fprintf(stderr, __FUNCTION__ ":%u " #a " = \"%s\"\n", __LINE__, a)
# define deb_int(a) fprintf(stderr, __FUNCTION__ ":%u " #a " = %d\n", __LINE__, a)
#else
# define deb_wait
# define deb_msg(a)
# define deb_str(a)
# define deb_int(a)
#endif

#define RAMDISK_2  "/dev/ram2"

#define MAX_FILENAME     300

typedef struct {
               char c;
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
               char      text [BUTTON_SIZE_LARGE];
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
  inst_dvd, inst_cdwithnet, inst_net
} instmode_t;


typedef struct {
  char *text;
  int (*func) (int);
  int di;
} item_t;


typedef struct slist_s {
  struct slist_s *next;
  char *key, *value;
} slist_t;


typedef struct {
  unsigned ok:1;		/* ip field is valid */
  struct in_addr ip;
  char *name;
} inet_t;


typedef struct {
  instmode_t scheme;
  char *server;
  char *dir;
  char *user;
  char *password;
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
} module_t;

typedef enum {
  nc_none, nc_static, nc_bootp, nc_dhcp
} net_config_t;


#define MAX_MODULE_TYPES 10

typedef struct {
  unsigned rebootmsg:1;		/* show reboot message */
  unsigned redraw_menu:1;	/* we need a better solution for this */
  unsigned initrd_has_ldso:1;	/* instsys contains a dynamic linker */
  unsigned suppress_warnings:1;	/* show less warning dialogs */
  unsigned is_iseries:1;	/* set if we run on an iSeries machine */
  unsigned win:1;		/* set if we are drawing windows */
  unsigned forceinsmod:1;	/* use 'insmod -f' if set */
  unsigned tmpfs:1;		/* we're using tmpfs for / */
  unsigned run_as_linuxrc:1;	/* set if we really are linuxrc */
  unsigned test:1;		/* we are in test mode */
  unsigned rescue:1;		/* start rescue system */
  unsigned demo:1;		/* start live cd */
  unsigned hwcheck:1;		/* do hardware check */
  unsigned shell_started:1;	/* there is a shell running on /dev/tty9 */
  unsigned extramount:1;	/* mountpoints.extra is in use */
  unsigned instdata_mounted:1;	/* install data are mounted */
  unsigned textmode:1;		/* start yast2 in text mode */
  unsigned debugwait:1;		/* pop up dialogs at some critical points */
  unsigned manual:1;		/* manual mode */
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
  unsigned fullnetsetup:1;	/* offer all network protocols */
  unsigned aborted:1;		/* yast did abort the installation */
  unsigned splash:1;		/* splash active */
  unsigned netstop:1;		/* shut down network iface at end */
  unsigned noshell:1;		/* don't start any shells */
  volatile unsigned restart_on_segv:1;	/* restart linuxrc after segfault */
  unsigned had_segv:1;		/* last linuxrc run ended with segv */
  unsigned run_memcheck:1;	/* run memcheck thread */
  unsigned hwdetect:1;		/* do automatic hardware detection */
  unsigned explode_win:1;	/* animated windows */
  unsigned scsi_before_usb:1;	/* load storage controller modules before usb/ieee1394 */
  pid_t memcheck_pid;		/* pid of memcheck thread */
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
  struct {
    char *file;			/* 'info' file name */
    char *loaded;		/* actual 'info' file that was loaded */
    unsigned add_cmdline:1;	/* parse cmdline, too */
    unsigned mod_autoload:1;	/* used internally */
    unsigned start_pcmcia:1;	/* dto  */
  } info;
  char *autoyast;		/* yast autoinstall parameter */
  char *linuxrc;		/* 'linuxrc' parameter */
  char *stderr_name;		/* stderr device name */
  int color;			/* color scheme: 0-3: undef, mono, color, alternate */
  enum langid_t language;	/* currently selected language */
  char *keymap;			/* current keymap */
  char *serverdir;		/* install base directory on server */
  instmode_t insttype;		/* install type (cdrom, network, etc. )*/
  instmode_t instmode;		/* ftp, nfs, smb, etc. */
  instmode_t instmode_extra;	/* for the stranger things... */
  int inst_ramdisk;		/* ramdisk with instsys */
  char *new_root;		/* root device to boot */
  char *installdir;		/* "/suse/inst-sys" */
  char *rootimage;		/* "/suse/images/root" */
  char *rescueimage;		/* "/suse/images/rescue" */
  char *demoimage;		/* "/suse/images/cd-demo" */
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
    unsigned delay;		/* wait this much after insmod */
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
  } mountpoint;

  struct {
    unsigned use_dhcp:1;	/* use dhcp instead of bootp */
    unsigned dhcp_active:1;	/* dhcpd is running */
    unsigned smb_available:1;	/* set if SMB functionality is available */
    unsigned device_given:1;	/* netdevice explicity set in info file */
    unsigned ifconfig:1;	/* setup network interface */
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
    int nfs_rsize;		/* nfs rsize mount option */
    int nfs_wsize;		/* nfs wsize mount option */
    inet_t netmask;
    inet_t network;
    inet_t broadcast;
    inet_t gateway;
    inet_t nameserver;
    inet_t proxy;
    inet_t hostname;
    inet_t server;
    inet_t pliphost;
    char *workgroup;		/* SMB */
    char *user;			/* if this is NULL, perform guest login */
    char *password;
    char *vncpassword;
    char *sshpassword;
    net_config_t configured;	/* how we configured the network device */
  } net;

} config_t;

config_t config;

extern int             max_x_ig;
extern int             max_y_ig;
extern colorset_t     *colors_prg;
extern char           *mountpoint_tg;
extern char           *kernellog_tg;
extern char           *lastlog_tg;
extern char           *bootmsg_tg;
extern char            netdevice_tg [20];
extern int             pcmcia_chip_ig;
extern int             cpu_ig;
extern int             force_ri_ig;
extern int             auto_ig;
extern int             auto2_ig;
extern char            machine_name_tg [100];
extern int             old_kernel_ig;
extern char            ppcd_tg [10];
extern int             yast2_update_ig;
extern int             yast2_serial_ig;
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
extern char            driver_update_dir[16];
extern int             cdrom_drives;
extern int             has_modprobe;
