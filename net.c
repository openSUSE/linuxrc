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
// settings for config.hwp.layer2
// (layer2 attribute for network cards)
#define LAYER2_UNDEF	0
#define LAYER2_NO	1
#define LAYER2_YES	2

int net_activate_s390_devs_ex(hd_t* hd, char** device);
#endif

static int net_choose_device(void);
static int net_input_data(void);
static int net_input_vlanid(void);

static int wlan_auth_cb(dia_item_t di);

static dia_item_t di_wlan_auth_last = di_none;
static void parse_leaseinfo(char *file);
static void net_wicked_dhcp(void);

static void net_cifs_build_options(char **options, char *user, char *password, char *workgroup);
static int ifcfg_write(char *device, ifcfg_t *ifcfg, int flags);
static int _ifcfg_write(char *device, ifcfg_t *ifcfg);
static char *inet2str(inet_t *inet, int type);
static int net_get_ip(char *text, char **ip, int with_prefix);
static int net_check_ip(char *buf, int multi, int with_prefix);
static int compare_subnet(char *ip1, char *ip2, unsigned prefix);
static void get_and_copy_ifcfg_flags(ifcfg_t *ifcfg, char *device);
static void update_sysconfig(slist_t *slist, char *filename);


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

  if(config.usessh && !(config.net.sshpassword || config.net.sshpassword_enc || config.net.sshkey)) {
    if(!config.win) util_disp_init();
    dia_input2("Enter your temporary SSH password.", &config.net.sshpassword, 20, 1);
  }

  if(config.win && !win_old) util_disp_done();
}


/*
 * Configure network. Ask for network config data if necessary.
 * Calls either net_dhcp() or net_static() to setup the interface.
 *
 * Return:
 *      0: ok
 *   != 0: error or abort
 *
 * Does nothing if DHCP is active.
 *
 * FIXME: needs window mode or not?
 */
int net_config()
{
  int rc = 0;
  char buf[256];

  // in manual mode, ask for everything
  if(config.manual) config.net.setup |= NS_DEFAULT;

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

  if(config.win) {
    rc = net_input_vlanid();
    if(rc) return -1;
  }

  /*
   * VLANID is handled in net_input_vlanid() a few lines above. Take this
   * into account when deciding if there's anything else besides DHCP to be
   * done.
   */
  if(config.win && (config.net.setup & ~NS_VLANID) != NS_DHCP) {
    if(
      config.net.setup & NS_DHCP &&
#if defined(__s390__) || defined(__s390x__)
      config.hwp.layer2 != LAYER2_NO &&
#endif
      !config.ifcfg.manual->ptp
    ) {
      sprintf(buf, "Automatic configuration via %s?", "DHCP");
      rc = dia_yesno(buf, YES);
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
 * config.net.device:    interface
 * /proc/net/route: configured interfaces
 */
void net_stop()
{
  char *device = config.ifcfg.current;

  if(!device) return;

  log_debug("%s: network down\n", device);

  if(config.test) {
    return;
  }

  net_wicked_down(device);

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
 * Setup network interface with static config.
 *
 * Return:
 *      0: ok
 *   != 0: error code
 *
 */
int net_static()
{
  char *s;

  /* make sure we get the interface name if a mac was passed */
  if((s = mac_to_interface(config.ifcfg.manual->device, NULL))) {
    free(config.ifcfg.manual->device);
    config.ifcfg.manual->device = s;
  }

  config.ifcfg.manual->dhcp = 0;

  get_and_copy_ifcfg_flags(config.ifcfg.manual, config.ifcfg.manual->device);

  if(!ifcfg_write(config.ifcfg.manual->device, config.ifcfg.manual, 0)) {
    return 1;
  }

  char *ifname = NULL;

  str_copy(&ifname, net_get_ifname(config.ifcfg.manual));

  net_wicked_up(ifname);

  str_copy(&ifname, NULL);

  return 0;
}


/**
 * Parse inet->name for ipv4/ipv6 adress.
 * If do_dns is set, try nameserver lookup.
 *
 * Sets name && ok && ((ip6 && prefix6) || (ipv4 && ip && net && prefix4))
 *
 * @return
 * -  0 : ok
 * -  1 : failed
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
          log_info("dns6: what is \"%s\"?\n", inet->name);
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
            log_info("dns6: %s is %s\n", inet->name, s);
          }
        }
      }
    }

    if(config.net.ipv4) {
      he = gethostbyname2(inet->name, AF_INET);
      if(!he) { sleep(1); gethostbyname2(inet->name, AF_INET); }
      if(!he) {
        if(config.run_as_linuxrc) {
          log_info("dns: what is \"%s\"?\n", inet->name);
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
            log_info("dns: %s is %s\n", inet->name, s);
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
int net_mount_cifs(char *mountpoint, char *server, char *share, char *user, char *password, char *workgroup, char *options)
{
  char *cmd = NULL;
  char *real_options = NULL;
  int err;

  if(!config.net.cifs.binary || !server) return -EDESTADDRREQ;	// -89

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

  strprintf(&cmd, "%s '//%s/%s' '%s' -o '%s'", config.net.cifs.binary, server, share, mountpoint, real_options);

  err = lxrc_run(cmd);

  str_copy(&cmd, NULL);
  str_copy(&real_options, NULL);

  return err ? -1 : 0;
}


/*
 * Mount NFS volume.
 *
 * mountpoint: mount point
 * server: NFS server name
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
int net_mount_nfs(char *mountpoint, char *server, char *hostdir, unsigned port, char *options)
{
  char *path = NULL;
  char *real_options = NULL;
  pid_t mount_pid;

  if(!server) return -EDESTADDRREQ;	// -89

  if(!hostdir) hostdir = "/";
  if(!mountpoint || !*mountpoint) mountpoint = "/";

  mount_pid = fork();
  if(mount_pid < 0) {
    perror_info("fork");

    return mount_pid;
  }
  else if(mount_pid > 0) {
    int err;
    pid_t pid;
    while((pid = waitpid(-1, &err, 0)) && pid != mount_pid);

    return WEXITSTATUS(err);
  }

  if(strchr(server, ':')) {
    strprintf(&path, "[%s]:%s", server, hostdir);
  }
  else {
    strprintf(&path, "%s:%s", server, hostdir);
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

  log_debug("mount -o '%s' '%s' '%s'\n", real_options, path, mountpoint);

  char *args[6] = { "mount", "-o", real_options, path, mountpoint /*, NULL */ };

  signal(SIGUSR1, SIG_IGN);
  execvp("mount", args);
  perror_info("execvp(\"mount\")");
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
    { "hsi",   "HiperSocket"   }
  };
  hd_data_t *hd_data = calloc(1, sizeof *hd_data);
  hd_t *hd, *hd_cards;
  hd_t **item_hds = NULL;
    
  // return if we already have one and we're not in manual mode
  if(config.ifcfg.manual->device && !config.manual) return 0;

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
    log_info("activate s390 devs 1\n");
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
      char* annotation = 0;
      hd_res_t *res;
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
      }
#endif
      if(!annotation) {
        for(res = hd->res; res; res = res->next) {
          if(res->any.type == res_hwaddr) {
            strprintf(&annotation, "(%s)", res->hwaddr.addr);
            break;
          }
        }
      }

      if(hd->unix_dev_name) {
        strprintf(items + item_cnt++, "%*s : %s %s", -width, hd->unix_dev_name, hd->model,
          annotation);
      }
      else {
        strprintf(items + item_cnt++, "%s %s", hd->model, annotation);
      }
      free(annotation);
      annotation = 0;
    }
  }

  // only this should be used rather than all the hd_list() calls above...
  update_device_list(0);

  if(item_cnt == 0) {
    dia_message("No network device found.\n"
                "Load a network module first.",
                MSGTYPE_ERROR);
    choice = -1;
  } else if(item_cnt == 1) {
    choice = 1;
  }
  else {
    choice = dia_list("Choose the network device.", 72, NULL, items, last_item, align_left);
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
    log_info("activate s390 devs 2\n");
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
      // set the layer2 tag for network cards
      // LAYER2_UNDEF means it doesn't have this attribute (e.g. virtio)
      if(*type == 0) {
        config.hwp.layer2 = LAYER2_UNDEF;
      }
      else if(*type == '1') {
        config.hwp.layer2 = LAYER2_YES;
      }
      else {
        config.hwp.layer2 = LAYER2_NO;
      }
    }
  }
#endif

  if(choice > 0) {
    str_copy(&config.ifcfg.manual->device, item_devs[choice - 1]);

    check_ptp(config.ifcfg.manual->device);

    if(item_hds && item_hds[choice - 1]) {
      hd = item_hds[choice - 1];
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


/*
 * Let user enter network config data.
 *
 * Config adata are stored in config.ifcfg.manual.
 *
 * Note: expects window mode.
 */
int net_input_data()
{
  if(config.ifcfg.manual->ptp) {
    if((config.net.setup & NS_HOSTIP)) {
      if(net_get_ip(
        "Enter your IP address.\n\n"
        "You can enter more than one, separated by space, if necessary.\n"
        "Leave empty for autoconfig.\n\n"
        "Examples: 192.168.5.77 2001:db8:75:fff::3",
        &config.ifcfg.manual->ip, 0) == 2
      ) return -1;
    }

    if(net_get_ip(
      "Enter the IP address of the PLIP partner.\n\n"
      "Examples: 192.168.5.77 2001:db8:75:fff::3",
      &config.ifcfg.manual->gw, 0) == 2
    ) return -1;
  }
  else {
    if((config.net.setup & NS_HOSTIP)) {
      if(net_get_ip(
        "Enter your IP address with network prefix.\n\n"
        "You can enter more than one, separated by space, if necessary.\n"
        "Leave empty for autoconfig.\n\n"
        "Examples: 192.168.5.77/24 2001:db8:75:fff::3/64",
        &config.ifcfg.manual->ip, 1) == 2
      ) return -1;
    }

    if((config.net.setup & NS_GATEWAY)) {
      if(net_get_ip(
        "Enter your gateway IP address.\n\n"
        "You can enter more than one, separated by space, if necessary.\n"
        "Leave empty if you don't need one.\n\n"
        "Examples: 192.168.5.77 2001:db8:75:fff::3",
        &config.ifcfg.manual->gw, 0) == 2
      ) return -1;
    }
  }

  if((config.net.setup & NS_NAMESERVER)) {
    if(net_get_ip(
      "Enter your name server IP address.\n\n"
      "You can enter more than one, separated by space, if necessary.\n"
      "Leave empty if you don't need one.\n\n"
      "Examples: 192.168.5.77 2001:db8:75:fff::3",
      &config.ifcfg.manual->ns, 0) == 2
    ) return -1;

    dia_input2(
      "Enter your search domains, separated by a space.",
      &config.ifcfg.manual->domain, 40, 0
    );
  }

  return 0;
}


/*
 * Ask for vlan id and store in config.ifcfg.manual.
 */
int net_input_vlanid()
{
  int err = 0, id;

  char *buf = NULL, *s;

  if(!(config.net.setup & NS_VLANID)) return 0;

  str_copy(&buf, config.ifcfg.manual->vlan);

  do {
    err = 0;

    if(dia_input2("Enter your VLAN ID\n\nLeave empty if you don't setup a VLAN.", &buf, 30, 0)) {
      err = 1;
      break;
    }

    if(!buf) break;

    id = strtoul(buf, &s, 0);
    if(*s || id <= 0) err = 1;
    if(err) dia_message("Invalid input.", MSGTYPE_ERROR);
  } while(err);

  if(!err) str_copy(&config.ifcfg.manual->vlan, buf);

  str_copy(&buf, NULL);

  return err;
}


/*
 * Ask user for a space-separated list of network addresses.
 *
 * If with_prefix is set, it must include the '/prefix' notation.
 *
 * return:
 *   0: ok
 *   1: empty input
 *   2: error or abort
 *
 * Note: expects window mode.
 */
int net_get_ip(char *text, char **ip, int with_prefix)
{
  int err = 0;

  char *buf = NULL;

  if(ip) str_copy(&buf, *ip);

  do {
    err = 0;

    if(dia_input2(text, &buf, 40, 0)) {
      err = 2;
      break;
    }

    if(!buf) {
      err = 1;
      break;
    }
    if(!net_check_ip(buf, 1, with_prefix)) err = 2;
    if(err) dia_message("Invalid input.", MSGTYPE_ERROR);
  } while(err);

  if(err != 2 && ip) str_copy(ip, buf);

  str_copy(&buf, NULL);

  return err;
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
    if((err = dia_input2(text, &input, 40, 0))) break;
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
void parse_leaseinfo(char *file)
{
  file_t *f0, *f;

  f0 = file_read_file(file, kf_dhcp);

  for(f = f0; f; f = f->next) {
    switch(f->key) {
      case key_ipaddr:
        name2inet(&config.net.hostname, f->value);
        net_check_address(&config.net.hostname, 0);
        break;

      default:
        break;
    }
  }

  file_free_file(f0);
}


/*
 * Start dhcp client and read dhcp info.
 *
 */
int net_dhcp()
{
  char *s;

  /* make sure we get the interface name if a mac was passed */
  if((s = mac_to_interface(config.ifcfg.manual->device, NULL))) {
    free(config.ifcfg.manual->device);
    config.ifcfg.manual->device = s;
  }

  get_and_copy_ifcfg_flags(config.ifcfg.manual, config.ifcfg.manual->device);

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
 *  config.net.gateway
 *  config.net.nisdomain
 *  config.net.nameserver
 */
void net_wicked_dhcp()
{
  char file[256], *buf = NULL;
  window_t win;
  int got_ip = 0, cfg_ok;
  ifcfg_t *ifcfg = NULL;
  char *type;

  if(config.test) {
    config.net.dhcp_active = 1;

    log_info("test mode: DHCP activated\n");

    return;
  }

  type = net_dhcp_type();

  // override the default by an explicit ifcfg=dhcp{4,6}
  if(!*type && config.ifcfg.manual->type) {
    if(!strcmp(config.ifcfg.manual->type, "dhcp4")) type = "4";
    if(!strcmp(config.ifcfg.manual->type, "dhcp6")) type = "6";
  }

  ifcfg = calloc(1, sizeof *ifcfg);
  ifcfg->dhcp = 1;

  strprintf(&ifcfg->type, "dhcp%s", type);

  ifcfg->flags = config.ifcfg.manual->flags;
  str_copy(&ifcfg->vlan, config.ifcfg.manual->vlan);

  cfg_ok = ifcfg_write(config.ifcfg.manual->device, ifcfg, 0);

  free(ifcfg->type);
  free(ifcfg->vlan);
  free(ifcfg);
  ifcfg = NULL;

  if(!cfg_ok) return;

  char *ifname = NULL;

  str_copy(&ifname, net_get_ifname(config.ifcfg.manual));

  strprintf(&buf, "Sending DHCP%s request to %s...", type, ifname);
  log_show_maybe(!config.win, "%s\n", buf);
  if(config.win) {
    dia_info(&win, buf, MSGTYPE_INFO);
  }
  str_copy(&buf, NULL);

  net_apply_ethtool(config.ifcfg.manual->device, NULL);

  net_wicked_up(ifname);

  if(config.net.ipv4) {
    snprintf(file, sizeof file, "/run/wicked/leaseinfo.%s.dhcp.ipv4", ifname);
    parse_leaseinfo(file);
  }

  if(!got_ip && config.net.ipv6) {
    snprintf(file, sizeof file, "/run/wicked/leaseinfo.%s.dhcp.ipv6", ifname);
    parse_leaseinfo(file);
  }

  if(slist_getentry(config.ifcfg.if_up, ifname)) got_ip = 1;

  if(config.win) win_close(&win);

  if(got_ip) {
    config.net.dhcp_active = 1;
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

    log_show_maybe(!config.win, "%s\n", buf);

    str_copy(&buf, NULL);
  }
  else {
    log_show_maybe(!config.win, "no/incomplete answer.\n");
  }

  str_copy(&ifname, NULL);
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

  log_info("checking CCW address %s\n", addr);

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
    log_info("ccwgroup %s has network IF %s\n", channel, e->d_name);
    strprintf(device, e->d_name);
    closedir(d);
    return 0;
  }
  closedir(d);
  log_info("no network IF found for channel group %s\n", channel);
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
      config.hwp.type = di_390net_virtio;
    }
    else {
      IFNOTAUTO(config.hwp.type) {
        config.hwp.type = dia_menu2("Please select the type of your network device.", 60, 0, items, config.hwp.type?:di_390net_iucv);
      }
    }
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

      IFNOTAUTO(config.hwp.layer2)
      {
        config.hwp.layer2 = dia_yesno("Enable OSI Layer 2 support?", YES) == YES ? LAYER2_YES : LAYER2_NO;
      }
      if(config.hwp.layer2 == LAYER2_YES) {
        IFNOTAUTO(config.hwp.osahwaddr) {
          dia_input2("Specifying a MAC address is optional.\n"\
                     "In most cases letting it default is the correct choice.\n"\
                     "Provide one only if you know it is truly necessary.\n"\
                     "Optional MAC address", &config.hwp.osahwaddr, 17, 0);
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
      
    }
    
    break;

  case di_390net_virtio:
    config.hwp.layer2 = LAYER2_UNDEF;
    return 0;
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
      if(config.hwp.protocol > 0)
        sprintf(cmd, "/sbin/chzdev -e ctc --no-root-update %s-%s protocol=%d", config.hwp.readchan, config.hwp.writechan, config.hwp.protocol - 1);
      else
        sprintf(cmd, "/sbin/chzdev -e ctc --no-root-update %s-%s", config.hwp.readchan, config.hwp.writechan);
      util_write_active_devices("%s,%s\n", config.hwp.readchan, config.hwp.writechan);
      break;
    case di_390net_hsi:
    case di_390net_osa:
      if (config.hwp.interface == di_osa_lcs) {
        sprintf(cmd, "/sbin/chzdev -e lcs --no-root-update %s-%s", config.hwp.readchan, config.hwp.writechan);
        util_write_active_devices("%s,%s\n", config.hwp.readchan, config.hwp.writechan);
          /* For whatever reason, LCS devices need to be enabled twice before they
           * actually come online. So, we execute lxrc_run here, and again after the end
           * of the case statement. */
          rc = lxrc_run(cmd);
      }
      else {
        ccmd += sprintf(ccmd, "/sbin/chzdev -e qeth --no-root-update ");
        if(config.hwp.portno)
          ccmd += sprintf(ccmd, "portno=%d ", config.hwp.portno - 1);
        ccmd += sprintf(ccmd, "%s %s:%s:%s ",
        config.hwp.layer2 == LAYER2_YES ? "layer2=1 " : "layer2=0 ",
        config.hwp.readchan,
        config.hwp.writechan,
        config.hwp.datachan);
        util_write_active_devices("%s,%s,%s\n", config.hwp.readchan, config.hwp.writechan, config.hwp.datachan);
      }
      break;
    default:
      sprintf(cmd, "unknown s390 network type %d", config.hwp.type);
      dia_message(cmd, MSGTYPE_ERROR);
      return -1;
      break;
  }

  rc = lxrc_run(cmd);
  if(rc) {
    sprintf(cmd, "network configuration script failed (error code %d)", rc);
    dia_message(cmd, MSGTYPE_ERROR);
    return -1;
  }

  rc = lxrc_run("/usr/bin/udevadm settle");
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
      perror_info("socket");
      return 1;
    }
    
    /* convert MAC address to binary */
    if((ea = ether_aton(config.hwp.osahwaddr)) == NULL) {
      log_info("MAC address invalid: %s\n", config.hwp.osahwaddr);
      return 1;
    }
    memcpy((void*) &ifr.ifr_hwaddr.sa_data[0], ea, ETHER_ADDR_LEN);
    
    //ifr.ifr_hwaddr.sa_len = ETHER_ADDR_LEN
    ifr.ifr_hwaddr.sa_family = AF_UNIX;
    
    if(ioctl(skfd, SIOCSIFHWADDR, &ifr) < 0) {
      log_info("SIOCSIFHWADDR: %s\n", strerror(errno));
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
      (hwaddr && !fnmatch(sl->key, hwaddr, FNM_CASEFOLD))
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
    lxrc_run(s);
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
    di_wlan_wpa_psk,
    di_wlan_wpa_peap,
    di_none
  };

  if(config.manual || !config.net.wlan.essid) {
    if(dia_input2("ESSID (Network Name)", &config.net.wlan.essid, 30, 0)) {
      return 1;
    }
  }

  switch(config.net.wlan.auth) {
    case wa_open:
      di = di_wlan_open;
      break;

    case wa_wpa_psk:
      di = di_wlan_wpa_psk;
      break;

    case wa_wpa_peap:
      di = di_wlan_wpa_peap;
      break;

    default:
      di = di_none;
  }

  if(di_wlan_auth_last == di_none) di_wlan_auth_last = di;

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
  int rc = 1;

  di_wlan_auth_last = di;

  switch(di) {
    case di_wlan_open:
      config.net.wlan.auth = wa_open;

      rc = 0;
      break;

    case di_wlan_wpa_psk:
      config.net.wlan.auth = wa_wpa_psk;

      if(config.manual || !config.net.wlan.wpa_psk) {
        if(dia_input2("WPA Key", &config.net.wlan.wpa_psk, 30, 0)) {
          rc = -1;
          break;
        }

        if(!config.net.wlan.wpa_psk || strlen(config.net.wlan.wpa_psk) < 8) {
          dia_message("Key is too short (at least 8 characters).", MSGTYPE_ERROR);
          rc = -1;
          break;
        }
      }

      rc = 0;
      break;

    case di_wlan_wpa_peap:
      config.net.wlan.auth = wa_wpa_peap;

      if(config.manual || !config.net.wlan.wpa_identity) {
        if(dia_input2("WPA Identity", &config.net.wlan.wpa_identity, 30, 0)) {
          rc = -1;
          break;
        }

        if(dia_input2("WPA Password", &config.net.wlan.wpa_password, 30, 0)) {
          rc = -1;
          break;
        }
      }

      rc = 0;
      break;

    default:
      break;
  }

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
 * Setup all interfaces in config.ifcfg.list.
 *
 * For flags see ifcfg_write().
 *
 * Note: Clears config.ifcfg.current!
 */
void net_update_ifcfg(int flags)
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

    if(ifcfg->device) {
      ifcfg_write(ifcfg->device, ifcfg, flags | IFCFG_INITIAL);
    }
    else {
      /*
       * If there's no device specified we just remember the settings.
       * So e.g. ifcfg=1.2.3.4/12,1.1.1.1 just becomes equivalent to
       * hostip=1.2.3.4/12 gateway=1.1.1.1.
       */
      ifcfg_copy(config.ifcfg.manual, ifcfg);
      ifcfg->used = 1;
    }
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

      if(matched) ifcfg_write(hd->unix_dev_name, ifcfg, flags | IFCFG_INITIAL);
    }
  }

  hd_free_hd_list(net_list);

  // append to config.ifcfg.all and clear list
  for(ifcfg_p = &config.ifcfg.all; *ifcfg_p; ifcfg_p = &(*ifcfg_p)->next);
  *ifcfg_p = config.ifcfg.list;
  config.ifcfg.list = NULL;

  str_copy(&config.ifcfg.current, NULL);
}


/*
 * Write ifcfg/ifroute files for device.
 *
 * May be a dhcp or static config.
 *
 * Wrapper around _ifcfg_write() that does some more logging.
 *
 * Note: 'device' is the interface name not including any vlan id.
 *
 * flags:
 *   - IFCFG_INITIAL: mark interface as 'initial'; that is, no further auto
 *     config is tried on it
 *   - IFCFG_IFUP: bring interface up immediately; else a separate call to
 *     net_wicked_up() is necessary
 */
int ifcfg_write(char *device, ifcfg_t *ifcfg, int flags)
{
  char *ifname = NULL;
  int i;

  str_copy(&config.ifcfg.current, NULL);

  str_copy(&ifname, device);

  if(ifcfg) {
    ifcfg->used = 1;
    if(ifcfg->vlan) strprintf(&ifname, "%s.%s", device, ifcfg->vlan);
  }

  // if a device spec is a wildcard patterm, don't allow to overwrite an existing config
  if(slist_getentry(config.ifcfg.initial, ifname)) {
    if(ifcfg && ifcfg->pattern) {
      log_info("%s: network config exists, keeping it\n", ifname);
      str_copy(&config.ifcfg.current, ifname);
      return 1;
    }
    else {
      log_info("%s: network config exists, overwriting it\n", ifname);
    }
  }

  i = _ifcfg_write(device, ifcfg);

  if(i) {
    str_copy(&config.ifcfg.current, ifname);
    if(flags & IFCFG_INITIAL) slist_append_str(&config.ifcfg.initial, ifname);
    log_show_maybe(!config.win, "%s: network config created\n", ifname);
    if(flags & IFCFG_IFUP) net_wicked_up(ifname);
  }
  else {
    log_show_maybe(!config.win, "%s: failed to create network config\n", ifname);
  }

  return i;
}


/*
 * Write ifcfg/ifroute files for device.
 *
 * May be a dhcp or static config.
 *
 * Note1: use ifcfg_write() instead!
 *
 * Note2: 'device' is the verbatim interface name as used in ifcfg-* or
 * ifroute-* (including vlan id) while ifcfg->device may even be a wildcard
 * pattern.
 */
int _ifcfg_write(char *device, ifcfg_t *ifcfg)
{
  char *fname;
  FILE *fp;
  char *gw = NULL;	// allocated
  char *ns = NULL;	// allocated
  char *domain = NULL;	// allocated
  char *vlan = NULL;	// allocated
  int is_dhcp = 0, ok = 0;
  slist_t *sl, *sl2;
  slist_t *sl_ifcfg = NULL;
  slist_t *sl_ifroute = NULL;
  slist_t *sl_global = NULL;
  slist_t *sl_dhcp = NULL;
  unsigned ptp = 0;
  char *v4_ip = NULL;	// allocated
  unsigned v4_prefix = 0;

  // obsolete: use global values
  if(!device || !ifcfg) {
    log_show("\n\nXXX  Old net config NOT SUPPORTED!  XXX\n\n");

    sleep(60);

    return ok;
  }

  log_info("ifcfg_write: device = %s, ifcfg = %s\n", device, ifcfg ? ifcfg->device : "");

  ptp = check_ptp(device);

  if(ptp) log_info("check_ptp: ptp = %u\n", ptp);

  ptp |= ifcfg->ptp;

  // set wicked timeout
  if(config.net.dhcp_timeout_set) {
    sl = slist_append_str(&sl_global, "WAIT_FOR_INTERFACES");
    strprintf(&sl->value, "%d", config.net.dhcp_timeout);
  }

  // 1. maybe dhcp config

  if(ifcfg->dhcp) {
    is_dhcp = 1;

    sl = slist_append_str(&sl_ifcfg, "BOOTPROTO");
    if(ifcfg->type) {
      str_copy(&sl->value, ifcfg->type);
    }
    else {
      strprintf(&sl->value, "dhcp%s", net_dhcp_type());
    }
    if(ifcfg->vlan) {
      strprintf(&vlan, ".%s", ifcfg->vlan);
      sl = slist_append_str(&sl_ifcfg, "ETHERDEVICE");
      str_copy(&sl->value, device);
    }
    if(ifcfg->rfc2132) {
      sl = slist_append_str(&sl_ifcfg, "DHCLIENT_CREATE_CID");
      str_copy(&sl->value, "rfc2132");
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

        // remember ip and net prefix for later use
        str_copy(&v4_ip, sl0->key);
        v4_prefix = ifcfg->netmask_prefix;
        char *t = strchr(v4_ip, '/');
        if(t) {
          *t = 0;
          v4_prefix = atoi(t + 1);
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
  }

  // handle additional flags and put them either into 'ifcfg-*' or 'config'

  for(sl = ifcfg->flags; sl; sl = sl->next) {
    if(slist_getentry(config.ifcfg.to_global, sl->key)) {
      if(!(sl2 = slist_getentry(sl_global, sl->key))) sl2 = slist_append(&sl_global, slist_new());
    }
    else {
      if(!(sl2 = slist_getentry(sl_ifcfg, sl->key))) sl2 = slist_append(&sl_ifcfg, slist_new());
    }
    str_copy(&sl2->key, sl->key);
    str_copy(&sl2->value, sl->value);
  }

  // set hostname, if requested
  if(config.net.sethostname) {
    slist_setentry(&sl_dhcp, "DHCLIENT_SET_HOSTNAME", "yes", 0);
  }

  // add wlan options, if necessary
  if(util_is_wlan(device)) {
    // ask for missing wifi parameters; abort of user pressed ESC
    if(wlan_setup()) goto err;

    if(config.net.wlan.essid) {
      slist_setentry(&sl_ifcfg, "WIRELESS_ESSID", config.net.wlan.essid, 0);
    }

    switch(config.net.wlan.auth) {
      case wa_open:
        slist_setentry(&sl_ifcfg, "WIRELESS_AUTH_MODE", "open", 0);
        break;

      case wa_wpa_psk:
        slist_setentry(&sl_ifcfg, "WIRELESS_AUTH_MODE", "psk", 0);
        slist_setentry(&sl_ifcfg, "WIRELESS_WPA_PSK", config.net.wlan.wpa_psk, 0);
        break;

      case wa_wpa_peap:
        slist_setentry(&sl_ifcfg, "WIRELESS_AUTH_MODE", "eap", 0);
        slist_setentry(&sl_ifcfg, "WIRELESS_EAP_MODE", "PEAP", 0);
        // seems not to be necessary
        // slist_setentry(&sl_ifcfg, "WIRELESS_EAP_AUTH", "mschapv2", 0);
        slist_setentry(&sl_ifcfg, "WIRELESS_WPA_IDENTITY", config.net.wlan.wpa_identity, 0);
        slist_setentry(&sl_ifcfg, "WIRELESS_WPA_PASSWORD", config.net.wlan.wpa_password, 0);
        break;

      default:
        break;
    }
  }

#if defined(__s390__) || defined(__s390x__)
  // s390 layer2 interfaces
  if(config.hwp.layer2 == LAYER2_YES && config.hwp.osahwaddr) {
    char *sysfs_string = NULL;
    char *card_type;

    strprintf(&sysfs_string, "/sys/bus/ccwgroup/devices/%s/card_type", config.hwp.readchan);
    card_type = util_get_attr(sysfs_string);
    // exclude virtual NICs (bnc #887238)
    if(strncmp(card_type, "Virt.NIC", sizeof "Virt.NIC" - 1)) {
      sl = slist_append_str(&sl_ifcfg, "LLADDR");
      str_copy(&sl->value, config.hwp.osahwaddr);
    }
    str_copy(&sysfs_string, NULL);
  }
#endif

  if(sl_ifcfg) {
    if(asprintf(&fname, "/etc/sysconfig/network/ifcfg-%s%s", device, vlan ?: "") == -1) fname = NULL;
    log_info("creating ifcfg-%s%s:\n", device, vlan ?: "");
    if(fname && (fp = fopen(fname, "w"))) {
      for(sl = sl_ifcfg; sl; sl = sl->next) {
        fprintf(fp, "%s='%s'\n", sl->key, sl->value);
      }

      fclose(fp);
    }

    for(sl = sl_ifcfg; sl; sl = sl->next) {
      log_info("  %s='%s'\n", sl->key, sl->value);
    }

    free(fname);
  }

  // 3. create ifroute entries

  if(!is_dhcp) {
    if(gw) {
      slist_t *sl1, *sl0 = slist_split(' ', ifcfg->gw);

      for(sl1 = sl0; sl1; sl1 = sl1->next) {
        sl = slist_append(&sl_ifroute, slist_new());

        // set explicit route to gw unless gw is in the same ipv4 subnet
        // note: we might as well set it always
        if(!compare_subnet(v4_ip, sl1->key, v4_prefix)) {
          strprintf(&sl->key, "%s - - %s%s", sl1->key, device, vlan ?: "");
          sl = slist_append(&sl_ifroute, slist_new());
        }

        strprintf(&sl->key, "default %s - %s%s", sl1->key, device, vlan ?: "");
      }

      slist_free(sl0);
    }
  }

  if(sl_ifroute) {
    if(asprintf(&fname, "/etc/sysconfig/network/ifroute-%s%s", device, vlan ?: "") == -1) fname = NULL;

    log_info("creating ifroute-%s%s:\n", device, vlan ?: "");
    if(fname && (fp = fopen(fname, "w"))) {
      for(sl = sl_ifroute; sl; sl = sl->next) {
        fprintf(fp, "%s\n", sl->key);
      }

      fclose(fp);
    }

    for(sl = sl_ifroute; sl; sl = sl->next) {
      log_info("  %s\n", sl->key);
    }

    free(fname);
  }

  // 4. set nameserver and search list

  if(!is_dhcp) {
    // if user has set NETCONFIG_* via flags, keep it

    if(ns && !slist_getentry(sl_global, "NETCONFIG_DNS_STATIC_SERVERS")) {
       sl = slist_append_str(&sl_global, "NETCONFIG_DNS_STATIC_SERVERS");
       str_copy(&sl->value, ns);
    }

    if(domain && !slist_getentry(sl_global, "NETCONFIG_DNS_STATIC_SEARCHLIST")) {
       sl = slist_append_str(&sl_global, "NETCONFIG_DNS_STATIC_SEARCHLIST");
       str_copy(&sl->value, domain);
    }
  }

  // 5. update global network config
  if(sl_global) {
    update_sysconfig(sl_global, "/etc/sysconfig/network/config");
  }

  // 6. update global DHCP config
  if(sl_dhcp) {
    update_sysconfig(sl_dhcp, "/etc/sysconfig/network/dhcp");
  }

  ok = 1;

err:

  str_copy(&gw, NULL);
  str_copy(&ns, NULL);
  str_copy(&domain, NULL);
  str_copy(&vlan, NULL);
  str_copy(&v4_ip, NULL);

  slist_free(sl_dhcp);
  slist_free(sl_global);
  slist_free(sl_ifcfg);
  slist_free(sl_ifroute);

  return ok;
}

// Update the sysconfig file *filename* with the data in *slist*.
//
// *slist* is mutated by appending "=" to the keys.
// A temporary file (*filename*.tmp) is used.
void update_sysconfig(slist_t *slist, char *filename) {
  slist_t *sl;
  FILE *fp, *fp2 = NULL;

  log_info("adjusting %s:\n", filename);

  // it's easier below if we append the '=' to the keys
  for(sl = slist; sl; sl = sl->next) {
    strprintf(&sl->key, "%s=", sl->key);
  }

  if((fp = fopen(filename, "r"))) {
    char buf[4096];
    char *filename_tmp;

    if (asprintf(&filename_tmp, "%s.tmp", filename) == -1) {
      filename_tmp = NULL;
    }
    if (filename_tmp) {
      // we allow open to fail and check fp2 for NULL later
      fp2 = fopen(filename_tmp, "w");
    }

    while(fgets(buf, sizeof buf, fp)) {
      if(*buf && *buf != '#' && !isspace(*buf)) {
        for(sl = slist; sl; sl = sl->next) {
          if(!strncmp(buf, sl->key, strlen(sl->key))) {
            log_info("  %s\"%s\"\n", sl->key, sl->value);
            if(fp2) fprintf(fp2, "%s\"%s\"\n", sl->key, sl->value);
            *buf = 0;
            break;
          }
        }
      }
      if(*buf && fp2) fputs(buf, fp2);
    }

    fclose(fp);

    if(fp2) {
      fclose(fp2);
      rename(filename_tmp, filename);
    }
    else {
      log_info("warning: %s not updated\n", filename);
    }

    free(filename_tmp);
  }
}


/*
 * Parse ifcfg option.
 *
 * Examples:
 *   ifcfg=10.10.0.1/24,10.10.0.254,10.10.1.1 10.10.1.2,suse.de[,XXX_OPTION=foo]
 *   ifcfg=dhcp
 *   ifcfg=eth*=10.10.0.1/24,10.10.0.254,10.10.1.1 10.10.1.2,suse.de[,XXX_OPTION=foo]
 *   ifcfg=eth*=dhcp
 */
ifcfg_t *ifcfg_parse(char *str)
{
  slist_t *sl, *sl0, *slx;
  ifcfg_t *ifcfg;
  char *s, *t;
  /*
   * optional keywords "dhcp", "dhcp4", "dhcp6", "try", "rfc2132" move the
   * position of following params
   */
  int option_shift = 0;

  if(!str) return NULL;

  log_debug("parsing ifcfg: %s\n", str);

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

  // parse optional keywords (in any order)
  while((s = slist_key(sl0, 1 + option_shift))) {
    if(!strcmp(s, "dhcp") || !strcmp(s, "dhcp4") || !strcmp(s, "dhcp6")) {
      str_copy(&ifcfg->type, s);
      ifcfg->dhcp = 1;

      option_shift++;
      continue;
    }

    if(!strcmp(s, "try")) {
      log_debug("Will try to detect interface with access to installation");
      ifcfg->search = 1;

      option_shift++;
      continue;
    }

    if(!strcmp(s, "rfc2132")) {
      ifcfg->rfc2132 = 1;

      option_shift++;
      continue;
    }

    break;
  }

  // if not dhcp, get static config options
  if(!ifcfg->dhcp) {
    str_copy(&ifcfg->type, "static");

    t = NULL;

    if(s && *s && !(t = strchr(s, '='))) str_copy(&ifcfg->ip, s);

    s = slist_key(sl0, 2 + option_shift);
    if(!t && s && *s && !(t = strchr(s, '='))) str_copy(&ifcfg->gw, s);

    s = slist_key(sl0, 3 + option_shift);
    if(!t && s && *s && !(t = strchr(s, '='))) str_copy(&ifcfg->ns, s);

    s = slist_key(sl0, 4 + option_shift);
    if(!t && s && *s && !(t = strchr(s, '='))) str_copy(&ifcfg->domain, s);
  }

  // get anything in the form of FOO=bar to be put directly into ifcfg file
  for(sl = sl0->next; sl; sl = sl->next) {
    if((t = strchr(sl->key, '='))) {
      *t++ = 0;
      slx = slist_append(&ifcfg->flags, slist_new());
      str_copy(&slx->key, sl->key);
      str_copy(&slx->value, t);
    }
  }

  slist_free(sl0);

  // decide if device spec refers to a device name or a pattern to match
  if(ifcfg->device) {
    for(s = ifcfg->device; *s; s++) {
      if(!isalnum(*s) && *s != '_') {
        ifcfg->pattern = 1;
        break;
      }
    }
  }

  log_debug("%s", ifcfg_print(ifcfg));

  return ifcfg;
}


/*
 * Make a (deep) copy of an ifcfg_t struct.
 */
void ifcfg_copy(ifcfg_t *dst, ifcfg_t *src)
{
  if(!dst || !src) return;

  str_copy(&dst->device, src->device);
  str_copy(&dst->type, src->type);

  dst->dhcp = src->dhcp;
  dst->used = src->used;
  dst->pattern = src->pattern;
  dst->ptp = src->ptp;
  dst->search = src->search;
  dst->netmask_prefix = src->netmask_prefix;

  str_copy(&dst->vlan, src->vlan);
  str_copy(&dst->ip, src->ip);
  str_copy(&dst->gw, src->gw);
  str_copy(&dst->ns, src->ns);
  str_copy(&dst->domain, src->domain);
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
    "%s  dhcp = %u, pattern = %u, used = %u, prefix = %d, ptp = %u, search = %u, rfc2132 = %u\n",
    buf,
    ifcfg->dhcp, ifcfg->pattern, ifcfg->used,
    ifcfg->netmask_prefix, ifcfg->ptp, ifcfg->search, ifcfg->rfc2132
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

  if((f = popen("wicked show all 2>/dev/null", "r"))) {
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

  log_debug("net_update_state: ");
  for(sl = config.ifcfg.if_state; sl; sl = sl->next) {
    log_debug("%s: %s%s", sl->key, sl->value, sl->next ? ", " : "");
  }
  log_debug("\n");
}


/*
 * Set up interface; ifname may be NULL, an interface or 'all'.
 */
void net_wicked_up(char *ifname)
{
  char *buf = NULL;

  if(!ifname) return;

  log_debug("wicked ifup %s\n", ifname);

  if(config.net.dhcp_timeout_set) {
    strprintf(&buf, "wicked ifup --timeout %d %s", config.net.dhcp_timeout, ifname);
  }
  else {
    strprintf(&buf, "wicked ifup %s", ifname);
  }

  if(!config.test) {
    lxrc_run(buf);
  }
  else {
    log_info("test mode: 'wicked ifup %s' called\n", ifname);
  }

  sleep(config.net.ifup_wait + 1);

  LXRC_WAIT

  net_update_state();

  str_copy(&buf, NULL);
}


/*
 * Shut down interface; ifname may be NULL, an interface or 'all'.
 */
void net_wicked_down(char *ifname)
{
  char *buf = NULL, *s;

  if(!ifname) return;

  log_debug("wicked ifdown %s\n", ifname);

  strprintf(&buf, "wicked ifdown %s", ifname);

  if(!config.test) lxrc_run(buf);

  sleep(1);

  LXRC_WAIT

  net_update_state();

  str_copy(&buf, ifname);

  /*
   * In case we were just taking down a vlan tagged interface, shutdown the
   * untagged interface as well, unless there's an explicit config file for
   * it (it was brought up by wicked implicitly).
   */
  if((s = strchr(buf, '.'))) {
    *s = 0;
    if(!util_check_exist2("/etc/sysconfig/network/ifcfg-", buf)) {
      net_wicked_down(buf);
    }
  }

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

  log_debug("netmask -> prefix: %s -> %d\n", netmask, prefix);

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
 * Check if interface is a ptp interface and set config.ifcfg.manual->ptp accordingly.
 */
unsigned check_ptp(char *ifname)
{
  unsigned ptp = 0;

  if(
    ifname &&
    (
      !strncmp(ifname, "plip", sizeof "plip" - 1) ||
      !strncmp(ifname, "iucv", sizeof "iucv" - 1) ||
      !strncmp(ifname, "ctc", sizeof "ctc" - 1) ||
      !strncmp(ifname, "sl", sizeof "sl" - 1)
    )
  ) ptp = 1;

  config.ifcfg.manual->ptp = ptp;

  return ptp;
}


/*
 * Check if buf is a valid ip address.
 *
 * If with_prefix is set, require '/prefix_bits' appended.
 * If multi is set, buf may be a space-separated list of ip addresses.
 *
 * Returns 1 for ok and 0 for not ok.
 */
int net_check_ip(char *buf, int multi, int with_prefix)
{
  int ok = 1;
  struct in_addr ip4;
  struct in6_addr ip6;
  slist_t *sl, *sl0;
  char *s;

  sl0 = slist_split(' ', buf);

  if(!sl0 || !*sl0->key || (sl0->next && !multi)) {
    slist_free(sl0);

    log_debug("check_ip: %s = wrong\n", buf);

    return 0;
  }

  for(sl = sl0; sl; sl = sl->next) {
    s = strchr(sl->key, '/');
    if(s) *s = 0;
    if((s && !with_prefix) || (!s && with_prefix)) {
      ok = 0;
      break;
    }
  }

  if(ok) {
    for(sl = sl0; sl; sl = sl->next) {
      if(strchr(sl->key, ':')) {
        if(!config.net.ipv6 || inet_pton(AF_INET6, sl->key, &ip6) <= 0) {
          ok = 0;
          break;
        }
      }
      else {
        if(!config.net.ipv4 || inet_pton(AF_INET, sl->key, &ip4) <= 0) {
          ok = 0;
          break;
        }
      }
    }
  }

  slist_free(sl0);

  log_debug("check_ip: %s = %s\n", buf, ok ? "ok" : "wrong");

  return ok;
}


/*
 * Read network config template and remember keys mentioned there.
 *
 * This list is used to decide whether to put network config options into
 * per interface files ifcfg-* or the global .../config.
 */
void net_wicked_get_config_keys()
{
  file_t *f0, *f1, *f;
  slist_t *sl0 = NULL;

  f0 = file_read_file("/etc/sysconfig/network/ifcfg.template", kf_none);
  f1 = file_read_file("/etc/sysconfig/network/config", kf_none);

  for(f = f0; f; f = f->next) {
    if(*f->key_str && *f->key_str != '#') slist_append_str(&sl0, f->key_str);
  }

  /*
   * There are keys that can go either into ifcfg-* or config.
   * We go for ifcfg-* in these cases.
   */
  for(f = f1; f; f = f->next) {
    if(
      *f->key_str &&
      *f->key_str != '#' &&
      !slist_getentry(sl0, f->key_str)
    ) {
      slist_append_str(&config.ifcfg.to_global, f->key_str);
    }
  }

  slist_free(sl0);

  file_free_file(f1);
  file_free_file(f0);
}


/*
 * Enable/disable wickedd-nanny according to config.nanny.
 */
void net_nanny()
{
  FILE *fp, *fp2;

  // keep the default unless explicitly changed
  if(!config.nanny_set) return;

  if((fp = fopen("/etc/wicked/common.xml", "r"))) {
    char buf[4096];

    // we allow open to fail and check fp2 for NULL later
    fp2 = fopen("/etc/wicked/common.xml.tmp", "w");

    while(fgets(buf, sizeof buf, fp)) {
      if(strstr(buf, "<use-nanny>") && strstr(buf, "</use-nanny>")) {
        log_debug("wickedd-nanny: %s\n", config.nanny ? "enabled" : "disabled");
        if(fp2) fprintf(fp2, "  <use-nanny>%s</use-nanny>\n", config.nanny ? "true" : "false");
        *buf = 0;
      }
      if(*buf && fp2) fputs(buf, fp2);
    }

    fclose(fp);

    if(fp2) {
      fclose(fp2);
      rename("/etc/wicked/common.xml.tmp", "/etc/wicked/common.xml");
    }
    else {
      log_info("warning: /etc/wicked/common.xml not updated\n");
    }
  }
}


/*
 * Check whether ip1 and ip2 share the same ipv4 subnet.
 */
int compare_subnet(char *ip1, char *ip2, unsigned prefix)
{
  struct in_addr ip4_1, ip4_2;
  uint32_t mask;
  int ok = 0;

  if(prefix > 32 || !ip1 || !ip2) return 0;

  // no ipv6
  if(strchr(ip1, ':') || strchr(ip2, ':')) return 0;

  mask = htonl(prefix ? -1 << (32 - prefix) : 0);

  if(
    inet_pton(AF_INET, ip1, &ip4_1) > 0 &&
    inet_pton(AF_INET, ip2, &ip4_2) > 0
  ) {
    ok = (ip4_1.s_addr & mask) == (ip4_2.s_addr & mask);
  }

  return ok;
}


/*
 * If there's an 'ifcfg' boot option matching 'device', copy any flags
 * specified there.
 *
 * This is used to supply config.ifcfg.manual with ifcfg flags.
 */
void get_and_copy_ifcfg_flags(ifcfg_t *ifcfg, char *device)
{
  ifcfg_t *tmp;

  if(!ifcfg || !device) return;

  ifcfg->flags = slist_free(ifcfg->flags);

  if(config.debug >= 2) log_debug("ifcfg flags, before(%s):\n%s", device, ifcfg_print(ifcfg));

  // 1st try, direct match
  for(tmp = config.ifcfg.all; tmp; tmp = tmp->next) {
    if(tmp->pattern || !tmp->device) continue;
    if(config.debug >= 2) log_debug("direct:\n%s", ifcfg_print(tmp));
    if(!strcmp(tmp->device, device)) break;
  }

  // 2nd try, tmp->device contains pattern or mac addr
  if(!tmp) {
    char *mac = interface_to_mac(device);

    for(tmp = config.ifcfg.all; tmp; tmp = tmp->next) {
      if(!tmp->pattern || !tmp->device) continue;
      if(config.debug >= 2) log_debug("pattern:\n%s", ifcfg_print(tmp));
      if(match_netdevice(device, mac, tmp->device)) break;
    }

    free(mac);
  }

  // match, copy flags
  if(tmp) {
    slist_t *sl;

    for(sl = tmp->flags; sl; sl = sl->next) {
      slist_t *sl1 = slist_append_str(&ifcfg->flags, sl->key);
      str_copy(&sl1->value, sl->value);
    }

    if(config.debug >= 2) log_debug("ifcfg flags, matched:\n%s", ifcfg_print(ifcfg));
  }
  else {
    if(config.debug >= 2) log_debug("ifcfg flags, no match\n");
  }
}


/*
 * Convenience function: provide network interface name in "INTERFACE.VLANID" form.
 *
 * Returns the string in a static buffer;
 */
char *net_get_ifname(ifcfg_t *ifcfg)
{
  static char *buf = NULL;

  if(!ifcfg || !ifcfg->device) return NULL;

  str_copy(&buf, ifcfg->device);

  if(ifcfg->vlan) strprintf(&buf, "%s.%s", buf, ifcfg->vlan);

  return buf;
}

