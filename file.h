/*
 *
 * file.h        Header file for file.c
 *
 * Copyright (c) 1996-2001  Hubert Mantel, SuSE GmbH  (mantel@suse.de)
 *
 */

typedef enum {
  key_none, key_swap, key_root, key_live, key_keytable, key_language
} file_key_t;

typedef struct file_s {
  struct file_s *next;
  file_key_t key;
  char *value;
} file_t;

extern char *file_key2str(file_key_t key);
extern file_t *file_read_file(char *name);
extern void file_free_file(file_t *file);

extern void file_write_yast_info (char *);
extern int  file_read_info       (void);
