#ifndef _SETTINGS_H
#define _SETTINGS_H

/*
 *
 * settings.h    Header file for settings.c
 *
 * Copyright (c) 1996-2008  Hubert Mantel, SuSE Linux AG  (mantel@suse.de)
 *
 */

typedef struct {
  enum langid_t  id;
  char *descr;
  char *keymap;
  char *font1;
  char *font2;
  int usermap;	// redundant, will drop it later -- snwint
  int write_info;
  char *locale;
  char *trans_id;	/* instsys translation image suffix */
  int xfonts;		/* needs extra xfonts */
} language_t;

typedef struct {
  char *descr;
  char *mapname;
} keymap_t;


extern enum langid_t set_langidbyname(char *name);
extern int  set_settings        (void);
extern void set_choose_display  (void);
extern void set_choose_keytable (int);
extern void set_activate_language(enum langid_t lang_id);
extern void set_activate_keymap (char *keymap);
extern void set_choose_language (void);
extern void set_write_info      (FILE *f);
language_t *current_language    (void);
extern void set_expert_menu     (void);

#endif  /* _SETTINGS_H */
