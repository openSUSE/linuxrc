/*
 *
 * settings.h    Header file for settings.c
 *
 * Copyright (c) 1996-2000  Hubert Mantel, SuSE GmbH  (mantel@suse.de)
 *
 */

extern enum langid_t set_langidbyname(char *name);
extern int  set_settings        (void);
extern void set_choose_display  (void);
extern void set_choose_keytable (int);
extern void set_choose_language (void);
extern void set_write_info      (FILE *file_prv);
