/*
 *
 * file.c        File access
 *
 * Copyright (c) 1996-2002  Hubert Mantel, SuSE Linux AG  (mantel@suse.de)
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <sys/mount.h>
#include <arpa/inet.h>

#include "global.h"
#include "file.h"
#include "text.h"
#include "util.h"
#include "module.h"
#include "window.h"
#include "dialog.h"
#include "net.h"
#include "settings.h"
#include "auto2.h"
#include "rootimage.h"
#include "display.h"
#include "fstype.h"

#define YAST_INF_FILE		"/etc/yast.inf"
#define INSTALL_INF_FILE	"/etc/install.inf"
#define MTAB_FILE		"/etc/mtab"
#define CMDLINE_FILE		"/proc/cmdline"

// #define DEBUG_FILE

static char *file_key2str(file_key_t key);
static file_key_t file_str2key(char *value);
static int sym2index(char *sym);
static void parse_value(file_t *ft);

void file_write_modparms(FILE *f);
static void file_module_load (char *insmod_arg);
#ifdef DEBUG_FILE
static void file_dump_flist(file_t *ft);
static void file_dump_mlist(module_t *ml);
#endif

static void file_write_inet(FILE *f, file_key_t key, inet_t *inet);

static struct {
  file_key_t key;
  char *value;
} keywords[] = {
  { key_none,           ""                 },
  { key_swap,           "Swap"             },
  { key_root,           "Root"             },
  { key_live,           "Live"             },
  { key_keytable,       "Keytable"         },
  { key_language,       "Language"         },
  { key_rebootmsg,      "RebootMsg"        },
  { key_insmod,         "Insmod"           },
  { key_autoprobe,      "Autoprobe"        },
  { key_start_pcmcia,   "StartPCMCIA"      },
  { key_display,        "Display"          },
  { key_bootmode,       "Bootmode"         },
  { key_ip,             "IP"               },
  { key_netmask,        "Netmask"          },
  { key_gateway,        "Gateway"          },
  { key_server,         "Server"           },
  { key_nameserver,     "Nameserver"       },
  { key_broadcast,      "Broadcast"        },
  { key_network,        "Network"          },
  { key_partition,      "Partition"        },
  { key_serverdir,      "Serverdir"        },
  { key_fstype,         "Fstyp"            },
  { key_netdevice,      "Netdevice"        },
  { key_livesrc,        "LiveSRC"          },
  { key_bootpwait,      "Bootpwait"        },
  { key_bootptimeout,   "BOOTPTimeout"     },
  { key_forcerootimage, "ForceRootimage"   },
  { key_rebootwait,     "WaitReboot"       },
  { key_sourcemounted,  "Sourcemounted"    },
  { key_cdrom,          "Cdrom"            },
  { key_pcmcia,         "PCMCIA"           },
  { key_haspcmcia,      "HasPCMCIA"        },
  { key_console,        "Console"          },
  { key_pliphost,       "PLIPHost"         },
  { key_domain,         "Domain"           },
  { key_ftpuser,        "FTPUser"          },
  { key_ftpproxy,       "FTPProxy"         },
  { key_ftpproxyport,   "FTPProxyport"     },
  { key_manual,         "Manual"           },
  { key_demo,           "Demo"             },
  { key_reboot,         "Reboot"           },
  { key_floppydisk,     "Floppydisk"       },
  { key_keyboard,       "Keyboard"         },
  { key_yast2update,    "YaST2update"      },
  { key_yast2serial,    "YaST2serial"      },
  { key_textmode,       "Textmode"         },
  { key_yast2color,     "YaST2color"       },
  { key_bootdisk,       "BootDisk"         },
  { key_disks,          "Disks"            },
  { key_username,       "Username"         },
  { key_password,       "Password"         },
  { key_workdomain,     "WorkDomain"       },
  { key_alias,          "Alias"            },
  { key_options,        "Options"          },
  { key_initrdmodules,  "InitrdModules"    },
  { key_locale,         "Locale"           },
  { key_font,           "Font"             },
  { key_screenmap,      "Screenmap"        },
  { key_fontmagic,      "Fontmagic"        },
  { key_autoyast,       "AutoYaST"         },
  { key_linuxrc,        "linuxrc"          },
  { key_forceinsmod,    "ForceInsmod"      },
  { key_dhcp,           "DHCP"             },
  { key_ipaddr,         "IPAddr"           },
  { key_hostname,       "Hostname"         },
  { key_nisdomain,      "NISDomain"        },
  { key_nisservers,     "NISServers"       },
  { key_dns,            "DNS"              },
  { key_nptservers,     "NTPServers"       },
  { key_dhcpsid,        "DHCPSID"          },
  { key_dhcpgiaddr,     "DHCPGIAddr"       },
  { key_dhcpsiaddr,     "DHCPSIAddr"       },
  { key_dhcpchaddr,     "DHCPCHAddr"       },
  { key_dhcpshaddr,     "DHCPSHAddr"       },
  { key_dhcpsname,      "DHCPSName"        },
  { key_rootpath,       "RootPath"         },
  { key_bootfile,       "BootFile"         },
  { key_install,        "Install"          },
  { key_instmode,       "InstMode"         },
  { key_memtotal,       "MemTotal"         },
  { key_memfree,        "MemFree"          },
  { key_buffers,        "Buffers"          },
  { key_cached,         "Cached"           },
  { key_swaptotal,      "SwapTotal"        },
  { key_swapfree,       "SwapFree"         },
  { key_memlimit,       "MemLimit"         },
  { key_memyast,        "MemYaST"          },
  { key_memyasttext,    "MemYaSTText"      },
  { key_memmodules,     "MemModules"       },
  { key_memloadimage,   "MemLoadImage"     },
  { key_info,           "Info"             },
  { key_proxy,          "Proxy"            },
  { key_proxyport,      "ProxyPort"        },
  { key_proxyproto,     "ProxyProto"       },
  { key_usedhcp,        "UseDHCP"          },
  { key_nfsport,        "NFSPort"          },
  { key_dhcptimeout,    "DHCPTimeout"      },
  { key_tftptimeout,    "TFTPTimeout"      },
  { key_tmpfs,          "_TmpFS"           },
  { key_netstop,        "_NetStop"         },
  { key_testmode,       "_TestMode"        },
  { key_debugwait,      "_DebugWait"       },
  { key_auto,           "_Auto"            },
  { key_expert,         "Expert"           },
  { key_rescue,         "Rescue"           },
  { key_rootimage,      "RootImage"        },
  { key_rescueimage,    "RescueImage"      },
  { key_installdir,     "InstallDir"       },
  { key_nopcmcia,       "NoPCMCIA"         },
  { key_vnc,            "VNC"              },
  { key_usessh,         "UseSSH"           },
  { key_vncpassword,    "VNCPassword"      },
  { key_sshpassword,    "SSHPassword"      },
  { key_usepivotroot,   "UsePivotRoot"     },
  { key_term,           "TERM"             },
  { key_addswap,        "AddSwap"          },
  { key_fullnetsetup,   "FullNetSetup"     },
  { key_aborted,        "Aborted"          },
  { key_exec,           "Exec"             },
  { key_usbwait,        "USBWait"          },
  { key_nfsrsize,       "NFS.RSize"        },
  { key_nfswsize,       "NFS.WSize"        },
  { key_hwcheck,        "HWCheck"          },
  { key_setupcmd,       "SetupCmd"         },
  { key_setupnetif,     "SetupNetIF"       },
  { key_netconfig,      "NetConfig"        },
  { key_noshell,        "NoShell"          },
  { key_memcheck,       "_MemCheck"        },
  { key_hwdetect,       "HWDetect"         },
  { key_floppydevice,   "FloppyDevice"     },
  { key_cdromdevice,    "CDROMDevice"      },
  { key_consoledevice,  "ConsoleDevice"    },
  { key_product,        "Product"          },
  { key_productdir,     "ProductDir"       },
  { key_linuxrcstderr,  "LinuxrcSTDERR"    },
  { key_comment,        "#"                },
  { key_kbdtimeout,     "KBDTimeout"       },
  { key_brokenmodules,  "BrokenModules"    },
  { key_testpivotroot,  "_TestPivotRoot"   },
  { key_scsibeforeusb,  "SCSIBeforeUSB"    },
  { key_hostip,         "HostIP"           },
  { key_linemode,       "Linemode"         },
  { key_moduledelay,    "ModuleDelay"      },
  { key_updatedir,      "UpdateDir"        },
  { key_usbscsi,        "USBSCSI"          },
  { key_useusbscsi,     "UseUSBSCSI"       },
  { key_lxrcdebug,      "LXRCDebug"        },
  { key_kernel_pcmcia,  "KernelPCMCIA"     }
};

static struct {
  char *name;
  int value;
} sym_constants[] = {
  { "n",         0                  },
  { "no",        0                  },
  { "y",         1                  },
  { "yes",       1                  },
  { "j",         1                  },	// keep for compatibility?
  { "default",   1                  },
  { "Undef",     0                  },
  { "Mono",      1                  },
  { "Color",     2                  },
  { "Alt"  ,     3                  },
  { "Reboot",    1                  },
  { "no scheme", inst_none          },
  { "file",      inst_file          },
  { "nfs",       inst_nfs           },
  { "ftp",       inst_ftp           },
  { "smb",       inst_smb           },
  { "http",      inst_http          },
  { "tftp",      inst_tftp          },
  { "cd",        inst_cdrom         },
  { "floppy",    inst_floppy        },
  { "hd",        inst_hd            },
  { "dvd",       inst_dvd           },
  { "cdwithnet", inst_cdwithnet     },
  { "net",       inst_net           },
  { "harddisk",  inst_hd            },
  { "cdrom",     inst_cdrom         }
};

char *file_key2str(file_key_t key)
{
  int i;

  for(i = 0; (unsigned) i < sizeof keywords / sizeof *keywords; i++) {
    if(keywords[i].key == key) {
      return keywords[i].value;
    }
  }

  return "";
}


/* !!! str is overwritten !!! */
file_key_t file_str2key(char *str)
{
  int i;
  char *s;

  if(!str || !*str) return key_none;

  /* remove all '-' and '_' */
  if(*str != '_') {
    for(i = 0, s = str; str[i]; i++) {
      if(str[i] != '_' && str[i] != '-') {
        *s++ = str[i];
      }
    }
    *s = 0;
  }

  if(!*str) return key_none;

  for(i = 0; (unsigned) i < sizeof keywords / sizeof *keywords; i++) {
    if(!strcasecmp(keywords[i].value, str)) {
      return keywords[i].key;
    }
  }

  return key_none;
}


int sym2index(char *sym)
{
  int i;

  for(i = 0; (unsigned) i < sizeof sym_constants / sizeof *sym_constants; i++) {
    if(!strcasecmp(sym_constants[i].name, sym)) return i;
  }

  return -1;
}


int file_sym2num(char *sym)
{
  int i;

  if((i = sym2index(sym)) < 0) return -1;

  return sym_constants[i].value;
}


char *file_num2sym(char *base_sym, int num)
{
  int i;

  i = sym2index(base_sym);

  if(i < 0 || num < 0 || (unsigned) i + num >= sizeof sym_constants / sizeof *sym_constants) {
    return NULL;
  }

  return sym_constants[i + num].name;
}


void parse_value(file_t *ft)
{
  char *s;
  int i;

  if(*ft->value) {
    i = strtol(ft->value, &s, 0);
    if(!*s) {
      ft->nvalue = i;
      ft->is.numeric = 1;
    }
    else {
      if((i = sym2index(ft->value)) >= 0) {
        ft->nvalue = sym_constants[i].value;
        ft->is.numeric = 1;
      }
    }
  }
}


file_t *file_read_file(char *name)
{
  FILE *f;
  char buf[1024];
  char *s, *t, *t1;
  file_t *ft0 = NULL, **ft = &ft0, *prev = NULL;

  if(!(f = fopen(name, "r"))) return NULL;

  while(fgets(buf, sizeof buf, f)) {
    for(s = buf; *s && isspace(*s); s++);
    t = s;
    strsep(&t, ":= \t\n");
    if(t) {
      while(*t && (*t == ':' || *t == '=' || isspace(*t))) t++;
      for(t1 = t + strlen(t); t1 > t;) {
        if(isspace(*--t1)) *t1 = 0; else break;
      }
    }
    else {
      t = "";
    }

    /* remove quotes */
    if(*t == '"') {
      t1 = t + strlen(t);
      if(t1 > t && t1[-1] == '"') {
        t++;
        t1[-1] = 0;
      }
    }

    if(*s) {
      *ft = calloc(1, sizeof **ft);

      (*ft)->key_str = strdup(s);
      (*ft)->key = file_str2key(s);	/* destroys s!!! */
      (*ft)->value = strdup(t);

      parse_value(*ft);

      (*ft)->prev = prev;
      prev = *ft;
      ft = &(*ft)->next;
    }
  }

  fclose(f);

  return ft0;
}


void file_free_file(file_t *file)
{
  file_t *next;

  for(; file; file = next) {
    next = file->next;
    if(file->key_str) free(file->key_str);
    if(file->value) free(file->value);
    free(file);
  }
}


int file_read_info()
{
  window_t win_ri;
  char *file = NULL, *s;
  char *info_file_old = NULL;

  if(config.win) {
    dia_info(&win_ri, txt_get(TXT_SEARCH_INFOFILE));
  }
  else {
    printf("%s...", txt_get(TXT_SEARCH_INFOFILE));
    fflush(stdout);
  }

  if(!config.info.file || !strcmp(config.info.file, "default")) {
    if(config.info.file || auto2_ig || auto_ig) {
      strprintf(&info_file_old, "floppy:/%s/setup/descr/info", config.product_dir);
      file = file_read_info_file(info_file_old, "floppy:/info");
      free(info_file_old);
      info_file_old = NULL;
    }
    if(!file) file = file_read_info_file("file:/info", NULL);
  }
  else {
    file = file_read_info_file(config.info.file, NULL);
  }

  if(config.info.add_cmdline) {
    s = file_read_info_file("cmdline", NULL);
    if(!file) file = s;
  }

  if(config.win) {
    win_close(&win_ri);
  }
  else {
    printf("\n");
  }

  if(file) {
    fprintf(stderr, "got info from %s\n", file);
  }

  config.info.loaded = strdup(file ?: "");

  return file ? 0 : 1;
}


char *file_read_info_file(char *file, char *file2)
{
  char filename[MAX_FILENAME];
  int i, mounted = 0;
  file_t *f0 = NULL;

#ifdef DEBUG_FILE
  fprintf(stderr, "looking for info file: %s\n", file);
#endif

  if(!strcmp(file, "cmdline")) {
    f0 = file_read_cmdline();
  }
  else if(!strncmp(file, "file:", 5)) {
    f0 = file_read_file(file + 5);
  }
  else if(!strncmp(file, "floppy:", 7)) {
    for(i = 0; i < config.floppies; i++) {
      if(!util_mount_ro(config.floppy_dev[i], mountpoint_tg)) break;
    }
    if(i < config.floppies) {
      config.floppy = i;	// remember currently used floppy
      mounted = 1;
      util_chk_driver_update(mountpoint_tg);
      sprintf(filename, "%s/%s", mountpoint_tg, file + 7);
      f0 = file_read_file(filename);
      if(!f0 && file2) {
        file = file2;
        sprintf(filename, "%s/%s", mountpoint_tg, file + 7);
        f0 = file_read_file(filename);
      }
    }
  }

  if(!f0) {
    if(mounted) umount(mountpoint_tg);
    return NULL;
  }

#ifdef DEBUG_FILE
  fprintf(stderr, "info file read from \"%s\":\n", file);
  file_dump_flist(f0);
#endif

  file_do_info(f0);

  file_free_file(f0);

  if(mounted) umount(mountpoint_tg);

//  if(config.info.mod_autoload) mod_autoload();

#if WITH_PCMCIA
//  if(config.info.start_pcmcia) pcmcia_load_core();
#endif

  return file;
}


void file_do_info(file_t *f0)
{
  file_t *f;
  url_t *url;
  int i, is_xml = 0;
  char buf[256], *s, *t;
  slist_t *sl;

  config.info.mod_autoload = 0;
  config.info.start_pcmcia = 0;

  /* maybe it's an AutoYaST XML file */
  for(f = f0; f; f = f->next) {
    if(f->key == key_comment && !strcmp(f->value, "start_linuxrc_conf")) {
      is_xml = 1;
      f0 = f->next;
      break;
    }
  }

  for(f = f0; f; f = f->next) {
    if(
      is_xml &&
      f->key == key_comment &&
      !strcmp(f->value, "end_linuxrc_conf")
    ) {
      break;
    }

    switch(f->key) {
      case key_insmod:
        file_module_load(f->value);
        break;

      case key_autoprobe:
        config.info.mod_autoload = 1;
        break;

      case key_start_pcmcia:
        config.info.start_pcmcia = 1;
        break;

      case key_language:
        i = set_langidbyname(f->value);
        if(i) config.language = i;
        break;

      case key_display:
        config.color = f->nvalue;
        if(config.color) disp_set_display();
        break;

      case key_keytable:
        str_copy(&config.keymap, *f->value ? f->value : NULL);
        break;

      case key_bootmode:
      case key_instmode:
        if(f->is.numeric) set_instmode(f->nvalue);
        break;

      case key_ip:
      case key_hostip:
        name2inet(&config.net.hostname, f->value);
        net_check_address2(&config.net.hostname, 0);
        if(config.net.hostname.ok && config.net.hostname.net.s_addr) {
          s_addr2inet(&config.net.netmask, config.net.hostname.net.s_addr);
        }
        break;

      case key_hostname:
        if(*f->value) str_copy(&config.net.realhostname, f->value);
        break;

      case key_netmask:
        name2inet(&config.net.netmask, f->value);
        net_check_address2(&config.net.netmask, 0);
        break;

      case key_gateway:
        name2inet(&config.net.gateway, f->value);
        net_check_address2(&config.net.gateway, 0);
        break;

      case key_server:
        name2inet(&config.net.server, f->value);
        net_check_address2(&config.net.server, 0);
        break;

      case key_nameserver:
        name2inet(&config.net.nameserver, f->value);
        net_check_address2(&config.net.nameserver, 0);
        break;

      case key_proxy:
      case key_ftpproxy:
        name2inet(&config.net.proxy, f->value);
        net_check_address2(&config.net.proxy, 0);
        break;

      case key_proxyport:
      case key_ftpproxyport:
        if(f->is.numeric) config.net.proxyport = f->nvalue;
        break;

      case key_proxyproto:
        if(f->is.numeric) config.net.proxyproto = f->nvalue;
        break;

      case key_partition:
        if(*f->value) str_copy(&config.partition, f->value);
        break;

      case key_serverdir:
        if(*f->value) str_copy(&config.serverdir, f->value);
        break;

      case key_netdevice:
        strncpy(netdevice_tg, f->value, sizeof netdevice_tg);
        netdevice_tg[sizeof netdevice_tg - 1] = 0;
        config.net.device_given = 1;
        break;

      case key_livesrc:
        strncpy(livesrc_tg, f->value, sizeof livesrc_tg);
        livesrc_tg[sizeof livesrc_tg - 1] = 0;
        if(config.serverdir) set_instmode(inst_nfs);
        break;

      case key_bootpwait:
        if(f->is.numeric) config.net.bootp_wait = f->nvalue;
        break;

      case key_bootptimeout:
        if(f->is.numeric) config.net.bootp_timeout = f->nvalue;
        break;

      case key_dhcptimeout:
        if(f->is.numeric) config.net.dhcp_timeout = f->nvalue;
        break;

      case key_tftptimeout:
        if(f->is.numeric) config.net.tftp_timeout = f->nvalue;
        break;

      case key_forcerootimage:
        force_ri_ig = f->nvalue;
        break;

      case key_rebootwait:
        reboot_wait_ig = f->nvalue;
        break;

      case key_textmode:
        config.textmode = f->nvalue;
        break;

      case key_username:
      case key_ftpuser:
        str_copy(&config.net.user, *f->value ? f->value : NULL);
        break;

      case key_password:
        str_copy(&config.net.password, *f->value ? f->value : NULL);
        break;

      case key_workdomain:
        str_copy(&config.net.workgroup, f->value);
        break;

      case key_forceinsmod:
        config.forceinsmod = f->nvalue;
        break;

      case key_dhcp:
        config.net.use_dhcp = f->is.numeric ? f->nvalue : 1;
        if(config.net.use_dhcp) net_config();
        break;

      case key_usedhcp:
        if(f->is.numeric) {
          config.net.use_dhcp = f->nvalue;
        }
        else {
          if(!*f->value) config.net.use_dhcp = 1;
        }
        break;

      case key_nfsport:
        if(f->is.numeric) config.net.nfs_port = f->nvalue;
        break;

      case key_memlimit:
        if(f->is.numeric) config.memory.min_free = f->nvalue;
        break;

      case key_memyast:
        if(f->is.numeric) config.memory.min_yast = f->nvalue;
        break;

      case key_memyasttext:
        if(f->is.numeric) config.memory.min_yast_text = f->nvalue;
        break;

      case key_memmodules:
        if(f->is.numeric) config.memory.min_modules = f->nvalue;
        break;

      case key_memloadimage:
        if(f->is.numeric) {
          config.memory.load_image = f->nvalue;
          force_ri_ig = config.memory.free > config.memory.load_image ? 1 : 0;
        }
        break;

      case key_tmpfs:
        if(f->is.numeric) config.tmpfs = f->nvalue;
        break;

      case key_netstop:
        if(f->is.numeric) config.netstop = f->nvalue;
        break;

      case key_testmode:
        if(f->is.numeric) config.test = f->nvalue;
        break;

      case key_debugwait:
        if(f->is.numeric) config.debugwait = f->nvalue;
        break;

      case key_manual:
        if(f->is.numeric) {
          config.manual = f->nvalue;
          auto_ig = 0;
          auto2_ig = config.manual ^ 1;
        }
        break;

      case key_auto:
        if(f->is.numeric) {
          switch(f->nvalue) {
            case 0:
              config.manual = 1;
              auto_ig = auto2_ig = 0;
              break;

            case 1:
              config.manual = 0;
              auto_ig = 1;
              auto2_ig = 0;
              break;

            case 2:
              config.manual = 0;
              auto_ig = 0;
              auto2_ig = 1;
              break;
          }
        }
        break;

      case key_expert:
        if(f->is.numeric) {
          if((f->nvalue & 1)) config.textmode = 1;
          if((f->nvalue & 2)) yast2_update_ig = 1;
          if((f->nvalue & 4)) yast2_serial_ig = 1;
        }
        break;

      case key_demo:
        if(f->is.numeric) {
          config.demo = f->nvalue;
          config.activate_storage = 1;
        }
        break;

      case key_nopcmcia:
        if(f->is.numeric) config.nopcmcia = f->nvalue;
        break;

      case key_domain:
        str_copy(&config.net.domain, f->value);
        break;

      case key_rootimage:
        str_copy(&config.rootimage, f->value);
        break;

      case key_rescueimage:
        str_copy(&config.rescueimage, f->value);
        break;

      case key_installdir:
        str_copy(&config.installdir, f->value);
        break;

      case key_hwcheck:
      case key_rescue:
      case key_install:
        i = f->is.numeric ? f->nvalue : 1;
        config.rescue = f->key == key_rescue ? i : 0;
        config.hwcheck = f->key == key_hwcheck ? i : 0;
        if(config.rescue || config.hwcheck) {
          config.activate_storage = 1;
          config.activate_network = 1;
        }
        url = parse_url(f->value);
        if(url && url->scheme) {
          set_instmode(url->scheme);
          if(url->port) config.net.port = url->port;
          str_copy(&config.serverdir, url->dir);
          str_copy(&config.net.user, url->user);
          str_copy(&config.net.password, url->password);

          if(config.insttype == inst_net) {
            name2inet(&config.net.server, url->server);
          }
        }
        break;

      case key_autoyast:
        str_copy(&config.autoyast, f->value);
        auto2_ig = TRUE;
        if(!config.instmode) {
          url = parse_url(config.autoyast);
          if(url && url->scheme) set_instmode(url->scheme);
        }
        break;

      case key_vnc:
        if(f->is.numeric) config.vnc = f->nvalue;
        if(f->nvalue) config.activate_network = 1;
        break;

      case key_usessh:
        if(f->is.numeric) config.usessh = f->nvalue;
        if(f->nvalue) config.activate_network = 1;
        break;

      case key_vncpassword:
        str_copy(&config.net.vncpassword, *f->value ? f->value : NULL);
	/* do not enable vnc nor network ... this is done with vnc=1 */
        break;

      case key_sshpassword:
        str_copy(&config.net.sshpassword, *f->value ? f->value : NULL);
	/* do not enable ssh nor network ... this is done with usessh=1 */
        break;

      case key_usepivotroot:
        if(f->is.numeric) config.pivotroot = f->nvalue;
        break;

      case key_testpivotroot:
        if(f->is.numeric) config.testpivotroot = f->nvalue;
        break;

      case key_fullnetsetup:
        if(f->is.numeric) config.fullnetsetup = f->nvalue;
        break;

      case key_term:
        str_copy(&config.term, *f->value ? f->value : NULL);
        break;

      case key_addswap:
        if(f->is.numeric) {
          if(f->nvalue <= 0) {
            config.addswap = f->nvalue;
          }
          else {
            util_update_disk_list(NULL, 1);
            i = f->nvalue;
            for(sl = config.partitions; sl; sl = sl->next) {
              if(sl->key) {
                sprintf(buf, "/dev/%s", sl->key);
                t = fstype(buf);
                if(t && !strcmp(t, "swap")) {
                  if(!--i) {
                    char *argv[2] = { };

                    argv[1] = buf;
                    fprintf(stderr, "swapon %s\n", buf);
                    util_swapon_main(2, argv);
                    break;
                  }
                }
              }
            }
          }
        }
        else {
          char *argv[2] = { };

          s = f->value;
          if(strstr(s, "/dev/") != s) {
            sprintf(s = buf, "/dev/%s", f->value);
          }
          t = fstype(s);
          if(t && !strcmp(t, "swap")) {
            argv[1] = s;
            fprintf(stderr, "swapon %s\n", s);
            util_swapon_main(2, argv);
          }
        }
        break;

      case key_exec:
        if(*f->value) system(f->value);
        break;

      case key_usbwait:
        if(f->is.numeric) config.usbwait = f->nvalue;
        break;

      case key_nfsrsize:
        if(f->is.numeric) config.net.nfs_rsize = f->nvalue;
        break;

      case key_nfswsize:
        if(f->is.numeric) config.net.nfs_wsize = f->nvalue;
        break;

      case key_setupcmd:
        str_copy(&config.setupcmd, *f->value ? f->value : NULL);
        break;

      case key_setupnetif:
        if(f->is.numeric) config.net.ifconfig = f->nvalue;
        break;

      case key_noshell:
        if(f->is.numeric) config.noshell = f->nvalue;
        break;

      case key_memcheck:
        if(f->is.numeric) config.run_memcheck = f->nvalue;
        break;

      case key_hwdetect:
        if(f->is.numeric) config.hwdetect = f->nvalue;
        break;

      case key_floppydevice:
        str_copy(
          &config.floppydev,
          strstr(f->value, "/dev/") == f->value ? f->value + sizeof "/dev/" - 1 : *f->value ? f->value : NULL
        );
        config.floppies = 1;
        config.floppy = 0;
        sprintf(buf, "/dev/%s", config.floppydev);
        str_copy(&config.floppy_dev[0], buf);
        break;

      case key_cdromdevice:
        str_copy(
          &config.cdromdev,
          strstr(f->value, "/dev/") == f->value ? f->value + sizeof "/dev/" - 1 : *f->value ? f->value : NULL
        );
        break;

      case key_consoledevice:
        if(*f->value) {
          if(!config.console || strcmp(config.console, f->value)) {
            str_copy(&config.console, f->value);
            freopen(config.console, "r", stdin);
            freopen(config.console, "a", stdout);
          }
        }
        break;

#if 0
      case key_console:
        if(*f->value) util_set_serial_console(f->value);
        break;
#endif

      case key_product:
        if(*f->value) str_copy(&config.product, f->value);
        break;

      case key_productdir:
        if(*f->value) util_set_product_dir(f->value);
        break;

      case key_linuxrcstderr:
        if(*f->value) util_set_stderr(f->value);
        break;

      case key_kbdtimeout:
        if(f->is.numeric) config.kbdtimeout = f->nvalue;
        break;

      case key_brokenmodules:
        slist_free(config.module.broken);
        config.module.broken = slist_split(',', f->value);
        break;

      case key_initrdmodules:
        slist_free(config.module.initrd);
        config.module.initrd = slist_split(',', f->value);
        break;

      case key_scsibeforeusb:
        if(f->is.numeric) config.scsi_before_usb = f->nvalue;
        break;

      case key_linemode:
        if(f->is.numeric) config.linemode = f->nvalue;
        break;

      case key_moduledelay:
        if(f->is.numeric) config.module.delay = f->nvalue;
        break;

      case key_updatedir:
        if(*f->value) str_copy(&config.updatedir, f->value);
        break;

      case key_usbscsi:
        if(f->is.numeric) f->nvalue ? usbscsi_on() : usbscsi_off();
        break;

      case key_useusbscsi:
        if(f->is.numeric) config.use_usbscsi = f->nvalue;
        break;

      case key_lxrcdebug:
        if(f->is.numeric) config.debug = f->nvalue;
        break;

      default:
        break;
    }
  }

  if((net_config_mask() & 3) == 3) {
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


int file_read_yast_inf()
{
  int root = 0;
  file_t *f0, *f;

  f0 = file_read_file(YAST_INF_FILE);

  for(f = f0; f; f = f->next) {
    switch(f->key) {
#if OBSOLETE_SWAPOFF
      case key_swap:
        swapoff(f->value);
        break;
#endif

#if OBSOLETE_YAST_LIVECD
      case key_live:		/* really obsolete */
        yast_live_cd = f->nvalue;
        break;
#endif

      case key_root:
        root = 1;
        if(!f->is.numeric) root_set_root(f->value);
        reboot_ig = f->nvalue;
        break;

      case key_keytable:
        set_activate_keymap(*f->value ? f->value : NULL);
        break;

      case key_language:
        set_activate_language(set_langidbyname(f->value));
        break;

      case key_rebootmsg:
        config.rebootmsg = f->nvalue;
        break;

      case key_aborted:
        config.aborted = f->nvalue;
        break;

      default:
        break;
    }
  }

  file_free_file(f0);

  return root || config.aborted ? 0 : -1;
}


void file_write_str(FILE *f, file_key_t key, char *str)
{
  if(str) fprintf(f, "%s: %s\n", file_key2str(key), str);
}


void file_write_num(FILE *f, file_key_t key, int num)
{
  fprintf(f, "%s: %d\n", file_key2str(key), num);
}


void file_write_sym(FILE *f, file_key_t key, char *base_sym, int num)
{
  int i;

  i = sym2index(base_sym);

  if(i < 0 || num < 0 || (unsigned) i + num >= sizeof sym_constants / sizeof *sym_constants) {
    file_write_num(f, key, num);
  }

  fprintf(f, "%s: %s\n", file_key2str(key), sym_constants[i + num].name);
}


void file_write_inet(FILE *f, file_key_t key, inet_t *inet)
{
  char *s = NULL;

  if(inet->ok) s = inet_ntoa(inet->ip);
  if(!s) s = inet->name;

  if(s) fprintf(f, "%s: %s\n", file_key2str(key), s);
}


void file_write_install_inf(char *dir)
{
  FILE *f;
  char file_name[256], *s;
  slist_t *sl;

  strcat(strcpy(file_name, dir), INSTALL_INF_FILE);

  if(!(f = fopen(file_name, "w"))) {
    fprintf(stderr, "Cannot open yast info file\n");
    return;
  }

  if(config.language) set_write_info(f);

  file_write_num(f, key_sourcemounted, config.instdata_mounted);

  file_write_sym(f, key_display, "Undef", config.color);

  if(config.keymap && !auto2_ig) {
    file_write_str(f, key_keytable, config.keymap);
  }

  file_write_str(f, key_cdrom, config.cdrom);
  if(
    (config.insttype == inst_net || (config.vnc || config.usessh)) &&
    *netdevice_tg
    /* && (s = net_if2module(netdevice_tg)) */
  ) {
    for(sl = config.net.devices; sl; sl = sl->next) {
      if(sl->key && sl->value) {
      fprintf(f, "%s: %s %s\n", file_key2str(key_alias), sl->key, sl->value);
      }
    }
  }

  if(*ppcd_tg) {
    fprintf(f, "post-install paride insmod %s\n", ppcd_tg);
    fprintf(f, "pre-remove paride rmmod %s\n", ppcd_tg);
  }

#if WITH_PCMCIA
  if(pcmcia_chip_ig == 1 || pcmcia_chip_ig == 2) {
    file_write_str(f, key_pcmcia, pcmcia_driver(pcmcia_chip_ig));
  }
#endif

#ifdef USE_LIBHD
  file_write_num(f, key_haspcmcia, auto2_pcmcia() || pcmcia_chip_ig ? 1 : 0);
#endif

  file_write_str(f, key_console, config.serial);

  file_write_str(f, key_instmode,
    get_instmode_name(config.instmode_extra == inst_dvd ? config.instmode_extra : config.instmode)
  );

  if(config.insttype == inst_hd) {
    file_write_str(f, key_partition, config.partition);
//    file_write_str(f, key_fstype, fstype_tg);
    file_write_str(f, key_serverdir, config.serverdir);
  }

  if(
    config.insttype == inst_net ||
    config.instmode_extra == inst_cdwithnet ||
    (config.vnc || config.usessh)
  ) {
    s = NULL;
    switch(config.net.configured) {
      case nc_none:
        break;
      case nc_static:
        s = "static";
        break;
      case nc_bootp:
        s = "bootp";
        break;
      case nc_dhcp:
        s = "dhcp";
        break;
    }
    if(s) file_write_str(f, key_netconfig, s);
    file_write_str(f, key_netdevice, netdevice_tg);
    file_write_inet(f, key_ip, &config.net.hostname);
    if(config.net.realhostname) {
      file_write_str(f, key_hostname, config.net.realhostname);
    }
    else {
      file_write_str(f, key_hostname, config.net.hostname.name);
    }
    file_write_inet(f, key_broadcast, &config.net.broadcast);
    file_write_inet(f, key_network, &config.net.network);
    if(config.net.pliphost.ok) {
      file_write_inet(f, key_pliphost, &config.net.pliphost);
    }
    else {
      file_write_inet(f, key_netmask, &config.net.netmask);
    }
    file_write_inet(f, key_gateway, &config.net.gateway);
    file_write_inet(f, key_nameserver, &config.net.nameserver);
    file_write_inet(f, key_server, &config.net.server);
    file_write_str(f, key_serverdir, config.serverdir);
    file_write_str(f, key_domain, config.net.domain);
  }

#if 0
  file_write_inet(f, key_ftpproxy, &config.net.proxy);
  if(config.net.proxyport) {
    file_write_num(f, key_ftpproxyport, config.net.proxyport);
  }
#endif

  if(
    config.net.proxyport &&
    (
      config.instmode == inst_ftp ||
      config.instmode == inst_http ||
      config.instmode == inst_tftp
    )
  ) {
    file_write_inet(f, key_proxy, &config.net.proxy);
    file_write_num(f, key_proxyport, config.net.proxyport);
    file_write_str(f,
      key_proxyproto,
      get_instmode_name(config.net.proxyproto ?: config.instmode)
    );
  }

  file_write_str(f, key_username, config.net.user);
  file_write_str(f, key_password, config.net.password);
  file_write_str(f, key_workdomain, config.net.workgroup);

  file_write_modparms(f);

  file_write_num(f, key_manual, config.manual);
  file_write_num(f, key_demo, config.demo);

  if(reboot_ig) file_write_num(f, key_reboot, reboot_ig);

#ifdef USE_LIBHD
  if(config.floppies) {
    file_write_str(f, key_floppydisk, config.floppy_dev[config.floppy]);
  }

  file_write_num(f, key_keyboard, has_kbd_ig);
  file_write_str(f, key_updatedir, config.updatedir);
  file_write_num(f, key_yast2update, yast2_update_ig || *driver_update_dir ? 1 : 0);

  file_write_num(f, key_yast2serial, yast2_serial_ig);
  file_write_num(f, key_textmode, config.textmode);

  file_write_str(f, key_autoyast, config.autoyast);

  util_update_meminfo();
  file_write_num(f, key_memfree, config.memory.current);

  file_write_num(f, key_vnc, config.vnc);
  file_write_str(f, key_vncpassword, config.net.vncpassword);
  file_write_num(f, key_usessh, config.usessh);

  if(yast2_color_ig) {
    fprintf(f, "%s: %06x\n", file_key2str(key_yast2color), yast2_color_ig);
  }

  if(config.noshell) file_write_num(f, key_noshell, config.noshell);

  {
    char *s;
    int boot_disk;

    fflush(f);	/* really necessary! */
    s = auto2_disk_list(&boot_disk);
    if(*s) {
      file_write_num(f, key_bootdisk, boot_disk ? 1 : 0);
      file_write_str(f, key_disks, s);
    }
  }
#endif

  fclose(f);
}


#if 0
void file_write_mtab()
{
  char smb_mount_options[256];
  FILE *f;

  if(!(f = fopen(MTAB_FILE, "w"))) return;

  fprintf(f,
    "/dev/initrd / minix rw 0 0\n"
    "none /proc proc rw 0 0\n"
  );

  if(config.inst_ramdisk >= 0) {
    fprintf(f, "%s %s ext2 ro 0 0\n",
      config.ramdisk[config.inst_ramdisk].dev,
      config.mountpoint.instsys
    );
  }
  else {
    switch(config.instmode) {
      case inst_cdrom:
        if(config.cdrom) {
          fprintf(f, "/dev/%s %s iso9660 ro 0 0\n",
            config.cdrom, config.mountpoint.instdata
          );
        }
        break;

      case inst_nfs:
        fprintf(f,
          "%s:%s %s nfs ro 0 0\n",
          inet_ntoa(config.net.server.ip), config.serverdir ?: "", config.mountpoint.instdata
        );
        break;

      case inst_smb:
        net_smb_get_mount_options(
          smb_mount_options,
          &config.net.server, config.net.user,
          config.net.password, config.net.workgroup
        );
        fprintf(f,
          "//%s/%s %s smbfs ro,%s 0 0\n",
          config.net.server.name, config.serverdir ?: "",
          config.mountpoint.instdata, smb_mount_options
        );
        break;

      default:
    }
  }

  fclose(f);
}
#endif

void file_write_modparms(FILE *f)
{
  file_t *ft0, *ft;
  module_t *ml;
  slist_t *sl0 = NULL, *sl1, *sl, *pl0, *pl;
  slist_t *initrd0 = NULL, *initrd;
  slist_t *modules0 = NULL;

  ft0 = file_read_file("/proc/modules");

  /* build list of modules & initrd modules, reverse /proc/modules order! */
  for(ft = ft0; ft; ft = ft->next) {
    ml = mod_get_entry(ft->key_str);
    if(ml) {
      sl = slist_add(&modules0, slist_new());
      sl->key = strdup(ft->key_str);
      if(ml->initrd || slist_getentry(config.module.initrd, ft->key_str)) {
        sl = slist_add(&sl0, slist_new());
        sl->key = strdup(ft->key_str);
      }
    }
  }

  file_free_file(ft0);

  /* resolve module deps for initrd module list */
  for(sl = sl0; sl; sl = sl->next) {
    ml = mod_get_entry(sl->key);
    if(ml) {	/* just to be sure... */
      pl0 = slist_split(' ', ml->pre_inst);
      for(pl = pl0; pl; pl = pl->next) {
        if(!slist_getentry(initrd0, pl->key) && slist_getentry(modules0, pl->key)) {
          initrd = slist_append(&initrd0, slist_new());
          initrd->key = strdup(pl->key);
        }
      }
      slist_free(pl0);
      if(!slist_getentry(initrd0, sl->key)) {
        initrd = slist_append(&initrd0, slist_new());
        initrd->key = strdup(sl->key);
      }
      pl0 = slist_split(' ', ml->post_inst);
      for(pl = pl0; pl; pl = pl->next) {
        if(!slist_getentry(initrd0, pl->key) && slist_getentry(modules0, pl->key)) {
          initrd = slist_append(&initrd0, slist_new());
          initrd->key = strdup(pl->key);
        }
      }
      slist_free(pl0);
    }
  }

  slist_free(sl0);

  /* write 'InitrdModules:' line */
  if(initrd0) {
    fprintf(f, "%s:", file_key2str(key_initrdmodules));
    for(initrd = initrd0; initrd; initrd = initrd->next) fprintf(f, " %s", initrd->key);
    fprintf(f, "\n");
  }

  slist_free(initrd0);

  /*
   * For every currently loaded module, check if we used parameters and write
   * appropriate 'Options:' lines.
   */
  for(sl = modules0; sl; sl = sl->next) {
    sl1 = slist_getentry(config.module.used_params, sl->key);
    if(sl1) {
      fprintf(f, "%s: %s %s\n", file_key2str(key_options), sl1->key, sl1->value);
    }
  }

  slist_free(modules0);
}


file_t *file_read_cmdline()
{
  FILE *f;
  char cmdline[1024];

  if(!(f = fopen(CMDLINE_FILE, "r"))) return NULL;
  if(!fgets(cmdline, sizeof cmdline, f)) *cmdline = 0;
  fclose(f);

  return file_parse_buffer(cmdline);
}


file_t *file_parse_buffer(char *buf)
{
  file_t *ft0 = NULL, **ft = &ft0;
  char *current, *s, *s1, *t;
  int i, quote;

  current = buf;

  do {
    while(isspace(*current)) current++;
    for(quote = 0, s = current; *s && (quote || !isspace(*s)); s++) {
      if(*s == '"') quote ^= 1;
    }
    if(s > current) {
      t = malloc(s - current + 1);

      for(s1 = t; s > current; current++) {
        if(*current != '"') *s1++ = *current;
      }
      *s1 = 0;

      if((s1 = strchr(t, '='))) *s1++ = 0;

      *ft = calloc(1, sizeof **ft);

      i = strlen(t);
      if(i && t[i - 1] == ':') t[i - 1] = 0;

      (*ft)->key_str = strdup(t);
      (*ft)->key = file_str2key(t);	/* destroys t!!! */
      (*ft)->value = strdup(s1 ?: "");

      parse_value(*ft);

      free(t);

      ft = &(*ft)->next;
    }
  }
  while(*current);

  return ft0;
}


file_t *file_get_cmdline(file_key_t key)
{
  static file_t *cmdline = NULL, *ft;

  if(!cmdline) cmdline = file_read_cmdline();

  for(ft = cmdline; ft; ft = ft->next) {
    if(ft->key == key) break;
  }

  return ft;
}


void file_module_load(char *insmod_arg)
{
  char module[64], params[256];
  int i;

  i = sscanf(insmod_arg, "%63s %255[^\n]", module, params);

  if(i < 1) return;

  if(i == 1) *params = 0;

  mod_modprobe(module, params);
}


#ifdef DEBUG_FILE

void file_dump_flist(file_t *ft)
{
  for(; ft; ft = ft->next) {
    fprintf(stderr, "%d: \"%s\" = \"%s\"\n", ft->key, ft->key_str, ft->value);
    if(ft->is.numeric) fprintf(stderr, "  num = %d\n", ft->nvalue);
  }
}

#endif


module_t *file_read_modinfo(char *name)
{
  FILE *f;
  char buf[1024];
  char *s, *s1, *t, *current;
  module_t *ml0 = NULL, **ml = &ml0, *ml1;
  int i, j, quote, fields, esc;
  char *field[8];
  int current_type = MAX_MODULE_TYPES - 1;	/* default to 'other' */

  if(!config.module.type_name[0]) {
    /*
     * cf. mod_init() & mod_menu()
     * note2: scsi_type etc. are implicitly assumed to be nonzero in module.c
     */
    config.module.type_name[0] = strdup("autoload");
    /* make it always appear as last menu entry */
    config.module.type_name[MAX_MODULE_TYPES - 1] = strdup("other");
  }

  if(!(f = fopen(name, "r"))) return NULL;

  while(fgets(buf, sizeof buf, f)) {
    current = buf;
    fields = 0;

    do {
      while(isspace(*current)) current++;
      if(*current == 0 || *current == ';' || *current == '#') break;
      
      for(quote = 0, s = current; *s && (quote || *s != ','); s++) {
        if(*s == '"') quote ^= 1;
      }

      if(s > current) {
        t = malloc(s - current + 1);

        for(esc = 0, s1 = t; s > current; current++) {
          if(*current == '\\' && !esc) {
            esc = 1;
            continue;
          }
          if(*current != '"' || esc) *s1++ = *current;
          esc = 0;
        }
        *s1 = 0;

        while(s1 > t && isspace(s1[-1])) *--s1 = 0;
      }
      else {
        t = strdup("");
      }
      field[fields++] = t;

      if(*current == ',') current++;
    }
    while(*current && (unsigned) fields < sizeof field / sizeof *field);

    if(fields == 1) {
      if(*(s = *field ) == '[' && (i = strlen(s)) && s[i - 1] == ']') {
        s[i - 1] = 0;
        s++;
        for(j = -1, i = 0; i < MAX_MODULE_TYPES; i++) {
          if(config.module.type_name[i]) {
            if(!strcasecmp(config.module.type_name[i], s)) {
              current_type = i;
              break;
            }
          }
          else {
            if(j < 0) j = i;
          }
        }
        if(i == MAX_MODULE_TYPES) {
          current_type = j >= 0 ? j : MAX_MODULE_TYPES - 1;
          if(!config.module.type_name[current_type]) {
            config.module.type_name[current_type] = strdup(s);

            if(!config.module.scsi_type && !strcasecmp(s, "ide/raid/scsi")) {
              config.module.scsi_type = current_type;
            }
            if(!config.module.network_type && !strcasecmp(s, "network")) {
              config.module.network_type = current_type;
            }
            if(!config.module.cdrom_type && !strcasecmp(s, "cd-rom")) {
              config.module.cdrom_type = current_type;
            }
            if(!config.module.pcmcia_type && !strcasecmp(s, "pcmcia")) {
              config.module.pcmcia_type = current_type;
            }
            if(!config.module.fs_type && !strcasecmp(s, "file system")) {
              config.module.fs_type = current_type;
            }
          }
        }
        free(field[--fields]);
      }
      else {
        if(!strncasecmp(field[0], "MoreModules", sizeof "MoreModules" - 1)) {
          s = field[0] + sizeof "MoreModules" - 1;
          while(*s == '=' || isspace(*s)) s++;
          str_copy(config.module.more_file + current_type, s);
          free(field[--fields]);
        }
        else if(!strncasecmp(field[0], "ModDisk", sizeof "ModDisk" - 1)) {
          s = field[0] + sizeof "ModDisk" - 1;
          while(*s == '=' || isspace(*s)) s++;
          j = strtol(s, &s1, 0);
          if(!*s1) config.module.disk[current_type] = j;
          free(field[--fields]);
        }
      }
    }

#if 0
    if(fields) {
      fprintf(stderr, "type = %d (%s)\n", current_type, config.module.type_name[current_type]);

      for(i = 0; i < fields; i++) {
        fprintf(stderr, ">%s< ", field[i]);
      }
      fprintf(stderr, "\n");
    }
#endif

    if(fields && **field) {
      ml1 = *ml = calloc(1, sizeof **ml);

      ml1->type = current_type;
      ml1->name = strdup(field[0]);
      if(fields > 1 && *field[1]) ml1->descr = strdup(field[1]);
      if(fields > 2 && *field[2]) {
        if(*field[2] == '-') {
          ml1->dontask = 1;
          if(field[2][1]) ml1->param = strdup(field[2] + 1);
        }
        else {
          ml1->param = strdup(field[2]);
        }
      }
      if(fields > 3 && *field[3]) ml1->pre_inst = strdup(field[3]);
      if(fields > 4 && *field[4]) ml1->post_inst = strdup(field[4]);
      if(fields > 5 && *field[5]) ml1->initrd = atoi(field[5]);
      ml1->autoload = fields > 6 && *field[6] ? atoi(field[6]) : 1;

      ml = &(*ml)->next;
    }

    while(fields--) free(field[fields]);
  }

  fclose(f);

  *ml = config.module.list;
  config.module.list = ml0;

#ifdef DEBUG_FILE
  file_dump_mlist(config.module.list);
#endif

  return ml0;
}


#ifdef DEBUG_FILE

void file_dump_mlist(module_t *ml)
{
  for(; ml; ml = ml->next) {
    fprintf(stderr, "%s (%s:%s): \"%s\"\n",
      ml->name,
      config.module.type_name[ml->type],
      config.module.more_file[ml->type] ?: "-",
      ml->descr ?: ""
    );
    fprintf(stderr, "  initrd = %s, show = %s, auto = %s, ask = %s\n",
      ml->initrd ? "yes" : "no",
      ml->descr ? "yes" : "no",
      ml->autoload ? "yes" : "no",
      ml->dontask ? "no" : "yes"
    );
    if(ml->param) fprintf(stderr, "  param: \"%s\"\n", ml->param);
    if(ml->pre_inst) fprintf(stderr, "  pre_inst: \"%s\"\n", ml->pre_inst);
    if(ml->post_inst) fprintf(stderr, "  post_inst: \"%s\"\n", ml->post_inst);
  }
}

#endif


