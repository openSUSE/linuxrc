/*
 *
 * install.h     Header file for install.c
 *
 * Copyright (c) 1996-2002  Hubert Mantel, SuSE Linux AG  (mantel@suse.de)
 *
 */

extern int inst_menu          (void);
extern int inst_start_demo    (void);
extern int inst_check_instsys (void);
extern int inst_start_install (void);
int inst_choose_partition(char **partition, int swap, char *txt_menu, char *txt_input);
int inst_umount (void);
int inst_update_cd(void);

int do_mount_nfs(void);
int do_mount_smb(void);
int do_mount_disk(char *dev, int disk_type);

