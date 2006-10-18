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
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <sys/select.h>

#include <hd.h>

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
#include "keyboard.h"

#define YAST_INF_FILE		"/etc/yast.inf"
#define INSTALL_INF_FILE	"/etc/install.inf"
#define MTAB_FILE		"/etc/mtab"
#define CMDLINE_FILE		"/proc/cmdline"
#define LIVE_CONF_FILE		"/etc/liveeval.conf"

// #define DEBUG_FILE

static char *file_key2str(file_key_t key);
static file_key_t file_str2key(char *value, file_key_flag_t flags);
static int sym2index(char *sym);
static void parse_value(file_t *ft);

void file_write_modparms(FILE *f);
static void file_module_load (char *insmod_arg);
#ifdef DEBUG_FILE
static void file_dump_flist(file_t *ft);
static void file_dump_mlist(module_t *ml);
#endif

static void file_write_inet(FILE *f, file_key_t key, inet_t *inet);
static void file_write_inet_str(FILE *f, char *name, inet_t *inet);
static void file_write_inet_both(FILE *f, file_key_t key, inet_t *inet);

static void add_driver(char *str);
static void parse_ethtool(slist_t *sl, char *str);
static void wait_for_conn(int port);
static int activate_network(void);


static struct {
  file_key_t key;
  char *value;
  file_key_flag_t flags;
} keywords[] = {
  { key_none,           "",               kf_none                        },
  { key_root,           "Root",           kf_yast + kf_boot              },
  { key_live,           "live",           kf_cfg + kf_cmd                },
  { key_keytable,       "Keytable",       kf_cfg + kf_cmd + kf_yast      },
  { key_language,       "Language",       kf_cfg + kf_cmd + kf_yast      },
  { key_language,       "lang",           kf_cfg + kf_cmd                },
  { key_rebootmsg,      "RebootMsg",      kf_yast                        },
  { key_insmod,         "Insmod",         kf_cfg + kf_cmd1               },
  { key_display,        "Display",        kf_cfg + kf_cmd                },
  { key_ip,             "IP",             kf_none                        },
  { key_netmask,        "Netmask",        kf_cfg + kf_cmd + kf_dhcp      },
  { key_gateway,        "Gateway",        kf_cfg + kf_cmd + kf_dhcp      },
  { key_server,         "Server",         kf_cfg + kf_cmd                },
  { key_nameserver,     "Nameserver",     kf_cfg + kf_cmd                },
  { key_broadcast,      "Broadcast",      kf_cfg + kf_cmd + kf_dhcp      },
  { key_network,        "Network",        kf_cfg + kf_cmd + kf_dhcp      },
  { key_partition,      "Partition",      kf_cfg + kf_cmd                },
  { key_serverdir,      "Serverdir",      kf_cfg + kf_cmd                },
  { key_netdevice,      "Netdevice",      kf_cfg + kf_cmd                },
  { key_livesrc,        "LiveSRC",        kf_cfg + kf_cmd                },
  { key_bootpwait,      "Bootpwait",      kf_cfg + kf_cmd                },
  { key_bootptimeout,   "BOOTPTimeout",   kf_cfg + kf_cmd                },
  { key_forcerootimage, "ForceRootimage", kf_cfg + kf_cmd                },
  { key_rebootwait,     "WaitReboot",     kf_cfg + kf_cmd                },	/* drop it? */
  { key_sourcemounted,  "Sourcemounted",  kf_none                        },
  { key_cdrom,          "Cdrom",          kf_none                        },
  { key_pcmcia,         "PCMCIA",         kf_none                        },
  { key_haspcmcia,      "HasPCMCIA",      kf_none                        },
  { key_console,        "Console",        kf_none                        },
  { key_ptphost,        "Pointopoint",    kf_cfg + kf_cmd                },
  { key_domain,         "Domain",         kf_cfg + kf_cmd + kf_dhcp      },
  { key_manual,         "Manual",         kf_cfg + kf_cmd + kf_cmd_early },
  { key_demo,           "Demo",           kf_none                        },	/* obsolete */
  { key_reboot,         "Reboot",         kf_none                        },	/* drop it? */
  { key_floppydisk,     "Floppydisk",     kf_none                        },	/* ??? */
  { key_keyboard,       "Keyboard",       kf_none                        },
  { key_yast2update,    "YaST2update",    kf_none                        },
  { key_textmode,       "Textmode",       kf_cfg + kf_cmd                },
  { key_yast2color,     "YaST2color",     kf_none                        },
  { key_bootdisk,       "BootDisk",       kf_none                        },	/* obsolete */
  { key_disks,          "Disks",          kf_none                        },	/* obsolete */
  { key_username,       "Username",       kf_cfg + kf_cmd                },
  { key_password,       "Password",       kf_cfg + kf_cmd                },
  { key_workdomain,     "WorkDomain",     kf_cfg + kf_cmd                },
  { key_alias,          "Alias",          kf_none                        },
  { key_options,        "Options",        kf_cfg + kf_cmd + kf_cmd_early },
  { key_initrdmodules,  "InitrdModules",  kf_cfg + kf_cmd                },
  { key_locale,         "Locale",         kf_none                        },
  { key_font,           "Font",           kf_none                        },
  { key_screenmap,      "Screenmap",      kf_none                        },
  { key_fontmagic,      "Fontmagic",      kf_none                        },
  { key_autoyast,       "AutoYaST",       kf_cfg + kf_cmd_early          },
  { key_linuxrc,        "linuxrc",        kf_cfg + kf_cmd_early          },
  { key_forceinsmod,    "ForceInsmod",    kf_cfg + kf_cmd                },
  { key_dhcp,           "DHCP",           kf_cmd                         },	/* not really useful */
  { key_ipaddr,         "IPAddr",         kf_dhcp                        },
  { key_hostname,       "Hostname",       kf_cfg + kf_cmd                },
  { key_dns,            "DNS",            kf_dhcp                        },
  { key_dhcpsiaddr,     "DHCPSIAddr",     kf_dhcp                        },
  { key_rootpath,       "RootPath",       kf_dhcp                        },
  { key_bootfile,       "BootFile",       kf_dhcp                        },
  { key_install,        "Install",        kf_cfg + kf_cmd                },
  { key_instmode,       "InstMode",       kf_cfg + kf_cmd                },
  { key_memtotal,       "MemTotal",       kf_mem                         },
  { key_memfree,        "MemFree",        kf_mem                         },
  { key_buffers,        "Buffers",        kf_mem                         },
  { key_cached,         "Cached",         kf_mem                         },
  { key_swaptotal,      "SwapTotal",      kf_mem                         },
  { key_swapfree,       "SwapFree",       kf_mem                         },
  { key_memlimit,       "MemLimit",       kf_cfg + kf_cmd                },
  { key_memyast,        "MemYaST",        kf_cfg + kf_cmd                },
  { key_memyasttext,    "MemYaSTText",    kf_cfg + kf_cmd                },
  { key_memmodules,     "MemModules",     kf_cfg + kf_cmd                },
  { key_memloadimage,   "MemLoadImage",   kf_cfg + kf_cmd                },
  { key_info,           "Info",           kf_cmd_early                   },
  { key_proxy,          "Proxy",          kf_cfg + kf_cmd                },
  { key_proxyport,      "ProxyPort",      kf_cfg + kf_cmd                },
  { key_proxyproto,     "ProxyProto",     kf_cfg + kf_cmd                },
  { key_usedhcp,        "UseDHCP",        kf_cfg + kf_cmd                },
  { key_nfsport,        "NFSPort",        kf_cfg + kf_cmd                },
  { key_dhcptimeout,    "DHCPTimeout",    kf_cfg + kf_cmd                },
  { key_tftptimeout,    "TFTPTimeout",    kf_cfg + kf_cmd                },
  { key_tmpfs,          "_TmpFS",         kf_cmd                         },
  { key_netstop,        "_NetStop",       kf_cfg + kf_cmd                },
  { key_testmode,       "_TestMode",      kf_cfg                         },
  { key_debugwait,      "_DebugWait",     kf_cfg + kf_cmd + kf_cmd_early },
  { key_expert,         "Expert",         kf_cfg + kf_cmd                },	/* drop it? */
  { key_rescue,         "Rescue",         kf_cfg + kf_cmd                },
  { key_rootimage,      "RootImage",      kf_cfg + kf_cmd                },
  { key_rescueimage,    "RescueImage",    kf_cfg + kf_cmd                },
  { key_installdir,     "InstallDir",     kf_cfg + kf_cmd                },
  { key_nopcmcia,       "NoPCMCIA",       kf_cfg + kf_cmd                },	/* kf_cmd_early? */
  { key_vnc,            "VNC",            kf_cfg + kf_cmd                },
  { key_vnc,            "UseVNC",         kf_cfg + kf_cmd                },
  { key_usessh,         "UseSSH",         kf_cfg + kf_cmd                },
  { key_usessh,         "SSH",            kf_cfg + kf_cmd                },
  { key_vncpassword,    "VNCPassword",    kf_cfg + kf_cmd                },
  { key_displayip,	"Display_IP",     kf_cfg + kf_cmd		 },
  { key_sshpassword,    "SSHPassword",    kf_cfg + kf_cmd                },
  { key_usepivotroot,   "UsePivotRoot",   kf_cfg + kf_cmd                },
  { key_term,           "TERM",           kf_cfg + kf_cmd                },
  { key_addswap,        "AddSwap",        kf_cfg + kf_cmd                },
  { key_aborted,        "Aborted",        kf_yast                        },
  { key_exec,           "Exec",           kf_cfg + kf_cmd                },
  { key_usbwait,        "USBWait",        kf_cfg + kf_cmd + kf_cmd_early },
  { key_nfsrsize,       "NFS.RSize",      kf_cfg + kf_cmd                },
  { key_nfswsize,       "NFS.WSize",      kf_cfg + kf_cmd                },
  { key_nfstcp,         "NFS.TCP",        kf_cfg + kf_cmd                },
  { key_setupcmd,       "SetupCmd",       kf_cfg + kf_cmd                },
  { key_setupnetif,     "SetupNetIF",     kf_cfg + kf_cmd                },
  { key_netconfig,      "NetConfig",      kf_none                        },
  { key_noshell,        "NoShell",        kf_cfg + kf_cmd + kf_cmd_early },
  { key_floppydevice,   "FloppyDevice",   kf_cfg + kf_cmd                },
  { key_cdromdevice,    "CDROMDevice",    kf_cfg + kf_cmd                },
  { key_consoledevice,  "ConsoleDevice",  kf_cfg + kf_cmd                },
  { key_product,        "Product",        kf_cfg + kf_cmd                },
  { key_productdir,     "ProductDir",     kf_cfg + kf_cmd                },
  { key_linuxrcstderr,  "LinuxrcSTDERR",  kf_cfg + kf_cmd + kf_cmd_early },
  { key_linuxrcstderr,  "LinuxrcLog",     kf_cfg + kf_cmd + kf_cmd_early },
  { key_comment,        "#",              kf_cfg                         },
  { key_kbdtimeout,     "KBDTimeout",     kf_cfg + kf_cmd                },
  { key_brokenmodules,  "BrokenModules",  kf_cfg + kf_cmd + kf_cmd_early },
  { key_testpivotroot,  "_TestPivotRoot", kf_cfg + kf_cmd                },
  { key_scsibeforeusb,  "SCSIBeforeUSB",  kf_cfg + kf_cmd + kf_cmd_early },
  { key_hostip,         "HostIP",         kf_cfg + kf_cmd                },
  { key_linemode,       "Linemode",       kf_cfg + kf_cmd + kf_cmd_early },
  { key_moduledelay,    "ModuleDelay",    kf_cfg + kf_cmd + kf_cmd_early },
  { key_updatedir,      "UpdateDir",      kf_cfg + kf_cmd                },
  { key_scsirename,     "SCSIRename",     kf_cfg + kf_cmd + kf_cmd_early },
  { key_doscsirename,   "DoSCSIRename",   kf_cfg + kf_cmd                },
  { key_lxrcdebug,      "LXRCDebug",      kf_cfg + kf_cmd + kf_cmd_early },
  { key_lxrcdebug,      "LinuxrcDebug",   kf_cfg + kf_cmd + kf_cmd_early },
  { key_kernel_pcmcia,  "KernelPCMCIA",   kf_cfg + kf_cmd                },
  { key_liveconfig,     "LiveConfig",     kf_cfg + kf_cmd                },
  { key_updatename,     "UpdateName",     kf_cfg + kf_cmd                },
  { key_updatestyle,    "UpdateStyle",    kf_cfg + kf_cmd                },
  { key_updateid,       "UpdateID",       kf_cfg                         },
  { key_updateprio,     "UpdatePriority", kf_cfg                         },
  { key_updateask,      "DriverUpdate",   kf_cfg + kf_cmd                },
  { key_updateask,      "DUD",            kf_cfg + kf_cmd                },
  { key_initrd,         "Initrd",         kf_boot                        },
  { key_vga,            "VGA",            kf_none                        },
  { key_bootimage,      "BOOTIMAGE",      kf_boot                        },
  { key_ramdisksize,    "ramdisksize",    kf_boot                        },
  { key_suse,           "SuSE",           kf_boot                        },
  { key_showopts,       "showopts",       kf_boot                        },
  { key_nosshkey,       "nosshkey",       kf_boot                        },
  { key_startshell,     "StartShell",     kf_cfg + kf_cmd                },
  { key_y2debug,        "y2debug",        kf_boot                        },
  { key_ro,             "ro",             kf_boot                        },
  { key_rw,             "rw",             kf_boot                        },
  { key_netid,          "NetUniqueID",    kf_none                        },
  { key_nethwaddr,      "HWAddr",         kf_none                        },
  { key_loglevel,       "LogLevel",       kf_cfg + kf_cmd + kf_cmd_early },
  { key_netsetup,       "NetSetup",       kf_cfg + kf_cmd                },
  { key_rootpassword,   "RootPassword",   kf_cfg + kf_cmd                },
  { key_loghost,        "Loghost",        kf_cfg + kf_cmd                },
  { key_escdelay,       "ESCDelay",       kf_cfg + kf_cmd                },
  { key_minmem,         "MinMemory",      kf_cfg + kf_cmd + kf_cmd_early },
#if defined(__s390__) || defined(__s390x__)
  { key_instnetdev,	"InstNetDev",	  kf_cfg + kf_cmd		 },
  { key_iucvpeer,	"IUCVPeer",	  kf_cfg + kf_cmd		 },
  { key_portname,	"Portname",	  kf_cfg + kf_cmd		 },
  { key_readchan,	"ReadChannel",	  kf_cfg + kf_cmd		 },
  { key_writechan,	"WriteChannel",   kf_cfg + kf_cmd		 },
  { key_datachan,	"DataChannel",	  kf_cfg + kf_cmd		 },
  { key_ctcprotocol,	"CTCProtocol",	  kf_cfg + kf_cmd		 },
  { key_osamedium,	"OSAMedium",	  kf_cfg + kf_cmd		 },
  { key_osainterface,	"OSAInterface",	  kf_cfg + kf_cmd		 },
  { key_layer2,		"Layer2",	  kf_cfg + kf_cmd		 },
#endif
  { key_netwait,        "NetWait",        kf_cfg + kf_cmd                },
  { key_newid,          "NewID",          kf_cfg + kf_cmd_early          },
  { key_moduledisks,    "ModuleDisks",    kf_cfg + kf_cmd                },
  { key_zen,            "Zen",            kf_cfg + kf_cmd + kf_cmd_early },
  { key_zenconfig,      "ZenConfig",      kf_cfg + kf_cmd + kf_cmd_early },
  { key_port,           "Port",           kf_none                        },
  { key_smbshare,       "Share",          kf_none                        },
  { key_rootimage2,     "RootImage2",     kf_cfg + kf_cmd                },
  { key_xxx,            "xxx",            kf_cfg + kf_cmd + kf_cmd_early },
  { key_instsys_id,     "InstsysID",      kf_cfg + kf_cmd                },
  { key_initrd_id,      "InitrdID",       kf_cfg + kf_cmd                },
  { key_instsys_complain, "InstsysComplain", kf_cfg + kf_cmd             },
  { key_dud_complain,   "UpdateComplain", kf_cfg + kf_cmd                },
  { key_dud_expected,   "UpdateExpected", kf_cfg + kf_cmd                },
  { key_staticdevices,  "StaticDevices",  kf_cfg + kf_cmd_early          },
  { key_withiscsi,      "WithiSCSI",      kf_cfg + kf_cmd                },
  { key_ethtool,        "ethtool",        kf_cfg + kf_cmd_early          },
  { key_listen,         "listen",         kf_cfg + kf_cmd                },
  { key_zombies,        "Zombies",        kf_cfg + kf_cmd                },
  { key_dhcpcd,         "DHCPCD",         kf_cfg + kf_cmd                },
  { key_wlan_essid,     "WlanESSID",      kf_cfg + kf_cmd                },
  { key_wlan_auth,      "WlanAuth",       kf_cfg + kf_cmd                },
  { key_wlan_key_ascii, "WlanKeyAscii",   kf_cfg + kf_cmd                },
  { key_wlan_key_hex,   "WlanKeyHex",     kf_cfg + kf_cmd                },
  { key_wlan_key_pass,  "WlanKeyPass",    kf_cfg + kf_cmd                },
  { key_wlan_key_len,   "WlanKeyLen",     kf_cfg + kf_cmd                }
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
  { "Halt",      2                  },
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
  { "slp",       inst_slp           },
  { "exec",      inst_exec          },
  /* add new inst modes _here_! */
  { "harddisk",  inst_hd            },
  { "cdrom",     inst_cdrom         },
  { "cifs",      inst_smb           },
#if defined(__s390__) || defined(__s390x__)
  { "osa",	 di_390net_osa      },
  { "ctc",	 di_390net_ctc	    },
  { "escon",	 di_390net_escon    },
  { "iucv",	 di_390net_iucv     },
  { "hsi",	 di_390net_hsi	    },
  { "eth",	 di_osa_eth	    },
  { "tr",	 di_osa_tr	    },
  { "qdio",	 di_osa_qdio	    },
  { "lcs",	 di_osa_lcs	    },
#endif
  { "open",      wa_open            },
  { "wep",       wa_wep_open        },
  { "wep_o",     wa_wep_open        },
  { "wep_r",     wa_wep_resticted   },
  { "wpa",       wa_wpa             },
};


file_t *file_getentry(file_t *f, char *key)
{
  if(key) {
    for(; f; f = f->next) {
      if(f->key_str && !strcmp(key, f->key_str)) return f;
    }
  }

  return NULL;
}


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

/*
 * Compare strings, ignoring '-', '_', and '.' characters in strings not
 * starting with '_'.
 */
static int strcasecmpignorestrich(const char* s1, const char* s2)
{
  char* str1 = strdup(s1);
  char* str2 = strdup(s2);
  char* s;
  int i;
  
  /* remove all '-' and '_' */
  if(*str1 != '_') {
    for(i = 0, s = str1; str1[i]; i++) {
      if(str1[i] != '_' && str1[i] != '-' && str1[i] != '.') {
        *s++ = str1[i];
      }
    }
    *s = 0;
  }

  /* remove all '-' and '_' */
  if(*str2 != '_') {
    for(i = 0, s = str2; str2[i]; i++) {
      if(str2[i] != '_' && str2[i] != '-' && str2[i] != '.') {
        *s++ = str2[i];
      }
    }
    *s = 0;
  }

  i = strcasecmp(str1, str2);
  free(str1); free(str2);
  return i;
}

/* !!! str is overwritten !!! */
file_key_t file_str2key(char *str, file_key_flag_t flags)
{
  int i;

  if(!str || !*str || flags == kf_none) return key_none;

  if(!*str) return key_none;

  for(i = 0; (unsigned) i < sizeof keywords / sizeof *keywords; i++) {
    if((keywords[i].flags & flags) && !strcasecmpignorestrich(keywords[i].value, str)) {
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


file_t *file_read_file(char *name, file_key_flag_t flags)
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
    if(*t == '"' || *t == '\'') {
      t1 = t + strlen(t);
      if(t1 > t && t1[-1] == *t) {
        t++;
        t1[-1] = 0;
      }
    }

    if(*s) {
      *ft = calloc(1, sizeof **ft);

      (*ft)->key_str = strdup(s);
      (*ft)->key = file_str2key(s, flags);	/* destroys s!!! */
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
    if(file->unparsed) free(file->unparsed);
    if(file->key_str) free(file->key_str);
    if(file->value) free(file->value);
    free(file);
  }
}


int file_read_info()
{
  window_t win_ri;
  char *file = NULL, *s;

  if(config.win) {
    dia_info(&win_ri, txt_get(TXT_SEARCH_INFOFILE));
  }
  else {
    printf("%s", txt_get(TXT_SEARCH_INFOFILE));
    fflush(stdout);
    config.linebreak = 1;
  }

  if(!config.info.file || !strcmp(config.info.file, "default")) {
    file = file_read_info_file("floppy:/info", kf_cfg);
    if(!file) file = file_read_info_file("file:/info", kf_cfg);
  }
  else {
    file = file_read_info_file(config.info.file, kf_cfg);
  }

  if(config.info.add_cmdline) {
    s = file_read_info_file("cmdline", kf_cmd);
    if(!file) file = s;
  }

  if(config.win) {
    win_close(&win_ri);
  }
  else {
    if(config.linebreak) {
      printf("\n");
      config.linebreak = 0;
    }
  }

  if(file) {
    fprintf(stderr, "got info from %s\n", file);
  }

  config.info.loaded = strdup(file ?: "");

  return file ? 0 : 1;
}


char *file_read_info_file(char *file, file_key_flag_t flags)
{
  char filename[MAX_FILENAME];
  int i, mounted = 0, dud = 0;
  file_t *f0 = NULL;

#ifdef DEBUG_FILE
  fprintf(stderr, "looking for info file: %s\n", file);
#endif

  if(!strcmp(file, "cmdline")) {
    f0 = file_read_cmdline(flags);
  }
  else if(!strncmp(file, "file:", 5)) {
    f0 = file_read_file(file + 5, flags);
  }
  else if(!strncmp(file, "floppy:", 7)) {
    for(i = 0; i < config.floppies; i++) {
      if(!util_mount_ro(config.floppy_dev[i], config.mountpoint.instdata)) break;
    }
    if(i < config.floppies) {
      config.floppy = i;	// remember currently used floppy
      mounted = 1;
      util_chk_driver_update(config.mountpoint.instdata, "floppy");
      dud = 1;
      sprintf(filename, "%s/%s", config.mountpoint.instdata, file + 7);
      f0 = file_read_file(filename, flags);
    }
  }

  if(mounted) umount(config.mountpoint.instdata);

  if(dud) util_do_driver_updates();

  if(!f0) return NULL;

#ifdef DEBUG_FILE
  fprintf(stderr, "info file read from \"%s\":\n", file);
  file_dump_flist(f0);
#endif

  file_do_info(f0);

  file_free_file(f0);

  return file;
}


void file_do_info(file_t *f0)
{
  file_t *f;
  url_t *url;
  int i, is_xml = 0;
  char buf[256], *s, *t;
  slist_t *sl, *sl0;
  unsigned u;

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

      case key_instmode:
        if(f->is.numeric) set_instmode(f->nvalue);
        break;

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

      case key_ptphost:
        name2inet(&config.net.ptphost, f->value);
        net_check_address2(&config.net.ptphost, 0);
        break;
      
      case key_server:
        name2inet(&config.net.server, f->value);
        net_check_address2(&config.net.server, 0);
        break;

      case key_nameserver:
        name2inet(&config.net.nameserver[0], f->value);
        net_check_address2(&config.net.nameserver[0], 0);
        break;

      case key_proxy:
        name2inet(&config.net.proxy, f->value);
        net_check_address2(&config.net.proxy, 0);
        break;

      case key_proxyport:
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
        if(*f->value) {
          str_copy(&config.net.device, f->value);
          config.net.device_given = 1;
        }
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
        if(f->is.numeric) config.manual = f->nvalue;
        break;

      case key_expert:
        if(f->is.numeric) {
          if((f->nvalue & 1)) config.textmode = 1;
          if((f->nvalue & 2)) config.update.ask = 1;
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

      case key_rescue:
      case key_install:
        i = f->is.numeric ? f->nvalue : 1;
        if(f->key == key_rescue) config.rescue = i;
        if(config.rescue) {
          config.activate_storage = 1;
          config.activate_network = 1;
        }
        url = parse_url(f->value);
        if(url && url->scheme) {
          set_instmode(url->scheme);
          if(url->port) config.net.port = url->port;

          if(config.instmode == inst_slp) {
            str_copy(&config.slp.proto, url->server && *url->server ? url->server : NULL);
            str_copy(&config.slp.key, url->dir && *url->dir && strcmp(url->dir, "/") ? url->dir : NULL);
            str_copy(&config.serverdir, "/");		/* necessary - believe me */
          }
          else {
            str_copy(&config.serverdir, url->dir);
            str_copy(&config.net.user, url->user);
            str_copy(&config.net.password, url->password);
            str_copy(&config.net.share, url->share);
            str_copy(&config.net.workgroup, url->domain);

            if(config.insttype == inst_net) {
              name2inet(&config.net.server, url->server);
            }
            else if(config.insttype == inst_cdrom && url->server) {
              str_copy(&config.cdromdev, url->server);
            }
            else if(config.insttype == inst_hd && url->server) {
              str_copy(&config.partition, url->server);
            }
          }
        }
        break;

      case key_autoyast:
        str_copy(&config.autoyast, *f->value ? f->value : "default");
        config.manual = 0;
        if(!config.instmode) {
          url = parse_url(config.autoyast);
          if(url && url->scheme) set_instmode(url->scheme);
        }
        break;

      case key_info:
        str_copy(&config.info.file, *f->value ? f->value : "default");
        break;

      case key_vnc:
        if(f->is.numeric) config.vnc = f->nvalue;
        if(config.vnc) {
          config.activate_network = 1;
          config.net.do_setup |= DS_VNC;
        }
        break;

      case key_usessh:
        if(f->is.numeric) config.usessh = f->nvalue;
        if(config.usessh) {
          config.activate_network = 1;
          config.net.do_setup |= DS_SSH;
        }
        break;

      case key_vncpassword:
        str_copy(&config.net.vncpassword, *f->value ? f->value : NULL);
	/* do not enable vnc nor network ... this is done with vnc=1 */
        break;

      case key_displayip:
        name2inet(&config.net.displayip, f->value);
        net_check_address2(&config.net.displayip, 0);
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
                    if(config.demo) {
                      slist_append_str(&config.live.useswap, buf + sizeof "/dev/" - 1);
                    }
                    else {
                      fprintf(stderr, "swapon %s\n", buf);
                      util_swapon_main(2, argv);
                    }
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
            if(config.demo) {
              slist_append_str(&config.live.useswap, s + sizeof "/dev/" - 1);
            }
            else {
              fprintf(stderr, "swapon %s\n", s);
              util_swapon_main(2, argv);
            }
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

      case key_nfstcp:
        if(f->is.numeric) config.net.nfs_tcp = f->nvalue;
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

      case key_floppydevice:
        str_copy(
          &config.floppydev,
          strstr(f->value, "/dev/") == f->value ? f->value + sizeof "/dev/" - 1 : *f->value ? f->value : NULL
        );
        config.floppy_probed = 1;
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
        if(config.linemode) config.utf8 = 0;
        break;

      case key_moduledelay:
        if(f->is.numeric) config.module.delay = f->nvalue;
        break;

      case key_updatedir:
        if(*f->value) str_copy(&config.update.dir, f->value);
        break;

      case key_scsirename:
        if(f->is.numeric) config.scsi_rename = f->nvalue;
        if(config.scsi_rename) {
          config.activate_storage = 1;
        }
        break;

      case key_doscsirename:
        if(f->is.numeric && f->nvalue) {
          config.scsi_rename = 1;
          scsi_rename();
        }
        break;

      case key_lxrcdebug:
        if(f->is.numeric) config.debug = f->nvalue;
        break;

      case key_linuxrc:
        slist_free(config.linuxrc);
        config.linuxrc = slist_split(',', f->value);
        if(slist_getentry(config.linuxrc, "nocmdline")) config.info.add_cmdline = 0;
        /* ###### still needed? */
        if(slist_getentry(config.linuxrc, "reboot")) reboot_ig = 1;
        break;

      case key_live:
        slist_append(&config.live.args, sl = slist_split(',', f->value));
        if(
          slist_getentry(config.live.args, "new") ||
          slist_getentry(config.live.args, "clean")
        ) {
          config.live.newconfig = 1;
        }
        if(slist_getentry(config.live.args, "nodisk")) {
          config.live.newconfig = 1;
          config.live.nodisk = 1;
        }
        if(slist_getentry(sl, "autopart")) config.live.autopart = 1;
        if(slist_getentry(sl, "-autopart")) config.live.autopart = 0;
        if(slist_getentry(sl, "autoswap")) config.live.autoswap = 1;
        if(slist_getentry(sl, "-autoswap")) config.live.autoswap = 0;
        break;

      case key_liveconfig:
        if(*f->value) {
          config.demo = 1;
          config.activate_storage = 1;
          str_copy(&config.live.cfg, f->value);
        }
        break;

      case key_kernel_pcmcia:
        if(f->is.numeric) config.kernel_pcmcia = f->nvalue;
        break;

      case key_updatename:
        if(*f->value) {
          slist_append_str(&config.update.name_list, f->value);
          config.update.name_added = 1;
        }
        break;

      case key_updatestyle:
        if(f->is.numeric) config.update.style = f->nvalue;
        break;

      case key_updateask:
        if(f->is.numeric) config.update.ask = f->nvalue;
        break;

      case key_loglevel:
        if(f->is.numeric) config.loglevel = f->nvalue;
        break;

      case key_netsetup:
        config.net.do_setup |= DS_SETUP;
        if(f->is.numeric) {
          config.net.setup = f->nvalue ? NS_DEFAULT : 0;
        }
        else {
          sl0 = slist_split(',', f->value);

          config.net.setup = 0;

          for(sl = sl0; sl; sl = sl->next) {
            if(*sl->key == '+' || *sl->key == '-') {
              config.net.setup = NS_DEFAULT;
              break;
            }
          }

          for(sl = sl0; sl; sl = sl->next) {
            s = sl->key;
            if(*s == '+' || *s == '-') s++;
            i = 0;
            if(!strcmp(s, "dhcp")) i = NS_DHCP;
            else if(!strcmp(s, "hostip")) i = NS_HOSTIP;
            else if(!strcmp(s, "netmask")) i = NS_NETMASK;
            else if(!strcmp(s, "gateway")) i = NS_GATEWAY;
            else if(!strncmp(s, "nameserver", sizeof "nameserver" - 1)) {
              i = NS_NAMESERVER;
              t = s + sizeof "nameserver" - 1;
              if(!*t) {
                u = 1;
              }
              else {
                u = strtoul(t, &t, 0);
                if(*t) u = 1;
              }
              config.net.nameservers =
                u > sizeof config.net.nameserver / sizeof *config.net.nameserver ?
                sizeof config.net.nameserver / sizeof *config.net.nameserver :
                u;
            }
            if(i) {
              if(*sl->key == '-') {
                config.net.setup &= ~i;
              }
              else {
                config.net.setup |= i;
              }
            }
          }

          slist_free(sl);
        }
        if(!config.net.setup) config.net.do_setup = 0;
        break;

      case key_rootpassword:
        if(*f->value) str_copy(&config.rootpassword, f->value);
        break;

      case key_loghost:
        str_copy(&config.loghost, f->value);
        break;

      case key_escdelay:
        if(f->is.numeric) config.escdelay = f->nvalue;
        break;

      case key_minmem:
        if(f->is.numeric) config.memory.ram_min = f->nvalue;
        break;

#if defined(__s390__) || defined(__s390x__)
      case key_instnetdev:
        if(*f->value) config.hwp.type=file_sym2num(f->value);
        break;
      case key_iucvpeer:
        if(*f->value) str_copy(&config.hwp.userid, f->value);
        break;
      case key_portname:
        if(*f->value) str_copy(&config.hwp.portname, f->value);
        break;
      case key_readchan:
        if(*f->value) str_copy(&config.hwp.readchan, f->value);
        break;
      case key_writechan:
        if(*f->value) str_copy(&config.hwp.writechan, f->value);
        break;
      case key_datachan:
        if(*f->value) str_copy(&config.hwp.datachan, f->value);
        break;
      case key_ctcprotocol:
        if(f->is.numeric) config.hwp.protocol = f->nvalue + 1;
        break;        
      case key_osamedium:
        if(*f->value) config.hwp.medium=file_sym2num(f->value);
        break;
      case key_osainterface:
        if(*f->value) config.hwp.interface=file_sym2num(f->value);
        break;
      case key_layer2:
        if(f->is.numeric) config.hwp.layer2 = f->nvalue + 1;
        break;
#endif      

      case key_netwait:
        if(f->is.numeric) config.net.ifup_wait = f->nvalue;
        break;

      case key_newid:
        add_driver(f->value);
        break;

      case key_moduledisks:
        if(f->is.numeric) config.module.disks = f->nvalue;
        break;

      case key_zen:
        if(f->is.numeric) config.zen = f->nvalue;
        break;

      case key_zenconfig:
        if(*f->value) str_copy(&config.zenconfig, f->value);
        break;

      case key_rootimage2:
        if(*f->value) str_copy(&config.rootimage2, f->value);
        break;

      case key_options:
        if(*f->value) {
          if((s = strchr(f->value, '='))) {
            *s++ = 0;
            if(config.debug >= 2) fprintf(stderr, "options[%s] = \"%s\"\n", f->value, s);
            sl = slist_add(&config.module.options, slist_new());
            sl->key = strdup(f->value);
            sl->value = strdup(s);
          }
        }
        break;

      case key_xxx:
        if(f->is.numeric) config.xxx = f->nvalue;
        break;

      case key_instsys_complain:
        if(f->is.numeric) config.instsys_complain = f->nvalue;
        break;

      case key_instsys_id:
        str_copy(&config.instsys_id, f->value);
        break;

      case key_initrd_id:
        str_copy(&config.initrd_id, f->value);
        break;

      case key_dud_complain:
        if(f->is.numeric) config.update_complain = f->nvalue;
        break;

      case key_dud_expected:
        if(*f->value) slist_append_str(&config.update.expected_name_list, f->value);
        break;

      case key_staticdevices:
        if(f->is.numeric) config.staticdevices = f->nvalue;
        break;

      case key_withiscsi:
        if(f->is.numeric) config.withiscsi = f->nvalue;
        break;

      case key_startshell:
        if(!*f->value) config.startshell = 1;
        if(f->is.numeric) config.startshell = f->nvalue;
        break;

      case key_ethtool:
        if(*f->value) {
          sl = slist_append(&config.ethtool, slist_new());
          parse_ethtool(sl, f->value);
        }
        break;

      case key_listen:
        if(f->is.numeric) {
          if(activate_network()) {
            config.net.keep = 1;
            str_copy(&config.setupcmd, "inst_setup yast");

            kbd_end(0);

            config.kbd_fd = 0;

            if(config.win) {
              disp_cursor_on();
            }
            if(!config.linemode) {
              if(config.utf8) printf("\033%%G");
              fflush(stdout);
            }
            
            wait_for_conn(f->nvalue);

            config.serial = strdup("/dev/tcp");
            if(!config.linemode) {
              char buf[10];
              fd_set fds;
              struct timeval timeout = { tv_sec: 2 };

              FD_ZERO(&fds);
              FD_SET(0, &fds);

              write(1, "\xff\xfb\03\xff\xfb\x01", 6);
              if(select(1, &fds, NULL, NULL, &timeout)) {
                read(0, buf, 10);
              }
            }
            kbd_init(1);
            disp_init();
            if(config.win) {
              disp_cursor_off();
              if(!config.linemode) disp_restore_screen();
            }
          }
        }
        break;

      case key_zombies:
        if(f->is.numeric) config.zombies = f->nvalue;
        break;

      case key_dhcpcd:
        if(*f->value) str_copy(&config.net.dhcpcd, f->value);
        break;

      case key_wlan_essid:
        str_copy(&config.net.wlan.essid, *f->value ? f->value : NULL);
        break;

      case key_wlan_key_ascii:
        str_copy(&config.net.wlan.key, *f->value ? f->value : NULL);
        config.net.wlan.key_type = kt_ascii;
        break;

      case key_wlan_key_hex:
        str_copy(&config.net.wlan.key, *f->value ? f->value : NULL);
        config.net.wlan.key_type = kt_hex;
        break;

      case key_wlan_key_pass:
        str_copy(&config.net.wlan.key, *f->value ? f->value : NULL);
        config.net.wlan.key_type = kt_pass;
        break;

      case key_wlan_key_len:
        if(f->is.numeric) config.net.wlan.key_len = f->nvalue;
        break;

      case key_wlan_auth:
        if(f->is.numeric) config.net.wlan.auth = f->nvalue;
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

  f0 = file_read_file(YAST_INF_FILE, kf_yast);

  for(f = f0; f; f = f->next) {
    switch(f->key) {
      case key_root:
        root = 1;
        if(!f->is.numeric) root_set_root(f->value);
        reboot_ig = f->nvalue;
        break;

      case key_keytable:
        set_activate_keymap(*f->value ? f->value : NULL);
        break;

      case key_language:
        config.language = set_langidbyname(f->value);
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

  set_activate_language(config.language);

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
  file_write_inet_str(f, file_key2str(key), inet);
}


void file_write_inet_str(FILE *f, char *name, inet_t *inet)
{
  char *s = NULL;

  if(inet->ok) s = inet_ntoa(inet->ip);
  if(!s) s = inet->name;

  if(s) fprintf(f, "%s: %s\n", name, s);
}


void file_write_inet_both(FILE *f, file_key_t key, inet_t *inet)
{
  char *key_name = file_key2str(key);
  char *ip = NULL;

  if(inet->ok) {
    ip = inet_ntoa(inet->ip);
    fprintf(f, "%sIP: %s\n", key_name, ip);
  }

  if(inet->name && *inet->name && (!ip || strcmp(ip, inet->name))) {
    fprintf(f, "%sName: %s\n", key_name, inet->name);
  }
}


void file_write_install_inf(char *dir)
{
  FILE *f;
  char file_name[256], buf[256], *s;
  slist_t *sl;
  file_t *ft0, *ft;
  int i;

  strcat(strcpy(file_name, dir), INSTALL_INF_FILE);

  if(!(f = fopen(file_name, "w"))) {
    fprintf(stderr, "Cannot open yast info file\n");
    return;
  }

  set_write_info(f);

  file_write_num(f, key_sourcemounted, config.instdata_mounted);

  file_write_sym(f, key_display, "Undef", config.color);

  if(config.keymap && config.manual) {
    file_write_str(f, key_keytable, config.keymap);
  }

  file_write_str(f, key_cdrom, config.cdrom);
  if(config.net.do_setup && config.net.device) {
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

#if 0
#if WITH_PCMCIA
  if(pcmcia_chip_ig == 1 || pcmcia_chip_ig == 2) {
    file_write_str(f, key_pcmcia, pcmcia_driver(pcmcia_chip_ig));
  }
#endif
#endif

  file_write_num(f, key_haspcmcia, config.has_pcmcia);

  file_write_num(f, key_nopcmcia, config.nopcmcia);

  file_write_str(f, key_console, config.serial);

  file_write_str(f, key_instmode,
    get_instmode_name(config.instmode_extra == inst_dvd ? config.instmode_extra : config.instmode)
  );

  if(config.insttype == inst_hd) {
    file_write_str(f, key_partition, config.partition);
    file_write_str(f, key_serverdir, config.serverdir);
  }

  if(config.net.do_setup) {
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
    file_write_str(f, key_netconfig, s);
    file_write_str(f, key_netdevice, config.net.device);
    if(config.manual < 2) get_net_unique_id();
    file_write_str(f, key_netid, config.net.unique_id);
    file_write_str(f, key_nethwaddr, config.net.hwaddr);
    file_write_str(f, key_ethtool, config.net.ethtool_used);
    file_write_inet(f, key_ip, &config.net.hostname);
    if(config.net.realhostname) {
      file_write_str(f, key_hostname, config.net.realhostname);
    }
    else {
      file_write_str(f, key_hostname, config.net.hostname.name);
    }
    file_write_inet(f, key_broadcast, &config.net.broadcast);
    file_write_inet(f, key_network, &config.net.network);
    if(config.net.ptphost.ok) {
      file_write_inet(f, key_ptphost, &config.net.ptphost);
    }
    else {
      file_write_inet(f, key_netmask, &config.net.netmask);
    }
    file_write_inet(f, key_gateway, &config.net.gateway);
    for(i = 0; i < config.net.nameservers; i++) {
      s = file_key2str(key_nameserver);
      if(i) { sprintf(buf, "%s%d", s, i + 1); s = buf; }
      file_write_inet_str(f, s, &config.net.nameserver[i]);
    }
    file_write_inet(f, key_server, &config.net.server);
    file_write_inet_both(f, key_server, &config.net.server);
    file_write_str(f, key_serverdir, config.serverdir);
    file_write_str(f, key_domain, config.net.domain);
    file_write_str(f, key_smbshare, config.net.share);
  }

  if(config.net.port) {
    file_write_num(f, key_port, config.net.port);
  }

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

  file_write_str(f, key_loghost, config.loghost);
  file_write_str(f, key_username, config.net.user);
  file_write_str(f, key_password, config.net.password);
  file_write_str(f, key_workdomain, config.net.workgroup);

  fprintf(f, "SourceType: %s\n", config.sourcetype ? "file" : "dir");

#if 0
  if(config.serverpath && config.serverfile) {
    fprintf(f, "ServerPath: %s\nServerFile: %s\n", config.serverpath, config.serverfile);
  }
#endif

  file_write_modparms(f);

  file_write_num(f, key_manual, config.manual);
  file_write_num(f, key_demo, config.demo);

  if(reboot_ig) file_write_num(f, key_reboot, reboot_ig);

  if(config.floppies) {
    file_write_str(f, key_floppydisk, config.floppy_dev[config.floppy]);
  }

  file_write_num(f, key_keyboard, 1);	/* we always have one - what's the point ??? */
  file_write_str(f, key_updatedir, config.update.dir);
  file_write_num(f, key_yast2update, config.update.ask || config.update.count ? 1 : 0);

  file_write_num(f, key_textmode, config.textmode);

  file_write_str(f, key_autoyast, config.autoyast);

  util_update_meminfo();
  file_write_num(f, key_memfree, config.memory.current);

  file_write_num(f, key_vnc, config.vnc);
  file_write_str(f, key_vncpassword, config.net.vncpassword);
  file_write_inet(f, key_displayip, &config.net.displayip);
  file_write_inet(f, key_ptphost, &config.net.ptphost);
  file_write_num(f, key_usessh, config.usessh);
  if(
    config.rootpassword &&
    strcmp(config.rootpassword, "ask")
  ) file_write_str(f, key_rootpassword, config.rootpassword);

  if(yast2_color_ig) {
    fprintf(f, "%s: %06x\n", file_key2str(key_yast2color), yast2_color_ig);
  }

  if(config.noshell) file_write_num(f, key_noshell, config.noshell);

#if 0
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

  ft0 = file_read_cmdline(kf_cmd + kf_cmd_early + kf_boot);

  for(i = 0, ft = ft0; ft; ft = ft->next) {
    if(ft->key == key_none) {
      fprintf(f, "%s%s", i ? " " : "Cmdline: ", ft->unparsed);
      i = 1;
    }
  }
  if(i) fprintf(f, "\n");

  file_write_str(f, key_initrd_id, config.initrd_id);
  file_write_str(f, key_instsys_id, config.instsys_id);

  file_write_num(f, key_withiscsi, config.withiscsi);

  file_write_num(f, key_startshell, config.startshell);


  file_free_file(ft0);

  fclose(f);
}


void file_write_modparms(FILE *f)
{
  file_t *ft0, *ft;
  module_t *ml;
  slist_t *sl0 = NULL, *sl1, *sl, *pl0, *pl;
  slist_t *initrd0 = NULL, *initrd;
  slist_t *modules0 = NULL;

  ft0 = file_read_file("/proc/modules", kf_none);

  /* build list of modules & initrd modules, reverse /proc/modules order! */
  for(ft = ft0; ft; ft = ft->next) {
    ml = mod_get_entry(ft->key_str);
    if(ml) {
      sl = slist_add(&modules0, slist_new());
      sl->key = strdup(ml->name);
      if(ml->initrd || slist_getentry(config.module.initrd, ml->name)) {
        sl = slist_add(&sl0, slist_new());
        sl->key = strdup(ml->name);
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


file_t *file_read_cmdline(file_key_flag_t flags)
{
  FILE *f;
  file_t *ft;
  char **argv, *cmdline = NULL;

  if(config.test) {
    if(!config.had_segv) {
      for((argv = config.argv) && argv++; *argv; argv++) {
        strprintf(&cmdline, "%s \"%s\"", cmdline ?: "", *argv);
      }
    }
  }
  else {
    if(!(f = fopen(CMDLINE_FILE, "r"))) return NULL;
    cmdline = calloc(1024, 1);
    if(!fread(cmdline, 1, 1023, f)) *cmdline = 0;
    fclose(f);
  }

  ft = file_parse_buffer(cmdline, flags);

  free(cmdline);

  return ft;
}


file_t *file_parse_buffer(char *buf, file_key_flag_t flags)
{
  file_t *ft0 = NULL, **ft = &ft0;
  char *current, *s, *s1, *t, *t1;
  int i, quote;

  if(!buf) return NULL;

  current = buf;

  do {
    while(isspace(*current)) current++;
    for(quote = 0, s = current; *s && (quote || !isspace(*s)); s++) {
      if(quote) {
        if(*s == quote) quote = 0;
      }
      else {
        if(*s == '"' || *s == '\'') quote = *s;
      }
    }
    if(s > current) {
      t = malloc(s - current + 1);
      t1 = malloc(s - current + 1);

      memcpy(t1, current, s - current);
      t1[s - current] = 0;

      for(quote = 0, s1 = t; s > current; current++) {
        if(quote) {
          if(*current == quote) {
            quote = 0;
          }
          else {
            *s1++ = *current;
          }
        }
        else {
          if(*current == '"' || *current == '\'') {
            quote = *current;
          }
          else {
            *s1++ = *current;
          }
        }
      }
      *s1 = 0;

      if((s1 = strchr(t, '='))) *s1++ = 0;

      *ft = calloc(1, sizeof **ft);

      i = strlen(t);
      if(i && t[i - 1] == ':') t[i - 1] = 0;

      (*ft)->unparsed = t1;
      (*ft)->key_str = strdup(t);
      (*ft)->key = file_str2key(t, flags);	/* destroys t!!! */
      (*ft)->value = strdup(s1 ?: "");

      parse_value(*ft);

      free(t);

      ft = &(*ft)->next;
    }
  }
  while(*current);

  return ft0;
}


/*
 * Returns last matching entry.
 */
file_t *file_get_cmdline(file_key_t key)
{
  static file_t *cmdline = NULL;
  file_t *ft, *ft_ok = NULL;

  if(!cmdline) cmdline = file_read_cmdline(kf_cmd + kf_cmd_early);

  for(ft = cmdline; ft; ft = ft->next) {
    if(ft->key == key) ft_ok = ft;
  }

  return ft_ok;
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


void file_write_live_config(char *dir)
{
  FILE *f;
  char file_name[256];
  slist_t *sl;

  strcat(strcpy(file_name, dir), LIVE_CONF_FILE);

  if(!(f = fopen(file_name, "w"))) {
    fprintf(stderr, "Cannot open live info file\n");
    return;
  }

  if(config.live.args) {
    fprintf(f, "%s=", file_key2str(key_live));
    for(sl = config.live.args; sl; sl = sl->next) {
      fprintf(f, "%s%c", sl->key, sl->next ? ',' : '\n');
    }
  }
  fprintf(f, "liveconfig=%s\n", config.live.cfg);
  fprintf(f, "l_new=%s\n", config.live.newconfig ? "1" : "");
  fprintf(f, "l_nodisk=%s\n", config.live.nodisk ? "1" : "");
  fprintf(f, "l_swapfile=%s\n", config.live.swapfile ? "1" : "");
  fprintf(f, "l_swappart=%s\n", config.live.useswap ? config.live.useswap->key : "");
  fprintf(f, "l_partition=%s\n", config.partition ?: "");

  fclose(f);
}


void add_driver(char *str)
{
  driver_t drv = { vendor:~0, device:~0, subvendor:~0, subdevice:~0 };
  slist_t *sl0;
  char *pci = NULL, *mod = NULL, *sys = NULL;
  int i;

  if(!str || !*str) return;

  sl0 = slist_split(',', str);

  if(sl0 && sl0->key) {
    pci = sl0->key;
    if(!*pci) pci = NULL;
    if(sl0->next && sl0->next->key) {
      mod = sl0->next->key;
      if(!*mod) mod = NULL;
      if(sl0->next->next && sl0->next->next->key) {
        sys = sl0->next->next->key;
        if(!*sys) sys = NULL;
      }
    }
  }

  if(pci && (mod || sys)) {

    i = sscanf(pci, " %x %x %x %x %x %x %lx",
      &drv.vendor, &drv.device,
      &drv.subvendor, &drv.subdevice,
      &drv.class, &drv.class_mask,
      &drv.driver_data
    );

    if(i > 0) {
      if(mod) drv.name = strdup(mod);
      if(sys) drv.sysfs_name = strdup(sys);

      drv.next = config.module.drivers;
      config.module.drivers = malloc(sizeof *config.module.drivers);
      memcpy(config.module.drivers, &drv, sizeof *config.module.drivers);

      fprintf(stderr, "new id: %s,%s,%s\n",
        print_driverid(config.module.drivers, 1),
        config.module.drivers->name ?: "",
        config.module.drivers->sysfs_name ?: ""
      );

      store_driverid(config.module.drivers);

      if(apply_driverid(config.module.drivers)) sleep(config.module.delay + 1);

    }
  }

  slist_free(sl0);
}


void get_ide_options()
{
  file_t *f0, *f;
  char *buf = NULL;
  slist_t *sl;

  f0 = file_read_cmdline(0);
  for(f = f0; f; f = f->next) {
    if(
      !strncmp(f->key_str, "ide", sizeof "ide" - 1) ||
      (
        !strncmp(f->key_str, "hd", sizeof "hd" - 1) &&
        strlen(f->key_str) == 3
      )
    ) {
      strprintf(&buf, "%s%s ", buf ?: "options=\"", f->unparsed);
    }
  }

  file_free_file(f0);

  if(buf) {
    buf[strlen(buf) - 1] = '"';
    sl = slist_add(&config.module.options, slist_new());
    sl->key = strdup("ide-core");
    sl->value = buf;
  }
}


/*
 * Parse str and return result in sl. Modifies str.
 *
 * Syntax: [if:]ethtool_options
 */
void parse_ethtool(slist_t *sl, char *str)
{
  char *s1, *s2;

  s1 = strchr(str, ' ');
  s2 = strchr(str, '=');

  if(s2 && (!s1 || s1 > s2)) {
    *s2++ = 0;
    while(*s2 == ' ') s2++;
    str_copy(&sl->key, str);
    str_copy(&sl->value, s2);
  }
  else {
    str_copy(&sl->key, "*");
    while(*str == ' ') str++;
    str_copy(&sl->value, str);
  }
}


void wait_for_conn(int port)
{
  int fd, sock;
  struct sockaddr_in addr = { sin_family: AF_INET, sin_addr: { s_addr: INADDR_ANY } };
  struct sockaddr peer;
  socklen_t peer_len;

  addr.sin_port = htons(port);

  printf("\n%s waiting for connection on port %d\n", inet2print(&config.net.hostname), port);
  fflush(stdout);

  if(
    (sock = socket(AF_INET, SOCK_STREAM, 0)) == -1 ||
    bind(sock, (struct sockaddr *) &addr, sizeof addr) ||
    listen(sock, 5) ||
    (fd = accept(sock, &peer, &peer_len)) == -1
  ) {
    close(sock);
    return;
  }

  dup2(fd, 0);
  dup2(fd, 1);
  // dup2(fd, 2);
  close(fd);

  setvbuf(stdin, NULL, _IONBF, 0);
  setvbuf(stdout, NULL, _IONBF, 0);
  // setvbuf(stderr, NULL, _IONBF, 0);

  close(sock);

  config.listen = 1;
}


int activate_network()
{
  if(config.net.configured != nc_none || config.test) return 1;

  load_network_mods();

  if(!config.net.device) {
    util_update_netdevice_list(NULL, 1);
    if(config.net.devices) str_copy(&config.net.device, config.net.devices->key);
  }

  if(!config.net.hostname.ok || !config.net.netmask.ok) {
    config.net.use_dhcp ? net_dhcp() : net_bootp();
    if(!config.net.hostname.ok) {
      fprintf(stderr, "%s: DHCP network setup failed\n", config.net.device);
      return 0;
    }

    config.net.configured = config.net.use_dhcp ? nc_dhcp : nc_bootp;
  }
  else {
    if(net_activate_ns()) {
      fprintf(stderr, "net activation failed\n");
      return 0;
    }

    config.net.configured = nc_static;
  }

  return 1;
}


slist_t *file_parse_xmllike(char *name, char *tag)
{
  slist_t *sl, *sl0 = NULL;
  FILE *f;
  char *buf = NULL, *tag_start = NULL, *tag_end = NULL;
  char *attr = NULL, *data = NULL;
  int buf_size = 0, buf_ptr = 0, i;
  char *ptr, *s0, *s1;

  if(!tag) return sl0;

  if(!(f = fopen(name, "r"))) return sl0;

  do {
    buf = realloc(buf, buf_size += 0x1000);
    i = fread(buf + buf_ptr, 1, buf_size - buf_ptr - 1, f);
    buf_ptr += i;
  }
  while(buf_ptr == buf_size - 1);

  buf[buf_ptr] = 0;

  fclose(f);

  if(!(buf_size = buf_ptr)) return sl0;

  strprintf(&tag_start, "<%s ", tag);
  strprintf(&tag_end, "</%s>", tag);

  ptr = buf;

  while(*ptr) {
    if(attr) {
      if((s0 = strstr(ptr, tag_end))) {
        *s0 = 0;
        ptr = s0 + strlen(tag_end);

        i = strlen(data);
        while(i > 0 && isspace(data[i - 1])) data[--i] = 0;

        sl = slist_append(&sl0, slist_new());
        str_copy(&sl->key, attr);
        str_copy(&sl->value, data);

        attr = data = NULL;
      }
      else {
        break;
      }
    }
    else {
      if((s0 = strstr(ptr, tag_start)) && (s1 = strchr(s0, '>'))) {
        *s1++ = 0;
        s0 += strlen(tag_start);
        while(isspace(*s0)) s0++;
        while(isspace(*s1)) s1++;
        attr = s0;
        ptr = data = s1;
      }
      else {
        break;
      }
    }
  }

  free(buf);
  free(tag_start);
  free(tag_end);

#if 0
  for(sl = sl0; sl; sl = sl->next) {
    fprintf(stderr, "key = \'%s\'\n", sl->key);
    fprintf(stderr, "value = \'%s\'\n", sl->value);
  }
#endif

  return sl0;
}

