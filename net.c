/*
 *
 * net.c         Network related functions
 *
 * Copyright (c) 1996-2000  Hubert Mantel, SuSE GmbH  (mantel@suse.de)
 *
 */

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <fcntl.h>
#include <rpc/rpc.h>
#include <rpc/pmap_prot.h>
#include <rpc/pmap_clnt.h>
#include <rpcsvc/mount.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/mount.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <errno.h>
#include <net/if.h>
#include <net/route.h>
#include <nfs/nfs.h>
#include <linux/nfs_mount.h>

#include "global.h"
#include "text.h"
#include "dialog.h"
#include "window.h"
#include "util.h"
#include "net.h"
#include "bootpc.h"

#define NFS_PROGRAM    100003
#define NFS_VERSION         2

#define MAX_NETDEVICE      10

int net_is_configured_im = FALSE;

static int  net_is_plip_im = FALSE;

static int  net_choose_device    (void);
static void net_show_error       (enum nfs_stat status_rv);
static void net_setup_nameserver (void);
static int  net_input_data       (void);
static int  net_bootp            (void);
static int  net_get_address      (char *text_tv, struct in_addr *address_prr);

int net_config (void)
    {
    int   rc_ii;


    if (net_is_configured_im &&
        dia_yesno (txt_get (TXT_NET_CONFIGURED), YES) == YES)
        return (0);

    if (net_choose_device ())
        return (-1);

    net_stop ();

    if (auto_ig)
        rc_ii = YES;
    else
        rc_ii = dia_yesno (txt_get (TXT_ASK_BOOTP), NO);

    if (rc_ii == ESCAPE)
        return (-1);

    if (rc_ii == YES)
        rc_ii = net_bootp ();
    else
        rc_ii = net_input_data ();

    if (rc_ii)
        return (-1);

    if (net_activate ())
        {
        (void) dia_message (txt_get (TXT_ERROR_CONF_NET), MSGTYPE_ERROR);
        return (-1);
        }

    net_setup_nameserver ();

    net_is_configured_im = TRUE;
    return (0);
    }


void net_stop (void)
    {
    int             socket_ii;
    struct ifreq    interface_ri;


    if (!net_is_configured_im)
        return;

    socket_ii = socket (AF_INET, SOCK_DGRAM, 0);
    if (socket_ii == -1)
        return;

    memset (&interface_ri, 0, sizeof (struct ifreq));
    strcpy (interface_ri.ifr_name, netdevice_tg);
    interface_ri.ifr_addr.sa_family = AF_INET;

    ioctl (socket_ii, SIOCGIFFLAGS, &interface_ri);
    interface_ri.ifr_flags &= ~(IFF_UP | IFF_RUNNING);
    ioctl (socket_ii, SIOCSIFFLAGS, &interface_ri);
    close (socket_ii);
    net_is_configured_im = FALSE;
    }


int net_setup_localhost (void)
    {
    char                address_ti [20];
    struct in_addr      ipaddr_ri;
    int                 socket_ii;
    struct ifreq        interface_ri;
    struct sockaddr_in  sockaddr_ri;
    int                 error_ii = FALSE;


    fprintf (stderr, "Setting up localhost...");
    fflush (stdout);

    socket_ii = socket (AF_INET, SOCK_DGRAM, 0);
    if (socket_ii == -1)
        return (socket_ii);

    memset (&interface_ri, 0, sizeof (struct ifreq));
    strcpy (interface_ri.ifr_name, "lo");

    sockaddr_ri.sin_family = AF_INET;
    sockaddr_ri.sin_port = 0;
    strcpy (address_ti, "127.0.0.1");
    if (!inet_aton (address_ti, &ipaddr_ri))
        error_ii = TRUE;
    sockaddr_ri.sin_addr = ipaddr_ri;
    memcpy (&interface_ri.ifr_addr, &sockaddr_ri, sizeof (sockaddr_ri));
    if (ioctl (socket_ii, SIOCSIFADDR, &interface_ri) < 0)
        {
        HERE
        error_ii = TRUE;
        }

    strcpy (address_ti, "255.0.0.0");
    if (!inet_aton (address_ti, &ipaddr_ri))
        error_ii = TRUE;
    sockaddr_ri.sin_addr = ipaddr_ri;
    memcpy (&interface_ri.ifr_netmask, &sockaddr_ri, sizeof (sockaddr_ri));
    if (ioctl (socket_ii, SIOCSIFNETMASK, &interface_ri) < 0)
        if (old_kernel_ig || netmask_rg.s_addr)
            {
            HERE
            error_ii = TRUE;
            }

    strcpy (address_ti, "127.255.255.255");
    if (!inet_aton (address_ti, &ipaddr_ri))
        error_ii = TRUE;
    sockaddr_ri.sin_addr = ipaddr_ri;
    memcpy (&interface_ri.ifr_broadaddr, &sockaddr_ri, sizeof (sockaddr_ri));
    if (ioctl (socket_ii, SIOCSIFBRDADDR, &interface_ri) < 0)
        if (old_kernel_ig || broadcast_rg.s_addr != 0xffffffff)
            {
            HERE
            error_ii = TRUE;
            }

    if (ioctl (socket_ii, SIOCGIFFLAGS, &interface_ri) < 0)
        {
        HERE
        error_ii = TRUE;
        }

    interface_ri.ifr_flags |= IFF_UP | IFF_RUNNING | IFF_LOOPBACK | IFF_BROADCAST;
    if (ioctl (socket_ii, SIOCSIFFLAGS, &interface_ri) < 0)
        {
        HERE
        error_ii = TRUE;
        }

    close (socket_ii);

    fprintf (stderr, "%s\n", error_ii ? "failure" : "done");
    return (error_ii);
    }


int net_activate (void)
    {
    int                 socket_ii;
    struct ifreq        interface_ri;
    struct rtentry      route_ri;
    struct sockaddr_in  sockaddr_ri;
    int                 error_ii = FALSE;


    socket_ii = socket (AF_INET, SOCK_DGRAM, 0);
    if (socket_ii == -1)
        return (socket_ii);

    memset (&interface_ri, 0, sizeof (struct ifreq));
    strcpy (interface_ri.ifr_name, netdevice_tg);

    sockaddr_ri.sin_family = AF_INET;
    sockaddr_ri.sin_port = 0;
    sockaddr_ri.sin_addr = ipaddr_rg;
    memcpy (&interface_ri.ifr_addr, &sockaddr_ri, sizeof (sockaddr_ri));
    if (ioctl (socket_ii, SIOCSIFADDR, &interface_ri) < 0)
        error_ii = TRUE;

    if (net_is_plip_im)
        {
        sockaddr_ri.sin_addr = plip_host_rg;
        memcpy (&interface_ri.ifr_dstaddr, &sockaddr_ri, sizeof (sockaddr_ri));
        if (ioctl (socket_ii, SIOCSIFDSTADDR, &interface_ri) < 0)
            error_ii = TRUE;
        }
    else
        {
        sockaddr_ri.sin_addr = netmask_rg;
        memcpy (&interface_ri.ifr_netmask, &sockaddr_ri, sizeof (sockaddr_ri));
        if (ioctl (socket_ii, SIOCSIFNETMASK, &interface_ri) < 0)
            if (old_kernel_ig || netmask_rg.s_addr)
                error_ii = TRUE;

        sockaddr_ri.sin_addr = broadcast_rg;
        memcpy (&interface_ri.ifr_broadaddr, &sockaddr_ri, sizeof (sockaddr_ri));
        if (ioctl (socket_ii, SIOCSIFBRDADDR, &interface_ri) < 0)
            if (old_kernel_ig || broadcast_rg.s_addr != 0xffffffff)
                error_ii = TRUE;
        }

    if (ioctl (socket_ii, SIOCGIFFLAGS, &interface_ri) < 0)
        error_ii = TRUE;

    interface_ri.ifr_flags |= IFF_UP | IFF_RUNNING;
    if (net_is_plip_im)
        interface_ri.ifr_flags |= IFF_POINTOPOINT | IFF_NOARP;
    else
        interface_ri.ifr_flags |= IFF_BROADCAST;
    if (ioctl (socket_ii, SIOCSIFFLAGS, &interface_ri) < 0)
        error_ii = TRUE;

    memset (&route_ri, 0, sizeof (struct rtentry));
    route_ri.rt_dev = netdevice_tg;

    if (net_is_plip_im)
        {
        sockaddr_ri.sin_addr = plip_host_rg;
        memcpy (&route_ri.rt_dst, &sockaddr_ri, sizeof (sockaddr_ri));
        route_ri.rt_flags = RTF_UP | RTF_HOST;
        if (ioctl (socket_ii, SIOCADDRT, &route_ri) < 0)
            error_ii = TRUE;

        memset (&route_ri.rt_dst, 0, sizeof (route_ri.rt_dst));
        route_ri.rt_dst.sa_family = AF_INET;
        memcpy (&route_ri.rt_gateway, &sockaddr_ri, sizeof (sockaddr_ri));
        route_ri.rt_flags = RTF_UP | RTF_GATEWAY;
        if (ioctl (socket_ii, SIOCADDRT, &route_ri) < 0)
            error_ii = TRUE;
        }
    else
        {
        sockaddr_ri.sin_addr = network_rg;
        memcpy (&route_ri.rt_dst, &sockaddr_ri, sizeof (sockaddr_ri));
        route_ri.rt_flags = RTF_UP;
        if (ioctl (socket_ii, SIOCADDRT, &route_ri) < 0)
            if (old_kernel_ig)
                error_ii = TRUE;

        if (gateway_rg.s_addr && gateway_rg.s_addr != ipaddr_rg.s_addr)
            {
            sockaddr_ri.sin_addr = gateway_rg;
            memset (&route_ri.rt_dst, 0, sizeof (route_ri.rt_dst));
            route_ri.rt_dst.sa_family = AF_INET;
            memcpy (&route_ri.rt_gateway, &sockaddr_ri, sizeof (sockaddr_ri));

/*            route_ri.rt_flags = RTF_UP | RTF_HOST;
            if (ioctl (socket_ii, SIOCADDRT, &route_ri) < 0)
                {
                error_ii = TRUE;
                } */

            route_ri.rt_flags = RTF_UP | RTF_GATEWAY;
            if (ioctl (socket_ii, SIOCADDRT, &route_ri) < 0)
                error_ii = TRUE;
            }
        }

    close (socket_ii);

    return (error_ii);
    }


int net_check_address (char *input_tv, struct in_addr *address_prr)
    {
    unsigned char  tmp_ti [20];
    unsigned char *start_pci;
    unsigned char *end_pci;
    int            i_ii;
    unsigned char *address_pci;


    address_pci = (unsigned char *) address_prr;

    strncpy (tmp_ti, input_tv, sizeof (tmp_ti));
    tmp_ti [sizeof (tmp_ti) - 1] = 0;

    for (i_ii = 0; i_ii < strlen (tmp_ti); i_ii++)
        if (!isdigit (tmp_ti [i_ii]) && tmp_ti [i_ii] != '.')
            return (-1);

    if (!isdigit (tmp_ti [strlen (tmp_ti) - 1]))
        return (-1);

    start_pci = tmp_ti;
    end_pci = strchr (tmp_ti, '.');
    if (!end_pci)
        return (-1);
    *end_pci = 0;
    address_pci [0] = (unsigned char) atoi (start_pci);

    start_pci = end_pci + 1;
    end_pci = strchr (start_pci, '.');
    if (!end_pci)
        return (-1);
    *end_pci = 0;
    address_pci [1] = (unsigned char) atoi (start_pci);

    start_pci = end_pci + 1;
    end_pci = strchr (start_pci, '.');
    if (!end_pci)
        return (-1);
    *end_pci = 0;
    address_pci [2] = (unsigned char) atoi (start_pci);
    address_pci [3] = (unsigned char) atoi (end_pci + 1);

    return (0);
    }


int net_mount_nfs (char *server_addr_tv, char *hostdir_tv)
    {
    struct sockaddr_in     server_ri;
    struct sockaddr_in     mount_server_ri;
    struct nfs_mount_data  mount_data_ri;
    int                    socket_ii;
    int                    fsock_ii;
    int                    port_ii;
    CLIENT                *mount_client_pri;
    struct timeval         timeout_ri;
    int                    rc_ii;
    struct fhstatus        status_ri;
    fhandle                root_fhandle_ri;
    char                   tmp_ti [1024];
    char                  *opts_pci;


    memset (&server_ri, 0, sizeof (struct sockaddr_in));
    server_ri.sin_family = AF_INET;
    server_ri.sin_addr.s_addr = inet_addr (server_addr_tv);
    memcpy (&mount_server_ri, &server_ri, sizeof (struct sockaddr_in));

    memset (&mount_data_ri, 0, sizeof (struct nfs_mount_data));
    mount_data_ri.rsize = 0;
    mount_data_ri.wsize = 0;
    mount_data_ri.timeo = 7;
    mount_data_ri.retrans = 3;
    mount_data_ri.acregmin = 3;
    mount_data_ri.acregmax = 60;
    mount_data_ri.acdirmin = 30;
    mount_data_ri.acdirmax = 60;
    mount_data_ri.version = NFS_MOUNT_VERSION;

    mount_server_ri.sin_port = htons (0);
    socket_ii = RPC_ANYSOCK;
    mount_client_pri = clnttcp_create (&mount_server_ri, MOUNTPROG, MOUNTVERS,
                                       &socket_ii, 0, 0);
    if (!mount_client_pri)
        {
        mount_server_ri.sin_port = htons (0);
        socket_ii = RPC_ANYSOCK;
        timeout_ri.tv_sec = 3;
        timeout_ri.tv_usec = 0;
        mount_client_pri = clntudp_create (&mount_server_ri, MOUNTPROG,
                                           MOUNTVERS, timeout_ri, &socket_ii);
        if (!mount_client_pri)
            {
            net_show_error ((enum nfs_stat) -1);
            return (-1);
            }
        }

    mount_client_pri->cl_auth = authunix_create_default ();
    timeout_ri.tv_sec = 20;
    timeout_ri.tv_usec = 0;

    rc_ii = clnt_call (mount_client_pri, MOUNTPROC_MNT,
                       (xdrproc_t) xdr_dirpath, (caddr_t) &hostdir_tv,
                       (xdrproc_t) xdr_fhstatus, (caddr_t) &status_ri,
                       timeout_ri);
    if (rc_ii)
        {
        net_show_error ((enum nfs_stat) -1);
        return (-1);
        }

    if (status_ri.fhs_status)
        {
        net_show_error (status_ri.fhs_status);
        return (-1);
        }

    memcpy ((char *) &root_fhandle_ri,
            (char *) status_ri.fhstatus_u.fhs_fhandle,
            sizeof (root_fhandle_ri));

    fsock_ii = socket (AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (fsock_ii < 0)
        {
        net_show_error ((enum nfs_stat) -1);
        return (-1);
        }

    if (bindresvport (fsock_ii, 0) < 0)
        {
        net_show_error ((enum nfs_stat) -1);
        return (-1);
        }

    if (nfsport_ig)
        {
        fprintf (stderr, "Using specified port %d\n", nfsport_ig);
        port_ii = nfsport_ig;
        }
    else
        {
        server_ri.sin_port = PMAPPORT;
        port_ii = pmap_getport (&server_ri, NFS_PROGRAM, NFS_VERSION, IPPROTO_UDP);
        if (!port_ii)
            port_ii = NFS_PORT;
        }

    server_ri.sin_port = htons (port_ii);

    mount_data_ri.fd = fsock_ii;
    memcpy ((char *) &mount_data_ri.root, (char *) &root_fhandle_ri,
            sizeof (root_fhandle_ri));
    memcpy ((char *) &mount_data_ri.addr, (char *) &server_ri,
            sizeof (mount_data_ri.addr));
    strncpy (mount_data_ri.hostname, server_addr_tv,
             sizeof (mount_data_ri.hostname));

    auth_destroy (mount_client_pri->cl_auth);
    clnt_destroy (mount_client_pri);
    close (socket_ii);

    sprintf (tmp_ti, "%s:%s", server_addr_tv, hostdir_tv);
    opts_pci = (char *) &mount_data_ri;
    rc_ii = mount (tmp_ti, mountpoint_tg, "nfs", MS_RDONLY | MS_MGC_VAL,
                   opts_pci);

    return (rc_ii);
    }


int xdr_dirpath (XDR *xdrs, dirpath *objp)
    {
    if (!xdr_string(xdrs, objp, MNTPATHLEN))
        return (FALSE);
    else
        return (TRUE);
    }


int xdr_fhandle (XDR *xdrs, fhandle objp)
    {
    if (!xdr_opaque(xdrs, objp, FHSIZE))
        return (FALSE);
    else
        return (TRUE);
    }


int xdr_fhstatus (XDR *xdrs, fhstatus *objp)
    {
    if (!xdr_u_int(xdrs, &objp->fhs_status))
        return (FALSE);

    if (!objp->fhs_status)
        if (!xdr_fhandle(xdrs, objp->fhstatus_u.fhs_fhandle))
            return (FALSE);

    return (TRUE);
    }


static int net_choose_device (void)
    {
    item_t  items_ari [MAX_NETDEVICE];
    int     nr_items_ii = 0;
    int     choice_ii;
    int     width_ii = 32;
    int     i_ii;
    FILE   *fd_pri;
    char    buffer_ti [MAX_X];

    
    if (auto_ig)
        return (0);

    util_create_items (items_ari, MAX_NETDEVICE, width_ii);

    fd_pri = fopen ("/proc/net/dev", "r");
    if (!fd_pri)
        return (-1);

    while (fgets (buffer_ti, MAX_X - 1, fd_pri) && nr_items_ii < MAX_NETDEVICE)
        {
        buffer_ti [6] = 0;
        i_ii = 0;
        while (buffer_ti [i_ii] == ' ')
            i_ii++;

        if (!strncmp (&buffer_ti [i_ii], "eth", 3))
            sprintf (items_ari [nr_items_ii++].text, "%-6s : %s",
                     &buffer_ti [i_ii], txt_get (TXT_NET_ETH0));
        else if (!strncmp (&buffer_ti [i_ii], "plip", 4))
            sprintf (items_ari [nr_items_ii++].text, "%-6s : %s",
                     &buffer_ti [i_ii], txt_get (TXT_NET_PLIP));
        else if (!strncmp (&buffer_ti [i_ii], "tr", 2))
            sprintf (items_ari [nr_items_ii++].text, "%-6s : %s",
                     &buffer_ti [i_ii], txt_get (TXT_NET_TR0));
        else if (!strncmp (&buffer_ti [i_ii], "arc", 3))
            sprintf (items_ari [nr_items_ii++].text, "%-6s : %s",
                     &buffer_ti [i_ii], txt_get (TXT_NET_ARC0));
        else if (!strncmp (&buffer_ti [i_ii], "fddi", 4))
            sprintf (items_ari [nr_items_ii++].text, "%-6s : %s",
                     &buffer_ti [i_ii], txt_get (TXT_NET_FDDI));
        else if (!strncmp (&buffer_ti [i_ii], "hip", 3))
            sprintf (items_ari [nr_items_ii++].text, "%-6s : %s",
                     &buffer_ti [i_ii], txt_get (TXT_NET_HIPPI));
        }

    fclose (fd_pri);

    for (i_ii = 0; i_ii < nr_items_ii; i_ii++)
        util_fill_string (items_ari [i_ii].text, width_ii);

    if (!nr_items_ii)
        {
        dia_message (txt_get (TXT_NO_NETDEVICE), MSGTYPE_ERROR);
        util_free_items (items_ari, MAX_NETDEVICE);
        return (-1);
        }

    if (nr_items_ii > 1)
        choice_ii = dia_menu (txt_get (TXT_CHOOSE_NET), items_ari, nr_items_ii, 1);
    else
        choice_ii = 1;

    if (choice_ii)
        {
        i_ii = 0;
        while (items_ari [choice_ii - 1].text [i_ii] != ' ')
            i_ii++;
        items_ari [choice_ii - 1].text [i_ii] = 0;
        strcpy (netdevice_tg, items_ari [choice_ii - 1].text);
        if (!strncmp (netdevice_tg, "plip", 4))
            net_is_plip_im = TRUE;
        else
            net_is_plip_im = FALSE;
        }

    util_free_items (items_ari, MAX_NETDEVICE);
    if (!choice_ii)
        return (-1);
    else
        return (0);
    }


static void net_show_error (enum nfs_stat status_rv)
    {
    struct { enum nfs_stat stat;
             int           errnumber;
           } nfs_err_ari [] = {
                              { NFS_OK,                 0               },
                              { NFSERR_PERM,            EPERM           },
                              { NFSERR_NOENT,           ENOENT          },
                              { NFSERR_IO,              EIO             },
                              { NFSERR_NXIO,            ENXIO           },
                              { NFSERR_ACCES,           EACCES          },
                              { NFSERR_EXIST,           EEXIST          },
                              { NFSERR_NODEV,           ENODEV          },
                              { NFSERR_NOTDIR,          ENOTDIR         },
                              { NFSERR_ISDIR,           EISDIR          },
                              { NFSERR_INVAL,           EINVAL          },
                              { NFSERR_FBIG,            EFBIG           },
                              { NFSERR_NOSPC,           ENOSPC          },
                              { NFSERR_ROFS,            EROFS           },
                              { NFSERR_NAMETOOLONG,     ENAMETOOLONG    },
                              { NFSERR_NOTEMPTY,        ENOTEMPTY       },
                              { NFSERR_DQUOT,           EDQUOT          },
                              { NFSERR_STALE,           ESTALE          }
                              };
    int   i_ii = 0;
    char  tmp_ti [1024];
    int   found_ii = FALSE;


    while (i_ii < sizeof (nfs_err_ari) / sizeof (nfs_err_ari [0]) && !found_ii)
        if (nfs_err_ari [i_ii].stat == status_rv)
            found_ii = TRUE;
        else
            i_ii++;

    sprintf (tmp_ti, txt_get (TXT_ERROR_NFSMOUNT), found_ii ?
             strerror (nfs_err_ari [i_ii].errnumber) : "unknown error");

    dia_message (tmp_ti, MSGTYPE_ERROR);
    }


static void net_setup_nameserver (void)
    {
    if (!nameserver_rg.s_addr)
        nameserver_rg = ipaddr_rg;

    if (!auto_ig && net_get_address (txt_get (TXT_INPUT_NAMED), &nameserver_rg))
        return;
    }


static int net_input_data (void)
    {
    if (net_get_address (txt_get (TXT_INPUT_IPADDR), &ipaddr_rg))
        return (-1);

    if (net_is_plip_im)
        {
        if (!plip_host_rg.s_addr)
            plip_host_rg = ipaddr_rg;

        if (net_get_address (txt_get (TXT_INPUT_PLIP_IP), &plip_host_rg))
            return (-1);

        if (!gateway_rg.s_addr)
            gateway_rg = plip_host_rg;

        if (!nfs_server_rg.s_addr)
            nfs_server_rg = plip_host_rg;
        }
    else
        {
        plip_host_rg.s_addr = 0;

        if (!gateway_rg.s_addr)
            gateway_rg = ipaddr_rg;

        if (!nfs_server_rg.s_addr)
            nfs_server_rg = ipaddr_rg;

        if (!ftp_server_rg.s_addr)
            ftp_server_rg = ipaddr_rg;

        if(!netmask_rg.s_addr) {
          char *s = inet_ntoa(ipaddr_rg);
          inet_aton (strstr(s, "10.10.") == s ? "255.255.0.0" : "255.255.255.0", &netmask_rg);
        }
        if (net_get_address (txt_get (TXT_INPUT_NETMASK), &netmask_rg))
            return (-1);

        broadcast_rg.s_addr = ipaddr_rg.s_addr | ~netmask_rg.s_addr;
        network_rg.s_addr = ipaddr_rg.s_addr & netmask_rg.s_addr;

        if (net_get_address (txt_get (TXT_INPUT_GATEWAY), &gateway_rg))
            gateway_rg.s_addr = 0;
        }

    return (0);
    }


static int net_bootp (void)
    {
    window_t  win_ri;
    int       rc_ii;
    char     *data_pci;
    char      tmp_ti [30];
    int       i_ii;


    if (auto_ig && ipaddr_rg.s_addr)
        return (0);

    plip_host_rg.s_addr = 0;
    ipaddr_rg.s_addr = 0;
    netmask_rg.s_addr = 0;
    network_rg.s_addr = 0;
    broadcast_rg.s_addr = 0xffffffff;

    if (net_activate ())
        {
        (void) dia_message (txt_get (TXT_ERROR_CONF_NET), MSGTYPE_ERROR);
        return (-1);
        }

    dia_info (&win_ri, txt_get (TXT_SEND_BOOTP));

    if (bootp_wait_ig)
        sleep (bootp_wait_ig);

    rc_ii = performBootp (netdevice_tg, "255.255.255.255", "", 10, 0, NULL,
                          0, 1, BP_PUT_ENV);
    win_close (&win_ri);

    if (rc_ii || !getenv ("BOOTP_IPADDR"))
        {
        (void) dia_message (txt_get (TXT_ERROR_BOOTP), MSGTYPE_ERROR);
        return (-1);
        }

    data_pci = getenv ("BOOTP_IPADDR");
    if (data_pci)
        inet_aton (data_pci, &ipaddr_rg);

    data_pci = getenv ("BOOTP_NETMASK");
    if (data_pci)
        inet_aton (data_pci, &netmask_rg);

    data_pci = getenv ("BOOTP_BROADCAST");
    if (data_pci)
        inet_aton (data_pci, &broadcast_rg);

    data_pci = getenv ("BOOTP_NETWORK");
    if (data_pci)
        inet_aton (data_pci, &network_rg);

    data_pci = getenv("BOOTP_GATEWAYS_1");
    if (data_pci)
        inet_aton (data_pci, &gateway_rg);

    data_pci = getenv("BOOTP_GATEWAYS");
    if (data_pci)
        inet_aton (data_pci, &gateway_rg);

    data_pci = getenv ("BOOTP_DNSSRVS_1");
    if (data_pci)
        inet_aton (data_pci, &nameserver_rg);

    data_pci = getenv ("BOOTP_DNSSRVS");
    if (data_pci)
        inet_aton (data_pci, &nameserver_rg);

    data_pci = getenv("BOOTP_HOSTNAME");
    if (data_pci)
        strncpy (machine_name_tg, data_pci, sizeof (machine_name_tg) - 1);

    data_pci = getenv ("BOOTP_DOMAIN");
    if (data_pci)
        strncpy (domain_name_tg, data_pci, sizeof (domain_name_tg) - 1);

    data_pci = getenv ("BOOTP_BOOTFILE");
    if (data_pci && strlen (data_pci))
        {
        fprintf (stderr, "»%s«\n", data_pci);

        i_ii = 0;
        memset (tmp_ti, 0, sizeof (tmp_ti));
        while (i_ii < sizeof (tmp_ti) - 1 &&
               i_ii < strlen (data_pci) &&
               data_pci [i_ii] != ':')
            tmp_ti [i_ii] = data_pci [i_ii++];


        if (tmp_ti [0] && data_pci [i_ii] == ':')
            {
            strncpy (server_dir_tg, data_pci + i_ii + 1,
                     sizeof (server_dir_tg));
            inet_aton (tmp_ti, &nfs_server_rg);
            }
        else
            strncpy (server_dir_tg, data_pci, sizeof (server_dir_tg));
        }


    data_pci = getenv ("BOOTP_SERVER");
    if (data_pci && !nfs_server_rg.s_addr)
        inet_aton (data_pci, &nfs_server_rg);

    net_stop ();
    return (0);
    }


static int net_get_address (char *text_tv, struct in_addr *address_prr)
    {
    int  rc_ii;
    char input_ti [20];


    if (address_prr->s_addr)
        strcpy (input_ti, inet_ntoa (*address_prr));
    else
        input_ti [0] = 0;
    do
        {
        rc_ii = dia_input (text_tv, input_ti, 16, 16);
        if (rc_ii)
            return (rc_ii);
        rc_ii = net_check_address (input_ti, address_prr);
        if (rc_ii)
            (void) dia_message (txt_get (TXT_INVALID_INPUT), MSGTYPE_ERROR);
        }
    while (rc_ii);

    return (0);
    }
