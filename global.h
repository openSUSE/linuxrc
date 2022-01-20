/*
 *
 * global.h      Global defines for linuxrc
 *
 * Copyright (c) 1996-2002  Hubert Mantel, SuSE Linux AG  (mantel@suse.de)
 *
 */


#include <stdio.h>
#include <netinet/in.h>
#include <netinet/ip6.h>
#include <inttypes.h>

#include <blkid/blkid.h>

#include <hd.h>
extern str_list_t *search_str_list(str_list_t *sl, char *str);
extern str_list_t *add_str_list(str_list_t **sl, char *str);
extern char *hd_join(char *del, str_list_t *str);

/* lang_undef _must_ be 0 */
enum langid_t {
  lang_undef,
  lang_en,
  lang_pt,
  lang_sk,
  lang_de,
  lang_it,
  lang_ja,
  lang_nl,
  lang_ru,
  lang_zh_TW,
  lang_el,
  lang_nb,
  lang_sl,
  lang_pt_BR,
  lang_zh_CN,
  lang_xh,
  lang_pl,
  lang_bg,
  lang_da,
  lang_zu,
  lang_fr,
  lang_af,
  lang_es,
  lang_uk,
  lang_ca,
  lang_hu,
  lang_fi,
  lang_sv,
  lang_cs,
  lang_dummy
};

#include "settings.h"

#include "version.h"

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

/* terminal sizes */
#define X_DEFAULT		80
#define Y_DEFAULT		24
#define MAX_X			500
#define MAX_Y			150
#define MIN_X			8
#define MIN_Y			4

#define BUTTON_SIZE_NORMAL	8
#define BUTTON_SIZE_LARGE	10
#define STATUS_SIZE		50

#define LXRC_WAIT util_wait(__FILE__, __LINE__, __FUNCTION__);

// LOG_* are bitmasks
#define LOG_LEVEL_SHOW	(1 << 0)
#define LOG_LEVEL_INFO	(1 << 1)
#define LOG_LEVEL_DEBUG	(1 << 2)
// add time stamps to log entries
#define LOG_TIMESTAMP	(1 << 3)
// add calling function name to log entries
#define LOG_CALLER	(1 << 4)

// log to the default console
#define log_show(...) util_log(LOG_LEVEL_SHOW, __VA_ARGS__)
// log to the logging console
#define log_info(...) util_log(LOG_LEVEL_INFO, __VA_ARGS__)
// log to the log file
#define log_debug(...) util_log(LOG_LEVEL_DEBUG, __VA_ARGS__)
// log to the default console if cond is true
// else log to logging console
#define log_show_maybe(cond, ...) util_log((cond) ? LOG_LEVEL_SHOW : LOG_LEVEL_INFO, __VA_ARGS__)
// log to the logging console if cond is true
// else log only to log file
#define log_info_maybe(cond, ...) util_log((cond) ? LOG_LEVEL_INFO : LOG_LEVEL_DEBUG, __VA_ARGS__)

// perror() equivalents
#define perror_show(a) util_perror(LOG_LEVEL_SHOW, a)
#define perror_info(a) util_perror(LOG_LEVEL_INFO, a)
#define perror_debug(a) util_perror(LOG_LEVEL_DEBUG, a)

// run command and redirect stdout & stderr to log file
#define lxrc_run(a) util_run(a, 1)

// run command and redirect stderr to log file
#define lxrc_run_console(a) util_run(a, 0)

#define RAMDISK_2  "/dev/ram2"

#define MAX_FILENAME     300

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
  inst_http, inst_https, inst_tftp, inst_cdrom, inst_floppy, inst_hd,
  inst_dvd, inst_cdwithnet, inst_net, inst_slp, inst_exec,
  inst_rel, inst_disk, inst_usb, inst_label,
  inst_extern ///< must be last
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


/** IPv4 and/or IPv6 address, with prefix, and hostname */
typedef struct {
  unsigned ok:1;		/**< at least ip or ip6 is valid */
  unsigned ipv4:1;		/**< 1: valid ipv4 */
  unsigned ipv6:1;		/**< 1: valid ipv6 */
  struct in_addr ip;		/**< v4 address */
  struct in6_addr ip6;		/**< v6 address */
  struct in_addr net;		/**< network mask based on prefix4 */
  unsigned prefix4;		/**< v4 network prefix length (if any) */
  unsigned prefix6;		/**< v6 network prefix length (if any) */
  char *name;			/**< hostname */
} inet_t;


typedef struct module2_s {
  struct module2_s *next;
  char *name;		/**< module name */
  char *descr;		/**< a description */
  char *param;		/**< sample module params */
  char *pre_inst;	/**< load these before */
  char *post_inst;	/**< load these after */
  int type;		/**< category, e.g. scsi, cdrom, network... */
  unsigned initrd:1;	/**< add it to initrd */
  unsigned autoload:1;	/**< can be autoloaded */
  unsigned exists:1;	/**< module really exists */
  unsigned dontask:1;	/**< don't ask for module params */
  unsigned detected:1;	/**< auto-detected */
  unsigned active:1;	/**< module loaded */
} module_t;


typedef struct driver_s {
  struct driver_s *next;
  char *name;		/**< module name */
  char *sysfs_name;	/**< sysfs directory name */
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
  nc_none, nc_static, nc_dhcp
} net_config_t;


typedef enum {
  wa_none, wa_open, wa_wpa_psk, wa_wpa_peap
} wlan_auth_t;


typedef struct {
  char *str;
  instmode_t scheme;
  char *server;
  char *share;
  char *path;
  char *user;
  char *password;
  char *domain;
  char *device;
  char *instsys;
  char *mount;
  char *tmp_mount;
  char *rewrite_for_zypp;	/**< pass this value instead of the original url to zypp */
  unsigned port;
  slist_t *query;
  slist_t *file_list;
  unsigned download:1;		/**< force download */
  unsigned search_all:1;	/**< dud: search all possible local storage devices */
  unsigned quiet:1;		/**< dud: don't report if nothing was found */
  struct {
    unsigned network:1;		/**< scheme needs network */
    unsigned mountable:1;	/**< scheme is mountable */
    unsigned cdrom:1;		/**< device is cdrom */
    unsigned file:1;		/**< path points to file (not to directory) */
    unsigned dir:1;		/**< path points to directory (not to file ) */
    unsigned wlan:1;		/**< wlan interface */
    unsigned blockdev:1;	/**< needs block device */
    unsigned nodevneeded:1;	/**< does not need any device */
  } is;
  struct {
    char *device;
    char *hwaddr;
    char *model;
    char *unique_id;
  } used;
  struct {
    /*
     * The original values of some url components that might get modified in url_set().
     */
    instmode_t scheme;
    char *server;
    char *share;
    char *path;
    char *instsys;
  } orig;
} url_t;


typedef struct ifcfg_s {
  struct ifcfg_s *next;
  char *device;		///< interface name or shell glob matching interface names
  char *type;		///< dhcp{,4,6} or static
  unsigned dhcp:1;	///< use dhcp
  unsigned used:1;	///< config has been used
  unsigned pattern:1;	///< 'device' is shell glob
  unsigned ptp:1;	///< ptp config, gw is ptp peer
  unsigned search:1;    ///< whether "try" feature is enabled for this ifcfg
  unsigned rfc2132:1;	///< set DHCLIENT_CREATE_CID=rfc2132
  int netmask_prefix;	///< prefix given via netmask option and only used if an ip doen't have one
  char *vlan;		///< vlan id, if any
  char *ip;		///< list of ip addresses, space separated
  char *gw;		///< gateway
  char *ns;		///< nameserver list, space separated
  char *domain;		///< domain search list
  slist_t *flags;	///< extra settings for ifcfg, key=value
} ifcfg_t;


/*
 * struct holding logging file info
 */
typedef struct {
  unsigned level;
  char *name;
  FILE *f;
} log_file_t;


/* > 100 and <= 1000 */
#define MAX_UPDATES		1000

#define MAX_MODULE_TYPES	10

/* config.net.do_setup bitmasks */

#define DS_SETUP		(1 << 0)	/**< NetSetup option has been used */
#define DS_INSTALL		(1 << 1)
#define DS_VNC			(1 << 2)
#define DS_SSH			(1 << 3)

/* config.net.setup bitmasks */

/* reserved 			(1 << 0) */
#define NS_DHCP			(1 << 1)
#define NS_HOSTIP		(1 << 2)
#define NS_VLANID		(1 << 3)
#define NS_GATEWAY		(1 << 4)
#define NS_NAMESERVER		(1 << 5)
#define NS_DISPLAY		(1 << 7)
#define NS_NOW			(1 << 9)

#if defined(__s390__) || defined(__s390x__)
#define NS_DEFAULT		(NS_DHCP | NS_HOSTIP | NS_GATEWAY | NS_NAMESERVER | NS_DISPLAY | NS_VLANID)
#else
#define NS_DEFAULT		(NS_DHCP | NS_HOSTIP | NS_GATEWAY | NS_NAMESERVER)
#endif

#define PLY_MODE_UPDATE		"updates"
#define PLY_MODE_UPGRADE	"system-upgrade"
#define PLY_MODE_REBOOT		"reboot"
#define PLY_MODE_SHUTDOWN	"shutdown"

typedef struct {
  unsigned rebootmsg:1;		/**< show reboot message */
  unsigned redraw_menu:1;	/**< we need a better solution for this */
  unsigned initrd_has_ldso:1;	/**< instsys contains a dynamic linker */
  unsigned suppress_warnings:1;	/**< show less warning dialogs */
  unsigned noerrors:1;		/**< no error messages */
  unsigned is_iseries:1;	/**< set if we run on an iSeries machine */
  unsigned win:1;		/**< set if we are drawing windows */
  unsigned forceinsmod:1;	/**< use 'insmod -f' if set */
  unsigned tmpfs:1;		/**< we're using tmpfs for / */
  unsigned run_as_linuxrc:1;	/**< set if we really are linuxrc */
  unsigned test:1;		/**< we are in test mode */
  unsigned rescue:1;		/**< start rescue system */
  unsigned shell_started:1;	/**< there is a shell running on /dev/tty9 */
  unsigned extramount:1;	/**< mountpoints.extra is in use */
  unsigned textmode:1;		/**< start yast2 in text mode */
  unsigned debugwait:1;		/**< pop up dialogs at some critical points */
  unsigned debugwait_off:1;	/**< force debugwait off */
  unsigned linemode:2;		/**< line mode */
  unsigned ask_language:1;	/**< let use choose language  */
  unsigned ask_keytable:1;	/**< let user choose keytable */
  unsigned use_ramdisk:1;	/**< used internally */
  unsigned vnc:1;		/**< vnc mode */
  unsigned usessh:1;		/**< ssh mode */
  unsigned sshd_only:1;		/**< start only sshd */
  unsigned addswap:2;		/**< offer to add swap if yast needs it */
  unsigned aborted:1;		/**< yast did abort the installation */
  unsigned netstop:2;		/**< shut down network interface at end 0: no, 1: yes, 3: auto */
  unsigned noshell:1;		/**< don't start any shells */
  volatile unsigned restart_on_segv:1;	/**< restart linuxrc after segfault */
  unsigned had_segv:1;		/**< last linuxrc run ended with segv */
  unsigned explode_win:1;	/**< animated windows */
  unsigned scsi_before_usb:1;	/**< load storage controller modules before usb/ieee1394 */
  unsigned scsi_rename:1;	/**< ensure hotplug scsi devs are last */
  unsigned debug;		/**< debug */
  unsigned manual;		/**< manual mode */
  unsigned utf8:1;		/**< in utf8 mode */
  unsigned fb:1;		/**< has frame buffer */
  unsigned instsys_complain:2;	/**< check instsys id */
  unsigned update_complain:2;	/**< check for certain updates */
  unsigned startshell:1;	/**< start shell before & after yast */
  unsigned listen:1;		/**< listen on port */
  unsigned zombies:1;		/**< keep zombies around */
  unsigned mediacheck:1;	/**< check media */
  unsigned installfilesread:1;	/**< already got install files */
  unsigned zen;			/**< zenworks mode */
  char *zenconfig;		/**< zenworks config file */
  unsigned ntfs_3g:1;		/**< use ntfs-3g */
  unsigned secure:1;		/**< secure mode (check digest of all downloaded files) */
  unsigned secure_always_fail:1;	/**< in secure mode: never ask the user but always fail directly */
  unsigned sslcerts:1;		/**< whether to check ssl certificates */
  unsigned sig_failed:2;	/**< signature check failed (1: not signed, 2: wrong signature) */
  unsigned kexec_reboot:1;	/**< kexec to installed system (just passed to yast) */
  unsigned nomodprobe:1;	/**< disable modprobe */
  unsigned y2gdb:1;		/**< pass to yast */
  unsigned squash:1;		/**< convert archive files to squashfs after download */
  unsigned keepinstsysconfig:1;	/**< don't reload instsys config data */
  unsigned device_by_id:1;	/**< use /dev/disk/by-id device names */
  unsigned withiscsi;		/**< iSCSI parameter */
  unsigned withfcoe;		/**< FCoE parameter */
  unsigned withipoib;		/**< IPoIB */
  unsigned restart_method;	/**< 0: start new root fs, 1: reboot, 2: halt, 3: kexec */
  unsigned efi_vars:1;		/**< efi vars exist */
  int efi;			/**< use efi; -1 = auto */
  unsigned udev_mods:1;		/**< let udev load modules */
  unsigned error_trace:1;	/**< enable backtrace log */
  unsigned early_bash:1;	/**< start bash on tty8 */
  unsigned devtmpfs:1;		/**< mount devtmpfs */
  unsigned plymouth:1;		/**< start plymouth */
  unsigned restarting:1;	/**< we are preparing for restart */
  unsigned restarted:1;		/**< we have been restarted */
  unsigned wicked:1;		/**< use wicked for network setup */
  unsigned nanny:1;		/**< use wickedd-nanny */
  unsigned nanny_set:1;		/**< nanny setting was changed */
  unsigned upgrade:1;		/**< upgrade or fresh install */
  unsigned media_upgrade:1;	/**< upgrade using media */
  unsigned systemboot:1;	/**< boot installed system */
  unsigned extend_running:1;	/**< currently running an 'extend' job */
  unsigned repomd:1;		/**< install repo is repo-md */
  unsigned norepo:1;            /**< disable repo location check, expect YaST */
  unsigned auto_assembly:1;	/**< enable MD/RAID auto-assembly */
  unsigned autoyast_parse:1;	/**< analyse autoyast parameter */
  unsigned autoyast_passurl:1;	/**< pass autoyast url unmodified on to yast */
  unsigned device_auto_config:2;	/**< run s390 device auto-config (cf. bsc#1168036) */
  unsigned device_auto_config_done:1;	/**< set after s390 device auto-config has been run */
  unsigned lock_device_list;	/**< prevent device list updates if != 0 */
  unsigned switch_to_fb:2;	/**< switch to framebuffer device; 0: no, 1: auto, 2: always */
  struct {
    char *root_size;		/**< zram root fs size (e.g. "1G" or "512M") */
    char *swap_size;		/**< zram swap size (e.g. "1G" or "512M") */
  } zram;
  struct {
    unsigned check:1;		/**< check for braille displays and start brld if found */
    char *dev;			/**< braille device */
    char *type;			/**< braille driver */
  } braille;
  slist_t *debugwait_list;	/**< list of positions to stop at; see debugwait */
  char *instsys_id;		/**< instsys id */
  char *initrd_id;		/**< initrd id */
  slist_t *disks;		/**< list of harddisk, without '/dev/' */
  slist_t *partitions;		/**< list of partitions, without '/dev/' */
  char *partition;		/**< currently used partition (hd install), without '/dev/' */
  slist_t *cdroms;		/**< list of cdroms, without '/dev/' */
  char *cdrom;			/**< currently used cdrom, without '/dev/' */
  slist_t *swaps;		/**< swap partitions, without '/dev/' */
  char *cdromdev;		/**< cdrom device specified via config file (no '/dev/') */
  struct {
    slist_t *file;		/**< 'info' file name */
    unsigned add_cmdline:1;	/**< parse cmdline, too */
  } info;
  char *yepurl;			/**< just pass it to yast */
  char *supporturl;		/**< just pass it to yast */
  slist_t *linuxrc;		/**< 'linuxrc' parameters */
  int color;			/**< color scheme: 0-3: undef, mono, color, alternate */
  enum langid_t language;	/**< currently selected language */
  char *keymap;			/**< current keymap */
  unsigned keymap_set:1;	/**< explicitly set via 'keytable' option */
  unsigned sourcetype:1;	/**< 0: directory, 1: file */
  char *new_root;		/**< root device to boot */
  char *rootimage;		/**< "boot/<arch>/root" */
  char *rescueimage;		/**< "boot/<arch>/rescue" */
  char *rootimage2;		/**< additional root image */
  char *term;			/**< TERM var */
  char *cdid;			/**< set if we found a install CD */
  int usbwait;			/**< sleep this much after loading usb modules */
  char *setupcmd;		/**< command used to start the install program */
  char **argv;			/**< store argv here */
  uint64_t segv_addr;		/**< segfault addr if last linuxrc run */
  char *console;		/**< console device */
  unsigned console_option:1;	/**< whether 'console' kernel boot option was used */
  char *serial;			/**< serial console parameters, e.g. ttyS0,38400 or ttyS1,9600n8 */
  char *product;		/**< product name */
  char *product_dir;		/**< product specific dir component (e.g. 'suse') */
  char *releasever;		/**< product version, to be used for replacing $releasever in zypp */
  int kbdtimeout;		/**< keyboard timeout (in s) */
  int escdelay;			/**< timeout to differ esc from function keys */
  int loglevel;			/**< set kernel log level */
  char *loghost;		/**< syslog host */
  char *rootpassword;
  int kbd_fd;			/**< fd for console */
  slist_t *ethtool;		/**< ethtool options */
  slist_t *cd1texts;		/**< text for requesting next product cd */
  unsigned swap_file_size;	/**< swap file size in MB */
  window_t progress_win;	/**< download status window */
  hd_data_t *hd_data;		/**< device list */
  char *kexec_kernel;		/**< kernel image for kexec */
  char *kexec_initrd;		/**< initrd image for kexec */
  char *device;			/**< local device to use */
  char *vga;			/**< vga option */
  int vga_mode;			/**< vga mode number */
  slist_t *extend_list;		/**< list of loaded instsys extensions */
  slist_t *udevrules;		/**< udev rules */
  char *namescheme;		/**< device name scheme (e.g.: by-id, by-label, by-path) */
  slist_t *ptoptions;		/**< pass-through options: options that just need to be added /etc/install.inf */
  char *change_config;		/**< for 'change config option' input field */
  char *run_command;		/**< for 'run command' input field */
  slist_t *defaultrepo;		/**< default repo locations */
  char *debugshell;		/**< command to run if we want to start a shell for debugging */
  slist_t *extern_scheme;	/**< externally handled URL schemes */
  slist_t *dia_extra_texts;	/**< dynamically defined dialog entries; cf. dia_get_text_id() */
  char *self_update_url;	/**< URL of YaST update for self-update */
  unsigned self_update:1;	/**< enables YaST self-update feature */
  char *core;			/**< linuxrc code dump destination (core dumps disabled if unset) */
  unsigned core_setup:1;	/**< linuxrc core dumps have been configured */
  slist_t *repomd_data;		/**< parsed repomd.xml info */
  unsigned kexec;		/**< kexec to kernel & initrd from repo (if inst-sys does not match)
                                 * 0: never
                                 * 1: always
                                 * 2: if necessary, with user dialog (default)
                                 * 3: if necessary, no user dialog
                                 */
  char *platform_name;           /* Human-readable name of the hardware */
  slist_t *extend_option;	/**< list of instsys extensions given via 'extend' boot option */

  struct {
    unsigned failed:1;		/**< digest check failed */
    slist_t *supported;		/**< supported digests (list of names) */
    slist_t *list;		/**< list of file digests */
  } digests;

  struct {
    char *instsys_default;	/**< default instsys url */
    slist_t *instsys_deps;	/**< instsys dependencies */
    slist_t *instsys_list;	/**< instsys list */
    url_t *install;		/**< install url */
    url_t *instsys;		/**< instsys url */
    url_t *proxy;		/**< proxy url */
    url_t *autoyast;		/**< yast autoinstall parameter */
    url_t *autoyast2;		/**< alternative yast autoinstall parameter */
  } url;

  struct {			/**< libblkid related things */
    blkid_cache cache;
  } blkid;

  struct {
    char *dir;			/**< driver update source dir */
    char *dst;			/**< driver update destination dir */
    char *dev;			/**< device recently used for updates (if any) */
    unsigned count;		/**< driver update count */
    unsigned ext_count;		/**< driver update instsys extension count */
    unsigned next;		/**< next driver update to do */
    unsigned style:1;		/**< 0: new style, 1: old style */
    unsigned ask:1;		/**< 1: ask for update disk */
    unsigned shown:1;		/**< 1: update dialog has been shown at least once */
    unsigned name_added:1;	/**< set if driver update has a name */
    char *id;			/**< current id, if any */
    unsigned prio;		/**< priority */
    unsigned char *map;		/**< track updates */
    slist_t *id_list;		/**< list of updates */
    slist_t *name_list;		/**< list of update names */
    slist_t **next_name;	/**< points into name_list */
    slist_t *expected_name_list;	/**< updates we must have */
    slist_t *urls;		/**< update sources */
  } update;

  struct {
    char *buf;
    unsigned size;
    unsigned cnt;
  } cache;			/**< used internally to buffer reads in some cases */

  struct {
    char *dir;				/**< modules directory */
    char *type_name[MAX_MODULE_TYPES];	/**< module type names */
    char *more_file[MAX_MODULE_TYPES];	/**< file name of module archive */
    int disk[MAX_MODULE_TYPES];		/**< number of module disk */
    module_t *list;			/**< list of all modules */
    int scsi_type;		/**< for some reasons... */
    int cdrom_type;		/**< dto. */
    int network_type;		/**< dto. */
    int pcmcia_type;		/**< dto. */
    int fs_type;		/**< dto. */
    slist_t *input_params;	/**< history for module loading dialog */
    slist_t *used_params;	/**< parameters that were used for insmod */
    slist_t *broken;		/**< list of modules that must not be loaded */
    slist_t *initrd;		/**< extra modules for initrd */
    unsigned keep_usb_storage:1;	/**< don't unload usb-storage */
    int delay;			/**< wait this much after insmod */
    driver_t *drivers;		/**< list of extra drive info */
    unsigned disks:1;		/**< automatically ask for module disks */
    slist_t *options;		/**< potential module parameters */
    unsigned modprobe_ok:1;	/**< if set, using modprobe is possible */
  } module;

  struct {			/**< Note: all memory sizes are now in bytes */
    int64_t total;		/**< memory size */
    int64_t free;		/**< free memory when linuxrc starts */
    int64_t free_swap;		/**< free swap */
    int64_t current;		/**< currently free memory */
    int64_t min_free;		/**< don't let it drop below this */
    int64_t min_yast;		/**< minimum for yast */
    int64_t load_image;		/**< _load_ rootimage, if we have at least that much */
    int64_t ram;		/**< ram size */
    int64_t ram_min;		/**< min required memory (ram size) needed for install */
  } memoryXXX;

  struct {			/**< mountpoints */
    unsigned cnt;		/**< mp counter */
    unsigned initrd_parts;	/**< initrd parts counter */
    char *instdata;
    char *instsys;
    char *update;
    char *swap;
    char *base;
  } mountpoint;

  struct {
    unsigned cnt;		/**< download counter */
    unsigned instsys:1;		/**< download instsys */
    unsigned instsys_set:1;	/**< the above was explicitly set */
    char *base;			/**< base dir for downloads */
  } download;

  struct {
    unsigned dhcp_active:1;	/**< dhcpd is running */
    unsigned ifconfig:1;	/**< setup network interface */
    unsigned all_ifs:1;		/**< try all interfaces */
    unsigned now:1;		/**< configure network _now_ */
    unsigned ipv4:1;		/**< do ipv4 config */
    unsigned ipv6:1;		/**< do ipv6 config */
    unsigned dhcp_timeout_set:1;	/**< dhcp_timeout was set explicitly */
    unsigned sethostname:1;	/**< wicked should set hostname */
    unsigned sethostname_used:1;	/**< user has used linuxrc's SetHostname option */
    unsigned do_setup;		/**< do network setup */
    unsigned setup;		/**< bitmask: do these network setup things */
    char *device;		/**< currently used device */
    slist_t *devices;		/**< list of active network devs */
    slist_t *dns_cache;		/**< cache dns lookups here */
    int file_length;		/**< length of currently retrieved file */
    char *nisdomain;		/**< NIS domain name */
    int dhcp_timeout;
    int tftp_timeout;
    int ifup_wait;		/**< wait this time (in s) after network setup */
    struct {
      char *opts;		/**< mount options string */
      unsigned rsize;		/**< nfs rsize mount option */
      unsigned wsize;		/**< nfs wsize mount option */
      unsigned udp:1;		/**< udp instead of tcp */
      unsigned vers;		/**< nfs version (2 or 3) */
    } nfs;
    int retry;			/**< max retry count for network connections */
    inet_t netmask;
    inet_t network;
    inet_t gateway;
    inet_t nameserver[4];	/**< up to 4 nameservers */
    unsigned nameservers;	/**< actual number of nameservers */
    inet_t proxy;
    inet_t hostname;
    char *realhostname;		/**< hostname, if explicitly set */
    char *workgroup;		/**< SMB */
    char *share;		/**< SMB */
    char *user;			/**< if this is NULL, perform guest login */
    char *vncpassword;
    char *displayip;		/**< name of remote X server */
    char *sshpassword;		/**< inst-sys root password */
    char *sshpassword_enc;	/**< encrypted inst-sys root password */
    char *sshkey;		/**< url pointing to ssh key */
    net_config_t configured;	/**< how we configured the network device */
    char *ethtool_used;		/**< ethtool options used for active card */
    char *dhcpfail;		/**< dhcp failure action */
    struct {
      char *binary;		/**< cifs/smb mount binary */
      char *module;		/**< cifs/smb kernel module */
    } cifs;
    struct {
      wlan_auth_t auth;		/**< open, wpa_psk, wpa_peap */
      char *essid;		/**< ESSID */
      char *wpa_psk;		/**< wpa pre-shared key */
      char *wpa_identity;	/**< wpa peap identity */
      char *wpa_password;	/**< wpa peap password */
      slist_t *devices;		/**< list of wlan interfaces */
      unsigned devices_fixed:1;	/**< device list updated only via WlanDevices config option (for debugging) */
    } wlan;
  } net;

  struct {
    ifcfg_t *list;		/**< list of ifcfg entries */
    ifcfg_t *manual;		/**< ifcfg data for manual network setup */
    ifcfg_t *all;		/**< all we ever did, kept for debugging */
    slist_t *initial;		/**< list of initially set up network interfaces */
    slist_t *if_state;		/**< config state of network interfaces */
    slist_t *if_up;		/**< network interfaces != lo that are 'up' */
    char *current;		/**< interface name for last written ifcfg file */
    slist_t *to_global;		/**< keys that go to global /etc/sysconfig/network/config */
    slist_t *ibft;		/**< list of ibft interfaces (not to be configured by linuxrc) */
  } ifcfg;

  struct {
    log_file_t dest[3];		/**< logging destinations, see linuxrc.c */
  } log;

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
    int type;
    int interface;
    int layer2;
    int portno;
    char* osahwaddr;
    char* hypervisor;
  } hwp;
  
#endif

} config_t;

extern config_t config;

extern int             max_x_ig;
extern int             max_y_ig;
extern colorset_t     *colors_prg;
extern char           *kernellog_tg;
extern char           *lastlog_tg;
extern char           *bootmsg_tg;
extern char            xkbmodel_tg [20];
extern int             reboot_wait_ig;
