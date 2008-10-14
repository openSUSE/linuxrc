/*
 *
 * linuxrc.h     Header file for linuxrc.c
 *
 * Copyright (c) 1996-2002  Hubert Mantel, SuSE Linux AG  (mantel@suse.de)
 *
 */

extern void lxrc_killall (int);
extern void lxrc_end     (void);
extern void lxrc_set_modprobe(char *prog);
extern const char *lxrc_new_root;
void find_shell(void);
void lxrc_readd_parts(void);
