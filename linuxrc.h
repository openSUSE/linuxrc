/*
 *
 * linuxrc.h     Header file for linuxrc.c
 *
 * Copyright (c) 1996-2002  Hubert Mantel, SuSE Linux AG  (mantel@suse.de)
 *
 */

extern void lxrc_killall (int);
extern void lxrc_end     (void);
extern void lxrc_set_modprobe  (char *program_tv);
extern const char *lxrc_new_root;
