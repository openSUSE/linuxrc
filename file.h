/*
 *
 * file.h        Header file for file.c
 *
 * Copyright (c) 1996-2001  Hubert Mantel, SuSE GmbH  (mantel@suse.de)
 *
 */

typedef enum {
  key_none, key_swap, key_root, key_live, key_keytable, key_language,
  key_rebootmsg, key_insmod, key_autoprobe, key_start_pcmcia, key_display,
  key_bootmode, key_ip, key_netmask, key_gateway, key_server,
  key_nameserver, key_broadcast, key_network, key_partition, key_serverdir,
  key_fstype, key_netdevice, key_livesrc, key_bootpwait, key_bootptimeout,
  key_forcerootimage, key_rebootwait, key_sourcemounted, key_cdrom,
  key_pcmcia, key_haspcmcia, key_console, key_pliphost, key_domain,
  key_ftpuser, key_ftpproxy, key_ftpproxyport, key_manual, key_demo,
  key_reboot, key_floppydisk, key_keyboard, key_yast2update,
  key_yast2serial, key_textmode, key_yast2autoinst, key_usb, key_yast2color,
  key_bootdisk, key_disks, key_username, key_password, key_workdomain,
  key_alias, key_options, key_initrdmodules, key_locale, key_font,
  key_screenmap, key_fontmagic, key_autoyast, key_linuxrc, key_forceinsmod,
  key_dhcp, key_ipaddr, key_hostname, key_nisdomain, key_nisservers,
  key_dns, key_nptservers, key_dhcpsid, key_dhcpgiaddr, key_dhcpsiaddr,
  key_dhcpchaddr, key_dhcpshaddr, key_dhcpsname, key_rootpath, key_bootfile,
  key_install
} file_key_t;

typedef struct file_s {
  struct file_s *next, *prev;
  file_key_t key;
  char *key_str;
  char *value;
  int nvalue;
  struct {
    unsigned numeric:1;
  } is; 
} file_t;

file_t *file_read_file(char *name);
void file_free_file(file_t *file);

void file_write_str(FILE *f, file_key_t key, char *str);
void file_write_num(FILE *f, file_key_t key, int num);
void file_write_sym(FILE *f, file_key_t key, char *base_sym, int num);

void file_write_install_inf(char *dir);
void file_write_mtab(void);
int file_read_info(void);
int file_read_yast_inf(void);
file_t *file_get_cmdline(file_key_t key);
module2_t *file_read_modinfo(char *name);
