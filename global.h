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
#define MAX_PARAM_LEN    256
#define STATUS_SIZE       50

#define BOOTMODE_FLOPPY    0
#define BOOTMODE_CD        1
#define BOOTMODE_NET       2
#define BOOTMODE_HARDDISK  3
#define BOOTMODE_FTP       4
#define BOOTMODE_CDWITHNET 5

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
               char  *text;
               int  (*func) (int);
               }
        item_t;

typedef struct {
  unsigned rebootmsg:1;
  unsigned redraw_menu:1;	/* we need a better solution for this */
} config_t;

config_t config;

extern int             max_x_ig;
extern int             max_y_ig;
extern enum langid_t   language_ig;
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
extern char            keymap_tg [30];
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
extern int             color_ig;
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
extern char            floppy_tg [20];
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
extern char            *mouse_type_xf86_ig;
extern char            *mouse_type_gpm_ig;
extern char            *mouse_dev_ig;
extern int             yast_version_ig;
extern int             valid_net_config_ig;
extern int             reboot_ig;
extern int             usb_ig;
extern char            *usb_mods_ig;
extern int             reboot_ig;
extern int             found_suse_cd_ig;
extern int             do_disp_init_ig;
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
