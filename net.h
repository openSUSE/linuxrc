/*
 *
 * net.h         Header file for net.c
 *
 * Copyright (c) 1996-1999  Hubert Mantel, SuSE GmbH  (mantel@suse.de)
 *
 */

extern int  net_config          (void);
extern int  net_mount_nfs       (char *server_addr_tv, char *hostdir_tv);
extern void net_stop            (void);
extern int  net_check_address   (char *input_tv, struct in_addr *address_prr);
extern int  net_setup_localhost (void);

extern int  net_activate        (void);
extern int  net_is_configured_im;
