/*
 *
 * net.c         Network related functions
 *
 * Copyright (c) 1996-2001  Hubert Mantel, SuSE GmbH  (mantel@suse.de)
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
static int  net_get_address      (char *text_tv, struct in_addr *address_prr);

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
  }
  else {
    rc = net_input_data();
  }

  if(rc) return -1;

  if(net_activate()) {
    dia_message(txt_get(TXT_ERROR_CONF_NET), MSGTYPE_ERROR);
    return -1;
  }

  net_setup_nameserver();

  net_is_configured_im = TRUE;
#endif

  return 0;
}


void net_stop (void)
    {
    int             socket_ii;
    struct ifreq    interface_ri;

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

    if(config.test || config.net.dhcp_active) return 0;

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


int net_check_address2(inet_t *inet)
{
  struct hostent *he = NULL;
  struct in_addr iaddr;
#ifdef DIET
  file_t *f0, *f;
  char *s;
  char *has_dots;
#endif

  if(!inet) return -1;

  if(inet->ok) return 0;

  if(!inet->name || !*inet->name) return -1;

  if(!net_check_address(inet->name, &iaddr)) {
    inet->ok = 1;
    inet->ip = iaddr;

//    fprintf(stderr, "%s is %s\n", inet->name, inet_ntoa(inet->ip));

    return 0;
  }

  /* ####### should be something like nameserver_active */
  if(!config.net.dhcp_active && !config.test && config.run_as_linuxrc) {
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

#if 0
  if(!he && !has_dots) {
//    fprintf(stderr, "trying >%s<\n", inet->name);
    he = gethostbyname(inet->name);
//    fprintf(stderr, "%p\n", he);  
  }
#endif

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

  if(config.run_as_linuxrc) {
    fprintf(stderr, "dns: %s is %s\n", inet->name, inet_ntoa(inet->ip));
  }

  return 0;
}


void net_smb_get_mount_options (char* options)
{
    sprintf(options,"ip=%s", inet_ntoa(config.net.smb.server.ip));
    if (config.net.smb.user) {
	strcat(options, ",username=");
	strcat(options, config.net.smb.user);
	strcat(options, ",password=");
	strcat(options, config.net.smb.password);
	if (config.net.smb.workgroup) {
	    strcat(options,",workgroup=");
	    strcat(options,config.net.smb.workgroup);
	}
    } else {
	strcat(options,",guest");
    }
}


int net_mount_smb()
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

  net_smb_get_mount_options(mount_options);

#if !defined(SUDO)
#  define SUDO
#endif

  sprintf(tmp,
    SUDO "smbmount //%s/%s %s -o %s >&2",
    config.net.smb.server.name,
    config.net.smb.share,
    mountpoint_tg,
    mount_options
  );

  mod_load_modules("smbfs", 0);

  if(system(tmp)) {

    sprintf(tmp, "%s", "Error trying to mount the SMB share.");

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
    char                   tmp_ti [1024];
    char                  *opts_pci;
    inet_t inet = {};

    inet.name = strdup(server_addr_tv);
    if(net_check_address2(&inet)) {
      free(inet.name);
      return -1;
    }

    memset (&server_ri, 0, sizeof (struct sockaddr_in));
    server_ri.sin_family = AF_INET;
    server_ri.sin_addr.s_addr = inet.ip.s_addr;
    memcpy (&mount_server_ri, &server_ri, sizeof (struct sockaddr_in));

    free(inet.name);
    inet.name = NULL; inet.ok = 0;

    memset (&mount_data_ri, 0, sizeof (struct nfs_mount_data));
//    mount_data_ri.flags = NFS_MOUNT_NONLM;
    mount_data_ri.rsize = 0;
    mount_data_ri.wsize = 0;
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
    memcpy ((char *) &mount_data_ri.addr, (char *) &server_ri,
            sizeof (mount_data_ri.addr));
    strncpy (mount_data_ri.hostname, server_addr_tv,
             sizeof (mount_data_ri.hostname));

    auth_destroy (mount_client_pri->cl_auth);
    clnt_destroy (mount_client_pri);
    close (socket_ii);

    sprintf (tmp_ti, "%s:%s", server_addr_tv, hostdir_tv);
    opts_pci = (char *) &mount_data_ri;
    rc_ii = mount (tmp_ti, mountpoint_tg, "nfs", MS_RDONLY | MS_MGC_VAL, opts_pci);

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
#else

int net_mount_nfs(char *server_addr_tv, char *hostdir_tv)
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
  static int last_item = 0;
  static struct {
    char *dev;
    int name;
  } net_dev[] = {
//    { "sit",   TXT_NET_TR0   },
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
static void net_setup_nameserver (void)
    {
    if(config.net.dhcp_active) return;

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
#endif


int net_bootp (void)
    {
    window_t  win_ri;
    int       rc_ii;
    char     *data_pci;
    char      tmp_ti [256];
    int       i_ii;
    int	      netconf_error;

    if (auto_ig && ipaddr_rg.s_addr)
        return (0);

    plip_host_rg.s_addr = 0;
    ipaddr_rg.s_addr = 0;
    netmask_rg.s_addr = 0;
    network_rg.s_addr = 0;
    broadcast_rg.s_addr = 0xffffffff;
    netconf_error	= 0;

    if (net_activate ())
        {
        if (!auto2_ig)
            dia_message (txt_get (TXT_ERROR_CONF_NET), MSGTYPE_ERROR);
        else
            fprintf(stderr, "network setup failed\n");
            
        return (-1);
        }

    if (!auto2_ig)
        {
        sprintf (tmp_ti, txt_get (TXT_SEND_DHCP), "BOOTP");
	dia_info (&win_ri, tmp_ti);
	}

    if (bootp_wait_ig)
        sleep (bootp_wait_ig);

    rc_ii = performBootp (netdevice_tg, "255.255.255.255", "",
                          bootp_timeout_ig, 0, NULL, 0, 1, BP_PUT_ENV, 1);
    win_close (&win_ri);

    if ( rc_ii || !getenv ("BOOTP_IPADDR") ) {
        if ( bootmode_ig == BOOTMODE_CDWITHNET ) {
	    dia_input ("HOSTNAME", machine_name_tg, 32, 16);
	    if( net_get_address (txt_get (TXT_INPUT_IPADDR), &ipaddr_rg) ) {
	        netconf_error++;
            }
	    if( net_get_address (txt_get (TXT_INPUT_NETMASK), &netmask_rg) ) {
	        netconf_error++;
	    }
            broadcast_rg.s_addr = ipaddr_rg.s_addr | ~netmask_rg.s_addr;
            network_rg.s_addr = ipaddr_rg.s_addr & netmask_rg.s_addr;
	    if( net_get_address (txt_get (TXT_INPUT_GATEWAY), &gateway_rg) ) {
   	        netconf_error++;
            }
	    if( netconf_error ) {
	        dia_message( "Configuration not valid: press any key to reboot, ... ", 
                             MSGTYPE_ERROR);
	        reboot (RB_AUTOBOOT);
            }
	    net_stop ();
	    return(0);
        } else {
            if (!auto2_ig)
                {
                sprintf (tmp_ti, txt_get (TXT_ERROR_DHCP), "BOOTP");
                dia_message (tmp_ti, MSGTYPE_ERROR);
                }
            return (-1);
        }
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

    data_pci = getenv ("BOOTP_ROOT_PATH");
    if (data_pci)
	fprintf (stderr, "root-path is defined. Will be used intead of bootfile");	
    else
	data_pci = getenv ("BOOTP_BOOTFILE");

    if (data_pci && strlen (data_pci))
        {
        fprintf (stderr, "\"%s\"\n", data_pci);

        i_ii = 0;
        memset (tmp_ti, 0, sizeof (tmp_ti));
        while (i_ii < sizeof (tmp_ti) - 1 &&
               i_ii < strlen (data_pci) &&
               data_pci [i_ii] != ':')
            tmp_ti [i_ii] = data_pci [i_ii++];


        if (tmp_ti [0] && data_pci [i_ii] == ':')
            {
            if ((valid_net_config_ig & 0x20) != 0x20)
                strncpy (server_dir_tg, data_pci + i_ii + 1,
                         sizeof (server_dir_tg));

            inet_aton (tmp_ti, &nfs_server_rg);
            }
        else
            {
            if ((valid_net_config_ig & 0x20) != 0x20)
                strncpy (server_dir_tg, data_pci, sizeof (server_dir_tg));
            }
        }

    data_pci = getenv ("BOOTP_SERVER");
    if (data_pci && !nfs_server_rg.s_addr)
        inet_aton (data_pci, &nfs_server_rg);

#ifdef CDWITHNET_DEBUG
    if( bootmode_ig == BOOTMODE_CDWITHNET ) {
       dia_message(machine_name_tg, MSGTYPE_ERROR);
    }
#endif

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

int net_dhcp()
{
  char cmd[256], file[256];
  file_t *f0, *f;
  window_t win;

  if(config.net.dhcp_active) return 0;

  if(!auto2_ig) {
    sprintf(cmd, txt_get(TXT_SEND_DHCP), "DHCP");
    dia_info(&win, cmd);
  }

  if(*machine_name_tg) {
    sprintf(cmd, "dhcpcd -h %s %s", machine_name_tg, netdevice_tg);
  }
  else {
    sprintf(cmd, "dhcpcd %s", netdevice_tg);
  }

  sprintf(file, "/var/lib/dhcpcd/dhcpcd-%s.info", netdevice_tg);

  unlink(file);

  system(cmd);

  f0 = file_read_file(file);

  for(f = f0; f; f = f->next) {
    switch(f->key) {
      case key_ipaddr:
        if(f->is.inet) ipaddr_rg = f->ivalue;
        break;

      case key_netmask:
        if(f->is.inet) netmask_rg = f->ivalue;
        break;

      case key_network:
        if(f->is.inet) network_rg = f->ivalue;
        break;

      case key_broadcast:
        if(f->is.inet) broadcast_rg = f->ivalue;
        break;

      case key_gateway:
        if(f->is.inet) gateway_rg = f->ivalue;
        break;

      case key_domain:
        if(*f->value) {
          if(config.net.domain) free(config.net.domain);
          config.net.domain = strdup(f->value);
          strcpy(domain_name_tg, config.net.domain);
        }
        break;

      case key_dhcpsiaddr:
        if(f->is.inet) nfs_server_rg = f->ivalue;
        break;

      case key_rootpath:
      case key_bootfile:
        if(*f->value) {
          if(config.serverdir) free(config.serverdir);
          config.serverdir = strdup(f->value);
          strcpy(server_dir_tg, config.serverdir);
        }
        break;

      default:
    }
  }

  win_close(&win);

  if(f0) {
    config.net.dhcp_active = 1;
  }
  else {
    if(!auto2_ig) {
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


