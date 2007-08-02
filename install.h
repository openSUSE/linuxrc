/*
 *
 * install.h     Header file for install.c
 *
 * Copyright (c) 1996-2002  Hubert Mantel, SuSE Linux AG  (mantel@suse.de)
 *
 */

int inst_menu(void);
int inst_start_install(void);
int inst_choose_partition(char **partition, int swap, char *txt_menu, char *txt_input);
int inst_update_cd(void);
