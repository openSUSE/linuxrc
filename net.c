/*
 *
 * net.c         Network related functions
 *
 * Copyright (c) 1996-2002  Hubert Mantel, SuSE Linux AG  (mantel@suse.de)
 *
 */

#include "dietlibc.h"

#define WITH_NFS

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
#include <sys/reboot.h>
#include <sys/socket.h>
#include <sys/mount.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <errno.h>
#include <net/if.h>
#include <net/route.h>
#include <nfs/nfs.h>
#include "nfs_mount4.h"

/* this is probably the wrong solution... */
#ifndef NFS_FHSIZE
#define NFS_FHSIZE 32
#endif
#ifndef NFS_PORT
#define NFS_PORT 2049
#endif

#include "global.h"
#include "text.h"
#include "dialog.h"
#include "window.h"
#include "util.h"
#include "net.h"
#include "bootpc.h"
#include "file.h"
#include "module.h"

#define NFS_PROGRAM    100003
#define NFS_VERSION         2

#define MAX_NETDEVICE      10

int net_is_configured_im = FALSE;

static int  net_is_plip_im = FALSE;

#if !defined(NETWORK_CONFIG)
#  if defined (__s390__) || defined (__s390x__)
#    define NETWORK_CONFIG 0
#  else
#    define NETWORK_CONFIG 1
#  endif
#endif

#if NETWORK_CONFIG
static int  net_choose_device    (void);
static void net_setup_nameserver (void);
static int  net_input_data       (void);
#endif
#ifdef WITH_NFS
static void net_show_error       (enum nfs_stat status_rv);
#endif

int net_config()
{
#if NETWORK_CONFIG
  int rc;
  char buf[256];

  if(
    net_is_configured_im &&
    dia_yesno(txt_get(TXT_NET_CONFIGURED), YES) == YES
  ) {
    return 0;
  }

  if(net_choose_device()) return -1;

  net_stop();

  config.net.configured = nc_none;

  if(!config.win) {
    rc = YES;
  }
  else {
    sprintf(buf, txt_get(TXT_ASK_DHCP), config.net.use_dhcp ? "DHCP" : "BOOTP");
    rc = dia_yesno(buf, NO);
  }

  if(rc == ESCAPE) return -1;

  if(rc == YES) {
    rc = config.net.use_dhcp ? net_dhcp() : net_bootp();
    if(!rc) config.net.configured = config.net.use_dhcp ? nc_dhcp : nc_bootp;
  }
  else {
    rc = net_input_data();
    if(!rc) config.net.configured = nc_static;
  }

  if(rc) return -1;

  if(net_activate()) {
    dia_message(txt_get(TXT_ERROR_CONF_NET), MSGTYPE_ERROR);
    config.net.configured = nc_none;
    return -1;
  }

  net_setup_nameserver();

  net_is_configured_im = TRUE;

  net_check_address2(&config.net.server, 1);
  // net_check_address2(&..., 1);

#endif

  return 0;
}


void net_stop (void)
    {
    int             socket_ii;
    struct ifreq    interface_ri;

    if(config.test) {
      net_is_configured_im = FALSE;
      return;
    }

    if(config.net.dhcp_active) {
      net_dhcp_stop();
      net_is_configured_im = FALSE;
      return;
    }

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

    if(config.test) return 0;

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
        if (old_kernel_ig || config.net.netmask.ip.s_addr)
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
        if (old_kernel_ig || config.net.broadcast.ip.s_addr != 0xffffffff)
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

    if(config.test || !config.net.ifconfig || config.net.dhcp_active) return 0;

    socket_ii = socket (AF_INET, SOCK_DGRAM, 0);
    if (socket_ii == -1)
        return (socket_ii);

    memset (&interface_ri, 0, sizeof (struct ifreq));
    strcpy (interface_ri.ifr_name, netdevice_tg);

    sockaddr_ri.sin_family = AF_INET;
    sockaddr_ri.sin_port = 0;
    sockaddr_ri.sin_addr = config.net.hostname.ip;
    memcpy (&interface_ri.ifr_addr, &sockaddr_ri, sizeof (sockaddr_ri));
    if (ioctl (socket_ii, SIOCSIFADDR, &interface_ri) < 0)
        error_ii = TRUE;

    if (net_is_plip_im)
        {
        sockaddr_ri.sin_addr = config.net.pliphost.ip;
        memcpy (&interface_ri.ifr_dstaddr, &sockaddr_ri, sizeof (sockaddr_ri));
        if (ioctl (socket_ii, SIOCSIFDSTADDR, &interface_ri) < 0)
            error_ii = TRUE;
        }
    else
        {
        sockaddr_ri.sin_addr = config.net.netmask.ip;
        memcpy (&interface_ri.ifr_netmask, &sockaddr_ri, sizeof (sockaddr_ri));
        if (ioctl (socket_ii, SIOCSIFNETMASK, &interface_ri) < 0)
            if (old_kernel_ig || config.net.netmask.ip.s_addr)
                error_ii = TRUE;

        sockaddr_ri.sin_addr = config.net.broadcast.ip;
        memcpy (&interface_ri.ifr_broadaddr, &sockaddr_ri, sizeof (sockaddr_ri));
        if (ioctl (socket_ii, SIOCSIFBRDADDR, &interface_ri) < 0)
            if (old_kernel_ig || config.net.broadcast.ip.s_addr != 0xffffffff)
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
        sockaddr_ri.sin_addr = config.net.pliphost.ip;
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
        sockaddr_ri.sin_addr = config.net.network.ip;
        memcpy (&route_ri.rt_dst, &sockaddr_ri, sizeof (sockaddr_ri));
        route_ri.rt_flags = RTF_UP;
        if (ioctl (socket_ii, SIOCADDRT, &route_ri) < 0)
            if (old_kernel_ig)
                error_ii = TRUE;

        if (
          config.net.gateway.ip.s_addr &&
          config.net.gateway.ip.s_addr != config.net.hostname.ip.s_addr
        )
            {
            sockaddr_ri.sin_addr = config.net.gateway.ip;
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


int net_check_address2(inet_t *inet, int do_dns)
{
  struct hostent *he = NULL;
  struct in_addr iaddr;
  slist_t *sl;
#ifdef DIET
  char *s;
  file_t *f0, *f;
  char *has_dots;
#endif

  if(!inet) return -1;

  if(inet->ok) return 0;

  if(!inet->name || !*inet->name) return -1;

  if(!net_check_address(inet->name, &iaddr)) {
    inet->ok = 1;
    inet->ip = iaddr;

    str_copy(&inet->name, inet_ntoa(inet->ip));

    return 0;
  }

  for(sl = config.net.dns_cache; sl; sl = sl->next) {
    if(sl->key && sl->value && !strcasecmp(sl->key, inet->name)) {
      if(!net_check_address(sl->value, &iaddr)) {
        inet->ok = 1;
        inet->ip = iaddr;
        return 0;
      }
    }
  }

  /* ####### should be something like nameserver_active */
  if(
    !do_dns ||
    (!config.net.dhcp_active && !config.test && config.run_as_linuxrc)
  ) {
    return -1;
  }

#ifdef DIET
  has_dots = strchr(inet->name, '.');

  if(has_dots) {
//    fprintf(stderr, "trying >%s<\n", inet->name);
    he = gethostbyname(inet->name);
//    fprintf(stderr, "%p\n", he);  
  }

  if(!he) {
    f0 = file_read_file("/etc/resolv.conf");
    for(f = f0; f; f = f->next) {
      if(!strcmp(f->key_str, "search")) {
        s = malloc(strlen(inet->name) + strlen(f->value) + 2);
        sprintf(s, "%s.%s", inet->name, f->value);
//        fprintf(stderr, "trying >%s<\n", s);
        he = gethostbyname(s);
//        fprintf(stderr, "%p\n", he);
        if(!he) {
          he = gethostbyname(s);
//          fprintf(stderr, "%p\n", he);
        }
        free(s);
        if(he) break;
      }
    }
    file_free_file(f0);
  }

#else

  he = gethostbyname(inet->name);

#endif

  if(!he) {
    if(config.run_as_linuxrc) {
      fprintf(stderr, "dns: what is \"%s\"?\n", inet->name);
    }
    return -1;
  }

  inet->ok = 1;
  inet->ip = *((struct in_addr *) *he->h_addr_list);

  sl = slist_add(&config.net.dns_cache, slist_new());
  str_copy(&sl->key, inet->name);
  str_copy(&sl->value, inet_ntoa(inet->ip));

  if(config.run_as_linuxrc) {
    fprintf(stderr, "dns: %s is %s\n", inet->name, inet_ntoa(inet->ip));
  }

  return 0;
}


void net_smb_get_mount_options(char *options, inet_t *server, char *user, char *password, char *workgroup)
{
  if(!options) return;
  *options = 0;

  if(!server) return;
  sprintf(options,"ip=%s", inet_ntoa(server->ip));

  if(user) {
    strcat(options, ",username=");
    strcat(options, user);
    strcat(options, ",password=");
    strcat(options, password ?: "");
    if(workgroup) {
      strcat(options, ",workgroup=");
      strcat(options, workgroup);
    }
  } else {
    strcat(options, ",guest");
  }
}


/*
 * mount windows share
 *
 * if user == NULL, use 'guest' login
 */
int net_mount_smb(char *mountpoint, inet_t *server, char *hostdir, char *user, char *password, char *workgroup)
{
    /*******************************************************************

       abhängig von [X] Guest login 
	 options += "guest"
       bzw.
	 options += "username=" + USERNAME + ",password=" + PASSWORD


	 device = "//" + SERVER + "/" + SHARE

	 options += ",workgroup=" + WORKGROUP   falls WORKGROUP gesetzt ist

	 options += ",ip=" + SERVER_IP          falls SERVER_IP gesetzt ist


	 "mount -t smbfs" + device + " " + mountpoint + " " + options

    *******************************************************************/
  char tmp[1024];
  char mount_options[256];

  if(net_check_address2(server, 1)) return -1;

  if(!hostdir) hostdir = "/";
  if(!mountpoint || !*mountpoint) mountpoint = "/";

  net_smb_get_mount_options(mount_options, server, user, password, workgroup);

#if !defined(SUDO)
#  define SUDO
#endif

  sprintf(tmp,
    SUDO "smbmount //%s/%s %s -o ro,%s >&2",
    server->name, hostdir, mountpoint, mount_options
  );

  mod_modprobe("smbfs", NULL);

  if(system(tmp)) {
    sprintf(tmp, "%s", "Error trying to mount SMB share.");

    if(config.win) {
      dia_message(tmp, MSGTYPE_ERROR);
    }
    else {
      fprintf(stderr, "%s\n", tmp);
    }

    return -1;
  }

  return 0;
}

#ifdef WITH_NFS
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


int net_mount_nfs(char *mountpoint, inet_t *server, char *hostdir)
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
    char                   tmp_ti [1024];
    char                  *opts_pci;

  if(net_check_address2(server, 1)) return -1;

  if(!hostdir) hostdir = "/";
  if(!mountpoint || !*mountpoint) mountpoint = "/";

    memset (&server_ri, 0, sizeof (struct sockaddr_in));
    server_ri.sin_family = AF_INET;
    server_ri.sin_addr.s_addr = server->ip.s_addr;
    memcpy (&mount_server_ri, &server_ri, sizeof (struct sockaddr_in));

    memset (&mount_data_ri, 0, sizeof (struct nfs_mount_data));
//    mount_data_ri.flags = NFS_MOUNT_NONLM;
    mount_data_ri.rsize = config.net.nfs_rsize;
    mount_data_ri.wsize = config.net.nfs_wsize;
    mount_data_ri.timeo = 70;
    mount_data_ri.retrans = 3;
    mount_data_ri.acregmin = 3;
    mount_data_ri.acregmax = 60;
    mount_data_ri.acdirmin = 30;
    mount_data_ri.acdirmax = 60;
    mount_data_ri.namlen = NAME_MAX;
    mount_data_ri.version = NFS_MOUNT_VERSION;

    mount_server_ri.sin_port = htons (0);
    socket_ii = RPC_ANYSOCK;
#if 0
    mount_client_pri = clnttcp_create (&mount_server_ri, MOUNTPROG, MOUNTVERS,
                                       &socket_ii, 0, 0);
#else
    mount_client_pri = NULL;
#endif

    if (!mount_client_pri)
        {
	mount_data_ri.timeo = 7;
        mount_server_ri.sin_port = htons (0);
        socket_ii = RPC_ANYSOCK;
        timeout_ri.tv_sec = 3;
        timeout_ri.tv_usec = 0;
        mount_client_pri = clntudp_create (&mount_server_ri, MOUNTPROG,
                                           MOUNTVERS, timeout_ri, &socket_ii);
        }

    if (!mount_client_pri)
        {
        sleep(2);

	mount_data_ri.timeo = 7;
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
                       (xdrproc_t) xdr_dirpath, (caddr_t) &hostdir,
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

    memcpy ((char *) &mount_data_ri.root.data,
            (char *) status_ri.fhstatus_u.fhs_fhandle,
            NFS_FHSIZE);
    mount_data_ri.root.size = NFS_FHSIZE;
    memcpy(&mount_data_ri.old_root.data,
	   (char *) status_ri.fhstatus_u.fhs_fhandle,
	   NFS_FHSIZE);

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

    if (config.net.nfs_port)
        {
        fprintf (stderr, "Using specified port %d\n", config.net.nfs_port);
        port_ii = config.net.nfs_port;
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
    memcpy ((char *) &mount_data_ri.addr, (char *) &server_ri,
            sizeof (mount_data_ri.addr));
    strncpy (mount_data_ri.hostname, inet_ntoa(server->ip),
             sizeof (mount_data_ri.hostname));

    auth_destroy (mount_client_pri->cl_auth);
    clnt_destroy (mount_client_pri);
    close (socket_ii);

    sprintf (tmp_ti, "%s:%s", inet_ntoa(server->ip), hostdir);
    opts_pci = (char *) &mount_data_ri;
    rc_ii = mount (tmp_ti, mountpoint, "nfs", MS_RDONLY | MS_MGC_VAL, opts_pci);

  return rc_ii;
}

#else

int net_mount_nfs(char *mountpoint, inet_t *server, char *hostdir);
{
  return -1;
}

#endif	/* WITH_NFS */


#if NETWORK_CONFIG
int net_choose_device()
{
  char *items[MAX_NETDEVICE + 1], *s;
  int i, item_cnt, choice;
  char buf[MAX_X];
  file_t *f0, *f;
  slist_t *sl;
  static int last_item = 0;
  static struct {
    char *dev;
    int name;
  } net_dev[] = {
    { "eth",   TXT_NET_ETH0  },
    { "veth",  TXT_NET_ETH0  },
    { "plip",  TXT_NET_PLIP  },
    { "tr",    TXT_NET_TR0   },
    { "arc",   TXT_NET_ARC0  },
    { "fddi",  TXT_NET_FDDI  },
    { "hip",   TXT_NET_HIPPI },
    { "ctc",   TXT_NET_CTC   },
    { "escon", TXT_NET_ESCON },
    { "ci",    TXT_NET_CLAW  },
    { "iucv",  TXT_NET_CLAW  }
  };
    
  if(auto_ig) return 0;

  /* re-read - just in case... */
  util_update_netdevice_list(NULL, 1);

  for(item_cnt = 0, sl = config.net.devices; sl; sl = sl->next) {
    if(sl->key) item_cnt++;
  }

  f0 = file_read_file("/proc/net/dev");
  if(!f0) return -1;

  for(item_cnt = 0, f = f0; f && item_cnt < sizeof items / sizeof *items - 1; f = f->next) {
    for(i = 0; i < sizeof net_dev / sizeof *net_dev; i++) {
      if(strstr(f->key_str, net_dev[i].dev) == f->key_str) {
        sprintf(buf, "%-6s : %s", f->key_str, txt_get(net_dev[i].name));
        items[item_cnt++] = strdup(buf);
        break;
      }
    }
  }
  items[item_cnt] = NULL;

  file_free_file(f0);

  if(item_cnt == 0) {
    dia_message(txt_get(TXT_NO_NETDEVICE), MSGTYPE_ERROR);
    choice = -1;
  } else if(item_cnt == 1) {
    choice = 1;
  }
  else {
    choice = dia_list(txt_get(TXT_CHOOSE_NET), 32, NULL, items, last_item, align_left);
    if(choice) last_item = choice;
  }

  if(choice > 0) {
    s = strchr(items[choice - 1], ' ');
    if(s) *s = 0;
    strcpy(netdevice_tg, items[choice - 1]);
    net_is_plip_im = strstr(netdevice_tg, "plip") == netdevice_tg ? TRUE : FALSE;
  }

  for(i = 0; i < item_cnt; i++) free(items[i]);

  return choice > 0 ? 0 : -1;
}
#endif

#ifdef WITH_NFS
static void net_show_error(enum nfs_stat status_rv)
{
  int i;
  char *s, tmp[1024], tmp2[64];

  struct {
    enum nfs_stat stat;
    int errnumber;
  } nfs_err[] = {
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

  s = NULL;

  for(i = 0; i < sizeof nfs_err / sizeof *nfs_err; i++) {
    if(nfs_err[i].stat == status_rv) {
      s = strerror(nfs_err[i].errnumber);
      break;
    }
  }

  if(!s) {
    sprintf(tmp2, "unknown error %d\n", status_rv);
    s = tmp2;
  }

  if(config.run_as_linuxrc) {
    sprintf(tmp, txt_get(TXT_ERROR_NFSMOUNT), s);

    if(config.win) {
      dia_message(tmp, MSGTYPE_ERROR);
    }
    else {
      fprintf(stderr, "%s\n", tmp);
    }
  }
  else {
    fprintf(stderr, "mount: nfs mount failed, reason given by server: %s\n", s);
  }
}
#endif

#if NETWORK_CONFIG
void net_setup_nameserver()
{
  char *s;
  FILE *f;

  if(config.net.dhcp_active || !config.win || config.test) return;

  if(!config.net.nameserver.name && config.net.hostname.ok) {
    s = inet_ntoa(config.net.hostname.ip);
    config.net.nameserver.name = strdup(s ?: "");
  }
  net_get_address(txt_get(TXT_INPUT_NAMED), &config.net.nameserver, 0);

  if(config.net.nameserver.ok) {
    if((f = fopen("/etc/resolv.conf", "w"))) {
      fprintf(f, "nameserver %s\n", config.net.nameserver.name);
      if(config.net.domain) {
        fprintf(f, "search %s\n", config.net.domain);
      }
      fclose(f);
    }
  }
}


int net_input_data()
{
  if(net_get_address(txt_get(TXT_INPUT_IPADDR), &config.net.hostname, 1)) return -1;

  if(net_is_plip_im) {
    if(!config.net.pliphost.name) {
      name2inet(&config.net.pliphost, config.net.hostname.name);
    }

    if(net_get_address(txt_get(TXT_INPUT_PLIP_IP), &config.net.pliphost, 1)) return -1;

    if(!config.net.gateway.name) {
      name2inet(&config.net.gateway, config.net.pliphost.name);
    }

    if(!config.net.server.name) {
      name2inet(&config.net.server, config.net.pliphost.name);
    }
  }
  else {
    name2inet(&config.net.pliphost, "");

    if(!config.net.gateway.name) {
      name2inet(&config.net.gateway, config.net.hostname.name);
    }

    if(!config.net.server.name) {
      name2inet(&config.net.server, config.net.hostname.name);
    }

    if(!config.net.netmask.ok) {
      char *s = inet_ntoa(config.net.hostname.ip);

      name2inet(
        &config.net.netmask,
        strstr(s, "10.10.") == s ? "255.255.0.0" : "255.255.255.0"
      );
    }
    if(net_get_address(txt_get(TXT_INPUT_NETMASK), &config.net.netmask, 0)) return -1;

    s_addr2inet(
      &config.net.broadcast,
      config.net.hostname.ip.s_addr | ~config.net.netmask.ip.s_addr
    );

    s_addr2inet(
      &config.net.network,
      config.net.hostname.ip.s_addr & config.net.netmask.ip.s_addr
    );

    net_get_address(txt_get(TXT_INPUT_GATEWAY), &config.net.gateway, 1);
  }

  return 0;
}
#endif


int net_bootp()
{
  window_t  win;
  int rc, netconf_error;
  char *s, *t;
  char tmp[256];

  if(auto_ig && config.net.hostname.ok) return 0;

  if(config.test) return 0;

  name2inet(&config.net.netmask, "");
  name2inet(&config.net.network, "");
  s_addr2inet(&config.net.broadcast, 0xffffffff);
  name2inet(&config.net.pliphost, "");
  name2inet(&config.net.hostname, "");
  netconf_error	= 0;

  if(net_activate ()) {
    if(config.win) {
      dia_message(txt_get(TXT_ERROR_CONF_NET), MSGTYPE_ERROR);
    }
    else {
      fprintf(stderr, "network setup failed\n");
    }
            
    return -1;
  }

  if(config.win) {
    sprintf(tmp, txt_get(TXT_SEND_DHCP), "BOOTP");
    dia_info(&win, tmp);
  }

  if(config.net.bootp_wait) sleep(config.net.bootp_wait);

  rc = performBootp(
    netdevice_tg, "255.255.255.255", "",
    config.net.bootp_timeout, 0, NULL, 0, 1, BP_PUT_ENV, 1
  );

  win_close(&win);

  if(rc || !getenv("BOOTP_IPADDR")) {
    if(config.instmode_extra == inst_cdwithnet) {
      dia_input("HOSTNAME", machine_name_tg, sizeof machine_name_tg - 1, 16, 0);

      if(net_get_address(txt_get(TXT_INPUT_IPADDR), &config.net.hostname, 0)) netconf_error++;

      if(net_get_address(txt_get(TXT_INPUT_NETMASK), &config.net.netmask, 0)) netconf_error++;

      s_addr2inet(
        &config.net.broadcast,
        config.net.hostname.ip.s_addr | ~config.net.netmask.ip.s_addr
      );

      s_addr2inet(
        &config.net.network,
        config.net.hostname.ip.s_addr & config.net.netmask.ip.s_addr
      );

      if(net_get_address(txt_get(TXT_INPUT_GATEWAY), &config.net.gateway, 0)) netconf_error++;

      if(netconf_error) {
        dia_message("Configuration not valid: press any key to reboot, ... ", MSGTYPE_ERROR);
        reboot(RB_AUTOBOOT);
      }

      net_stop();

      return 0;
    }
    else {
      if(config.win) {
        sprintf(tmp, txt_get (TXT_ERROR_DHCP), "BOOTP");
        dia_message(tmp, MSGTYPE_ERROR);
      }
      return -1;
    }
  }

  name2inet(&config.net.hostname, getenv("BOOTP_IPADDR"));
  net_check_address2(&config.net.hostname, 0);

  name2inet(&config.net.netmask, getenv("BOOTP_NETMASK"));
  net_check_address2(&config.net.netmask, 0);

  name2inet(&config.net.broadcast, getenv("BOOTP_BROADCAST"));
  net_check_address2(&config.net.broadcast, 0);

  name2inet(&config.net.network, getenv("BOOTP_NETWORK"));
  net_check_address2(&config.net.network, 0);

  name2inet(&config.net.gateway, getenv("BOOTP_GATEWAYS"));
  name2inet(&config.net.gateway, getenv("BOOTP_GATEWAYS_1"));
  net_check_address2(&config.net.gateway, 0);

  name2inet(&config.net.nameserver, getenv("BOOTP_DNSSRVS"));
  name2inet(&config.net.nameserver, getenv("BOOTP_DNSSRVS_1"));
  net_check_address2(&config.net.nameserver, 0);

  s = getenv("BOOTP_HOSTNAME");
  if(s && !config.net.hostname.name) config.net.hostname.name = strdup(s);

  if((s = getenv("BOOTP_DOMAIN"))) {
    if(config.net.domain) free(config.net.domain);
    config.net.domain = strdup(s);
  }

  s = getenv("BOOTP_ROOT_PATH");
  if(!s) s = getenv("BOOTP_BOOTFILE");

  if(s && *s) {
    s = strdup(s);

    fprintf(stderr, "bootp root: \"%s\"\n", s);

    if((t = strchr(s, ':'))) {
      *t++ = 0;
    }
    else {
      t = s;
    }

    if(*t && !config.serverdir) config.serverdir = strdup(t);

    if(t != s && !config.net.server.name) {
      name2inet(&config.net.server, s);
      net_check_address2(&config.net.server, 0);
    }

    free(s);
  }

  if(!config.net.server.name) {
    name2inet(&config.net.server, getenv("BOOTP_SERVER"));
    net_check_address2(&config.net.server, 0);
  }

#ifdef CDWITHNET_DEBUG
  if(config.instmode_extra == inst_cdwithnet) {
    dia_message(machine_name_tg, MSGTYPE_ERROR);
  }
#endif

  net_stop();

  return 0;
}


int net_get_address(char *text, inet_t *inet, int do_dns)
{
  int rc;
  char input[256];

  *input = 0;
  if(inet->name) strcpy(input, inet->name);

  do {
    if((rc = dia_input(text, input, sizeof input - 1, 16, 0))) return rc;
    name2inet(inet, input);
    rc = net_check_address2(inet, do_dns);
    if(rc) dia_message(txt_get(TXT_INVALID_INPUT), MSGTYPE_ERROR);
  } while(rc);

  return 0;
}


int net_dhcp()
{
  char cmd[256], file[256], *s;
  file_t *f0, *f;
  window_t win;

  if(config.net.dhcp_active) return 0;

  if(config.test) return 0;

  if(config.win) {
    sprintf(cmd, txt_get(TXT_SEND_DHCP), "DHCP");
    dia_info(&win, cmd);
  }

  strcpy(cmd, "dhcpcd -B");
  if(config.net.dhcp_timeout != 60) {
    sprintf(cmd + strlen(cmd), "-t %d", config.net.dhcp_timeout);
  }
  if(*machine_name_tg) {
    sprintf(cmd + strlen(cmd), " %s", machine_name_tg);
  }
  sprintf(cmd + strlen(cmd), " %s", netdevice_tg);

  sprintf(file, "/var/lib/dhcpcd/dhcpcd-%s.info", netdevice_tg);

  unlink(file);

  system(cmd);

  f0 = file_read_file(file);

  for(f = f0; f; f = f->next) {
    switch(f->key) {
      case key_ipaddr:
        name2inet(&config.net.hostname, f->value);
        net_check_address2(&config.net.hostname, 0);
        break;

      case key_netmask:
        name2inet(&config.net.netmask, f->value);
        net_check_address2(&config.net.netmask, 0);
        break;

      case key_network:
        name2inet(&config.net.network, f->value);
        net_check_address2(&config.net.network, 0);
        break;

      case key_broadcast:
        name2inet(&config.net.broadcast, f->value);
        net_check_address2(&config.net.broadcast, 0);
        break;

      case key_gateway:
        name2inet(&config.net.gateway, f->value);
        net_check_address2(&config.net.gateway, 0);
        break;

      case key_domain:
        if(*f->value) {
          if(config.net.domain) free(config.net.domain);
          config.net.domain = strdup(f->value);
        }
        break;

      case key_dhcpsiaddr:
        if(!config.net.server.name) {
          name2inet(&config.net.server, f->value);
          net_check_address2(&config.net.server, 0);
        }
        break;

      case key_rootpath:
      case key_bootfile:
        if(*f->value && !config.serverdir) {
          str_copy(&config.serverdir, f->value);
        }
        break;

      case key_dns:
        if((s = strchr(f->value, ','))) *s = 0;
        name2inet(&config.net.nameserver, f->value);
        net_check_address2(&config.net.nameserver, 0);
        break;

      default:
    }
  }

  if(config.win) win_close(&win);

  if(f0) {
    config.net.dhcp_active = 1;
  }
  else {
    if(config.win) {
      sprintf(cmd, txt_get(TXT_ERROR_DHCP), "DHCP");
      dia_message(cmd, MSGTYPE_ERROR);
    }
  }

  file_free_file(f0);

  return config.net.dhcp_active ? 0 : 1;
}


void net_dhcp_stop()
{
  if(!config.net.dhcp_active) return;

  system("dhcpcd -k");

  config.net.dhcp_active = 0;
}


unsigned net_config_mask()
{
  unsigned u = 0;

  if(config.net.hostname.name) u |= 1;
  if(config.net.netmask.ok) u |= 2;
  if(config.net.gateway.ok) u |= 4;
  if(config.net.server.name) u |= 8;
  if(config.net.nameserver.ok) u |= 0x10;
  if(config.serverdir) u |= 0x20;

  return u;
}

char *net_if2module(char *net_if)
{
  slist_t *sl;

  for(sl = config.net.devices; sl; sl = sl->next ) {
    if(sl->key && !strcmp(sl->key, net_if)) return sl->value;
  }

  return NULL;
}


