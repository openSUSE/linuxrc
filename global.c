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

/*
 * for default config, look at linuxrc.c::lxrc_init()
 * Do NOT set it here!
 */
config_t config;

colorset_t     *colors_prg;
char           *kernellog_tg = "/etc/kernellog";
char           *lastlog_tg = "/etc/lastlog";
char           *bootmsg_tg = "/var/log/boot.msg";
int             pcmcia_chip_ig = 0;
int             cpu_ig = 0;
int             force_ri_ig = FALSE;
char            ppcd_tg [10];
int             has_floppy_ig = TRUE;
int             has_kbd_ig = TRUE;
int             reboot_ig;
int             usb_ig = 0;
char            *usb_mods_ig = NULL;
char            xkbmodel_tg [20] = "";
unsigned        yast2_color_ig = 0;
int             reboot_wait_ig = FALSE;
char            livesrc_tg[16] = "";
int             cdrom_drives = 0;
int             has_modprobe = 0;
