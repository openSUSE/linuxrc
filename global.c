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
char            xkbmodel_tg [20] = "";
unsigned        yast2_color_ig = 0;
int             reboot_wait_ig = FALSE;
