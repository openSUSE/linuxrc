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

#define BOOTMODE_FLOPPY    0
#define BOOTMODE_CD        1
#define BOOTMODE_NET       2
#define BOOTMODE_HARDDISK  3
#define BOOTMODE_FTP       4
#define BOOTMODE_CDWITHNET 5
#define BOOTMODE_SMB       6

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
  unsigned       available:1;	/* set if SMB functionality is available */
  struct in_addr server;	/* SMB server to install from */
  char           *share;
  char           *workgroup;
  char           *user;		/* if this is 0, perform guest login */
  char           *password;
} smb_t;


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
  int floppies;			/* number of floppy drives */
  int floppy;			/* floppy drive recently used */
  char *floppy_dev[4];		/* list of floppy devices */
  char *instsys;		/* installation system mount point */
  smb_t smb;			/* SMB installation info */
  char *infofile;		/* 'info' file name */
  char *infoloaded;		/* actual 'info' file that was loaded */
  char *stderr_name;		/* stderr device name */
  int color;			/* color scheme: 0-3: undef, mono, color, alternate */
  enum langid_t language;	/* currently selected language */
  char *keymap;			/* current keymap */
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
} config_t;

config_t config;

extern int             max_x_ig;
extern int             max_y_ig;
extern colorset_t     *colors_prg;
extern struct in_addr  ipaddr_rg;
extern struct in_addr  netmask_rg;
extern struct in_addr  broadcast_rg;
extern struct in_addr  gateway_rg;
extern struct in_addr  network_rg;
extern struct in_addr  ftp_server_rg;
extern struct in_addr  nfs_server_rg;
extern struct in_addr  plip_host_rg;
extern struct in_addr  nameserver_rg;
extern char            server_dir_tg [MAX_FILENAME];
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
extern char            domain_name_tg [100];
extern int             old_kernel_ig;
extern int             bootp_wait_ig;
extern int             bootp_timeout_ig;
extern int             testing_ig;
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
extern int             valid_net_config_ig;
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
extern int             ask_for_moddisk;
extern int             splash_active;
extern char           *fs_types_atg [];
extern int             has_modprobe;
