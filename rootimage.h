/*
 *
 * rootimage.h   Header file for rootimage.c
 *
 * Copyright (c) 1996-2002  Hubert Mantel, SuSE Linux AG  (mantel@suse.de)
 *
 */

extern void root_set_root       (char *root_tv);
extern int  root_boot_system    (void);
int load_image(char *file_name, instmode_t mode);
int ramdisk_open(void);
void ramdisk_close(int rd);
void ramdisk_free(int rd);
int ramdisk_write(int rd, void *buf, int count);
int ramdisk_umount(int rd);
int ramdisk_mount(int rd, char *dir);
int ask_for_swap(int size, char *msg);
