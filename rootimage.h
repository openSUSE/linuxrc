/*
 *
 * rootimage.h   Header file for rootimage.c
 *
 * Copyright (c) 1996-2001  Hubert Mantel, SuSE GmbH  (mantel@suse.de)
 *
 */

extern int  root_load_rootimage (char *infile_tv);
extern void root_set_root       (char *root_tv);
extern int  root_boot_system    (void);
