/*
 *
 * linuxrc.h     Header file for linuxrc.c
 *
 * Copyright (c) 1996-1998  Hubert Mantel, SuSE GmbH  (mantel@suse.de)
 *
 */

extern void lxrc_reboot  (void);
extern void lxrc_killall (int);
extern void lxrc_end     (void);
extern void lxrc_set_modprobe  (char *program_tv);
