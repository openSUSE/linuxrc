/*
 *
 * file.h        Header file for file.c
 *
 * Copyright (c) 1996-2001  Hubert Mantel, SuSE GmbH  (mantel@suse.de)
 *
 */

typedef enum {
  key_none, key_swap, key_root, key_live, key_keytable, key_language,
  key_rebootmsg, key_insmod, key_autoprobe, key_start_pcmcia, key_color,
  key_bootmode, key_ip, key_netmask, key_gateway, key_server, key_dnsserver,
  key_partition, key_serverdir, key_netdevice, key_livesrc, key_bootpwait,
  key_bootptimeout, key_forcerootimage, key_rebootwait
} file_key_t;

typedef struct file_s {
  struct file_s *next;
  file_key_t key;
  char *key_str;
  char *value;
  int nvalue;
  struct in_addr ivalue;
  struct {
    unsigned numeric:1;
    unsigned inet:1;
  } is; 
} file_t;

extern char *file_key2str(file_key_t key);
extern file_t *file_read_file(char *name);
extern void file_free_file(file_t *file);

extern void file_write_yast_info (char *);
extern int  file_read_info       (void);
