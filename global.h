/*
 *
 * global.h      Global defines for linuxrc
 *
 * Copyright (c) 1996-2001  Hubert Mantel, SuSE GmbH  (mantel@suse.de)
 *
 */


#include <netinet/in.h>
#include <inttypes.h>

#include "version.h"

// should be architecture dependent!
#define MEM_LIMIT_RAMDISK_YAST1	 93*1024*1024
#ifdef __alpha__
#define MEM_LIMIT_RAMDISK_YAST2	500*1024*1024
#else
#define MEM_LIMIT_RAMDISK_YAST2	252*1024*1024	// the Y2 partitioner needs that much...
#endif
#define MEM_LIMIT_RAMDISK_FTP	 28*1024*1024
#define MEM_LIMIT_YAST2		 28*1024*1024

#define MEM_LIMIT_SWAP_MSG	15000000
#define MEM_LIMIT_CACHE_LIBS	63000000

#if defined(__sparc__) || defined(__PPC__) || defined(__s390__) || defined(__s390x__)
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
#define TRUE               1
#endif
#ifndef FALSE
#define FALSE              0
#endif

#define YES                1
#define NO                 0
#define ESCAPE          (-1)

#define MAX_X            250
#define MAX_Y            150

#define BUTTON_SIZE        9
/* MAX_PARAM_LEN should not be less than 256 */
#define MAX_PARAM_LEN    256
#define STATUS_SIZE       50

#if 0
#define BOOTMODE_FLOPPY    0
#define BOOTMODE_CD        1
#define BOOTMODE_NET       2
#define BOOTMODE_HARDDISK  3
#define BOOTMODE_FTP       4
#define BOOTMODE_CDWITHNET 5
#define BOOTMODE_SMB       6
#endif

#define  LXRC_DEBUG

#ifdef LXRC_DEBUG
# define deb_wait if((guru_ig & 2)) printf(__FUNCTION__ ":%d: Press a key...\n", __LINE__), getchar()
# define deb_msg(a) fprintf(stderr, __FUNCTION__ ":%u %s\n", __LINE__, a)
# define deb_str(a) fprintf(stderr, __FUNCTION__ ":%u " #a " = \"%s\"\n", __LINE__, a)
# define deb_int(a) fprintf(stderr, __FUNCTION__ ":%u " #a " = %d\n", __LINE__, a)
#else
# define deb_wait
# define deb_msg(a)
# define deb_str(a)
# define deb_int(a)
#endif

#define ACT_DEMO		(1 << 0)
#define ACT_DEMO_LANG_SEL	(1 << 1)
#define ACT_LOAD_NET		(1 << 2)
#define ACT_LOAD_DISK		(1 << 3)
#define ACT_YAST2_AUTO_INSTALL	(1 << 4)
#define ACT_RESCUE		(1 << 5)
#define ACT_NO_PCMCIA		(1 << 6)
#define ACT_DEBUG		(1 << 7)

#include "po/text_langids.h"

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
               char      text [BUTTON_SIZE];
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
  inst_dvd, inst_cdwithnet
} instmode_t;

#define BOOTMODE_FLOPPY    inst_floppy
#define BOOTMODE_CD        inst_cdrom
#define BOOTMODE_NET       inst_nfs
#define BOOTMODE_HARDDISK  inst_hd
#define BOOTMODE_FTP       inst_ftp
#define BOOTMODE_CDWITHNET inst_cdwithnet
#define BOOTMODE_SMB       inst_smb

typedef enum {
  insttype_cdrom, insttype_hd, insttype_floppy, insttype_net
} insttype_t;


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
} module2_t;


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
  int floppies;			/* number of floppy drives */
  int floppy;			/* floppy drive recently used */
  char *floppy_dev[4];		/* list of floppy devices */
  char *instsys;		/* installation system mount point */
  struct {
    char *file;			/* 'info' file name */
    char *loaded;		/* actual 'info' file that was loaded */
    unsigned add_cmdline:1;	/* parse cmdline, too */
  } info;
  char *stderr_name;		/* stderr device name */
  int color;			/* color scheme: 0-3: undef, mono, color, alternate */
  enum langid_t language;	/* currently selected language */
  char *keymap;			/* current keymap */
  char *serverdir;		/* install base directory on server */
  instmode_t instmode;		/* ftp, nfs, smb, etc. */
  insttype_t insttype;		/* install type (cdrom, network, etc. )*/

  struct {
    char *dir;				/* modules directory */
    char *type_name[MAX_MODULE_TYPES];	/* module type names */
    char *more_file[MAX_MODULE_TYPES];	/* file name of module archive */
    module2_t *list;			/* list of all modules */
    int scsi_type;		/* for historical reasons... */
    int cdrom_type;		/* dto. */
    int network_type;		/* dto. */
    int pcmcia_type;		/* dto. */
    slist_t *input_params;	/* history for module loading dialog */
    slist_t *used_params;	/* parameters that were used for insmod */
    unsigned ramdisk:1;		/* ramdisk currently mounted to dir */
  } module;

  struct {			/* mountpoints */
    char *floppy;
    char *ramdisk2;
  } mountpoint;

  struct {
    unsigned use_dhcp:1;	/* use dhcp instead of bootp */
    unsigned dhcp_active:1;	/* dhcpd is running */
    unsigned smb_available:1;	/* set if SMB functionality is available */
    char *domain;		/* domain name */
    char *nisdomain;		/* NIS domain name */
    unsigned proxyport;		/* proxy port */
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
  } net;

} config_t;

config_t config;

extern int             max_x_ig;
extern int             max_y_ig;
extern colorset_t     *colors_prg;
extern char            rootimage_tg [MAX_FILENAME];
extern char           *mountpoint_tg;
extern char           *inst_mountpoint_tg;
extern char           *kernellog_tg;
extern char           *lastlog_tg;
extern char           *bootmsg_tg;
extern char            installdir_tg [MAX_FILENAME];
extern char            harddisk_tg [12];
extern char           *fstype_tg;
extern char            scsi_tg [20];
extern char            net_tg [20];
extern char            netdevice_tg [20];
extern char            cdrom_tg [20];
extern int             bootmode_ig;
extern int             pcmcia_chip_ig;
extern uint64_t        memory_ig;
extern int             cpu_ig;
extern int             force_ri_ig;
extern int             ramdisk_ig;
extern int             explode_win_ig;
extern int             auto_ig;
extern int             demo_ig;
extern int             auto2_ig;
extern int             nfsport_ig;
extern char            machine_name_tg [100];
extern int             old_kernel_ig;
extern int             bootp_wait_ig;
extern int             bootp_timeout_ig;
extern int             passwd_mode_ig;
extern char            ftp_user_tg [20];
extern char            ftp_password_tg [20];
extern char            ftp_proxy_tg [50];
extern int             ftp_proxyport_ig;
extern char            ppcd_tg [10];
extern int             serial_ig;
extern char            console_tg [30];
extern char            console_parms_tg [30];
extern int             smp_ig;
extern int             guru_ig;
extern int             text_mode_ig;
extern int             yast2_update_ig;
extern int             yast2_serial_ig;
extern int             has_floppy_ig;
extern int             has_kbd_ig;
extern unsigned        frame_buffer_mode_ig;
extern int             yast_version_ig;
extern int             reboot_ig;
extern int             usb_ig;
extern char            *usb_mods_ig;
extern int             reboot_ig;
extern int             found_suse_cd_ig;
extern char            xkbrules_tg [20];
extern char            xkbmodel_tg [20];
extern char            xkblayout_tg [20];
extern unsigned        yast2_color_ig;
extern unsigned        action_ig;
extern int             reboot_wait_ig;
extern char            *braille_ig;
extern char            *braille_dev_ig;
extern char            *x11i_tg;
extern char            livesrc_tg[16];
extern char            driver_update_dir[16];
extern int             cdrom_drives;
extern int             splash_active;
extern char           *fs_types_atg [];
extern int             has_modprobe;
