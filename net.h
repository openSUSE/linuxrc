/*
 *
 * net.h         Header file for net.c
 *
 * Copyright (c) 1996-2002  Hubert Mantel, SuSE Linux AG  (mantel@suse.de)
 *
 */

// flags to pass to ifcfg_write()
#define IFCFG_INITIAL	(1 << 0)
#define IFCFG_IFUP	(1 << 1)

int net_config(void);
int net_config2(int type);
void net_ask_password(void);
int net_mount_nfs(char *mountpoint, char *server, char *hostdir, unsigned port, char *options);
int net_mount_cifs(char *mountpoint, char *server, char *hostdir, char *user, char *password, char *workgroup, char *options);
void net_stop(void);
int net_check_address(inet_t *inet, int do_dns);
int net_static(void);
int net_activate_s390_devs(void);
int net_dhcp(void);
unsigned net_config_mask(void);
int net_get_address(char *text, inet_t *inet, int do_dns);
int net_get_address2(char *text, inet_t *inet, int do_dns, char **user, char **password, unsigned *port);
void net_apply_ethtool(char *device, char *hwaddr);
int wlan_setup(void);
char *net_dhcp_type(void);
void net_update_ifcfg(int flags);
ifcfg_t *ifcfg_parse(char *str);
ifcfg_t *ifcfg_append(ifcfg_t **p0, ifcfg_t *p);
void ifcfg_copy(ifcfg_t *dst, ifcfg_t *src);
char *ifcfg_print(ifcfg_t *ifcfg);
void net_update_state(void);
void net_wicked_up(char *ifname);
void net_wicked_down(char *ifname);
int netmask_to_prefix(char *netmask);
int net_config_needed(int really);
unsigned check_ptp(char *ifname);
void net_wicked_get_config_keys(void);
void net_nanny(void);
char *net_get_ifname(ifcfg_t *ifcfg);
