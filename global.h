/*
 *
 * global.h      Global defines for linuxrc
 *
 * Copyright (c) 1996-2000  Hubert Mantel, SuSE GmbH  (mantel@suse.de)
 *
 */


#include <netinet/in.h>
#include <inttypes.h>

#ifdef LINUXRC_AXP
#define MEM_LIMIT1_RAMDISK    50000000
#define MEM_LIMIT2_RAMDISK    50000000
#else
#define MEM_LIMIT1_RAMDISK    46000000
#define MEM_LIMIT2_RAMDISK    46000000
#endif
#define MEM_LIMIT_SWAP_MSG     6500000
#define MEM_LIMIT_CACHE_LIBS  15000000
#define MEM_LIMIT_YAST2       12000000

#define LXRC_VERSION  "1.0.3"

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

enum langid_t
    {
    LANG_UNDEF,
    LANG_GERMAN,
    LANG_ENGLISH,
    LANG_SPANISH,
    LANG_ITALIAN,
    LANG_FRENCH,
    LANG_BRAZIL,
    LANG_GREEK,
    LANG_HUNGARIA,
    LANG_POLISH,
    LANG_DUTCH,
    LANG_ROMANIA,
    LANG_RUSSIA,
    LANG_SLOVAK,
    LANG_INDONESIA,
    LANG_PORTUGUESE,
    LANG_ROMANIAN,
    LANG_CZECH,
    LANG_TURKEY,
    LANG_BRETON
    };

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
extern int             bogomips_ig;
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
extern int             testing_ig;
extern int             passwd_mode_ig;
extern char            ftp_user_tg [20];
extern char            ftp_password_tg [20];
extern char            ftp_proxy_tg [50];
extern int             ftp_proxyport_ig;
extern char            floppy_tg [10];
extern char            ppcd_tg [10];
extern int             serial_ig;
extern char            console_tg [30];
extern int             smp_ig;
#ifdef LXRC_DEBUG
extern int             guru_ig;
#endif
extern int             text_mode_ig;
extern int             yast2_update_ig;
extern int             has_floppy_ig;
extern unsigned        frame_buffer_mode_ig;
extern char            *mouse_type_ig;
extern char            *mouse_dev_ig;
extern int             yast_version_ig;
extern int             valid_net_config_ig;
extern int             reboot_ig;
