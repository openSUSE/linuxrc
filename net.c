/*
 *
 * net.c         Network related functions
 *
 * Copyright (c) 1996-2002  Hubert Mantel, SuSE Linux AG  (mantel@suse.de)
 *
 */

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <fcntl.h>
#include <fnmatch.h>
#include <dirent.h>
#include <rpc/rpc.h>
#include <rpc/pmap_prot.h>
#include <rpc/pmap_clnt.h>
#include <rpcsvc/mount.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/ioctl.h>
#include <sys/reboot.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/mount.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <errno.h>
#include <signal.h>
#include <net/if.h>
#include <net/route.h>
#include <netinet/in.h>
#include <netinet/ether.h>
#include <nfs/nfs.h>
#include "nfs_mount4.h"

/* this is probably the wrong solution... */
#ifndef NFS_FHSIZE
#define NFS_FHSIZE 32
#endif
#ifndef NFS_PORT
#define NFS_PORT 2049
#endif

#include <hd.h>

#include "global.h"
#include "text.h"
#include "dialog.h"
#include "window.h"
#include "util.h"
#include "net.h"
#include "bootpc.h"
#include "file.h"
#include "module.h"
#include "hotplug.h"
#include "url.h"
#include "auto2.h"

#define NFS_PROGRAM    100003
#define NFS_VERSION         2

int  net_is_ptp_im = FALSE;

#if !defined(NETWORK_CONFIG)
#  define NETWORK_CONFIG 1
#endif

static int net_activate4(void);
static int net_activate6(void);
#if defined(__s390__) || defined(__s390x__)
int net_activate_s390_devs(void);
int net_activate_s390_devs_ex(hd_t* hd, char** device);
#endif
static void net_setup_nameserver(void);

#if NETWORK_CONFIG
static int net_choose_device(void);
static int net_input_data(void);
#endif
static void net_show_error(enum nfs_stat status_rv);
static int _net_mount_nfs(char *mountpoint, inet_t *server, char *hostdir, unsigned port, int flags);

static void if_down(char *dev);
static int wlan_auth_cb(dia_item_t di);

static dia_item_t di_wlan_auth_last = di_none;
static int net_dhcp4(void);
static int net_dhcp6(void);

static void net_ask_domain(void);


/*
 * Ask for VNC & SSH password, unless they have already been set.
 *
 * Global vars changed:
 *  config.net.vncpassword
 *  config.net.sshpassword
 */
void net_ask_password()
{
  int win_old = config.win;

  if(config.vnc && (!config.net.vncpassword || strlen(config.net.vncpassword) < 8)) {
    if(!config.win) util_disp_init();
    for(;;) {
      if(dia_input2(txt_get(TXT_VNC_PASSWORD), &config.net.vncpassword, 20, 1)) break;
      if(config.net.vncpassword && strlen(config.net.vncpassword) >= 8) break;
      dia_message(txt_get(TXT_VNC_PASSWORD_TOO_SHORT), MSGTYPE_ERROR);
    }
  }

  if(config.usessh && !config.net.sshpassword) {
    if(!config.win) util_disp_init();
    dia_input2(txt_get(TXT_SSH_PASSWORD), &config.net.sshpassword, 20, 1);
  }

  if(config.win && !win_old) util_disp_done();
}


void net_ask_domain()
{
  char *tmp = NULL;
  slist_t *sl0, *sl;
  inet_t ip = { };
  int err, ndomains;

  str_copy(&tmp, config.net.domain);

  do {
    err = ndomains = 0;

    dia_input2(txt_get(TXT_INPUT_DOMAIN), &tmp, 40, 0);  
    if(!tmp) {
      str_copy(&config.net.domain, NULL);
      return;
    }

    sl0 = slist_split(' ', tmp);

    for(sl = sl0; sl; sl = sl->next) {
      if(++ndomains > 6) {
        dia_message(txt_get(TXT_DOMAIN_TOOMANY), MSGTYPE_ERROR);
        break;
      }
      str_copy(&ip.name, sl->key);
/*      net_check_address(&ip, 0);
      if(!ip.ok) {
        dia_message(txt_get(TXT_DOMAIN_ALPHANUMERIC), MSGTYPE_ERROR);
        break;
      } */
    }

    if(!sl) {
      str_copy(&config.net.domain, NULL);
      config.net.domain = slist_join(" ", sl0);
    }
    else {
      err = 1;
    }

    slist_free(sl0);
  }
  while(err);

  str_copy(&tmp, NULL);
  str_copy(&ip.name, NULL);
}

/*
 * Configure network. Ask for network config data if necessary.
 * Does either DHCP, BOOTP or calls net_activate_ns() to setup the interface.
 *
 * Return:
 *      0: ok
 *   != 0: error or abort
 *
 * Global vars changed:
 *  config.net.is_configured
 *
 * Does nothing if DHCP is active.
 *
 * FIXME: needs window mode or not?
 */
int net_config()
{
  int rc = 0;
#if NETWORK_CONFIG
  char buf[256];

  if(config.net.keep) return 0;

  net_ask_password();

  if(
    config.win &&
    config.net.is_configured &&
    (!config.manual || dia_yesno(txt_get(TXT_NET_CONFIGURED), YES) == YES)
  ) {
    return 0;
  }

  if(net_choose_device()) return -1;

  net_stop();

  config.net.configured = nc_none;

  if(config.win && config.net.setup != NS_DHCP) {
    if((config.net.setup & NS_DHCP)) {
      sprintf(buf, txt_get(TXT_ASK_DHCP), config.net.use_dhcp ? "DHCP" : "BOOTP");
      rc = dia_yesno(buf, NO);
    }
    else {
      rc = NO;
    }
  }
  else {
    rc = YES;
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

  if(net_activate_ns()) {
    dia_message(txt_get(TXT_ERROR_CONF_NET), MSGTYPE_ERROR);
    config.net.configured = nc_none;
    if(!config.test) return rc = -1;
  }

#endif

  return rc;
}


static void net_config2_manual(void);


/*
 * Configure network interface. Ask for network config data if necessary.
 * Does either DHCP, BOOTP or static network setup.
 *
 * config.net.device: network interface
 * config.net.use_dhcp: dhcp vs. bootp
 *
 * Return:
 *      0: ok
 *   != 0: error or abort
 *
 *   config.net.is_configured: nc_none, nc_dhcp, nc_bootp, nc_static
 *
 * Does nothing if DHCP is active.
 *
 */
int net_config2(int type)
{
  config.net.configured = nc_none;

  // ###### FIXME: use net_choose_device()
  if(!config.net.device) str_copy(&config.net.device, config.netdevice);
  if(!config.net.device) {
    util_update_netdevice_list(NULL, 1);
    if(config.net.devices) str_copy(&config.net.device, config.net.devices->key);
  }

  if(!config.net.device) {
    fprintf(stderr, "interface setup: no interfaces\n");
    return 1;
  }

  fprintf(stderr, "interface setup: %s\n", config.net.device);

//  if(url->is.wlan && wlan_setup()) return 0;

  if((config.net.do_setup & DS_SETUP)) net_config2_manual();

  if(config.net.configured == nc_none) config.net.configured = nc_static;

  /* we need at least ip & netmask for static network config */
  if((net_config_mask() & 3) != 3) {
    printf(
      "Sending %s request to %s...\n",
      config.net.use_dhcp ? config.net.ipv6 ? "DHCP6" : "DHCP" : "BOOTP",
      config.net.device
    );
    fflush(stdout);
    fprintf(stderr,
      "sending %s request to %s... ",
      config.net.use_dhcp ? config.net.ipv6 ? "DHCP6" : "DHCP" : "BOOTP",
      config.net.device
    );

    config.net.use_dhcp ? net_dhcp() : net_bootp();

    if(
      !config.test &&
      !config.net.ipv6 &&
      (
        !config.net.hostname.ok ||
        !config.net.netmask.ok ||
        !config.net.broadcast.ok
      )
    ) {
      fprintf(stderr, "no/incomplete answer.\n");
      config.net.configured = nc_none;

      return 0;
    }
    fprintf(stderr, "ok.\n");

    config.net.configured = config.net.use_dhcp ? nc_dhcp : nc_bootp;
  }

  if(net_activate_ns()) {
    fprintf(stderr, "network setup failed\n");
    config.net.configured = nc_none;

    return 0;
  }
  else {
    fprintf(stderr, "%s activated\n", config.net.device);
  }





  return 0;
}


void net_config2_manual()
{
  int win_old;
  slist_t *sl;

  if(!config.net.do_setup) return;

  if((net_config_mask() & 3) == 3) {	/* we have ip & netmask */
    config.net.configured = nc_static;
    /* looks a bit weird, but we need it here for net_activate_ns() */
    if(!config.net.device) str_copy(&config.net.device, config.netdevice);
    if(!config.net.device) {
      util_update_netdevice_list(NULL, 1);
      if(config.net.devices) str_copy(&config.net.device, config.net.devices->key);
    }
    if(net_activate_ns()) {
      fprintf(stderr, "net activation failed\n");
      config.net.configured = nc_none;
    }
  }

  if(config.net.configured == nc_none || config.net.do_setup) {
    if(config.net.all_ifs && (config.net.setup & NS_DHCP)) {
      util_update_netdevice_list(NULL, 1);

      config.net.configured = nc_none;

      for(sl = config.net.devices; sl && config.net.configured == nc_none; sl = sl->next) {
        str_copy(&config.net.device, sl->key);

        printf(
          "Sending %s request to %s...\n",
          config.net.use_dhcp ? "DHCP" : "BOOTP", config.net.device
        );
        fflush(stdout);
        fprintf(stderr,
          "Sending %s request to %s... ",
          config.net.use_dhcp ? "DHCP" : "BOOTP", config.net.device
        );
        config.net.use_dhcp ? net_dhcp() : net_bootp();
        if(
          !config.net.hostname.ok ||
          !config.net.netmask.ok ||
          !config.net.broadcast.ok
        ) {
          fprintf(stderr, "no/incomplete answer.\n");
        }
        else {
          config.net.configured = config.net.use_dhcp ? nc_dhcp : nc_bootp;

          if(net_activate_ns()) {
            fprintf(stderr, "%s: net activation failed\n", config.net.device);
            config.net.configured = nc_none;
          }
          else {
            fprintf(stderr, "%s: ok\n", config.net.device);
          }
        }
      }
    }
    else {
      if(!(win_old = config.win)) util_disp_init();
      net_config();
      if(!win_old) util_disp_done();
    }
  }

  if(config.net.configured == nc_none) {
    config.vnc = config.usessh = 0;
  }
}


/*
 * Shut down all network interfaces.
 *
 * Global vars changed:
 *  config.net.is_configured
 *
 * config.net.device:    interface
 * /proc/net/route: configured interfaces
 */
void net_stop()
{
  file_t *f0, *f;
  slist_t *sl0 = NULL, *sl;

  if(config.debug) fprintf(stderr, "shutting network down\n");

  if(config.net.keep) return;

  if(config.test) {
    config.net.is_configured = nc_none;
    return;
  }

  if(config.net.dhcp_active) {
    net_dhcp_stop();
    config.net.is_configured = nc_none;
  }

  if(!config.net.is_configured) return;

  /* build list of configured interfaces */
  slist_append_str(&sl0, config.net.device);

  f0 = file_read_file("/proc/net/route", kf_none);
  for((f = f0) && (f = f->next); f; f = f->next) {
    if(f->key_str && !slist_getentry(sl0, f->key_str)) slist_append_str(&sl0, f->key_str);
  }
  file_free_file(f0);

  for(sl = sl0; sl; sl = sl->next) if_down(sl->key);

  slist_free(sl0);

  config.net.is_configured = nc_none;
}


/*
 * Configure loopback interface.
 */
int net_setup_localhost()
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

    if(!util_check_exist("/etc/hosts")) system("echo 127.0.0.1 localhost >/etc/hosts");

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
        if (config.net.netmask.ip.s_addr)
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
        if (config.net.broadcast.ip.s_addr != 0xffffffff)
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

  return error_ii;
}


/*
 * Setup network interface and write name server config.
 *
 * Return:
 *      0: ok
 *   != 0: error code
 *
 * Global vars changed:
 *  config.net.is_configured
 *  config.net.nameserver
 *
 * Writes nameserver & domain to /etc/resolv.conf.
 *
 * config.net.device: interface
 */
int net_activate_ns()
{
  int err4 = 1, err6 = 1;

  if(config.net.keep) return 0;

  if(config.net.ipv4) err4 = net_activate4();
  if(config.net.ipv6) err6 = net_activate6();

  if(!err4 || !err6) net_setup_nameserver();

  // at least one should have worked
  return err4 && err6 ? 1 : 0;
}


/*
 * Setup IPv4 network interface.
 *
 * Return:
 *      0: ok
 *   != 0: error code
 *
 * Global vars changed:
 *  config.net.is_configured
 *
 * Does nothing if DHCP is active.
 *
 * config.net.device: interface
 */
int net_activate4()
{
    int                 socket_ii;
    struct ifreq        interface_ri;
    struct rtentry      route_ri;
    struct sockaddr_in  sockaddr_ri;
    int                 error_ii = FALSE;
    char                command[1000];
    int                 rc;

    if(!config.net.ifconfig || config.net.dhcp_active || config.net.keep) return 0;

    if(config.test) {
      config.net.is_configured = nc_static;

      return 0;
    }

    if(!config.net.device) {
      util_error_trace("net_activate: no network interface!\n");
      return 1;
    }

    config.net.is_configured = nc_none;

    net_apply_ethtool(config.net.device, config.net.hwaddr);

    if(!config.forceip && util_check_exist("/sbin/arping")) {
       sprintf(command, "ifconfig %s up", config.net.device);
       util_error_trace("net_activate: %s\n", command);
       rc = system(command);
       if (rc) {
           util_error_trace("net_activate: ifconfig %s up failed!\n", config.net.device);
           return 1;
       }

       sleep(config.net.ifup_wait + 2);

       sprintf(command, "arping -c 1 -I %s -D %s 1>&2", config.net.device, inet_ntoa(config.net.hostname.ip));
       util_error_trace("net_activate: %s\n", command);
       rc = system(command);

       sprintf(command, "ifconfig %s down", config.net.device);
       (void)system (command);
       util_error_trace("net_activate: %s\n", command);

       if (rc) {
           util_error_trace("net_activate: address %s in use by another machine!\n", inet_ntoa(config.net.hostname.ip));
           sprintf(command, txt_get(TXT_IP_ADDRESS_IN_USE), inet_ntoa(config.net.hostname.ip));
           dia_message(command, MSGTYPE_ERROR);
           return 1;
       }
    }

    socket_ii = socket (AF_INET, SOCK_DGRAM, 0);
    if (socket_ii == -1)
    {
        util_error_trace("net_activate: socket(AF_INET, SOCK_DGRAM, 0) failed at %d\n",__LINE__);
        return (socket_ii);
    }

    memset (&interface_ri, 0, sizeof (struct ifreq));
    strcpy (interface_ri.ifr_name, config.net.device);

    sockaddr_ri.sin_family = AF_INET;
    sockaddr_ri.sin_port = 0;
    sockaddr_ri.sin_addr = config.net.hostname.ip;
    memcpy (&interface_ri.ifr_addr, &sockaddr_ri, sizeof (sockaddr_ri));
    if (ioctl (socket_ii, SIOCSIFADDR, &interface_ri) < 0)
    {
        error_ii = TRUE;
        util_error_trace("net_activate: SIOCSIFADDR failed at %d\n",__LINE__);
    }

    if (net_is_ptp_im)
        {
        sockaddr_ri.sin_addr = config.net.ptphost.ip;
        memcpy (&interface_ri.ifr_dstaddr, &sockaddr_ri, sizeof (sockaddr_ri));
        if (ioctl (socket_ii, SIOCSIFDSTADDR, &interface_ri) < 0)
        {
            error_ii = TRUE;
            util_error_trace("net_activate: SIOCSIFDSTADDR failed at %d\n",__LINE__);
        }
        }
    else
        {
        sockaddr_ri.sin_addr = config.net.netmask.ip;
        memcpy (&interface_ri.ifr_netmask, &sockaddr_ri, sizeof (sockaddr_ri));
        if (ioctl (socket_ii, SIOCSIFNETMASK, &interface_ri) < 0)
            if (config.net.netmask.ip.s_addr)
            {
                error_ii = TRUE;
                util_error_trace("net_activate: SIOCSIFNETMASK failed at %d\n",__LINE__);
            }

        sockaddr_ri.sin_addr = config.net.broadcast.ip;
        memcpy (&interface_ri.ifr_broadaddr, &sockaddr_ri, sizeof (sockaddr_ri));
        if (ioctl (socket_ii, SIOCSIFBRDADDR, &interface_ri) < 0)
            if (config.net.broadcast.ip.s_addr != 0xffffffff)
            {
                error_ii = TRUE;
                util_error_trace("net_activate: SIOCSIFBRDADDR failed at %d\n",__LINE__);
            }
        }

    if (ioctl (socket_ii, SIOCGIFFLAGS, &interface_ri) < 0)
    {
        error_ii = TRUE;
        util_error_trace("net_activate: SIOCGIFFLAGS failed at %d\n",__LINE__);
    }

    interface_ri.ifr_flags |= IFF_UP | IFF_RUNNING;
    if (net_is_ptp_im)
        interface_ri.ifr_flags |= IFF_POINTOPOINT | IFF_NOARP;
    else
        interface_ri.ifr_flags |= IFF_BROADCAST;
    if (ioctl (socket_ii, SIOCSIFFLAGS, &interface_ri) < 0)
    {
        error_ii = TRUE;
        util_error_trace("net_activate: SIOCSIFFLAGS failed at %d\n",__LINE__);
    }

    memset (&route_ri, 0, sizeof (struct rtentry));
    route_ri.rt_dev = config.net.device;

    if (net_is_ptp_im)
        {
        sockaddr_ri.sin_addr = config.net.ptphost.ip;
        memcpy (&route_ri.rt_dst, &sockaddr_ri, sizeof (sockaddr_ri));
        route_ri.rt_flags = RTF_UP | RTF_HOST;
        if (ioctl (socket_ii, SIOCADDRT, &route_ri) < 0)
        {
            error_ii = TRUE;
            util_error_trace("net_activate: SIOCADDRT failed at %d\n",__LINE__);
        }

        memset (&route_ri.rt_dst, 0, sizeof (route_ri.rt_dst));
        route_ri.rt_dst.sa_family = AF_INET;
        memcpy (&route_ri.rt_gateway, &sockaddr_ri, sizeof (sockaddr_ri));
        route_ri.rt_flags = RTF_UP | RTF_GATEWAY;
        if (ioctl (socket_ii, SIOCADDRT, &route_ri) < 0)
        {
            error_ii = TRUE;
            util_error_trace("net_activate: SIOCADDRT failed at %d\n",__LINE__);
        }
        }
    else
        {
        sockaddr_ri.sin_addr = config.net.network.ip;
        memcpy (&route_ri.rt_dst, &sockaddr_ri, sizeof (sockaddr_ri));
        route_ri.rt_flags = RTF_UP;
        ioctl (socket_ii, SIOCADDRT, &route_ri);

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
            {
                error_ii = TRUE;
                util_error_trace("net_activate: SIOCADDRT failed at %d\n",__LINE__);
            }
            }
        }

  close(socket_ii);

  if(!error_ii) {
    config.net.is_configured = nc_static;
    if(config.net.ifup_wait) sleep(config.net.ifup_wait);
  }

  return error_ii;
}


/*
 * Setup IPv6 network interface.
 *
 * Return:
 *      0: ok
 *   != 0: error code
 *
 * Global vars changed:
 *  config.net.is_configured
 *
 * Does nothing if DHCP is active.
 *
 * config.net.device: interface
 */
int net_activate6()
{
  int err = 0, delay = config.net.ifup_wait + 5;
  char *cmd = NULL, ip_buf[INET6_ADDRSTRLEN], buf1[512], buf2[512], c;
  const char *ip;
  FILE *f;
  inet_t host6 = { };

  if(!config.net.ifconfig || config.net.dhcp_active || config.net.keep) return 0;

  if(config.test) {
    config.net.is_configured = nc_static;

    return 0;
  }

  if(!config.net.device) {
    fprintf(stderr, "net_activate: no network interface!\n");
    return 1;
  }

  config.net.is_configured = nc_none;

  if(!config.net.ipv4) net_apply_ethtool(config.net.device, config.net.hwaddr);

  strprintf(&cmd, "ip link set %s up", config.net.device);
  err = system(cmd);

  if(!err) {
    if(
      config.net.hostname.ok &&
      config.net.hostname.ipv6 &&
      (ip = inet_ntop(AF_INET6, &config.net.hostname.ip6, ip_buf, sizeof ip_buf))
    ) {
      strprintf(&cmd, "ip addr add %s/%u dev %s", ip, config.net.hostname.prefix6, config.net.device);
      err = system(cmd);
    }
    else {
      // wait for autoconfig
      strprintf(&cmd, "ip -o addr show dev %s", config.net.device);

      do {
        sleep(1);

        if((f = popen(cmd, "r"))) {
          while(fgets(buf1, sizeof buf1, f)) {
            if(sscanf(buf1, "%*d: %*s inet6 %511s scope global %c", buf2, &c) == 2) {
              if(config.debug) fprintf(stderr, "ip6: %s\n", buf2);
              name2inet(&host6, buf2);
              net_check_address(&host6, 0);
              if(host6.ok && host6.ipv6) {
                config.net.hostname.ok = host6.ok;
                config.net.hostname.ipv6 = host6.ipv6;
                config.net.hostname.ip6 = host6.ip6;
                config.net.hostname.prefix6 = host6.prefix6;
                str_copy(&config.net.hostname.name, host6.name);
              }
              break;
            }
          }
          pclose(f);
        }

      } while(--delay > 0 && f && !config.net.hostname.ok && !config.net.hostname.ipv6);
    }
  }

  if(!err) {
    config.net.is_configured = nc_static;
    if(config.net.ifup_wait) sleep(config.net.ifup_wait);
  }

  return err;
}


/*
 * Parse inet->name for ipv4/ipv6 adress. If do_dns is set, try nameserver lookup.
 *
 * return:
 *   0 : ok
 *   1 : failed
 */
int net_check_address(inet_t *inet, int do_dns)
{
  struct hostent *he = NULL;
  slist_t *sl;
  char *s, buf[INET6_ADDRSTRLEN];
  int net_bits = 0;

  if(!inet) return 1;

  s = inet->name;

  memset(inet, 0, sizeof *inet);
  inet->name = s;

  if(!inet->name) return 1;
  if(!*inet->name) {
    str_copy(&inet->name, NULL);
    return 1;
  }

  inet->ok = 1;

  if((s = strchr(inet->name, '/'))) {
    *s++ = 0;
    net_bits = strtoul(s, &s, 0);
    if(*s || net_bits < 0) {
      inet->ok = 0;
    }
  }

  if(inet->ok) {
    if(strchr(inet->name, ':')) {
      if(inet_pton(AF_INET6, inet->name, &inet->ip6) > 0) {
        inet->ipv6 = 1;
        s = (char *) inet_ntop(AF_INET6, &inet->ip6, buf, sizeof buf);
        if(s) str_copy(&inet->name, s);
        if(net_bits > 0) inet->prefix6 = net_bits;
      }
      else {
        inet->ok = 0;
      }
    }
    else {
      if(inet_pton(AF_INET, inet->name, &inet->ip) > 0) {
        inet->ipv4 = 1;
        s = (char *) inet_ntop(AF_INET, &inet->ip, buf, sizeof buf);
        if(s) str_copy(&inet->name, s);
        if(net_bits > 0) {
          inet->prefix4 = net_bits;
          inet->net.s_addr = htonl(-1 << (32 - net_bits));
        }
      }
      else {
        inet->ok = 0;
      }
    }
  }

  if(
    !do_dns ||
    // do we really need this check ??
    (
      !config.net.dhcp_active &&
      !config.net.nameserver[0].ok &&
      !config.test &&
      config.run_as_linuxrc
    )
  ) {
    inet->ok = inet->ok && ((config.net.ipv6 && inet->ipv6) || (config.net.ipv4 && inet->ipv4));

    return inet->ok ? 0 : 1;
  }

  if(!inet->ipv6 && !inet->ipv4) {
    for(sl = config.net.dns_cache; sl; sl = sl->next) {
      if(sl->key && sl->value && !strcasecmp(sl->key, inet->name)) {
        if(!inet->ipv6 && config.net.ipv6 && strchr(sl->value, ':')) {
          if(inet_pton(AF_INET6, sl->value, &inet->ip6) > 0) {
            inet->ipv6 = 1;
          }
        }

        if(!inet->ipv4 && config.net.ipv4 && !strchr(sl->value, ':')) {
          if(inet_pton(AF_INET, sl->value, &inet->ip) > 0) {
            inet->ipv4 = 1;
          }
        }
      }
    }
  }

  if(inet->ipv6 || inet->ipv4) inet->ok = 1;

  if(!inet->ok) {
    if(config.net.ipv6) {
      he = gethostbyname2(inet->name, AF_INET6);
      if(!he) { sleep(1); he = gethostbyname2(inet->name, AF_INET6); }
      if(!he) {
        if(config.run_as_linuxrc) {
          fprintf(stderr, "dns6: what is \"%s\"?\n", inet->name);
        }
      }
      else {
        inet->ok = 1;
        inet->ipv6 = 1;
        memcpy(&inet->ip6, *he->h_addr_list, he->h_length);

        s = (char *) inet_ntop(AF_INET6, &inet->ip6, buf, sizeof buf);
        if(s) {
          sl = slist_add(&config.net.dns_cache, slist_new());
          str_copy(&sl->key, inet->name);
          str_copy(&sl->value, s);

          if(config.run_as_linuxrc) {
            fprintf(stderr, "dns6: %s is %s\n", inet->name, s);
          }
        }
      }
    }

    if(config.net.ipv4) {
      he = gethostbyname2(inet->name, AF_INET);
      if(!he) { sleep(1); gethostbyname2(inet->name, AF_INET); }
      if(!he) {
        if(config.run_as_linuxrc) {
          fprintf(stderr, "dns: what is \"%s\"?\n", inet->name);
        }
      }
      else {
        inet->ok = 1;
        inet->ipv4 = 1;
        memcpy(&inet->ip, *he->h_addr_list, he->h_length);

        s = (char *) inet_ntop(AF_INET, &inet->ip, buf, sizeof buf);
        if(s) {
          sl = slist_add(&config.net.dns_cache, slist_new());
          str_copy(&sl->key, inet->name);
          str_copy(&sl->value, s);

          if(config.run_as_linuxrc) {
            fprintf(stderr, "dns: %s is %s\n", inet->name, s);
          }
        }
      }
    }
  }

  inet->ok = inet->ok && ((config.net.ipv6 && inet->ipv6) || (config.net.ipv4 && inet->ipv4));

  return inet->ok ? 0 : 1;
}


/*
 * Build mount option suitable for muont.cifs.
 */
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
     /*
      * mount.cifs needs a username, otherwise it takes LOGNAME from
      * environment. see bugzilla #20152
      */
    strcat(options, ",username=root,guest");
  }
}


/*
 * Mount windows share.
 * (Run mount.cifs.)
 *
 * Return:
 *      0: ok
 *   != 0: error code
 *
 * mountpoint: mount point
 * server: SMB server
 * share: share
 * user: user (NULL: guest)
 * password: password (NULL: no password)
 * workgroup: workgroup (NULL: no workgroup)
 *
 */

/*
 * depending on guest login
 *   options += "guest"
 * resp.
 *   options += "username=" + USERNAME + ",password=" + PASSWORD
 *
 *   device = "//" + SERVER + "/" + SHARE
 *   options += ",workgroup=" + WORKGROUP   falls WORKGROUP gesetzt ist
 *   options += ",ip=" + SERVER_IP          falls SERVER_IP gesetzt ist
 * "  mount -t smbfs" + device + " " + mountpoint + " " + options
 */
int net_mount_smb(char *mountpoint, inet_t *server, char *share, char *user, char *password, char *workgroup)
{
  char tmp[1024];
  char mount_options[256];

  if(!config.net.cifs.binary) return -2;

  if(net_check_address(server, 1)) return -3;

  if(!share) share = "";
  if(!mountpoint || !*mountpoint) mountpoint = "/";

  net_smb_get_mount_options(mount_options, server, user, password, workgroup);

  sprintf(tmp,
    "%s //%s/%s %s -o ro,%s >&2",
    config.net.cifs.binary, server->name, share, mountpoint, mount_options
  );

  mod_modprobe(config.net.cifs.module, NULL);

  fprintf(stderr, "%s\n", tmp);

  if(system(tmp)) return -1;

  return 0;
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


/*
 * Mount NFS volume.
 *
 * Tries v3 first, then v2.
 *
 * mountpoint: mount point
 * server: server address
 * hostdir: directory on server
 *
 * config.net.nfs: nfs options
 *
 * return:
 *      0: ok
 *   != 0: error code
 *
 */
int net_mount_nfs(char *mountpoint, inet_t *server, char *hostdir, unsigned port)
{
  int err, flags = NFS_MOUNT_NONLM;

  if(!config.net.nfs.udp) flags |= NFS_MOUNT_TCP;
  if(config.net.nfs.vers != 2) flags |= NFS_MOUNT_VER3;

  /* first, v3 with tcp */
  err = _net_mount_nfs(mountpoint, server, hostdir, port, flags);

  /* if that doesn't work, try v2, with udp */
  if(err == EPROTONOSUPPORT) {
    err = _net_mount_nfs(mountpoint, server, hostdir, port, NFS_MOUNT_NONLM);
  }

  return err;
}


/*
 * Mount NFS volume.
 *
 * Similar to net_mount_nfs() but lets you specify NFS mount flags.
 *
 * mountpoint: mount point
 * server: server address
 * hostdir: directory on server
 * flags: NFS mount flags
 *
 * config.net.nfs: nfs options
 *
 * return:
 *      0: ok
 *   != 0: error code
 *
 */
int _net_mount_nfs(char *mountpoint, inet_t *server, char *hostdir, unsigned port, int flags)
{
  struct sockaddr_in server_in, mount_server_in;
  struct nfs_mount_data mount_data;
  CLIENT *client;
  int sock, fsock, err, i;
  struct timeval tv;
  struct fhstatus fhs;
  char *buf = NULL;

  if(net_check_address(server, 1)) return -2;

  if(!hostdir) hostdir = "/";
  if(!mountpoint || !*mountpoint) mountpoint = "/";

  memset(&server_in, 0, sizeof server_in);
  server_in.sin_family = AF_INET;
  server_in.sin_addr.s_addr = server->ip.s_addr;
  memcpy(&mount_server_in, &server_in, sizeof mount_server_in);
  memset(&mount_data, 0, sizeof mount_data);
  mount_data.flags = flags;
  mount_data.rsize = config.net.nfs.rsize;
  mount_data.wsize = config.net.nfs.wsize;
  mount_data.retrans = 3;
  mount_data.acregmin = 3;
  mount_data.acregmax = 60;
  mount_data.acdirmin = 30;
  mount_data.acdirmax = 60;
  mount_data.namlen = NAME_MAX;
  mount_data.version = NFS_MOUNT_VERSION;

  /* two tries */
  for(i = 0, client = NULL; i < 2 && !client; i++) {
    if(i) sleep(2);
    mount_data.timeo = 7;
    mount_server_in.sin_port = htons(0);
    sock = RPC_ANYSOCK;
    tv.tv_sec = 3;
    tv.tv_usec = 0;
    client = clntudp_create(&mount_server_in, MOUNTPROG, MOUNTVERS, tv, &sock);
  }

  if(!client) {
    net_show_error(-1);

    return -1;
  }

  client->cl_auth = authunix_create_default();
  tv.tv_sec = 20;
  tv.tv_usec = 0;

  err = clnt_call(client, MOUNTPROC_MNT,
    (xdrproc_t) xdr_dirpath, (caddr_t) &hostdir,
    (xdrproc_t) xdr_fhstatus, (caddr_t) &fhs,
    tv
  );

  if(err) {
    net_show_error(-1);
    return -1;
  }

  if(fhs.fhs_status) {
    net_show_error(fhs.fhs_status);

    return -1;
  }

  memcpy(&mount_data.root.data, fhs.fhstatus_u.fhs_fhandle, NFS_FHSIZE);
  mount_data.root.size = NFS_FHSIZE;

  memcpy(&mount_data.old_root.data, fhs.fhstatus_u.fhs_fhandle, NFS_FHSIZE);

  fsock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if(fsock < 0) {
    net_show_error(-1);
    
    return -1;
  }

  if(bindresvport(fsock, 0) < 0) {
    net_show_error(-1);

    return -1;
  }

  if(!port) {
    server_in.sin_port = PMAPPORT;
    port = pmap_getport(&server_in, NFS_PROGRAM, NFS_VERSION, IPPROTO_UDP);
    if(!port) port = NFS_PORT;
  }

  server_in.sin_port = htons(port);

  mount_data.fd = fsock;
  memcpy(&mount_data.addr, &server_in, sizeof mount_data.addr);

  strncpy(mount_data.hostname, inet_ntoa(server->ip), sizeof mount_data.hostname);

  auth_destroy(client->cl_auth);
  clnt_destroy(client);
  close(sock);

  strprintf(&buf, "%s:%s", inet_ntoa(server->ip), hostdir);

  err = mount(buf, mountpoint, "nfs", MS_RDONLY | MS_MGC_VAL, &mount_data);

  free(buf);

  if(err == -1) return errno;

  return err;
}


#if NETWORK_CONFIG
/*
 * Let user select a network interface.
 *
 * Does nothing if network interface has been explicitly specified.
 * No user interaction if there is exactly one interface.
 * Shows error message if there is no interface.
 *
 * Note: expects window mode.
 *
 * Global vars changed:
 *  config.net.device
 *
 */
int net_choose_device()
{
  char **items, **item_devs = NULL;
  int i, max_items = 0, item_cnt, choice, width;
  char *buf = NULL;
  file_t *f0, *f;
  slist_t *sl;
  window_t win;
  static int net_drivers_loaded = 0;
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
    { "iucv",  TXT_NET_IUCV  },
    { "hsi",   TXT_NET_HSI   }
  };
  hd_data_t *hd_data = calloc(1, sizeof *hd_data);
  hd_t *hd, *hd_cards;
  hd_t **item_hds = NULL;
    
  if(config.netdevice) {
    str_copy(&config.net.device, config.netdevice);

    return 0;
  }

  if(config.manual == 1 && !net_drivers_loaded) {
    dia_info(&win, txt_get(TXT_LOAD_NETWORK_DRIVERS), MSGTYPE_INFO);
    load_network_mods();
    win_close(&win);
    net_drivers_loaded = 1;
  }

  if(config.manual >= 2) {
#if defined(__s390__) || defined(__s390x__)
    /* bring up network devices, write hwcfg */
    fprintf(stderr, "activate s390 devs 1\n");
    if(net_activate_s390_devs()) return 1;
#endif

    /* re-read - just in case... */
    util_update_netdevice_list(NULL, 1);

    for(sl = config.net.devices; sl; sl = sl->next) {
      if(sl->key) max_items++;
    }

    items = calloc(max_items + 1, sizeof *items);
    item_devs = calloc(max_items + 1, sizeof *item_devs);

    f0 = file_read_file("/proc/net/dev", kf_none);
    if(!f0) return -1;

    for(item_cnt = 0, f = f0; f && item_cnt < max_items; f = f->next) {
      for(i = 0; i < sizeof net_dev / sizeof *net_dev; i++) {
        if(strstr(f->key_str, net_dev[i].dev) == f->key_str) {
          strprintf(&buf, "%-6s : %s", f->key_str, txt_get(net_dev[i].name));
          item_devs[item_cnt] = strdup(f->key_str);
          items[item_cnt++] = strdup(buf);
          break;
        }
      }
    }

    file_free_file(f0);
  }
  else {
    hd_data->flags.nowpa = 1;

    hd_cards = hd_list(hd_data, hw_network_ctrl, 1, NULL);
    for(hd = hd_cards; hd; hd = hd->next) max_items++;

    items = calloc(max_items + 1, sizeof *items);
    item_devs = calloc(max_items + 1, sizeof *item_devs);
    item_hds = calloc(max_items + 1, sizeof *item_hds);

    for(width = 0, hd = hd_cards; hd; hd = hd->next) {
      if(hd->unix_dev_name) {
        i = strlen(hd->unix_dev_name);
        if(i > width) width = i;
      }
    }

    for(item_cnt = 0, hd = hd_cards; hd; hd = hd->next) {
      item_hds[item_cnt] = hd;
      if(hd->unix_dev_name) {
        item_devs[item_cnt] = strdup(hd->unix_dev_name);
      }
#if defined(__s390__) || defined(__s390x__)
#define MAX_NET_DEVICES_SHOWN 20
      if(item_cnt > MAX_NET_DEVICES_SHOWN) {
        item_hds[item_cnt] = NULL;
        strprintf(items + item_cnt++, txt_get(TXT_MANUAL_NETDEV_PARAMS));
        break;
      } else {
        int lcss = -1;
        int ccw = -1;
        hd_res_t* r;
        char* annotation = 0;
        
        if(hd->detail && hd->detail->ccw.data)
          lcss = hd->detail->ccw.data->lcss;
        
        for(r = hd->res; r; r = r->next) {
          if(r->any.type == res_io) {
            ccw = (int) r->io.base;
          }
        }
        if ( ccw == -1 ) {
          /* IUCV device */
          if(hd->rom_id) strprintf(&annotation, "(%s)", hd->rom_id);
          else strprintf(&annotation, "");
        }
        else {
          strprintf(&annotation, "(%1x.%1x.%04x)", lcss >> 8, lcss & 0xf, ccw);
        }
        
        if(hd->unix_dev_name) {
          strprintf(items + item_cnt++, "%*s : %s %s", -width, hd->unix_dev_name, hd->model,
            annotation);
        }
        else {
          strprintf(items + item_cnt++, "%s %s", hd->model, annotation);
        }
        free(annotation);
      }
#else
      if(hd->unix_dev_name) {
        strprintf(items + item_cnt++, "%*s : %s", -width, hd->unix_dev_name, hd->model);
      }
      else {
        strprintf(items + item_cnt++, "%s", hd->model);
      }
#endif
    }
  }

  if(item_cnt == 0) {
    dia_message(txt_get(TXT_NO_NETDEVICE), MSGTYPE_ERROR);
    choice = -1;
  } else if(item_cnt == 1) {
    choice = 1;
  }
  else {
    choice = dia_list(txt_get(TXT_CHOOSE_NET), 50, NULL, items, last_item, align_left);
    if(choice) last_item = choice;
  }

  if(choice > 0 && !item_devs[choice - 1]) {
#if defined(__s390__) || defined(__s390x__)
    fprintf(stderr, "activate s390 devs 2\n");
    net_activate_s390_devs_ex(item_hds[choice - 1], &item_devs[choice - 1]);
    if(!item_devs[choice - 1]) {
#endif
      dia_message(txt_get(TXT_NO_NETDEVICE), MSGTYPE_ERROR);
      choice = -1;
#if defined(__s390__) || defined(__s390x__)
    }
#endif
  }

  if(choice > 0) {
    str_copy(&config.net.device, item_devs[choice - 1]);
    net_is_ptp_im = FALSE;
    if(strstr(config.net.device, "plip") == config.net.device) net_is_ptp_im = TRUE;
    if(strstr(config.net.device, "iucv") == config.net.device) net_is_ptp_im = TRUE;
    if(strstr(config.net.device, "ctc") == config.net.device) net_is_ptp_im = TRUE;

    if(item_hds && item_hds[choice - 1]) {
      hd = item_hds[choice - 1];
      if(hd->is.wlan) {
        if(wlan_setup()) choice = -1;
      }
    }
  }

  for(i = 0; i < item_cnt; i++) {
    free(items[i]);
    if(item_devs) free(item_devs[i]);
  }
  free(items);
  free(item_devs);
  free(item_hds);

  hd_free_hd_data(hd_data);
  free(hd_data);

  free(buf);

  return choice > 0 ? 0 : -1;
}
#endif


/*
 * Show NFS error messages.
 *
 * Helper for net_mount_nfs().
 *
 * nfs_stat: NFS status
 */
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

  for(i = 0; (unsigned) i < sizeof nfs_err / sizeof *nfs_err; i++) {
    if(nfs_err[i].stat == status_rv) {
      s = strerror(nfs_err[i].errnumber);
      break;
    }
  }

  if(!s) {
    sprintf(tmp2, "unknown error %d\n", status_rv);
    s = tmp2;
  }

  sprintf(tmp,
    config.win ? txt_get(TXT_ERROR_NFSMOUNT) : "mount: nfs mount failed, server says: %s\n",
    s
  );

  if(config.win) {
    dia_message(tmp, MSGTYPE_ERROR);
  }
  else {
    fprintf(stderr, "%s\n", tmp);
  }
}


/*
 * Let user enter nameservers.
 *
 * Asks for name servers if window mode is active.
 *
 * Writes nameserver & domain to /etc/resolv.conf.
 *
 * Global vars changed:
 *  config.net.nameserver
 */
void net_setup_nameserver()
{
  char *s, buf[256];
  FILE *f;
  unsigned u;

  if(config.win && !config.net.dhcp_active) {

    if((config.net.setup & NS_NAMESERVER)) {
      for(u = 0; u < config.net.nameservers; u++) {
        if(config.net.nameservers == 1) {
          s = "Enter the IP address of your name server. Leave empty if you don't need one.";
          if(config.linemode) s = "Enter the IP address of your name server. Leave empty or enter \"+++\" if you don't need one.";
        }
        else {
           sprintf(buf, txt_get(config.linemode ? TXT_INPUT_NAMED1_S390 : TXT_INPUT_NAMED1), u + 1);
           s = buf;
        }
        if(net_get_address(s, &config.net.nameserver[u], 0)) break;
      }
      for(; u < config.net.nameservers; u++) {
        str_copy(&config.net.nameserver[u].name, NULL);
        config.net.nameserver[u].ok = 0;
      }
    }
  }

  if(config.test) return;

  if(
    !(config.net.ipv6 && config.net.dhcp_active) &&
    (f = fopen("/etc/resolv.conf", "w"))
  ) {
    for(u = 0; u < config.net.nameservers; u++) {
      if(config.net.nameserver[u].ok) {
        fprintf(f, "nameserver %s\n", config.net.nameserver[u].name);
      }
    }
    if(config.net.domain) {
      fprintf(f, "search %s\n", config.net.domain);
    }
    fclose(f);
  }
}


#if NETWORK_CONFIG
/*
 * Let user enter some network config data.
 *
 * Note: expects window mode.
 *
 * Global vars changed:
 *  config.net.hostname
 *  config.net.netmask
 *  config.net.ptphost
 *  config.net.gateway
 *  config.net.broadcast
 *  config.net.network
 *  config.net.gateway
 */
int net_input_data()
{
  inet_t host6 = {};

  config.net.netmask.ok = 0;

  if((config.net.setup & NS_HOSTIP)) {
    if(config.net.ipv4) {
      if(net_get_address("Enter your IPv4 address.\n Example: 192.168.5.77/24", &config.net.hostname, 1)) return -1;
    }
    if(config.net.ipv6) {
      if(net_get_address("Enter your IPv6 address (leave empty for autoconfig).\nExample: 2001:db8:75:fff::3/64", &host6, 1) == 2) return -1;
      if(host6.ok && host6.ipv6) {
        config.net.hostname.ok = host6.ok;
        config.net.hostname.ipv6 = host6.ipv6;
        config.net.hostname.ip6 = host6.ip6;
        config.net.hostname.prefix6 = host6.prefix6;
        if(!config.net.hostname.name) str_copy(&config.net.hostname.name, host6.name);
      }
    }
  }

  if(config.net.hostname.ipv4 && config.net.hostname.net.s_addr) {
    s_addr2inet(&config.net.netmask, config.net.hostname.net.s_addr);
  }

  if(net_is_ptp_im) {
    if(!config.net.ptphost.name) {
      name2inet(&config.net.ptphost, config.net.hostname.name);
    }

    if(net_get_address(txt_get(TXT_INPUT_PLIP_IP), &config.net.ptphost, 1)) return -1;

    if(!config.net.gateway.name) {
      name2inet(&config.net.gateway, config.net.ptphost.name);
    }
  }
  else {
    name2inet(&config.net.ptphost, "");

    if(config.net.ipv4) {
      if(!config.net.netmask.ok) {
        char *s = inet_ntoa(config.net.hostname.ip);

        name2inet(
          &config.net.netmask,
          strstr(s, "10.10.") == s ? "255.255.0.0" : "255.255.255.0"
        );
      }

      if(
        !config.net.hostname.prefix4 &&
        !config.net.netmask.ok &&
        (config.net.setup & NS_NETMASK)
      ) {
        if(net_get_address(txt_get(TXT_INPUT_NETMASK), &config.net.netmask, 0)) return -1;
      }

      if(config.net.hostname.ipv4) {
        s_addr2inet(
          &config.net.broadcast,
          config.net.hostname.ip.s_addr | ~config.net.netmask.ip.s_addr
        );

        s_addr2inet(
          &config.net.network,
          config.net.hostname.ip.s_addr & config.net.netmask.ip.s_addr
        );
      }
    }

    if((config.net.setup & NS_GATEWAY)) {
      config.net.gateway.ok = 0;
      if(net_get_address("Enter the IP address of the gateway. Leave empty if you don't need one.", &config.net.gateway, 1) == 2) return -1;
    }

    if((config.net.setup & NS_NAMESERVER)) net_ask_domain();
  }

  return 0;
}
#endif


/*
 * Use bootp to get network config data.
 *
 * Does nothing if we already got a hostip address (in config.net.hostname).
 *
 * Note: shuts all interfaces down.
 *
 * Global vars changed:
 *  config.net.hostname
 *  config.net.netmask
 *  config.net.network
 *  config.net.broadcast
 *  config.net.ptphost
 *  config.net.gateway
 *  config.net.nameserver
 *  config.net.domain
 * 
 * config.net.bootp_wait: delay between interface setup & bootp request
 * config.net.device: interface
 */
int net_bootp()
{
  window_t  win;
  int rc, netconf_error;
  char *s;
  char tmp[256];

  if(config.net.hostname.ok || config.net.keep) return 0;

  if(config.test) return 0;

  name2inet(&config.net.netmask, "");
  name2inet(&config.net.network, "");
  s_addr2inet(&config.net.broadcast, 0xffffffff);
  name2inet(&config.net.ptphost, "");
  name2inet(&config.net.hostname, "");
  netconf_error	= 0;

  if(net_activate_ns()) {
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
    dia_info(&win, tmp, MSGTYPE_INFO);
  }

  if(config.net.bootp_wait) sleep(config.net.bootp_wait);

  rc = performBootp(
    config.net.device, "255.255.255.255", "",
    config.net.bootp_timeout, 0, NULL, 0, 1, BP_PUT_ENV, 1
  );

  win_close(&win);

  if(rc || !getenv("BOOTP_IPADDR")) {
    if(config.win) {
      sprintf(tmp, txt_get(TXT_ERROR_DHCP), "BOOTP");
      dia_message(tmp, MSGTYPE_ERROR);
    }
    return -1;
  }

  name2inet(&config.net.hostname, getenv("BOOTP_IPADDR"));
  net_check_address(&config.net.hostname, 0);

  name2inet(&config.net.netmask, getenv("BOOTP_NETMASK"));
  net_check_address(&config.net.netmask, 0);

  name2inet(&config.net.broadcast, getenv("BOOTP_BROADCAST"));
  net_check_address(&config.net.broadcast, 0);

  name2inet(&config.net.network, getenv("BOOTP_NETWORK"));
  net_check_address(&config.net.network, 0);

  name2inet(&config.net.gateway, getenv("BOOTP_GATEWAYS"));
  name2inet(&config.net.gateway, getenv("BOOTP_GATEWAYS_1"));
  net_check_address(&config.net.gateway, 0);

  name2inet(&config.net.nameserver[0], getenv("BOOTP_DNSSRVS"));
  name2inet(&config.net.nameserver[0], getenv("BOOTP_DNSSRVS_1"));
  net_check_address(&config.net.nameserver[0], 0);

  s = getenv("BOOTP_HOSTNAME");
  if(s && !config.net.hostname.name) config.net.hostname.name = strdup(s);

  if((s = getenv("BOOTP_DOMAIN"))) {
    if(config.net.domain) free(config.net.domain);
    config.net.domain = strdup(s);
  }

  net_stop();

  return 0;
}


/*
 * Ask user for some network address.
 * (Used for netmasks, too.)
 *
 * Either numeric or dns resolved.
 *
 * return:
 *   0: ok
 *   1: empty input
 *   2: error or abort
 *
 * Note: expects window mode.
 */
int net_get_address(char *text, inet_t *inet, int do_dns)
{
  int err = 0;

  do {
    err = 0;

    if(dia_input2(text, &inet->name, 32, 0)) {
      err = 2;
      break;
    }
    if(!inet->name) {
      err = 1;
      break;
    }
    if(net_check_address(inet, do_dns)) err = 2;
    if(err) dia_message(txt_get(TXT_INVALID_INPUT), MSGTYPE_ERROR);
  } while(err);

  return err;
}


/*
 * Ask user for some network address.
 * (Used for netmasks, too.)
 *
 * Either numeric or dns resolved.
 *
 * Return:
 *      0: ok
 *   != 0: error or abort
 *
 * Note: expects window mode.
 */
int net_get_address2(char *text, inet_t *inet, int do_dns, char **user, char **password, unsigned *port)
{
  int err = 0;
  unsigned n_port = 0;
  char *input = NULL, *buf = NULL, *n_user = NULL, *n_password = NULL;
  url_t *url;

  str_copy(&input, inet->name);

  do {
    if((err = dia_input2(text, &input, 32, 0))) break;
    if(input) {
      if(
        (user || password || port) &&
        (
          (strchr(input, ':') && !config.net.ipv6) || 
          strchr(input, '@') ||
          (strchr(input, '[') && strchr(input, ']'))
        )
      ) {
        strprintf(&buf, "http://%s", input);
        url = url_set(buf);
        if(url->server) {
          name2inet(inet, url->server);
          n_port = url->port;
          str_copy(&n_user, url->user);
          str_copy(&n_password, url->password);
        }
        else {
          err = 1;
        }
        url_free(url);
      }
      else {
        name2inet(inet, input);
      }
      if(!err) err = net_check_address(inet, do_dns);
    }
    else {
      err = 1;
    }
    if(err) dia_message(txt_get(TXT_INVALID_INPUT), MSGTYPE_ERROR);
  } while(err);

  if(!err) {
    if(port) *port = n_port;
    if(user) str_copy(user, n_user);
    if(password) str_copy(password, n_password);
  }

  str_copy(&input, NULL);
  str_copy(&buf, NULL);
  str_copy(&n_user, NULL);
  str_copy(&n_password, NULL);

  return err;
}


/*
 * Start dhcp client and read dhcp info.
 *
 * Global vars changed:
 *  config.net.dhcp_active
 *  config.net.hostname
 *  config.net.netmask
 *  config.net.network
 *  config.net.broadcast
 *  config.net.gateway
 *  config.net.domain
 *  config.net.nisdomain
 *  config.net.nameserver
 */
int net_dhcp()
{
  unsigned active4, active = config.net.dhcp_active;

  if(config.net.ipv4) net_dhcp4();

  active4 = config.net.dhcp_active;
  config.net.dhcp_active = active;

  if(config.net.ipv6) net_dhcp6();
  config.net.dhcp_active |= active4;

  return config.net.dhcp_active ? 0 : 1;
}


/*
 * Start dhcp client and read dhcp info.
 *
 * Global vars changed:
 *  config.net.dhcp_active
 *  config.net.hostname
 *  config.net.netmask
 *  config.net.network
 *  config.net.broadcast
 *  config.net.gateway
 *  config.net.domain
 *  config.net.nisdomain
 *  config.net.nameserver
 */
int net_dhcp4()
{
  char cmd[256], file[256], *s;
  file_t *f0, *f;
  window_t win;
  int got_ip = 0, i, is_static = 0, rc;
  slist_t *sl0, *sl;

  if(config.net.dhcp_active || config.net.keep) return 0;

  if(config.test) {
    config.net.dhcp_active = 1;

    return 0;
  }

  if(config.win) {
    sprintf(cmd, txt_get(TXT_SEND_DHCP), "DHCP");
    dia_info(&win, cmd, MSGTYPE_INFO);
  }

  net_apply_ethtool(config.net.device, config.net.hwaddr);

  strcpy(cmd, "dhcpcd --noipv4ll");

  if(config.net.dhcpcd) {
    sprintf(cmd + strlen(cmd), " %s", config.net.dhcpcd);
  }

  sprintf(cmd + strlen(cmd), " -t %d", config.net.dhcp_timeout);

  sprintf(cmd + strlen(cmd), " %s", config.net.device);

  sprintf(file, "/var/lib/dhcpcd/dhcpcd-%s.info", config.net.device);

  unlink(file);

  system(cmd);

  f0 = file_read_file(file, kf_dhcp);

  for(f = f0; f; f = f->next) {
    switch(f->key) {
      case key_ipaddr:
        got_ip = 1;
        name2inet(&config.net.hostname, f->value);
        net_check_address(&config.net.hostname, 0);
        break;

      case key_hostname:
        str_copy(&config.net.realhostname, f->value);
        break;

      case key_netmask:
        name2inet(&config.net.netmask, f->value);
        net_check_address(&config.net.netmask, 0);
        break;

      case key_network:
        name2inet(&config.net.network, f->value);
        net_check_address(&config.net.network, 0);
        break;

      case key_broadcast:
        name2inet(&config.net.broadcast, f->value);
        net_check_address(&config.net.broadcast, 0);
        break;

      case key_gateway:
        if((s = strchr(f->value, ' '))) *s = 0;
        name2inet(&config.net.gateway, f->value);
        net_check_address(&config.net.gateway, 0);
        break;

      case key_domain:
        if(*f->value) str_copy(&config.net.domain, f->value);
        break;

#if 0
      case key_rootpath:
      case key_bootfile:
        break;
#endif

      case key_dns:
        for(config.net.nameservers = 0, sl = sl0 = slist_split(' ', f->value); sl; sl = sl->next) {
          name2inet(&config.net.nameserver[config.net.nameservers], sl->key);
          net_check_address(&config.net.nameserver[config.net.nameservers], 0);
          if(++config.net.nameservers >= sizeof config.net.nameserver / sizeof *config.net.nameserver) break;
        }
        slist_free(sl0);
        break;

      case key_nisdomain:
        if(*f->value) str_copy(&config.net.nisdomain, f->value);
        break;

      default:
        break;
    }
  }

  if(config.win) win_close(&win);

  if(got_ip) {
    config.net.dhcp_active = 1;
    if(config.net.ifup_wait) sleep(config.net.ifup_wait);
  }
  else {
    if(config.win) {
      if(config.net.dhcpfail && strcmp(config.net.dhcpfail, "ignore")) {
        if(!strcmp(config.net.dhcpfail, "show")) {
          sprintf(cmd, txt_get(TXT_ERROR_DHCP), "DHCP");
          dia_info(&win, cmd, MSGTYPE_ERROR);
          sleep(4);
          win_close(&win);
        }
        else if(!strcmp(config.net.dhcpfail, "manual")) {
          sprintf(cmd, txt_get(TXT_ERROR_DHCP), "DHCP");
          i = dia_yesno(cmd, NO);
          if(i == YES) {
            is_static = 1;
            // ?????
          }
        }
      }
    }
  }

  file_free_file(f0);

  rc = config.net.dhcp_active ? 0 : 1;

  return rc;
}


/*
 * Start dhcp client and read dhcp info.
 *
 * Global vars changed:
 *  config.net.dhcp_active
 *  config.net.hostname
 *  config.net.netmask
 *  config.net.network
 *  config.net.broadcast
 *  config.net.gateway
 *  config.net.domain
 *  config.net.nisdomain
 *  config.net.nameserver
 */
int net_dhcp6()
{
  char *cmd = NULL, *fname = NULL;
  window_t win;
  FILE *f;
  DIR *dir;
  struct dirent *de;
  int ok = 0;
  unsigned timeout = config.net.dhcp_timeout;
  char buf[256];

  if(config.net.dhcp_active || config.net.keep) return 0;

  if(config.test) {
    config.net.dhcp_active = 1;

    return 0;
  }

  if(config.win) {
    strprintf(&cmd, txt_get(TXT_SEND_DHCP), "DHCP6");
    dia_info(&win, cmd, MSGTYPE_INFO);
  }

  if(!config.net.ipv4) net_apply_ethtool(config.net.device, config.net.hwaddr);

  system("/bin/rm -f /var/lib/dhcpv6/client6.leases*");

  if((f = fopen("/etc/dhcp6c.conf", "w"))) {
    fprintf(f,
      "interface %s {\n  request domain-name-servers;\n  request domain-search-list;\n};\n",
      config.net.device
    );
    fclose(f);
  }

  unlink("/tmp/dhcp6c_update.done");

  strprintf(&cmd, "dhcp6c %s", config.net.device);

  system(cmd);

  // now wait a bit
  do {
    sleep(1);

    if((dir = opendir("/var/lib/dhcpv6"))) {
      while((de = readdir(dir))) {
        if(!strncmp(de->d_name, "client6.leases", sizeof "client6.leases" - 1)) {
          strprintf(&fname, "/var/lib/dhcpv6/%s", de->d_name);
          if((f = fopen(fname, "r"))) {
            if(fscanf(f, "lease %255s", buf) == 1) {
              name2inet(&config.net.hostname, buf);
              net_check_address(&config.net.hostname, 0);
              ok = 1;
            }
            fclose(f);
          }
        }
      }

      closedir(dir);
    }
  } while(!ok && --timeout);

  while(ok && timeout--) {
    sleep(1);

    if(util_check_exist("/tmp/dhcp6c_update.done")) break;
  } 

  if(ok) {
    config.net.dhcp_active = 1;
    sleep(config.net.ifup_wait + 10);
  }

  if(config.win) win_close(&win);

  if(!ok && config.win) {
    strprintf(&cmd, txt_get(TXT_ERROR_DHCP), "DHCP6");
    dia_message(cmd, MSGTYPE_ERROR);
  }

  free(fname);
  free(cmd);

  return config.net.dhcp_active ? 0 : 1;
}


/*
 * Stop dhcp client.
 *
 * Global vars changed:
 *  config.net.dhcp_active
 */
void net_dhcp_stop()
{
  if(!config.net.dhcp_active) return;

  /* kill them all */
  util_killall("dhcpcd", SIGHUP);
  util_killall("dhcp6c", SIGTERM);
  /* give them some time */
  sleep(2);

  config.net.dhcp_active = 0;
}


/*
 * Return current network config state as bitmask.
 */
unsigned net_config_mask()
{
  unsigned u = 0;

  if(config.net.hostname.name) u |= 1;
  if(config.net.netmask.ok || config.net.hostname.prefix4 || config.net.hostname.prefix6) u |= 2;
  if(config.net.gateway.ok) u |= 4;
  if(config.net.nameserver[0].ok) u |= 0x10;

  return u;
}


/*
 * Return module that handles a network interface.
 *
 * Returns NULL if unknown.
 *
 * net_if: interface
 */
char *net_if2module(char *net_if)
{
  slist_t *sl;

  for(sl = config.net.devices; sl; sl = sl->next ) {
    if(sl->key && !strcmp(sl->key, net_if)) return sl->value;
  }

  return NULL;
}


/*
 * Shut down single network interface.
 *
 * dev: interface
 */
void if_down(char *dev)
{
  int sock;
  struct ifreq iface = {};

  if(!dev || !*dev) return;

  fprintf(stderr, "if %s down\n", dev);

  sock = socket(AF_INET, SOCK_DGRAM, 0);
  if(sock == -1) return;

  strcpy(iface.ifr_name, dev);
  iface.ifr_addr.sa_family = AF_INET;

  ioctl(sock, SIOCGIFFLAGS, &iface);
  iface.ifr_flags &= ~(IFF_UP | IFF_RUNNING);
  ioctl(sock, SIOCSIFFLAGS, &iface);
  close(sock);
}

#if defined(__s390__) || defined(__s390x__)

/* turn on display and execute if expr is false */
#define IFNOTAUTO(expr) if(!(expr) && !config.win) { \
                          util_disp_init(); \
                        } \
                        if(config.manual || !(expr))

#include <dirent.h>

int net_check_ccw_address(char* addr)
{
  int i;
  fprintf(stderr, "checking CCW address %s\n",addr);
  /* format: x.x.xxxx, each x is a hex digit */
  if(strlen(addr)!=8) goto error;
  for(i=0;i<8;i++)
  {
    if(i==1 || i==3)
    {
     if(addr[i] != '.') goto error;
    }
    else
    {
     addr[i] = tolower(addr[i]);
     if((addr[i] < 'a' || addr[i] > 'f') && (addr[i] < '0' || addr[i] > '9')) goto error;
    }
  }

  return 0;

error:
  if(!config.win) util_disp_init();
  dia_message(txt_get(TXT_INVALID_CCW_ADDRESS), MSGTYPE_ERROR);
  return -1;
}

/* ask user for read and write channels */
static int net_s390_getrwchans_ex(hd_t* hd)
{
  int rc;

  if(hd && config.hwp.readchan == 0) {
    int lcss = hd->detail->ccw.data->lcss;
    int ccw = -1;
    hd_res_t* r;
    for(r = hd->res; r; r = r->next) {
      if(r->any.type == res_io) {
        ccw = (int) r->io.base;
      }
    }
    if(ccw != -1) {
      strprintf(&config.hwp.readchan, "%1x.%1x.%04x", lcss >> 8, lcss & 0xf, ccw);
      if(!config.hwp.writechan)
        strprintf(&config.hwp.writechan, "%1x.%1x.%04x", lcss >> 8, lcss & 0xf, ccw + 1);
      if(!config.hwp.datachan)
        strprintf(&config.hwp.datachan, "%1x.%1x.%04x", lcss >> 8, lcss & 0xf, ccw + 2);
    }
  }

  IFNOTAUTO(config.hwp.readchan) if((rc=dia_input2_chopspace(txt_get(TXT_CTC_CHANNEL_READ), &config.hwp.readchan, 9, 0))) return rc;
  if((rc=net_check_ccw_address(config.hwp.readchan))) return rc;
  IFNOTAUTO(config.hwp.writechan) if((rc=dia_input2_chopspace(txt_get(TXT_CTC_CHANNEL_WRITE), &config.hwp.writechan, 9, 0))) return rc;
  if((rc=net_check_ccw_address(config.hwp.writechan))) return rc;
  return 0;
}

#if 0 /* currently unused */
static int net_s390_getrwchans()
{
  return net_s390_getrwchans_ex(NULL);
}
#endif

int net_activate_s390_devs(void)
{
  return net_activate_s390_devs_ex(NULL, NULL);
}

/* find the network device name for a CCW group */
int net_s390_get_ifname(char* channel, char** device)
{
  char path[100];
  DIR* d;
  struct dirent* e;
  sprintf(path, "/sys/bus/ccwgroup/devices/%s/net", channel);
  d = opendir(path);
  if(!d) return -1;
  while((e = readdir(d))) {
    if(e->d_name[0] == '.') continue;
    fprintf(stderr, "ccwgroup %s has network IF %s\n", channel, e->d_name);
    strprintf(device, e->d_name);
    closedir(d);
    return 0;
  }
  closedir(d);
  fprintf(stderr, "no network IF found for channel group %s\n", channel);
  return -1;
}

int net_activate_s390_devs_ex(hd_t* hd, char** device)
{
  int rc, i;
  char buf[100];
  char hwcfg_name[40];
  char* chans[3] = { config.hwp.readchan, config.hwp.writechan, config.hwp.datachan };
  char chanlist[27];

  if(hd) switch(hd->sub_class.id) {
  case 0x89:	/* OSA2 */
    config.hwp.type = di_390net_osa;
    config.hwp.interface = di_osa_lcs;
    break;
  case 0x86:	/* OSA Express */
    config.hwp.type = di_390net_osa;
    config.hwp.interface = di_osa_qdio;
    break;
  case 0x90:	/* IUCV */
    config.hwp.type = di_390net_iucv;
    break;
  case 0x87:	/* HSI */
    config.hwp.type = di_390net_hsi;
    config.hwp.interface = di_osa_qdio;
    config.hwp.medium = di_osa_eth;
    break;
  case 0x88:	/* CTC */
    config.hwp.type = di_390net_ctc;
    break;
  case 0x8f:	/* ESCON */
    config.hwp.type = di_390net_escon;
    break;
  default:
    return -1;
  } else {	/* no hd_t entry -> ask */
    dia_item_t di;
    dia_item_t items[] = {
      di_390net_osa,
      di_390net_hsi,
      di_390net_sep,
      di_390net_ctc,
      di_390net_escon,
      di_390net_iucv,
      di_none
    };
  
    IFNOTAUTO(config.hwp.type) {
      di = dia_menu2(txt_get(TXT_CHOOSE_390NET), 60, 0, items, config.hwp.type?:di_390net_iucv);
      config.hwp.type = di;
    } else di = config.hwp.type;
  }
       
  
  /* hwcfg parms common to all devices */
  config.hwp.startmode="auto";
  config.hwp.module_options="";
  config.hwp.module_unload="yes";
  
  switch(config.hwp.type)
  {
  case di_390net_iucv:
    IFNOTAUTO(config.hwp.userid)
      if((rc=dia_input2_chopspace(txt_get(TXT_IUCV_PEER), &config.hwp.userid,20,0))) return rc;

    if(mod_modprobe("netiucv",NULL)) {
      dia_message("failed to load netiucv module",MSGTYPE_ERROR);
      return -1;
    }

    break;

  case di_390net_ctc:
  case di_390net_escon:
    if(mod_modprobe("ctcm",NULL)) {
      dia_message("failed to load ctcm module",MSGTYPE_ERROR);
      return -1;
    }

    if((rc=net_s390_getrwchans_ex(hd))) return rc;
    
    /* ask for CTC protocol */
    dia_item_t protocols[] = {
      di_ctc_compat,
      di_ctc_ext,
      di_ctc_zos390,
      di_none
    };
    if(config.hwp.protocol)
      switch(config.hwp.protocol)
      {
      case 0+1:	rc=di_ctc_compat; break;
      case 1+1: rc=di_ctc_ext; break;
      case 3+1: rc=di_ctc_zos390; break;
      default: return -1;
      }
    else rc=0;    
    IFNOTAUTO(config.hwp.protocol)
    {
      rc=dia_menu2(txt_get(TXT_CHOOSE_CTC_PROTOCOL), 50, 0, protocols, rc);
      switch(rc)
      {
      case di_ctc_compat: config.hwp.protocol=0+1; break;
      case di_ctc_ext: config.hwp.protocol=1+1; break;
      case di_ctc_zos390: config.hwp.protocol=3+1; break;
      default: return -1;
      }
    }

    break;
    
  case di_390net_osa:
  case di_390net_hsi:
    if(config.hwp.type == di_390net_hsi)
    {
      config.hwp.interface=di_osa_qdio;
      config.hwp.medium=di_osa_eth;
    }
    else
    {
      if(!hd) {	/* if we have an hd_t entry, we know already */
        /* ask for LCS/QDIO */
        dia_item_t interfaces[] = {
          di_osa_qdio,
          di_osa_lcs,
          di_none
        };

        IFNOTAUTO(config.hwp.interface)
        {
          rc = dia_menu2(txt_get(TXT_CHOOSE_OSA_INTERFACE), 33, 0, interfaces, config.hwp.interface?:di_osa_qdio);
          if(rc == -1) return rc;
          config.hwp.interface=rc;
        }
        else
          rc=config.hwp.interface;
      }

      /* ask for TR/ETH */

      dia_item_t media[] = {
        di_osa_eth,
        di_osa_tr,
        di_none
      };

      IFNOTAUTO(config.hwp.medium)
      {
        rc = dia_menu2(txt_get(TXT_CHOOSE_OSA_MEDIUM), 33, 0, media, config.hwp.medium?:di_osa_eth);
        if(rc == -1) return rc;
        config.hwp.medium=rc;
      }
      else
        rc=config.hwp.medium;
        
      if(!hd || hd->is.dualport)
      {
        IFNOTAUTO(config.hwp.portno)
        {
          char* port = NULL;
          if((rc=dia_input2_chopspace(txt_get(TXT_OSA_PORTNO), &port,2,0))) return rc;
          if(port) config.hwp.portno = atoi(port) + 1;
          else config.hwp.portno = 0 + 1;
        }
      }
    }

    if(config.hwp.interface == di_osa_qdio)
    {
      if(mod_modprobe("qeth",NULL)) {
        dia_message("failed to load qeth module",MSGTYPE_ERROR);
        return -1;
      }

      if((rc=net_s390_getrwchans_ex(hd))) return rc;
      IFNOTAUTO(config.hwp.datachan)
        if((rc=dia_input2_chopspace(txt_get(TXT_CTC_CHANNEL_DATA), &config.hwp.datachan, 9, 0))) return rc;
      if((rc=net_check_ccw_address(config.hwp.datachan))) return rc;

      if (config.hwp.type != di_390net_hsi) {
	  IFNOTAUTO(config.hwp.portname)
	  {
	      if((rc=dia_input2_chopspace(txt_get(TXT_QETH_PORTNAME), &config.hwp.portname,9,0))) return rc;
	      // FIXME: warn about problems related to empty portnames
	  }
      }
      
      if(config.hwp.medium == di_osa_eth)
      {
        IFNOTAUTO(config.hwp.layer2)
        {
          config.hwp.layer2 = dia_yesno(txt_get(TXT_ENABLE_LAYER2), YES) == YES ? 2 : 1;
        }
        if(config.hwp.layer2 == 2) {
          IFNOTAUTO(config.hwp.osahwaddr) {
            dia_input2(txt_get(TXT_HWADDR), &config.hwp.osahwaddr, 17, 1);
          }
        }
      }
      
    }
    else	/* LCS */
    {
      if(mod_modprobe("lcs",NULL)) {
        dia_message("failed to load lcs module", MSGTYPE_ERROR);
        return -1;
      }
      
      if((rc=net_s390_getrwchans_ex(hd))) return rc;
      
      IFNOTAUTO(config.hwp.portname)
        if((rc=dia_input2_chopspace(txt_get(TXT_OSA_PORTNO), &config.hwp.portname,9,0))) return rc;

    }
    
    break;
    
  default:
    return -1;
  }
  
  /* some devices need a little time to come up;
     this is only a problem in auto-install mode, because
     otherwise querying the user for input delays things
     enough for the device to settle; see bnc#473749 */
  if (!config.manual) sleep(1);
  
  char cmd[256];
  char* ccmd = cmd;
  switch(config.hwp.type) {
    case di_390net_iucv:
      /* add netiucv to MODULES_LOADED_ON_BOOT */
      /* is this copied by YaST already? */
      if(mkdir("/etc/sysconfig", (mode_t)0755) && errno != EEXIST)
        return -1;
      FILE* fpk = fopen("/etc/sysconfig/kernel", "a");
      if(!fpk) return -1;
      fprintf(fpk, "MODULES_LOADED_ON_BOOT=\"$MODULES_LOADED_ON_BOOT netiucv\"\n");
      fclose(fpk);
      sprintf(cmd, "iucv_configure %s 1", config.hwp.userid);
      break;
    case di_390net_ctc:
    case di_390net_escon:
setup_ctc:
      if(config.hwp.protocol > 0)
        sprintf(cmd, "ctc_configure %s %s 1 %d", config.hwp.readchan, config.hwp.writechan, config.hwp.protocol - 1);
      else
        sprintf(cmd, "ctc_configure %s %s 1", config.hwp.readchan, config.hwp.writechan);
      break;
    case di_390net_hsi:
    case di_390net_osa:
      if (config.hwp.interface == di_390net_lcs)
        goto setup_ctc;
      ccmd += sprintf(ccmd, "qeth_configure ");
      if(config.hwp.portno)
        ccmd += sprintf(ccmd, "-n %d ", config.hwp.portno - 1);
      ccmd += sprintf(ccmd, "%s %s %s %s %s %s 1",
        config.hwp.portname ? "-p" : "",
        config.hwp.portname ? config.hwp.portname : "",
        config.hwp.layer2 == 2 ? "-l" : "",
        config.hwp.readchan,
        config.hwp.writechan,
        config.hwp.datachan);
      break;
    default:
      sprintf(cmd, "unknown s390 network type %d", config.hwp.type);
      dia_message(cmd, MSGTYPE_ERROR);
      return -1;
      break;
  }
  rc = system(cmd);
  if(rc) {
    sprintf(cmd, "network configuration script failed (error code %d)", rc);
    dia_message(cmd, MSGTYPE_ERROR);
    return -1;
  }
  
  if(config.hwp.osahwaddr && strlen(config.hwp.osahwaddr) > 0) {
    struct ifreq ifr;
    struct ether_addr* ea;
    int skfd;
    DIR* d;
    struct dirent* de;
    char* ifname = NULL;
    
    net_s390_get_ifname(config.hwp.readchan, &ifname);
    strcpy(ifr.ifr_name, ifname);
    free(ifname);
    
    if((skfd = socket(PF_INET, SOCK_DGRAM, IPPROTO_IP)) < 0) {
      perror("socket");
      return 1;
    }
    
    /* convert MAC address to binary */
    if((ea = ether_aton(config.hwp.osahwaddr)) == NULL) {
      fprintf(stderr,"MAC address invalid: %s\n", config.hwp.osahwaddr);
      return 1;
    }
    memcpy((void*) &ifr.ifr_hwaddr.sa_data[0], ea, ETHER_ADDR_LEN);
    
    //ifr.ifr_hwaddr.sa_len = ETHER_ADDR_LEN
    ifr.ifr_hwaddr.sa_family = AF_UNIX;
    
    if(ioctl(skfd, SIOCSIFHWADDR, &ifr) < 0) {
      fprintf(stderr, "SIOCSIFHWADDR: %s\n", strerror(errno));
      close(skfd);
      return 1;
    }
    close(skfd);
  }
  
  if (device) {
    if (config.hwp.type == di_390net_iucv)
      strprintf(device, "iucv0"); /* bold assumption */
    else
      net_s390_get_ifname(config.hwp.readchan, device);
  }

  return 0;
}
#endif


/*
 * Run ethtool for matching devices.
 */
void net_apply_ethtool(char *device, char *hwaddr)
{
  slist_t *sl;
  char *s = NULL;

  for(sl = config.ethtool; sl; sl = sl->next) {
    if(
      (device && !fnmatch(sl->key, device, 0)) ||
      (hwaddr && !fnmatch(sl->key, hwaddr, 0))
    ) {
      if(s) {
        strprintf(&s, "%s %s", s, sl->value);
      }
      else {
        str_copy(&s, sl->value);
      }
    }
  }

  if(s) {
    str_copy(&config.net.ethtool_used, s);
    strprintf(&s, "ethtool -s %s %s", device, s);
    fprintf(stderr, "%s\n", s);
    system(s);
    free(s);
  }
}


/*
 * 0: ok, 1: failed
 */
int wlan_setup()
{
  int win_old = config.win;
  dia_item_t di;
  dia_item_t items[] = {
    di_wlan_open,
    di_wlan_wep_o,
    di_wlan_wep_r,
    di_wlan_wpa,
    di_none
  };

  switch(config.net.wlan.auth) {
    case wa_open:
      di = di_wlan_open;
      break;

    case wa_wep_open:
      di = di_wlan_wep_o;
      break;

    case wa_wep_restricted:
      di = di_wlan_wep_r;
      break;

    case wa_wpa:
      di = di_wlan_wpa;
      break;

    default:
      di = di_none;
  }

  if(config.manual || di == di_none) {
    di = dia_menu2("WLAN Authentication", 40, wlan_auth_cb, items, di_wlan_auth_last);
  }
  else {
    if(wlan_auth_cb(di)) di = di_none;
  }

  if(config.win && !win_old) util_disp_done();

  return di == di_none ? 1 : 0;
}


/*
 * return values:
 * -1    : abort (aka ESC)
 *  0    : ok
 *  other: stay in menu
 */
int wlan_auth_cb(dia_item_t di)
{
  int rc = 1, i, j;
  char *buf = NULL, *key = NULL, *s;
  int wep_mode = 0;
  static char *wep_key_items[] = {
    "ASCII", "HEX", "Passphrase - 40 bit", "Passphrase - 104 bit",
    NULL
  };
  static char *wpa_key_items[] = {
    "HEX", "Passphrase",
    NULL
  };
  FILE *f;

  di_wlan_auth_last = di;

  util_killall("wpa_supplicant", 15);
  usleep(100000);
  util_killall("wpa_supplicant", 9);

  switch(di) {
    case di_wlan_open:
      config.net.wlan.auth = wa_open;

      if(config.manual || !config.net.wlan.essid) {
        if(dia_input2("ESSID", &config.net.wlan.essid, 30, 0)) {
         rc = -1;
          break;
        }
      }

      strprintf(&buf, "iwconfig %s essid %s'%s'",
        config.net.device,
        config.net.wlan.essid ? "-- " : "",
        config.net.wlan.essid ?: "any"
      );
      fprintf(stderr, "%s\n", buf);
      system_log(buf);

      strprintf(&buf, "iwconfig %s key off", config.net.device);
      fprintf(stderr, "%s\n", buf);
      system_log(buf);

      rc = 0;
      break;

    case di_wlan_wep_o:
      wep_mode = 1;

    case di_wlan_wep_r:
      config.net.wlan.auth = wep_mode ? wa_wep_open : wa_wep_restricted;

      if(config.manual || !config.net.wlan.essid) {
        if(dia_input2("ESSID", &config.net.wlan.essid, 30, 0)) {
          rc = -1;
         break;
        }
      }

      if(config.manual || !config.net.wlan.key) {
        i = dia_list("WEP Key Type", 30, NULL, wep_key_items, config.net.wlan.key_type + 1, align_left) - 1;

        if(i < 0) {
          rc = -1;
          break;
        }

        switch(i) {
          case 0:
            config.net.wlan.key_type = kt_ascii;
            config.net.wlan.key_len = 0;
            break;

          case 1:
            config.net.wlan.key_type = kt_hex;
            config.net.wlan.key_len = 0;
            break;

          case 2:
            config.net.wlan.key_type = kt_pass;
            config.net.wlan.key_len = 40;
            break;

          case 3:
            config.net.wlan.key_type = kt_pass;
            config.net.wlan.key_len = 104;
            break;
        }

        if(dia_input2("WEP Key", &config.net.wlan.key, 30, 0) || !config.net.wlan.key) {
          rc = -1;
          break;
        }
      }

      switch(config.net.wlan.key_type) {
        case kt_ascii:
          strprintf(&key, "s:%s", config.net.wlan.key);
          break;

        case kt_hex:
          str_copy(&key, config.net.wlan.key);
          break;

        case kt_pass:
          strprintf(&buf, "lwepgen%s '%s'",
            config.net.wlan.key_len == 104 ? " -s" : "",
            config.net.wlan.key
          );
          f = popen(buf, "r");
          if(f) {
            fgets(key = calloc(1, 256), 256, f);
            if((s = strchr(key, '\n'))) *s = 0;
            pclose(f);
            if(!*key) rc = -1;
          }
          else {
            rc = -1;
          }
          break;

        default:
          rc = -1;
          break;
      }

      if(rc == -1) break;

      strprintf(&buf, "iwconfig %s essid %s'%s'",
        config.net.device,
        config.net.wlan.essid ? "-- " : "",
        config.net.wlan.essid ?: "any"
      );
      fprintf(stderr, "%s\n", buf);
      system_log(buf);

      strprintf(&buf, "iwconfig %s key %s '%s'",
        config.net.device,
        config.net.wlan.auth == wa_wep_open ? "open" : "restricted",
        key
      );
      fprintf(stderr, "%s\n", buf);
      system_log(buf);

      rc = 0;
      break;

    case di_wlan_wpa:
      if(config.manual || !config.net.wlan.essid) {
        if(dia_input2("ESSID", &config.net.wlan.essid, 30, 0)) {
          rc = -1;
          break;
        }
      }

      if(config.manual || !config.net.wlan.key) {
        j = config.net.wlan.key_type == kt_pass ? 2 : 1;

        i = dia_list("WPA Key Type", 30, NULL, wpa_key_items, j, align_left);

        if(i < 1) {
          rc = -1;
          break;
        }

        config.net.wlan.key_type = i == 1 ? kt_hex : kt_pass;
        config.net.wlan.key_len = 0;

        if(dia_input2("WPA Key", &config.net.wlan.key, 30, 0) || !config.net.wlan.key) {
          rc = -1;
          break;
        }

        if(config.net.wlan.key_type == kt_pass && strlen(config.net.wlan.key) < 8) {
          dia_message(txt_get(TXT_VNC_PASSWORD_TOO_SHORT), MSGTYPE_ERROR);
          rc = -1;
          break;
        }
      }

      if(config.net.wlan.key_type == kt_pass) {
        strprintf(&key, "\"%s\"", config.net.wlan.key);
      }
      else {
        str_copy(&key, config.net.wlan.key);
      }

      f = fopen("/tmp/wpa_supplicant.conf", "w");
      if(!f) {
        rc = -1;
        break;
      }
      fprintf(f,
        "ap_scan=1\n"
        "network={\n"
        "  key_mgmt=WPA-PSK\n"
        "  scan_ssid=1\n"
        "  ssid=\"%s\"\n"
        "  psk=%s\n"
        "}\n",
        config.net.wlan.essid,
        key
      );
      fclose(f);

      strprintf(&buf, "wpa_supplicant -B -Dwext -i%s -c/tmp/wpa_supplicant.conf",
        config.net.device
      );
      fprintf(stderr, "%s\n", buf);
      system_log(buf);

      rc = 0;
      break;

    default:
      break;
  }

  free(buf);
  free(key);

  return rc;
}


