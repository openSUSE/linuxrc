/*
 *
 * info.h        Header file for info.c
 *
 * Copyright (c) 1996-2002  Hubert Mantel, SuSE Linux AG  (mantel@suse.de)
 *
 */

extern void info_menu           (void);
extern void info_init           (void);
extern void info_show_hardware  (void);
extern int  info_eide_cd_exists (void);
extern int  info_scsi_exists    (void);
extern int  info_scsi_cd_exists (void);
