/*
 *
 * install.h     Header file for install.c
 *
 * Copyright (c) 1996-2002  Hubert Mantel, SuSE Linux AG  (mantel@suse.de)
 *
 */

extern int inst_menu          (void);
extern int inst_auto_install  (void);
extern int inst_start_demo    (void);
extern int inst_check_instsys (void);
extern int inst_start_install (void);
int inst_choose_partition(char **partition, int swap, char *txt_menu, char *txt_input);
