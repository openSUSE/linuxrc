/*
 *
 * net.h         Header file for net.c
 *
 * Copyright (c) 1996-2002  Hubert Mantel, SuSE Linux AG  (mantel@suse.de)
 *
 */

int net_config(void);
int net_config2(int type);
void net_ask_password(void);
int net_mount_nfs(char *mountpoint, inet_t *server, char *hostdir, unsigned port, char *options);
int net_mount_smb(char *mountpoint, inet_t *server, char *hostdir, char *user, char *password, char *workgroup);
void net_smb_get_mount_options(char *options, inet_t *server, char *user, char *password, char *workgroup);
void net_stop(void);
int net_check_address(inet_t *inet, int do_dns);
int net_activate_ns(void);
int net_activate_s390_devs(void);
int net_dhcp(void);
unsigned net_config_mask(void);
int net_get_address(char *text, inet_t *inet, int do_dns);
int net_get_address2(char *text, inet_t *inet, int do_dns, char **user, char **password, unsigned *port);
char *net_if2module(char *net_if);
void net_apply_ethtool(char *device, char *hwaddr);
int wlan_setup(void);
char *net_dhcp_type(void);
void net_update_ifcfg(void);
ifcfg_t *ifcfg_parse(char *str);
ifcfg_t *ifcfg_append(ifcfg_t **p0, ifcfg_t *p);
void net_update_state(void);
void net_wicked_up(char *ifname);
void net_wicked_down(char *ifname);
int netmask_to_prefix(char *netmask);
