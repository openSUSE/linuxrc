/*
 *
 * rootimage.h   Header file for rootimage.c
 *
 * Copyright (c) 1996-2002  Hubert Mantel, SuSE Linux AG  (mantel@suse.de)
 *
 */

extern int  root_load_rootimage (char *infile_tv);
extern void root_set_root       (char *root_tv);
extern int  root_boot_system    (void);
int load_image(char *file_name, instmode_t mode);
int ramdisk_open(void);
void ramdisk_close(int rd);
void ramdisk_free(int rd);
int ramdisk_write(int rd, void *buf, int count);
