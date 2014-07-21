/*
 *
 * net.c         Network related functions
 *
 * Copyright (c) 1996-2002  Hubert Mantel, SuSE Linux AG  (mantel@suse.de)
 *
 */

#define _GNU_SOURCE 1
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
#include <sys/wait.h>

#include <hd.h>

#include "global.h"
#include "dialog.h"
#include "window.h"
#include "util.h"
#include "net.h"
#include "file.h"
#include "module.h"
#include "url.h"
#include "auto2.h"


#if defined(__s390__) || defined(__s390x__)
int net_activate_s390_devs_ex(hd_t* hd, char** device);
#endif

static int net_choose_device(void);
static int net_input_data(void);

static int wlan_auth_cb(dia_item_t di);

static dia_item_t di_wlan_auth_last = di_none;
static int parse_leaseinfo(char *file);
static void net_wicked_dhcp(void);

static void net_cifs_build_options(char **options, char *user, char *password, char *workgroup);
static void net_ask_domain(void);
static int ifcfg_write2(char *device, ifcfg_t *ifcfg, int initial);
static int ifcfg_write(char *device, ifcfg_t *ifcfg);
static char *inet2str(inet_t *inet, int type);


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
      if(dia_input2("Enter your VNC password.", &config.net.vncpassword, 20, 1)) break;
      if(config.net.vncpassword && strlen(config.net.vncpassword) >= 8) break;
      dia_message("Password is too short (must have at least 8 characters).", MSGTYPE_ERROR);
    }
  }

  if(config.usessh && !config.net.sshpassword) {
    if(!config.win) util_disp_init();
    dia_input2("Enter your temporary SSH password.", &config.net.sshpassword, 20, 1);
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

    dia_input2("Enter your search domains, separated by a space:", &tmp, 40, 0);  
    if(!tmp) {
      str_copy(&config.net.domain, NULL);
      return;
    }

    sl0 = slist_split(' ', tmp);

    for(sl = sl0; sl; sl = sl->next) {
      if(++ndomains > 6) {
        dia_message("Only up to six search domains are allowed.", MSGTYPE_ERROR);
        break;
      }
      str_copy(&ip.name, sl->key);
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
 * Calls either net_dhcp() or net_static() to setup the interface.
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
  char buf[256];

  if(!config.win && !config.manual) return 0;

  // FIXME: not really here
  net_ask_password();

  if(
    config.win &&
    config.ifcfg.if_up &&
    (!config.manual || dia_yesno("Your network is already configured. Keep this configuration?", YES) != NO)
  ) {
    return 0;
  }

  if(net_choose_device()) return -1;

  net_stop();

  config.net.configured = nc_none;

  if(config.win && config.net.setup != NS_DHCP) {
    if(
      config.net.setup & NS_DHCP &&
#if defined(__s390__) || defined(__s390x__)
      config.hwp.layer2 - 1 &&
#endif
      !config.net.ptp
    ) {
      sprintf(buf, "Automatic configuration via %s?", "DHCP");
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
    rc = net_dhcp();
    if(!rc) config.net.configured = nc_dhcp;
  }
  else {
    rc = net_input_data();
    if(!rc) {
      net_static();
      config.net.configured = nc_static;
    }
  }

  if(rc) return -1;

  // dia_message("An error occurred during the network configuration. Your network card probably was not recognized by the kernel.", MSGTYPE_ERROR);

  return rc;
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
  char *device = config.net.device;

  if(config.ifcfg.current) device = config.ifcfg.current;

  if(config.debug) fprintf(stderr, "%s: network down\n", device);

  if(config.test) {
    config.net.is_configured = nc_none;
    return;
  }

  net_wicked_down(device);
  config.net.is_configured = nc_none;

  // delete current config
  if(config.ifcfg.current) {
    char *buf = NULL;
    strprintf(&buf, "/etc/sysconfig/network/ifcfg-%s", config.ifcfg.current);
    unlink(buf);
    strprintf(&buf, "/etc/sysconfig/network/ifroute-%s", config.ifcfg.current);
    unlink(buf);
    str_copy(&buf, NULL);

    str_copy(&config.ifcfg.current, NULL);
  }
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
int net_static()
{
  // int err4 = 1, err6 = 1;
  char *s;

  /* make sure we get the interface name if a mac was passed */
  if((s = mac_to_interface(config.net.device, NULL))) {
    free(config.net.device);
    config.net.device = s;
  }

  // net_setup_nameserver

  config.ifcfg.manual->dhcp = 0;
  ifcfg_write2(config.net.device, config.ifcfg.manual, 0);

  return 0;
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
void net_cifs_build_options(char **options, char *user, char *password, char *workgroup)
{
  if(!options) return;

  str_copy(options, "ro");

  if(user) {
    strprintf(options, "%s,username=%s,password=%s", *options, user, password ?: "");
    if(workgroup) {
      strprintf(options, "%s,workgroup=%s", *options, workgroup);
    }
  } else {
    strprintf(options, "%s,guest", *options);
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
int net_mount_cifs(char *mountpoint, inet_t *server, char *share, char *user, char *password, char *workgroup, char *options)
{
  char *cmd = NULL;
  char *real_options = NULL;
  int err;

  if(!config.net.cifs.binary || !server->name) return -2;

  mod_modprobe(config.net.cifs.module, NULL);

  if(!share) share = "";
  if(!mountpoint || !*mountpoint) mountpoint = "/";

  net_cifs_build_options(&real_options, user, password, workgroup);

  if(options) {
    if(*options == '-') {
      str_copy(&real_options, options + 1);
    }
    else {
      strprintf(&real_options, "%s,%s", real_options, options);
    }
  }

  strprintf(&cmd, "%s '//%s/%s' '%s' -o '%s' >&2", config.net.cifs.binary, server->name, share, mountpoint, real_options);

  fprintf(stderr, "%s\n", cmd);

  err = system(cmd);

  str_copy(&cmd, NULL);
  str_copy(&real_options, NULL);

  return err ? -1 : 0;
}


/*
 * Mount NFS volume.
 *
 * mountpoint: mount point
 * server: server address
 * hostdir: directory on server
 * options: NFS mount options
 *
 * config.net.nfs: nfs options if options == NULL
 *
 * options are added to any options linuxrc uses unless it is prefixed with '-'.
 *
 * return:
 *      0: ok
 *   != 0: error code
 *
 */
int net_mount_nfs(char *mountpoint, inet_t *server, char *hostdir, unsigned port, char *options)
{
  char *path = NULL;
  char *real_options = NULL;
  pid_t mount_pid;

  if(!server->name) return -2;

  if(!hostdir) hostdir = "/";
  if(!mountpoint || !*mountpoint) mountpoint = "/";

  mount_pid = fork();
  if(mount_pid < 0) {
    perror("fork");

    return mount_pid;
  }
  else if(mount_pid > 0) {
    int err;
    pid_t pid;
    while((pid = waitpid(-1, &err, 0)) && pid != mount_pid);

    return WEXITSTATUS(err);
  }

  if(strchr(server->name, ':')) {
    strprintf(&path, "[%s]:%s", server->name, hostdir);
  }
  else {
    strprintf(&path, "%s:%s", server->name, hostdir);
  }

  str_copy(&real_options, "nolock");

  if(!options) options = config.net.nfs.opts;

  if(options) {
    if(*options == '-') {
      str_copy(&real_options, options + 1);
    }
    else {
      strprintf(&real_options, "%s,%s", real_options, options);
    }
  }

  if(config.debug) fprintf(stderr, "mount -o '%s' '%s' '%s'\n", real_options, path, mountpoint);

  char *args[6] = { "mount", "-o", real_options, path, mountpoint /*, NULL */ };

  signal(SIGUSR1, SIG_IGN);
  execvp("mount", args);
  perror("execvp(\"mount\")");
  exit(EXIT_FAILURE);
}


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
    char *name;
  } net_dev[] = {
    { "eth",   "ethernet network card"  },
    { "veth",  "ethernet network card"  },
    { "plip",  "parallel port"  },
    { "arc",   "arcnet network card"  },
    { "fddi",  "FDDI network card"  },
    { "hip",   "HIPPI network card" },
    { "ctc",   "channel to channel connection"   },
    { "escon", "ESCON connection" },
    { "ci",    "channel attached cisco router"  },
    { "iucv",  "IUCV connection"  },
    { "hsi",   "Hipersocket"   }
  };
  hd_data_t *hd_data = calloc(1, sizeof *hd_data);
  hd_t *hd, *hd_cards;
  hd_t **item_hds = NULL;
    
  if(config.netdevice) {
    str_copy(&config.net.device, config.netdevice);

    return 0;
  }

  if(config.manual == 1 && !net_drivers_loaded) {
    dia_info(&win, "Detecting and loading network drivers", MSGTYPE_INFO);
    load_network_mods();
    win_close(&win);
    net_drivers_loaded = 1;
  }

  /* The IUCV driver is special. There's no way for anything to detect
     IUCV is available for use unless the driver is already loaded. So,
     if we're running on z/VM we always load it, no matter what.       */
  #if defined(__s390__) || defined(__s390x__)
  if(!strncmp(config.hwp.hypervisor, "z/VM", sizeof "z/VM" - 1 )) {
     dia_info(&win, "We are running on z/VM", MSGTYPE_INFO);
     dia_info(&win, "Loading the IUCV network driver", MSGTYPE_INFO);
     mod_modprobe("netiucv","");
  }
  #endif

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
          strprintf(&buf, "%-6s : %s", f->key_str, net_dev[i].name);
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
        strprintf(items + item_cnt++, "Enter network device parameters manually");
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
    dia_message("No network device found.\n"
                "Load a network module first.",
                MSGTYPE_ERROR);
    choice = -1;
  } else if(item_cnt == 1) {
    choice = 1;
  }
  else {
    choice = dia_list("Choose the network device.", 50, NULL, items, last_item, align_left);
    if(choice) last_item = choice;
  }

/*
 *
 * If item_devs[choice - 1] has a value, then that means the interface should already
 * configured. If so, we can check sysfs to see if we should be using layer2 or not.
 *
 */
  if(choice > 0 && !item_devs[choice - 1]) {
#if defined(__s390__) || defined(__s390x__)
    fprintf(stderr, "activate s390 devs 2\n");
    net_activate_s390_devs_ex(item_hds[choice - 1], &item_devs[choice - 1]);
    if(!item_devs[choice - 1]) {
#endif
      dia_message("No network device found.\n"
                  "Load a network module first.",
                  MSGTYPE_ERROR);
      choice = -1;
#if defined(__s390__) || defined(__s390x__)
    }
#endif
  }
#if defined(__s390__) || defined(__s390x__)
  else {
    if(choice > 0) {
      char path[PATH_MAX]="";
      char *type;
      sprintf(path, "/sys/class/net/%s/device/layer2", item_devs[choice - 1]);
      type = util_get_attr(path);
      if(!strncmp(type, "1", sizeof "1" )) {config.hwp.layer2=2; }
      else {config.hwp.layer2=1;}
    }
  }
#endif

  if(choice > 0) {
    str_copy(&config.net.device, item_devs[choice - 1]);

    check_ptp(NULL);

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


#if 0
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
        }
        else {
           sprintf(buf, "Enter the IP of name server %u or press ESC.", u + 1);
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

  /* write resolv.conf for a static network setup */
  if(
    !config.net.dhcp_active &&
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
#endif


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

  if(config.net.ptp) {
    if(!config.net.ptphost.name) {
      name2inet(&config.net.ptphost, config.net.hostname.name);
    }

    if(net_get_address("Enter the IP address of the PLIP partner.", &config.net.ptphost, 1)) return -1;

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
        if(net_get_address("Enter your netmask. For a normal class C network, this is usually 255.255.255.0.", &config.net.netmask, 0)) return -1;
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
    if(err) dia_message("Invalid input.", MSGTYPE_ERROR);
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
    if(err) dia_message("Invalid input.", MSGTYPE_ERROR);
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
 * Parse dhcp leaseinfo and set network config vars accordingly.
 * Returns 1 to indicate that valid data were found.
 */
int parse_leaseinfo(char *file)
{
  file_t *f0, *f;
  char *s;
  slist_t *sl0, *sl;
  int got_ip = 0;

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

  file_free_file(f0);

  return got_ip;
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
  char *s;

  /* make sure we get the interface name if a mac was passed */
  if((s = mac_to_interface(config.net.device, NULL))) {
    free(config.net.device);
    config.net.device = s;
  }

  net_wicked_dhcp();

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
void net_wicked_dhcp()
{
  char file[256], *buf = NULL;
  window_t win;
  int got_ip = 0;
  ifcfg_t *ifcfg = NULL;

  if(config.test) {
    config.net.dhcp_active = 1;

    return;
  }

  strprintf(&buf, "Sending DHCP%s request to %s...", net_dhcp_type(), config.net.device);
  fprintf(stderr, "%s\n", buf);
  if(config.win) {
    dia_info(&win, buf, MSGTYPE_INFO);
  }
  else {
    printf("%s\n", buf);
    fflush(stdout);
  }
  str_copy(&buf, NULL);

  ifcfg = calloc(1, sizeof *ifcfg);
  ifcfg->dhcp = 1;

  ifcfg_write2(config.net.device, ifcfg, 0);

  free(ifcfg->type);
  free(ifcfg);
  ifcfg = NULL;

  net_apply_ethtool(config.net.device, config.net.hwaddr);

  net_wicked_up(config.net.device);

  if(config.net.ipv4) {
    snprintf(file, sizeof file, "/run/wicked/leaseinfo.%s.dhcp.ipv4", config.net.device);
    parse_leaseinfo(file);
  }

  if(!got_ip && config.net.ipv6) {
    snprintf(file, sizeof file, "/run/wicked/leaseinfo.%s.dhcp.ipv6", config.net.device);
    parse_leaseinfo(file);
  }

  if(slist_getentry(config.ifcfg.if_up, config.net.device)) got_ip = 1;

  if(config.win) win_close(&win);

  if(got_ip) {
    config.net.dhcp_active = 1;
    if(config.net.ifup_wait) sleep(config.net.ifup_wait);
  }
  else {
    if(config.win && config.net.dhcpfail && !strcmp(config.net.dhcpfail, "show")) {
      dia_info(&win, "DHCP configuration failed.", MSGTYPE_ERROR);
      sleep(4);
      win_close(&win);
    }
  }

  if(config.net.dhcp_active) {
    char *s;

    str_copy(&buf, "ok");

    if((s = inet2str(&config.net.hostname, 4))) {
      strprintf(&buf, "%s, ip = %s/%u", buf, s, config.net.hostname.prefix4);
    }

    if((s = inet2str(&config.net.hostname, 6))) {
      strprintf(&buf, "%s, ip = %s/%u", buf, s, config.net.hostname.prefix6);
    }

    fprintf(stderr, "%s\n", buf);
    if(!config.win) printf("%s\n", buf);

    str_copy(&buf, NULL);
  }
  else {
    fprintf(stderr, "no/incomplete answer.\n");
    if(!config.win) printf("no/incomplete answer.\n");
  }
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
  if(!addr) goto error;
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
  dia_message("This is not a valid CCW address.", MSGTYPE_ERROR);
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

  IFNOTAUTO(config.hwp.readchan)
    if((rc=dia_input2_chopspace("Device address for read channel", &config.hwp.readchan, 9, 0)))
      return rc;
  if((rc=net_check_ccw_address(config.hwp.readchan)))
    return rc;
  IFNOTAUTO(config.hwp.writechan)
    if((rc=dia_input2_chopspace("Device address for write channel", &config.hwp.writechan, 9, 0)))
      return rc;
  if((rc=net_check_ccw_address(config.hwp.writechan)))
    return rc;

  return 0;
}

int net_activate_s390_devs()
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
    break;
  case 0x88:	/* CTC */
    config.hwp.type = di_390net_ctc;
    break;
  case 0x8f:	/* ESCON */
    config.hwp.type = di_390net_escon;
    break;
  default:
    return -1;
  }
  else {	/* no hd_t entry -> ask */
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
    if(!strncmp(config.hwp.hypervisor, "KVM", sizeof "KVM" - 1)) {
      items[0] = di_390net_virtio;
      items[1] = di_none;
    }

    IFNOTAUTO(config.hwp.type) {
      di = dia_menu2("Please select the type of your network device.", 60, 0, items, config.hwp.type?:di_390net_iucv);
      config.hwp.type = di;
    }
    else di = config.hwp.type;
  }

  /* hwcfg parms common to all devices */
  config.hwp.startmode="auto";
  config.hwp.module_options="";
  config.hwp.module_unload="yes";
  
  switch(config.hwp.type)
  {
  case di_390net_iucv:
    IFNOTAUTO(config.hwp.userid)
      if((rc=dia_input2_chopspace("Please enter the name (user ID) of the target VM guest.", &config.hwp.userid,20,0))) return rc;

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
      rc=dia_menu2("Select protocol for this CTC device.", 50, 0, protocols, rc);
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
          rc = dia_menu2("Please choose the CCW bus interface.", 33, 0, interfaces, config.hwp.interface?:di_osa_qdio);
          if(rc == -1) return rc;
          config.hwp.interface=rc;
        }
        else
          rc=config.hwp.interface;
      }

      if(!hd || hd->is.dualport)
      {
        IFNOTAUTO(config.hwp.portno)
        {
          char* port = NULL;
          if((rc=dia_input2_chopspace("Enter the relative port number", &port,2,0))) return rc;
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
        if((rc=dia_input2_chopspace("Device address for data channel", &config.hwp.datachan, 9, 0))) return rc;
      if((rc=net_check_ccw_address(config.hwp.datachan))) return rc;

      if (config.hwp.type != di_390net_hsi) {
	  IFNOTAUTO(config.hwp.portname)
	  {
	      if((rc=dia_input2_chopspace("Portname to use", &config.hwp.portname,9,0))) return rc;
	      // FIXME: warn about problems related to empty portnames
	  }
      }
      
      IFNOTAUTO(config.hwp.layer2)
      {
        config.hwp.layer2 = dia_yesno("Enable OSI Layer 2 support?", YES) == YES ? 2 : 1;
      }
      if(config.hwp.layer2 == 2) {
        IFNOTAUTO(config.hwp.osahwaddr) {
          dia_input2("MAC address", &config.hwp.osahwaddr, 17, 1);
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
        if((rc=dia_input2_chopspace("Enter the relative port number", &config.hwp.portname,9,0))) return rc;

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
      if (config.hwp.interface == di_osa_lcs)
        goto setup_ctc;
      ccmd += sprintf(ccmd, "qeth_configure ");
      if(config.hwp.portno)
        ccmd += sprintf(ccmd, "-n %d ", config.hwp.portno - 1);
      ccmd += sprintf(ccmd, "%s%s%s %s %s %s %s 1",
        config.hwp.portname ? "-p \"" : "",
        config.hwp.portname ? config.hwp.portname : "",
        config.hwp.portname ? "\"" : "",
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

  rc = system("/sbin/udevadm settle");
  if(rc) {
    sprintf(cmd, "udevadm settle failed (error code %d)", rc);
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
    strncpy(ifr.ifr_name, ifname, sizeof ifr.ifr_name - 1);
    ifr.ifr_name[sizeof ifr.ifr_name - 1] = 0;
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
          dia_message("Password is too short (must have at least 8 characters).", MSGTYPE_ERROR);
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


char *inet2str(inet_t *inet, int type)
{
  static char buf[INET6_ADDRSTRLEN];
  const char *s = NULL;

  if(!inet) return NULL;

  if(type == 4 && config.net.ipv4 && inet->ipv4) {
    s = inet_ntop(AF_INET, &inet->ip, buf, sizeof buf);
  }
  else if(type == 6 && config.net.ipv6 && inet->ipv6) {
    s = inet_ntop(AF_INET6, &inet->ip6, buf, sizeof buf);
  }


  return (char *) s;
}


/*
 * Return currently active dhcp type ("4", "6", or "" (= both)).
 */
char *net_dhcp_type()
{
  static char t[2] = "4";

  t[0] = 0;
  if(config.net.ipv4 && !config.net.ipv6) t[0] = '4';
  if(!config.net.ipv4 && config.net.ipv6) t[0] = '6';

  return t;
}


/*
 *
 */
#if 0
ifcfg=10.10.0.1/24,10.10.0.254,10.10.1.1 10.10.1.2,suse.de[,XXX_OPTION=foo]
ifcfg=dhcp
ifcfg=eth*=10.10.0.1/24,10.10.0.254,10.10.1.1 10.10.1.2,suse.de[,XXX_OPTION=foo]
ifcfg=eth*=dhcp
#endif
void net_update_ifcfg()
{
  int matched;
  hd_t *net_list, *hd;
  hd_res_t *res;
  char *hwaddr;
  ifcfg_t *ifcfg, **ifcfg_p;

  if(!config.ifcfg.list) return;

  update_device_list(0);
  // list of network cards (this will exclude, e.g., 'lo')
  net_list = hd_list(config.hd_data, hw_network_ctrl, 0, NULL);

  // 1st, all explicitly named interfaces
  for(ifcfg = config.ifcfg.list; ifcfg; ifcfg = ifcfg->next) {
    if(ifcfg->pattern) continue;
    // static config must be used only once
    if(ifcfg->used && !ifcfg->dhcp) continue;

    ifcfg_write2(ifcfg->device, ifcfg, 1);
  }

  // 2nd, all interfaces with wildcard patterns
  for(hd = net_list; hd; hd = hd->next) {
    for(hwaddr = NULL, res = hd->res; res; res = res->next) {
      if(res->any.type == res_hwaddr) {
        hwaddr = res->hwaddr.addr;
        break;
      }
    }

    for(ifcfg = config.ifcfg.list; ifcfg; ifcfg = ifcfg->next) {
      if(!ifcfg->pattern) continue;
      // static config must be used only once
      if(ifcfg->used && !ifcfg->dhcp) continue;

      matched = ifcfg->device ? match_netdevice(hd->unix_dev_name, hwaddr, ifcfg->device) : 0;

      if(matched) ifcfg_write2(hd->unix_dev_name, ifcfg, 1);
    }
  }

  hd_free_hd_list(net_list);

  // append to config.ifcfg.all and clear list
  for(ifcfg_p = &config.ifcfg.all; *ifcfg_p; ifcfg_p = &(*ifcfg_p)->next);
  *ifcfg_p = config.ifcfg.list;
  config.ifcfg.list = NULL;
}


/*
 * Wrapper around ifcfg_write() that does some more logging.
 *
 * If initial is set, mark interface is 'initial'; that is, no further auto
 * config is tried on it.
 */
int ifcfg_write2(char *device, ifcfg_t *ifcfg, int initial)
{
  char *ifname = NULL;
  int i;

  str_copy(&config.ifcfg.current, NULL);

  str_copy(&ifname, device);

  if(ifcfg) {
    ifcfg->used = 1;
    if(ifcfg->vlan) strprintf(&ifname, "%s.%s", device, ifcfg->vlan);
  }

  // FIXME: the next line is basically correct but shouldn't be here in this place
  if(!ifname) {
    str_copy(&ifname, config.net.device);
  }

  // if a device spec is a wildcard patterm, don't allow to overwrite an existing config
  if(slist_getentry(config.ifcfg.initial, ifname)) {
    if(ifcfg && ifcfg->pattern) {
      fprintf(stderr, "%s: network config exists, keeping it\n", ifname);
      str_copy(&config.ifcfg.current, ifname);
      return 1;
    }
    else {
      fprintf(stderr, "%s: network config exists, overwriting it\n", ifname);
    }
  }

  i = ifcfg_write(device, ifcfg);

  if(i) {
    str_copy(&config.ifcfg.current, ifname);
    if(initial) slist_append_str(&config.ifcfg.initial, ifname);
    fprintf(stderr, "%s: network config created\n", ifname);
    if(!config.win) printf("%s: network config created\n", ifname);
  }
  else {
    fprintf(stderr, "%s: failed to create network config\n", ifname);
    if(!config.win) printf("%s: failed to create network config\n", ifname);
  }

  return i;
}


/*
 * Write ifcfg/ifroute files for device.
 *
 * - may be a dhcp or static config
 * - if device or ifcfg are NULL use current data from global config
 * - if global config is used we always create a ***static*** config
 *
 * Note: use ifcfg_write2()!
 */
int ifcfg_write(char *device, ifcfg_t *ifcfg)
{
  char *fname, *s;
  FILE *fp, *fp2;
  char *gw = NULL;	// allocated
  char *ns = NULL;	// allocated
  char *domain = NULL;	// allocated
  char *vlan = NULL;	// allocated
  int global_values = 0;
  int is_dhcp = 0;
  slist_t *sl;
  slist_t *sl_ifcfg = NULL;
  slist_t *sl_ifroute = NULL;
  unsigned ptp = 0;

  // use global values
  if(!device || !ifcfg) global_values = 1;

  fprintf(stderr, "ifcfg_write: device = %s, global = %d, ifcfg = %s\n", device, global_values, ifcfg ? ifcfg->device : "");

  if(global_values) {
    device = config.net.device;

    if(!config.net.hostname.ok) return 0;

    // calculate prefix from netmask if missing
    if(
      config.net.hostname.ipv4 &&
      !config.net.hostname.prefix4 &&
      config.net.netmask.ok &&
      config.net.netmask.ip.s_addr
    ) {
      int i = netmask_to_prefix(config.net.netmask.name);
      if(i >= 0) config.net.hostname.prefix4 = i;
    }
  }

  if(!device) return 0;

  ptp = check_ptp(device);

  if(ptp) fprintf(stderr, "check_ptp: ptp = %u\n", ptp);

  ptp |= ifcfg->ptp;

  // 1. maybe dhcp config, but only if passed explicitly

  if(!global_values && ifcfg->dhcp) {
    is_dhcp = 1;

    sl = slist_append_str(&sl_ifcfg, "BOOTPROTO");
    if(ifcfg->type) {
      str_copy(&sl->value, ifcfg->type);
    }
    else {
      strprintf(&ifcfg->type, "dhcp%s", net_dhcp_type());
    }
    if(ifcfg->vlan) {
      strprintf(&vlan, ".%s", ifcfg->vlan);
      sl = slist_append_str(&sl_ifcfg, "ETHERDEVICE");
      str_copy(&sl->value, device);
    }
  }

  // 2. create ifcfg entries

  if(!is_dhcp) {
    sl = slist_append_str(&sl_ifcfg, "BOOTPROTO");
    str_copy(&sl->value, "static");
  }

  sl = slist_append_str(&sl_ifcfg, "STARTMODE");
  str_copy(&sl->value, "auto");

  if(!is_dhcp) {
    if(global_values) {
      char *ip1 = NULL, *ip2 = NULL;

      if((s = inet2str(&config.net.hostname, 4))) {
        if(asprintf(&ip1, "%s/%u", s, config.net.hostname.prefix4) == -1) ip1 = NULL;
      }

      if((s = inet2str(&config.net.hostname, 6))) {
        if(asprintf(&ip2, "%s/%u", s, config.net.hostname.prefix6) == -1) ip2 = NULL;
      }

      if(ip1 && ip2) {
        sl = slist_append_str(&sl_ifcfg, "IPADDR_1");
        str_copy(&sl->value, ip1);
        sl = slist_append_str(&sl_ifcfg, "IPADDR_2");
        str_copy(&sl->value, ip2);
      }
      else {
        if(!ip1) {
          ip1 = ip2;
          ip2 = NULL;
        }
        sl = slist_append_str(&sl_ifcfg, "IPADDR");
        str_copy(&sl->value, ip1);
      }

      free(ip1);
      free(ip2);

      // net_apply_ethtool()
      // ETHTOOL_OPTIONS
    }
    else {
      int i;
      slist_t *sl0, *sl1;

      str_copy(&gw, ifcfg->gw);
      str_copy(&ns, ifcfg->ns);
      str_copy(&domain, ifcfg->domain);
      if(ifcfg->vlan) {
        strprintf(&vlan, ".%s", ifcfg->vlan);
        sl = slist_append_str(&sl_ifcfg, "ETHERDEVICE");
        str_copy(&sl->value, device);
      }

      if((sl0 = slist_split(' ', ifcfg->ip))) {
        if(!sl0->next) {
          sl = slist_append_str(&sl_ifcfg, "IPADDR");
          str_copy(&sl->value, sl0->key);
          if(ifcfg->netmask_prefix > 0 && !strchr(sl->value, '/')) {
            strprintf(&sl->value, "%s/%d", sl->value, ifcfg->netmask_prefix);
          }
        }
        else {
          for(i = 0, sl1 = sl0; sl1; sl1 = sl1->next) {
            sl = slist_append(&sl_ifcfg, slist_new());
            strprintf(&sl->key, "IPADDR_%d", ++i);
            str_copy(&sl->value, sl1->key);
            if(ifcfg->netmask_prefix > 0 && !strchr(sl->value, '/')) {
              strprintf(&sl->value, "%s/%d", sl->value, ifcfg->netmask_prefix);
            }
          }
        }

        sl0 = slist_free(sl0);
      }

      if(ptp) {
        if((sl0 = slist_split(' ', ifcfg->gw))) {
          if(!sl0->next) {
            sl = slist_append_str(&sl_ifcfg, "REMOTE_IPADDR");
            str_copy(&sl->value, sl0->key);
          }
          else {
            for(i = 0, sl1 = sl0; sl1; sl1 = sl1->next) {
              sl = slist_append(&sl_ifcfg, slist_new());
              strprintf(&sl->key, "REMOTE_IPADDR_%d", ++i);
              str_copy(&sl->value, sl1->key);
            }
          }

          sl0 = slist_free(sl0);
        }
      }

      for (sl = ifcfg->flags; sl; sl = sl->next) {
        if(!(sl1 = slist_getentry(sl_ifcfg, sl->key))) sl1 = slist_append(&sl_ifcfg, slist_new());
        str_copy(&sl1->key, sl->key);
        str_copy(&sl1->value, sl->value);
      }
    }
  }

  if(sl_ifcfg) {
    if(asprintf(&fname, "/etc/sysconfig/network/ifcfg-%s%s", device, vlan ?: "") == -1) fname = NULL;
    fprintf(stderr, "creating ifcfg-%s%s:\n", device, vlan ?: "");
    if(fname && (fp = fopen(fname, "w"))) {
      for(sl = sl_ifcfg; sl; sl = sl->next) {
        fprintf(fp, "%s='%s'\n", sl->key, sl->value);
      }

      fclose(fp);
    }

    for(sl = sl_ifcfg; sl; sl = sl->next) {
      fprintf(stderr, "  %s='%s'\n", sl->key, sl->value);
    }

    free(fname);
  }

  // 3. create ifroute entries

  if(!is_dhcp) {
    if((global_values && config.net.gateway.ok) || gw) {
      if(global_values) {
        if((s = inet2str(&config.net.gateway, 4))) {
          sl = slist_append(&sl_ifroute, slist_new());
          strprintf(&sl->key, "default %s - %s", s, device);
        }

        if((s = inet2str(&config.net.gateway, 6))) {
          sl = slist_append(&sl_ifroute, slist_new());
          strprintf(&sl->key, "default %s - %s", s, device);
        }
      }
      else {
        slist_t *sl1, *sl0 = slist_split(' ', ifcfg->gw);

        for(sl1 = sl0; sl1; sl1 = sl1->next) {
          sl = slist_append(&sl_ifroute, slist_new());
          strprintf(&sl->key, "default %s - %s", sl1->key, device);
        }

        slist_free(sl0);
      }
    }
  }

  if(sl_ifroute) {
    if(asprintf(&fname, "/etc/sysconfig/network/ifroute-%s%s", device, vlan ?: "") == -1) fname = NULL;

    fprintf(stderr, "creating ifroute-%s%s:\n", device, vlan ?: "");
    if(fname && (fp = fopen(fname, "w"))) {
      for(sl = sl_ifroute; sl; sl = sl->next) {
        fprintf(fp, "%s\n", sl->key);
      }

      fclose(fp);
    }

    for(sl = sl_ifroute; sl; sl = sl->next) {
      fprintf(stderr, "  %s\n", sl->key);
    }

    free(fname);
  }

  // 4. set nameserver and search list

  if(!is_dhcp) {
    if(global_values) {
      str_copy(&domain, config.net.domain);
      if(config.net.nameserver[0].ok) {
        unsigned u, first;

        for(u = 0, first = 1; u < config.net.nameservers; u++) {
          if(config.net.nameserver[u].ok) {
            strprintf(&ns, "%s%s%s", ns ?: "", first ? "" : " ", config.net.nameserver[u].name);
            first = 0;
          }
        }
      }
    }

    if(ns || domain) {
      fprintf(stderr, "adjusting network/config:\n");
      if(ns) fprintf(stderr, "  NETCONFIG_DNS_STATIC_SERVERS=\"%s\"\n", ns);
      if(domain) fprintf(stderr, "  NETCONFIG_DNS_STATIC_SEARCHLIST=\"%s\"\n", domain);

      if((fp = fopen("/etc/sysconfig/network/config", "r"))) {
        if((fp2 = fopen("/etc/sysconfig/network/config.tmp", "w"))) {
          char buf[1024];

          while(fgets(buf, sizeof buf, fp)) {
            if(
              domain &&
              !strncmp(buf, "NETCONFIG_DNS_STATIC_SEARCHLIST=", sizeof "NETCONFIG_DNS_STATIC_SEARCHLIST=" - 1)
            ) {
              fprintf(fp2, "NETCONFIG_DNS_STATIC_SEARCHLIST=\"%s\"\n", domain);
            }
            else if(
              ns &&
              !strncmp(buf, "NETCONFIG_DNS_STATIC_SERVERS=", sizeof "NETCONFIG_DNS_STATIC_SERVERS=" - 1)
            ) {
              fprintf(fp2, "NETCONFIG_DNS_STATIC_SERVERS=\"%s\"\n", ns);
            }
            else {
              fputs(buf, fp2);
            }
          }

          fclose(fp2);
        }

        fclose(fp);

        rename("/etc/sysconfig/network/config.tmp", "/etc/sysconfig/network/config");
      }
    }
  }

  str_copy(&gw, NULL);
  str_copy(&ns, NULL);
  str_copy(&domain, NULL);
  str_copy(&vlan, NULL);

  slist_free(sl_ifcfg);
  slist_free(sl_ifroute);

  return 1;
}


ifcfg_t *ifcfg_parse(char *str)
{
  slist_t *sl, *sl0, *slx;
  ifcfg_t *ifcfg;
  char *s, *t;

  if(!str) return NULL;

  if(config.debug) fprintf(stderr, "parsing ifcfg: %s\n", str);

  ifcfg = calloc(1, sizeof *ifcfg);

  sl0 = slist_new();
  sl0->next = sl = slist_split(',', str);

  if(sl) {
    char *t;
    if((t = strchr(sl->key, '='))) {
      *t++ = 0;
      sl0->key = sl->key;
      sl->key = strdup(t);
    }
    else {
      sl0->key = strdup("");
    }
  }

  s = slist_key(sl0, 0);
  if(s && *s) {
    if((t = strrchr(s, '.'))) {
      char *t2;
      *t++ = 0;
      strtol(t, &t2, 10);
      // valid number?
      if(*t && *t2 == 0) str_copy(&ifcfg->vlan, t);
    }
    str_copy(&ifcfg->device, s);
  }

  s = slist_key(sl0, 1);
  if(s && !strncmp(s, "dhcp", sizeof "dhcp" - 1)) {
    str_copy(&ifcfg->type, s);
    ifcfg->dhcp = 1;
  }
  else {
    str_copy(&ifcfg->type, "static");

    t = NULL;

    if(s && *s && !(t = strchr(s, '='))) str_copy(&ifcfg->ip, s);

    s = slist_key(sl0, 2);
    if(!t && s && *s && !(t = strchr(s, '='))) str_copy(&ifcfg->gw, s);

    s = slist_key(sl0, 3);
    if(!t && s && *s && !(t = strchr(s, '='))) str_copy(&ifcfg->ns, s);

    s = slist_key(sl0, 4);
    if(!t && s && *s && !(t = strchr(s, '='))) str_copy(&ifcfg->domain, s);
  }

  for(sl = sl0->next; sl; sl = sl->next) {
    if((t = strchr(sl->key, '='))) {
      *t++ = 0;
      slx = slist_append(&ifcfg->flags, slist_new());
      str_copy(&slx->key, sl->key);
      str_copy(&slx->value, t);
    }
  }

  slist_free(sl0);

  if(ifcfg->device) {
    for(s = ifcfg->device; *s; s++) {
      if(!isalnum(*s) && *s != '_') {
        ifcfg->pattern = 1;
        break;
      }
    }
  }

  if(config.debug) fprintf(stderr, "%s", ifcfg_print(ifcfg));

  return ifcfg;
}


ifcfg_t *ifcfg_append(ifcfg_t **p0, ifcfg_t *p)
{
  for(; *p0; p0 = &(*p0)->next);
  return *p0 = p;
}


/*
 * Returns static buffer.
 */
char *ifcfg_print(ifcfg_t *ifcfg)
{
  slist_t *sl;
  static char *buf = NULL;

  str_copy(&buf, NULL);

  strprintf(&buf, "  device = %s\n", ifcfg->device);
  if(ifcfg->vlan) strprintf(&buf, "%s  vlan = %s\n", buf, ifcfg->vlan);
  if(ifcfg->type) strprintf(&buf, "%s  type = %s\n", buf, ifcfg->type);
  strprintf(&buf,
    "%s  dhcp = %u, pattern = %u, used = %u, prefix = %d, ptp = %u\n",
    buf,
    ifcfg->dhcp, ifcfg->pattern, ifcfg->used,
    ifcfg->netmask_prefix, ifcfg->ptp
  );
  if(ifcfg->ip) strprintf(&buf, "%s  ip = %s\n", buf, ifcfg->ip);
  if(ifcfg->gw) strprintf(&buf, "%s  gw = %s\n", buf, ifcfg->gw);
  if(ifcfg->ns) strprintf(&buf, "%s  ns = %s\n", buf, ifcfg->ns);
  if(ifcfg->domain) strprintf(&buf, "%s  domain = %s\n", buf, ifcfg->domain);
  for (sl = ifcfg->flags; sl; sl = sl->next) {
    strprintf(&buf, "%s  %s = \"%s\"\n", buf, sl->key, sl->value);
  }

  return buf;
}


/*
 * Run wicked to collect all interface states.
 *
 * config.ifcfg.if_state
 */
void net_update_state()
{
  FILE *f;
  char buf[256], *s1, *s2,*s3;
  slist_t *sl;

  config.ifcfg.if_state = slist_free(config.ifcfg.if_state);

  if((f = popen("wicked show all", "r"))) {
    while(fgets(buf, sizeof buf, f)) {
      if(!isspace(*buf)) {
        for(s1 = buf; *s1 && !isspace(*s1); s1++);
        for(s2 = s1; *s2 && isspace(*s2); s2++);
        for(s3 = s2; *s3 && !isspace(*s3); s3++);
        *s1 = *s3 = 0;
        if(*buf && *s2 && s2 != buf) {
          sl = slist_append(&config.ifcfg.if_state, slist_new());
          str_copy(&sl->key, buf);
          str_copy(&sl->value, s2);
        }
      }
    }
    pclose(f);
  }

  config.ifcfg.if_up = slist_free(config.ifcfg.if_up);
  for(sl = config.ifcfg.if_state; sl; sl = sl->next) {
    // interfaces != lo that are 'up'
    if(strcmp(sl->key, "lo") && !strcmp(sl->value, "up")) slist_append_str(&config.ifcfg.if_up, sl->key);
  }

  if(config.debug) {
    fprintf(stderr, "net_update_state: ");
    for(sl = config.ifcfg.if_state; sl; sl = sl->next) {
      fprintf(stderr, "%s: %s%s", sl->key, sl->value, sl->next ? ", " : "");
    }
    fprintf(stderr, "\n");
  }
}


/*
 * Set up interface; ifname may be NULL, an interface or 'all'.
 */
void net_wicked_up(char *ifname)
{
  char *buf = NULL;

  if(!ifname) return;

  if(config.net.dhcp_timeout_set) {
    strprintf(&buf, "wicked ifup --timeout %d %s >&2", config.net.dhcp_timeout, ifname);
  }
  else {
    strprintf(&buf, "wicked ifup %s >&2", ifname);
  }

  system(buf);

  sleep(1);

  net_update_state();

  str_copy(&buf, NULL);
}


/*
 * Shut down interface; ifname may be NULL, an interface or 'all'.
 */
void net_wicked_down(char *ifname)
{
  char *buf = NULL;

  if(!ifname) return;

  strprintf(&buf, "wicked ifdown %s >&2", ifname);

  system(buf);

  sleep(1);

  net_update_state();

  str_copy(&buf, NULL);
}


/*
 * Convert netmask string to network prefix bits.
 * Both ipv4 and ipv6 forms are allowed.
 *
 * Return -1 if it fails.
 */
int netmask_to_prefix(char *netmask)
{
  int prefix = -1;
  uint32_t u;
  struct in_addr ip4;
  struct in6_addr ip6;

  if(netmask) {
    if(strchr(netmask, ':')) {
      if(inet_pton(AF_INET6, netmask, &ip6) > 0) {
        prefix = 0;
        for(u = 0; u < 16 && ip6.s6_addr[u] == 0xff; u++) {
          prefix += 8;
        }
        if(u < 16) {
          for(u = ip6.s6_addr[u]; u & 0x80; u <<= 1, prefix++);
        }
      }
    }
    else {
      if(inet_pton(AF_INET, netmask, &ip4) > 0) {
        u = ntohl(ip4.s_addr);
        if(u == 0) {
          prefix = 0;
        }
        else {
          prefix = 1;
          while(u <<= 1) prefix++;
        }
      }
    }
  }

  if(config.debug) fprintf(stderr, "netmask -> prefix: %s -> %d\n", netmask, prefix);

  return prefix;
}


/*
 * Check whether a network setup is necessary.
 *
 * That is: we need a network but no interface is up.
 *
 * We need a network if really or config.net.do_setup is set.
 */
int net_config_needed(int really)
{
  if(!(config.net.do_setup || really)) return 0;

  net_update_state();

  return config.ifcfg.if_up ? 0 : 1;
}


/*
 * Check if interface is a ptp interface and set config.net.ptp accordingly.
 */
unsigned check_ptp(char *ifname)
{
  unsigned ptp = 0;

  if(!ifname) ifname = config.net.device;

  if(
    ifname &&
    (
      !strncmp(ifname, "plip", sizeof "plip" - 1) ||
      !strncmp(ifname, "iucv", sizeof "iucv" - 1) ||
      !strncmp(ifname, "ctc", sizeof "ctc" - 1) ||
      !strncmp(ifname, "sl", sizeof "sl" - 1)
    )
  ) ptp = 1;

  return config.net.ptp = ptp;
}

