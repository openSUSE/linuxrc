/*
 *
 * global.c      Global data
 *
 * Copyright (c) 1996-2002  Hubert Mantel, SuSE Linux AG  (mantel@suse.de)
 *
 */

#include "global.h"

#define X_DEFAULT         80
#define Y_DEFAULT         25

int             max_x_ig = X_DEFAULT;
int             max_y_ig = Y_DEFAULT;

/* for default config, look at linuxrc.c::lxrc_init() */
config_t config;

colorset_t     *colors_prg;
char            rootimage_tg [MAX_FILENAME] = "/suse/images/root";
char           *mountpoint_tg = "/var/adm/mount";
char           *inst_mountpoint_tg = "/inst-img";
char           *kernellog_tg = "/etc/kernellog";
char           *lastlog_tg = "/etc/lastlog";
char           *bootmsg_tg = "/var/log/boot.msg";
char            netdevice_tg [20] = "eth0";
int             pcmcia_chip_ig = 0;
uint64_t        memory_ig = 8192000;
int             cpu_ig = 0;
int             force_ri_ig = FALSE;
int             ramdisk_ig = FALSE;
int             explode_win_ig = TRUE;
int             auto_ig = FALSE;
#if defined(__PPC__) || defined(__sparc__)
int             auto2_ig = TRUE;
#else
int             auto2_ig = FALSE;
#endif
int             demo_ig = FALSE;
char            machine_name_tg [100];
int             old_kernel_ig = TRUE;
char            ppcd_tg [10];
int             serial_ig = FALSE;
char            console_tg [30] = "/dev/console";
char		console_parms_tg [30] = "";
int             yast2_update_ig = FALSE;
int             yast2_serial_ig = FALSE;
int             has_floppy_ig = TRUE;
int             has_kbd_ig = TRUE;
unsigned        frame_buffer_mode_ig = 0;
int             yast_version_ig = 0;
int             reboot_ig;
int             usb_ig = 0;
char            *usb_mods_ig = NULL;
int             found_suse_cd_ig = FALSE;
char            xkbmodel_tg [20] = "";
unsigned        yast2_color_ig = 0;
unsigned        action_ig = 0;
int             reboot_wait_ig = FALSE;
char            *x11i_tg = NULL;
char            livesrc_tg[16] = "";
char            driver_update_dir[16] = "";
int             cdrom_drives = 0;
int             splash_active = FALSE;
int             has_modprobe = 0;
