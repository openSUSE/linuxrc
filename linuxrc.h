/*
 *
 * linuxrc.h     Header file for linuxrc.c
 *
 * Copyright (c) 1996-2002  Hubert Mantel, SuSE Linux AG  (mantel@suse.de)
 *
 */

#ifndef LINUXRC_H_
#define LINUXRC_H_

extern void lxrc_killall (int);
extern void lxrc_end     (void);
extern const char *lxrc_new_root;
void find_shell(void);
void lxrc_readd_parts(void);

#endif // LINUXRC_H_
