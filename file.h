/*
 *
 * file.h        Header file for file.c
 *
 * Copyright (c) 1996-2002  Hubert Mantel, SuSE Linux AG  (mantel@suse.de)
 *
 */

typedef enum {
  key_none, key_root, key_keytable, key_language, key_rebootmsg, key_insmod,
  key_display, key_ip, key_netmask, key_gateway, key_server, key_nameserver,
  key_network, key_partition, key_serverdir, key_netdevice,
  key_forcerootimage, key_rebootwait,
  key_sourcemounted, key_cdrom, key_console,
  key_ptphost, key_domain, key_manual, key_reboot, key_floppydisk,
  key_keyboard, key_yast2update, key_textmode, key_bootdisk,
  key_disks, key_username, key_password, key_passwordenc, key_workdomain, key_alias,
  key_options, key_initrdmodules, key_locale, key_font, key_screenmap,
  key_fontmagic, key_autoyast, key_linuxrc, key_forceinsmod, key_dhcp,
  key_ipaddr, key_hostname, key_dns, key_dhcpsiaddr, key_rootpath,
  key_bootfile, key_install, key_instsys, key_instmode, key_memtotal,
  key_memfree, key_buffers, key_cached, key_swaptotal, key_swapfree,
  key_memlimit, key_memyast, key_memloadimage, key_info, key_proxy,
  key_usedhcp, key_dhcptimeout, key_tftptimeout, key_tmpfs,
  key_testmode, key_debugwait, key_expert, key_rescue, key_rootimage,
  key_rescueimage, key_vnc, key_vncpassword, key_displayip,
  key_sshpassword, key_sshpasswordenc, key_term, key_addswap, key_aborted, key_netstop,
  key_exec, key_usbwait, key_nfsrsize, key_nfswsize, key_setupcmd,
  key_setupnetif, key_netconfig, key_usessh, key_sshd, key_noshell, key_hwdetect,
  key_consoledevice, key_product, key_productdir,
  key_linuxrcstderr, key_comment, key_kbdtimeout, key_brokenmodules,
  key_scsibeforeusb, key_hostip, key_linemode, key_moduledelay,
  key_updatedir, key_scsirename, key_doscsirename, key_lxrcdebug,
  key_updatename, key_updatestyle, key_updateid,
  key_updateask, key_initrd, key_vga, key_bootimage, key_ramdisksize,
  key_suse, key_showopts, key_nosshkey, key_startshell, key_y2debug, key_ro,
  key_rw, key_netid, key_nethwaddr, key_loglevel, key_netsetup,
  key_rootpassword, key_loghost, key_escdelay, key_minmem, key_updateprio,
  key_instnetdev, key_iucvpeer, key_portname, key_readchan, key_writechan,
  key_datachan, key_ctcprotocol, key_netwait, key_newid, key_moduledisks,
  key_port, key_smbshare, key_rootimage2, key_instsys_id,
  key_initrd_id, key_instsys_complain,
  key_osainterface, key_dud_complain, key_dud_expected,
  key_withiscsi, key_ethtool, key_listen, key_zombies,
  key_layer2, key_wlan_essid, key_wlan_auth, key_wlan_wpa_psk,
  key_wlan_wpa_id, key_wlan_wpa_pass, key_wlan_device, key_netcardname,
  key_ibft_hwaddr, key_ibft_ipaddr, key_ibft_netmask, key_ibft_gateway,
  key_ibft_dns, key_net_retry, key_bootif, key_swap_size, key_ntfs_3g,
  key_hash, key_insecure, key_kexec, key_nisdomain, key_nomodprobe, key_device,
  key_nomdns, key_yepurl, key_mediacheck, key_y2gdb, key_squash,
  key_kexec_reboot, key_devbyid, key_braille, key_nfsopts, key_ipv4, key_ipv4only,
  key_ipv6, key_ipv6only, key_efi, key_supporturl, key_portno,
  key_osahwaddr, key_zen, key_zenconfig, key_udevrule, key_dhcpfail,
  key_namescheme, key_ptoptions, key_is_ptoption, key_withfcoe, key_digests,
  key_plymouth, key_sslcerts, key_restart, key_restarted, key_autoyast2,
  key_withipoib, key_upgrade, key_media_upgrade, key_ifcfg, key_defaultinstall,
  key_nanny, key_vlanid,
  key_sshkey, key_systemboot, key_sethostname, key_debugshell, key_self_update,
  key_ibft_devices, key_linuxrc_core, key_norepo, key_auto_assembly, key_autoyast_parse,
  key_device_auto_config, key_autoyast_passurl, key_rd_zdev, key_insmod_pre,
  key_zram, key_zram_root, key_zram_swap, key_extend, key_switch_to_fb
} file_key_t;

typedef enum {
  kf_all = -1,
  kf_none = 0,
  kf_cfg = 1 << 0,		/**< /linuxrc.config & /info */
  kf_cmd = 1 << 1,		/**< /proc/cmdline, after info */
  kf_cmd_early = 1 << 2,	/**< /proc/cmdline, before info  */
  kf_yast = 1 << 3,		/**< /etc/yast.inf */
  kf_dhcp = 1 << 4,		/**< dhcp info file */
  kf_mem = 1 << 5,		/**< /proc/meminfo */
  kf_boot = 1 << 6,		/**< things the boot loader used */
  kf_cmd1 = 1 << 7,		/**< between starting udevd and starting hw detection */
  kf_ibft = 1 << 8,		/**< ibft values (iSCSI BIOS) */
  kf_cont = 1 << 9,		/**< 'content' file */
  kf_comma = 1 << 10,		/**< accept commas as option separator (in command line syntax) */
  kf_cmd0 = 1 << 11,		/**< between cmd_early and starting udevd */
} file_key_flag_t;

typedef struct file_s {
  struct file_s *next, *prev;
  file_key_t key;
  char *unparsed;
  char *key_str;
  char *value;
  int nvalue;
  struct {
    unsigned numeric:1;
  } is; 
} file_t;

file_t *file_getentry(file_t *f, char *key);
file_t *file_read_file(char *name, file_key_flag_t flags);
void file_free_file(file_t *file);

void file_write_str(FILE *f, file_key_t key, char *str);
void file_write_num(FILE *f, file_key_t key, int64_t num);
void file_write_sym(FILE *f, file_key_t key, char *base_sym, int num);

void file_write_install_inf(char *dir);
char *file_read_info_file(char *file, file_key_flag_t flags);
int file_read_yast_inf(void);
file_t *file_get_cmdline(file_key_t key);
file_t *file_read_cmdline(file_key_flag_t flags);
module_t *file_read_modinfo(char *name);
int file_sym2num(char *sym);
char *file_num2sym(char *base_sym, int num);
file_t *file_parse_buffer(char *buf, file_key_flag_t flags);
void file_do_info(file_t *f0, file_key_flag_t flags);
void get_ide_options(void);
slist_t *file_parse_xmllike(char *name, char *tag);
void file_parse_repomd(char *file);
void file_parse_checksums(char *file);

