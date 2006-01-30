/*
 *
 * net.h         Header file for net.c
 *
 * Copyright (c) 1996-2002  Hubert Mantel, SuSE Linux AG  (mantel@suse.de)
 *
 */

extern int  net_config          (void);
extern int  net_bootp           (void);

extern void net_ask_password    (void);

int net_mount_nfs(char *mountpoint, inet_t *server, char *hostdir);

int net_mount_smb(char *mountpoint, inet_t *server, char *hostdir, char *user, char *password, char *workgroup);
void net_smb_get_mount_options(char *options, inet_t *server, char *user, char *password, char *workgroup);

extern void net_stop            (void);
extern int  net_check_address   (char *input_tv, struct in_addr *address_prr, int *net_bits);
int net_check_address2(inet_t *inet, int do_dns);
extern int  net_setup_localhost (void);

extern int  net_activate_ns      (void);

int net_dhcp(void);
void net_dhcp_stop(void);

unsigned net_config_mask(void);
int net_get_address(char *text, inet_t *inet, int do_dns);
char *net_if2module(char *net_if);
void net_apply_ethtool(char *device, char *hwaddr);

