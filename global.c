/*
 *
 * global.c      Global data
 *
 * Copyright (c) 1996-2000  Hubert Mantel, SuSE GmbH  (mantel@suse.de)
 *
 */

#include "global.h"

#define X_DEFAULT         80
#define Y_DEFAULT         25

int             max_x_ig = X_DEFAULT;
int             max_y_ig = Y_DEFAULT;

enum langid_t   language_ig = LANG_UNDEF;

colorset_t     *colors_prg;
struct in_addr  ipaddr_rg;
struct in_addr  netmask_rg;
struct in_addr  broadcast_rg;
struct in_addr  gateway_rg;
struct in_addr  network_rg;
struct in_addr  nfs_server_rg;
struct in_addr  ftp_server_rg;
struct in_addr  plip_host_rg;
struct in_addr  nameserver_rg;
char            rootimage_tg [MAX_FILENAME] = "/suse/images/root";
char           *mountpoint_tg = "/var/adm/mount";
char           *inst_mountpoint_tg = "/inst-img";
char            server_dir_tg [MAX_FILENAME] = "/cdrom";
char           *kernellog_tg = "/tmp/kernellog";
char           *lastlog_tg = "/tmp/lastlog";
char           *bootmsg_tg = "/var/log/boot.msg";
char            installdir_tg [MAX_FILENAME] = "/suse/inst-sys";
char            harddisk_tg [12] = "/dev/hda1";
char           *fstype_tg;
char            scsi_tg [20];
char            net_tg [20];
char            netdevice_tg [20] = "eth0";
char            cdrom_tg [20];
char            keymap_tg [30];
int             bootmode_ig = BOOTMODE_CD;
int             pcmcia_chip_ig = 0;
int             bogomips_ig = 0;
uint64_t        memory_ig = 8192000;
int             cpu_ig = 0;
int             force_ri_ig = FALSE;
int             ramdisk_ig = FALSE;
int             explode_win_ig = TRUE;
int             auto_ig = FALSE;
int             auto2_ig = FALSE;
int             demo_ig = FALSE;
int             color_ig = FALSE;
int             nfsport_ig = 0;
char            machine_name_tg [100];
char            domain_name_tg [100];
int             old_kernel_ig = TRUE;
int             bootp_wait_ig = 0;
int             testing_ig = FALSE;
int             passwd_mode_ig = FALSE;
char            ftp_user_tg [20];
char            ftp_password_tg [20];
char            ftp_proxy_tg [50];
int             ftp_proxyport_ig = -1;
char            floppy_tg [10];
char            ppcd_tg [10];
int             serial_ig = FALSE;
char            console_tg [30] = "/dev/console";
int             smp_ig = FALSE;
int             guru_ig = FALSE;
int             text_mode_ig = FALSE;
int             yast2_update_ig = FALSE;
int             yast2_serial_ig = FALSE;
int             has_floppy_ig = TRUE;
int             has_kbd_ig = TRUE;
unsigned        frame_buffer_mode_ig = 0;
char            *mouse_type_xf86_ig = NULL;
char            *mouse_type_gpm_ig = NULL;
char            *mouse_dev_ig = NULL;
int             yast_version_ig = 0;
int             valid_net_config_ig = 0;
int             reboot_ig;
int             usb_ig = 0;
