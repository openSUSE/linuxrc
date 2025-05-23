/*
 *
 * file.c        File access
 *
 * Copyright (c) 1996-2002  Hubert Mantel, SuSE Linux AG  (mantel@suse.de)
 *
 */

#define _GNU_SOURCE		/* getline, fnmatch */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <fnmatch.h>
#include <signal.h>
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
#include "util.h"
#include "module.h"
#include "window.h"
#include "dialog.h"
#include "net.h"
#include "settings.h"
#include "auto2.h"
#include "display.h"
#include "keyboard.h"
#include "url.h"

#define YAST_INF_FILE		"/etc/yast.inf"
#define INSTALL_INF_FILE	"/etc/install.inf"
#define CMDLINE_FILE		"/proc/cmdline"

// #define DEBUG_FILE

#define INET_WRITE_IP		1
#define INET_WRITE_IP_BOTH	2
#define INET_WRITE_NAME_OR_IP	4
#define INET_WRITE_PREFIX	8

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

static void add_driver(char *str);
static void parse_ethtool(slist_t *sl, char *str);
static void wait_for_conn(int port);


static struct {
  file_key_t key;
  char *value;
  file_key_flag_t flags;
} keywords[] = {
  { key_none,           "",               kf_none                        },
  { key_root,           "Root",           kf_yast + kf_boot              },
  { key_keytable,       "Keytable",       kf_cfg + kf_cmd + kf_yast      },
  { key_language,       "Language",       kf_cfg + kf_cmd + kf_yast      },
  { key_language,       "lang",           kf_cfg + kf_cmd                },
  { key_rebootmsg,      "RebootMsg",      kf_yast                        },
  { key_insmod,         "Insmod",         kf_cfg + kf_cmd1               },
  { key_insmod_pre,     "InsmodPre",      kf_cfg + kf_cmd0               },
  { key_display,        "Display",        kf_cfg + kf_cmd                },
  { key_ip,             "IP",             kf_none                        },
  { key_netmask,        "Netmask",        kf_cfg + kf_cmd + kf_dhcp      },
  { key_gateway,        "Gateway",        kf_cfg + kf_cmd + kf_dhcp      },
  { key_gateway,        "Gateways",       kf_cfg + kf_cmd + kf_dhcp      },
  { key_server,         "Server",         kf_none                        },
  { key_nameserver,     "Nameserver",     kf_cfg + kf_cmd                },
  { key_network,        "Network",        kf_cfg + kf_cmd + kf_dhcp      },
  { key_partition,      "Partition",      kf_cfg + kf_cmd                },
  { key_serverdir,      "Serverdir",      kf_none                        },
  { key_netdevice,      "Netdevice",      kf_cfg + kf_cmd                },
  { key_forcerootimage, "ForceRootimage", kf_cfg + kf_cmd                },
  { key_forcerootimage, "LoadImage",      kf_cfg + kf_cmd                },
  { key_rebootwait,     "WaitReboot",     kf_cfg + kf_cmd                },	/* drop it? */
  { key_sourcemounted,  "Sourcemounted",  kf_none                        },
  { key_cdrom,          "Cdrom",          kf_none                        },
  { key_console,        "Console",        kf_cmd0                        },
  { key_ptphost,        "Pointopoint",    kf_cfg + kf_cmd                },
  { key_domain,         "Domain",         kf_cfg + kf_cmd + kf_dhcp      },
  { key_domain,         "DNSDOMAIN",      kf_cfg + kf_cmd + kf_dhcp      },
  { key_manual,         "Manual",         kf_cfg + kf_cmd + kf_cmd_early },
  { key_reboot,         "Reboot",         kf_none                        },	/* drop it? */
  { key_floppydisk,     "Floppydisk",     kf_none                        },	/* ??? */
  { key_keyboard,       "Keyboard",       kf_none                        },
  { key_yast2update,    "YaST2update",    kf_none                        },
  { key_textmode,       "Textmode",       kf_cfg + kf_cmd                },
  { key_bootdisk,       "BootDisk",       kf_none                        },	/* obsolete */
  { key_disks,          "Disks",          kf_none                        },	/* obsolete */
  { key_username,       "Username",       kf_cfg + kf_cmd                },
  { key_password,       "Password",       kf_cfg + kf_cmd                },
  { key_passwordenc,    "PasswordEnc",    kf_cfg + kf_cmd                },
  { key_workdomain,     "WorkDomain",     kf_cfg + kf_cmd                },
  { key_alias,          "Alias",          kf_none                        },
  { key_options,        "Options",        kf_cfg + kf_cmd_early          },
  { key_initrdmodules,  "InitrdModules",  kf_cfg + kf_cmd                },
  { key_locale,         "Locale",         kf_none                        },
  { key_font,           "Font",           kf_none                        },
  { key_screenmap,      "Screenmap",      kf_none                        },
  { key_fontmagic,      "Fontmagic",      kf_none                        },
  { key_autoyast,       "AutoYaST",       kf_cfg + kf_cmd_early          },
  { key_autoyast,       "AY",             kf_cfg + kf_cmd_early          },
  { key_autoyast_parse, "AutoYaSTParse",  kf_cfg + kf_cmd_early          },
  { key_autoyast_parse, "AYParse",        kf_cfg + kf_cmd_early          },
  { key_autoyast_passurl, "AutoYaSTPassURL",  kf_cfg + kf_cmd_early      },
  { key_autoyast2,      "AutoYaST2",      kf_cfg + kf_cmd_early          },
  { key_linuxrc,        "linuxrc",        kf_cfg + kf_cmd_early          },
  { key_forceinsmod,    "ForceInsmod",    kf_cfg + kf_cmd                },
  { key_ipaddr,         "IPAddr",         kf_dhcp                        },
  { key_hostname,       "Hostname",       kf_cfg + kf_cmd                },
  { key_dns,            "DNS",            kf_dhcp                        },
  { key_dns,            "DNSSERVERS",     kf_dhcp                        },
  { key_dhcpsiaddr,     "DHCPSIAddr",     kf_dhcp                        },
  { key_rootpath,       "RootPath",       kf_dhcp                        },
  { key_bootfile,       "BootFile",       kf_dhcp                        },
  { key_install,        "Install",        kf_cfg + kf_cmd                },
  { key_install,        "Repo",           kf_cfg + kf_cmd                },
  { key_instsys,        "InstSys",        kf_cfg + kf_cmd                },
  { key_instmode,       "InstMode",       kf_none                        },
  { key_memtotal,       "MemTotal",       kf_mem                         },
  { key_memfree,        "MemFree",        kf_mem                         },
  { key_buffers,        "Buffers",        kf_mem                         },
  { key_cached,         "Cached",         kf_mem                         },
  { key_swaptotal,      "SwapTotal",      kf_mem                         },
  { key_swapfree,       "SwapFree",       kf_mem                         },
  { key_memlimit,       "MemLimit",       kf_cfg + kf_cmd                },
  { key_memyast,        "MemYaST",        kf_cfg + kf_cmd                },
  { key_memloadimage,   "MemLoadImage",   kf_cfg + kf_cmd                },
  { key_info,           "Info",           kf_cfg + kf_cmd_early          },
  { key_proxy,          "Proxy",          kf_cfg + kf_cmd                },
  { key_usedhcp,        "UseDHCP",        kf_cfg + kf_cmd                },
  { key_dhcptimeout,    "DHCPTimeout",    kf_cfg + kf_cmd_early          },
  { key_dhcptimeout,    "WickedTimeout",  kf_cfg + kf_cmd_early          },
  { key_tftptimeout,    "TFTPTimeout",    kf_cfg + kf_cmd                },
  { key_tmpfs,          "_TmpFS",         kf_cmd                         },
  { key_netstop,        "NetStop",        kf_cfg + kf_cmd                },
  { key_testmode,       "_TestMode",      kf_cfg                         },
  { key_debugwait,      "DebugWait",      kf_cfg + kf_cmd + kf_cmd_early },
  { key_expert,         "Expert",         kf_cfg + kf_cmd                },	/* drop it? */
  { key_rescue,         "Rescue",         kf_cfg + kf_cmd                },
  { key_rootimage,      "RootImage",      kf_cfg + kf_cmd                },
  { key_rescueimage,    "RescueImage",    kf_cfg + kf_cmd                },
  { key_vnc,            "VNC",            kf_cfg + kf_cmd                },
  { key_vnc,            "UseVNC",         kf_cfg + kf_cmd                },
  { key_usessh,         "UseSSH",         kf_cfg + kf_cmd                },
  { key_usessh,         "SSH",            kf_cfg + kf_cmd                },
  { key_sshd,           "SSHD",           kf_cfg + kf_cmd                },
  { key_vncpassword,    "VNCPassword",    kf_cfg + kf_cmd                },
  { key_displayip,	"Display_IP",     kf_cfg + kf_cmd		 },
  { key_sshpassword,    "SSHPassword",    kf_cfg + kf_cmd                },
  { key_sshpasswordenc, "SSHPasswordEnc", kf_cfg + kf_cmd                },
  { key_sshkey,         "SSHKey",         kf_cfg + kf_cmd                },
  { key_term,           "TERM",           kf_cfg + kf_cmd                },
  { key_addswap,        "AddSwap",        kf_cfg + kf_cmd                },
  { key_aborted,        "Aborted",        kf_yast                        },
  { key_exec,           "Exec",           kf_cfg + kf_cmd                },
  { key_usbwait,        "USBWait",        kf_cfg + kf_cmd + kf_cmd_early },
  { key_nfsrsize,       "NFS.RSize",      kf_cfg + kf_cmd                },
  { key_nfswsize,       "NFS.WSize",      kf_cfg + kf_cmd                },
  { key_setupcmd,       "SetupCmd",       kf_cfg + kf_cmd                },
  { key_setupnetif,     "SetupNetIF",     kf_cfg + kf_cmd                },
  { key_netconfig,      "NetConfig",      kf_none                        },
  { key_noshell,        "NoShell",        kf_cfg + kf_cmd + kf_cmd_early },
  { key_device,         "CDROMDevice",    kf_cfg + kf_cmd                },
  { key_consoledevice,  "ConsoleDevice",  kf_cfg + kf_cmd                },
  { key_product,        "Product",        kf_cfg + kf_cmd                },
  { key_productdir,     "ProductDir",     kf_cfg + kf_cmd                },
  { key_linuxrcstderr,  "LinuxrcSTDERR",  kf_cfg + kf_cmd + kf_cmd_early },
  { key_linuxrcstderr,  "LinuxrcLog",     kf_cfg + kf_cmd + kf_cmd_early },
  { key_comment,        "#",              kf_cfg                         },
  { key_kbdtimeout,     "KBDTimeout",     kf_cfg + kf_cmd                },
  { key_brokenmodules,  "BrokenModules",  kf_cfg + kf_cmd + kf_cmd_early },
  { key_scsibeforeusb,  "SCSIBeforeUSB",  kf_cfg + kf_cmd + kf_cmd_early },
  { key_hostip,         "HostIP",         kf_cfg + kf_cmd                },
  { key_linemode,       "Linemode",       kf_cfg + kf_cmd + kf_cmd_early },
  { key_moduledelay,    "ModuleDelay",    kf_cfg + kf_cmd + kf_cmd_early },
  { key_updatedir,      "UpdateDir",      kf_cfg + kf_cmd                },
  { key_scsirename,     "SCSIRename",     kf_cfg + kf_cmd + kf_cmd_early },
  { key_doscsirename,   "DoSCSIRename",   kf_cfg + kf_cmd                },
  { key_lxrcdebug,      "LXRCDebug",      kf_cfg + kf_cmd + kf_cmd_early },
  { key_lxrcdebug,      "LinuxrcDebug",   kf_cfg + kf_cmd + kf_cmd_early },
  { key_updatename,     "UpdateName",     kf_cfg + kf_cmd                },
  { key_updatestyle,    "UpdateStyle",    kf_cfg + kf_cmd                },
  { key_updateid,       "UpdateID",       kf_cfg                         },
  { key_updateprio,     "UpdatePriority", kf_cfg                         },
  { key_updateask,      "DriverUpdate",   kf_cfg + kf_cmd                },
  { key_updateask,      "DUD",            kf_cfg + kf_cmd                },
  { key_initrd,         "Initrd",         kf_boot                        },
  { key_vga,            "VGA",            kf_cmd_early                   },
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
  { key_osainterface,	"OSAInterface",	  kf_cfg + kf_cmd		 },
  { key_layer2,		"Layer2",	  kf_cfg + kf_cmd		 },
  { key_portno,         "PortNo",         kf_cfg + kf_cmd                },
  { key_osahwaddr,	"OSAHWAddr",      kf_cfg + kf_cmd		 },
#endif
  { key_netwait,        "NetWait",        kf_cfg + kf_cmd_early          },
  { key_newid,          "NewID",          kf_cfg + kf_cmd_early          },
  { key_moduledisks,    "ModuleDisks",    kf_cfg + kf_cmd                },
  { key_zen,            "Zen",            kf_cfg + kf_cmd + kf_cmd_early },
  { key_zenconfig,      "ZenConfig",      kf_cfg + kf_cmd + kf_cmd_early },
  { key_port,           "Port",           kf_none                        },
  { key_smbshare,       "Share",          kf_none                        },
  { key_rootimage2,     "RootImage2",     kf_cfg + kf_cmd                },
  { key_instsys_id,     "InstsysID",      kf_cfg + kf_cmd                },
  { key_initrd_id,      "InitrdID",       kf_cfg + kf_cmd                },
  { key_instsys_complain, "InstsysComplain", kf_cfg + kf_cmd             },
  { key_dud_complain,   "UpdateComplain", kf_cfg + kf_cmd                },
  { key_dud_expected,   "UpdateExpected", kf_cfg + kf_cmd                },
  { key_withiscsi,      "WithiSCSI",      kf_cfg + kf_cmd                },
  { key_ethtool,        "ethtool",        kf_cfg + kf_cmd_early          },
  { key_listen,         "listen",         kf_cfg + kf_cmd                },
  { key_zombies,        "Zombies",        kf_cfg + kf_cmd                },
  { key_wlan_essid,     "WlanESSID",      kf_cfg + kf_cmd_early          },
  { key_wlan_essid,     "ESSID",          kf_cfg + kf_cmd_early          },
  { key_wlan_auth,      "WlanAuth",       kf_cfg + kf_cmd_early          },
  { key_wlan_wpa_psk,   "WPAPSK",         kf_cfg + kf_cmd_early          },
  { key_wlan_wpa_pass,  "WPAPassword",    kf_cfg + kf_cmd_early          },
  { key_wlan_wpa_id,    "WPAIdentity",    kf_cfg + kf_cmd_early          },
  { key_wlan_device,    "WlanDevice",     kf_cfg + kf_cmd_early          },
  { key_netcardname,    "NetCardName",    kf_none                        },
  { key_ibft_hwaddr,    "iSCSI_INITIATOR_HWADDR",   kf_ibft              },
  { key_ibft_ipaddr,    "iSCSI_INITIATOR_IPADDR",   kf_ibft              },
  { key_ibft_netmask,   "iSCSI_INITIATOR_NETMASK",  kf_ibft              },
  { key_ibft_gateway,   "iSCSI_INITIATOR_GATEWAY",  kf_ibft              },
  { key_ibft_dns,       "iSCSI_INITIATOR_DNSADDR1", kf_ibft              },
  { key_net_retry,      "NetRetry",       kf_cfg + kf_cmd                },
  { key_bootif,         "BOOTIF",         kf_cmd                         },
  { key_swap_size,      "SwapSize",       kf_cfg + kf_cmd                },
  { key_ntfs_3g,        "UseNTFS-3G",     kf_cfg + kf_cmd + kf_cmd_early },
  { key_hash,           "HASH",           kf_cfg + kf_cont               },
  { key_insecure,       "Insecure",       kf_cfg + kf_cmd + kf_cmd_early },
  { key_sslcerts,       "SSLCerts",       kf_cfg + kf_cmd + kf_cmd_early },
  { key_kexec,          "kexec",          kf_cfg + kf_cmd                },
  { key_kexec_reboot,   "kexec_reboot",   kf_cfg + kf_cmd                },
  { key_nisdomain,      "NISDomain",      kf_dhcp                        },
  { key_nomodprobe,     "nomodprobe",     kf_cfg + kf_cmd_early          },
  { key_device,         "Device",         kf_cfg + kf_cmd                },
  { key_nomdns,         "NoMDNS",         kf_cfg + kf_cmd                },
  { key_yepurl,         "regurl",         kf_cfg + kf_cmd                },
  { key_mediacheck,     "mediacheck",     kf_cfg + kf_cmd_early          },
  { key_y2gdb,          "Y2GDB",          kf_cfg + kf_cmd                },
  { key_squash,         "squash",         kf_cfg + kf_cmd                },
  { key_devbyid,        "devbyid",        kf_cfg + kf_cmd_early          },
  { key_braille,        "braille",        kf_cfg + kf_cmd_early          },
  { key_nfsopts,        "nfs.opts",       kf_cfg + kf_cmd                },
  { key_ipv4,           "ipv4",           kf_cfg + kf_cmd + kf_cmd_early },
  { key_ipv4only,       "ipv4only",       kf_cfg + kf_cmd + kf_cmd_early },
  { key_ipv6,           "ipv6",           kf_cfg + kf_cmd + kf_cmd_early },
  { key_ipv6only,       "ipv6only",       kf_cfg + kf_cmd + kf_cmd_early },
  { key_efi,            "EFI",            kf_cfg + kf_cmd                },
  { key_supporturl,     "supporturl",     kf_cfg + kf_cmd                },
  { key_udevrule,       "udev.rule",      kf_cfg + kf_cmd_early          },
  { key_dhcpfail,       "DHCPFail",       kf_cfg + kf_cmd                },
  { key_namescheme,     "NameScheme",     kf_cfg + kf_cmd + kf_cmd_early },
  { key_ptoptions,      "PTOptions",      kf_cfg + kf_cmd_early          },
  { key_withfcoe,       "WithFCoE",       kf_cfg + kf_cmd                },
  { key_digests,        "Digests",        kf_cfg + kf_cmd + kf_cmd_early },
  { key_plymouth,       "Plymouth",       kf_cfg + kf_cmd_early          },
  { key_plymouth,       "PlymouthEnable", kf_cfg + kf_cmd_early          },
  { key_restart,        "Restart",        kf_cfg                         },
  { key_restarted,      "Restarted",      kf_cfg                         },
  { key_withipoib,      "WithIPoIB",      kf_cfg + kf_cmd_early          },
  { key_upgrade,        "Upgrade",        kf_cfg + kf_cmd                },
  { key_media_upgrade,  "MediaUpgrade",   kf_cfg + kf_cmd                },
  { key_ifcfg,          "ifcfg",          kf_cfg + kf_cmd_early          },
  { key_defaultinstall, "DefaultInstall", kf_cfg + kf_cmd                },
  { key_defaultinstall, "DefaultRepo",    kf_cfg + kf_cmd                },
  { key_nanny,          "nanny",          kf_cfg + kf_cmd_early          },
  { key_vlanid,         "VLanID",         kf_cfg + kf_cmd                },
  { key_systemboot,     "SystemBoot",     kf_cfg + kf_cmd                },
  { key_sethostname,    "SetHostname",    kf_cfg + kf_cmd_early          },
  { key_debugshell,     "DebugShell",     kf_cfg + kf_cmd + kf_cmd_early },
  { key_self_update,    "SelfUpdate",     kf_cfg + kf_cmd                },
  { key_firmware_devices, "FirmwareDevices", kf_cfg + kf_cmd             },
  { key_firmware_devices, "IBFTDevices",  kf_cfg + kf_cmd                },
  { key_linuxrc_core,   "LinuxrcCore",    kf_cfg + kf_cmd_early          },
  { key_norepo,         "NoRepo",         kf_cfg + kf_cmd                },
  { key_auto_assembly,  "AutoAssembly",   kf_cfg + kf_cmd_early          },
  { key_device_auto_config, "DeviceAutoConfig",  kf_cfg + kf_cmd_early   },
  { key_rd_zdev,        "rd.zdev",        kf_cfg + kf_cmd_early          },
  { key_zram,           "zram",           kf_cmd_early                   },
  { key_zram_root,      "zram_root",      kf_cmd_early                   },
  { key_zram_swap,      "zram_swap",      kf_cmd_early                   },
  { key_extend,         "Extend",         kf_cfg + kf_cmd                },
  { key_switch_to_fb,   "SwitchToFB",     kf_cfg + kf_cmd_early          },
  { key_edid,           "EDID",           kf_cmd_early                   },
  { key_hypervisor,     "Hypervisor",     kf_cmd_early                   },
  { key_usenbft,        "UseNBFT",        kf_cfg + kf_cmd                },
  { key_firmware_types, "FirmwareTypes",  kf_cfg + kf_cmd                },
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
  { "ask",       2                  },
  { "Undef",     0                  },
  { "Mono",      1                  },
  { "Color",     2                  },
  { "Alt"  ,     3                  },
  { "Reboot",    1                  },
  { "Halt",      2                  },
  { "kexec",     3                  },
  // URL schemes moved to url.c
#if defined(__s390__) || defined(__s390x__)
  { "osa",	 di_390net_osa      },
  { "ctc",	 di_390net_ctc	    },
  { "iucv",	 di_390net_iucv     },
  { "hsi",	 di_390net_hsi	    },
  { "qdio",	 di_osa_qdio	    },
  { "lcs",	 di_osa_lcs	    },
#endif
  { "open",      wa_open            },
  { "psk",       wa_wpa_psk         },
  { "wpa",       wa_wpa_psk         },
  { "peap",      wa_wpa_peap        },
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
static int strcasecmpignorestrich(const char *s1, const char *s2)
{
  char *str1 = strdup(s1);
  char *str2 = strdup(s2);
  char *s;
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

  free(str1);
  free(str2);

  return i;
}


file_key_t file_str2key(char *str, file_key_flag_t flags)
{
  int i;
  slist_t *sl;

  if(!str || !*str || flags == kf_none) return key_none;

  if(!*str) return key_none;

  for(i = 0; i < sizeof keywords / sizeof *keywords; i++) {
    if((keywords[i].flags & flags) && !strcasecmpignorestrich(keywords[i].value, str)) {
      return keywords[i].key;
    }
  }

  if(flags & (kf_cmd + kf_cfg)) {
    for(sl = config.ptoptions; sl; sl = sl->next) {
      if(!strcasecmpignorestrich(sl->key, str)) return key_is_ptoption;
      if(!fnmatch(sl->key, str, FNM_EXTMATCH + FNM_CASEFOLD)) return key_is_ptoption;
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

  if(!name || !(f = fopen(name, "r"))) return NULL;

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
      (*ft)->key = file_str2key(s, flags);
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


char *file_read_info_file(char *file, file_key_flag_t flags)
{
  file_t *f0 = NULL;

#ifdef DEBUG_FILE
  log_debug("looking for info file: %s\n", file);
#endif

  if(!strcmp(file, "cmdline")) {
    f0 = file_read_cmdline(flags);
  }
  else if(!strncmp(file, "file:", 5)) {
    f0 = file_read_file(file + 5, flags);
  }

  if(!f0) return NULL;

#ifdef DEBUG_FILE
  log_debug("info file read from \"%s\":\n", file);
  file_dump_flist(f0);
#endif

  file_do_info(f0, flags);

  file_free_file(f0);

  return file;
}


/*
 * Note: may modify f->key if f->key is key_none.
 */
void file_do_info(file_t *f0, file_key_flag_t flags)
{
  file_t *f, *xml_section_start = 0;
  int i, is_xml = 0;
  char buf[256], *s, *t, *s1;
  slist_t *sl, *sl0;
  unsigned u;
  FILE *w;

  if(config.debug >= 2) {
    log_debug("= config file [flags 0x%02x] =\n", flags);
    for(f = f0; f; f = f->next) {
      log_info("  key %d \"%s\", value %d \"%s\", raw \"%s\"\n", f->key, f->key_str, f->nvalue, f->value, f->unparsed ?: "");
    }
  }

  /*
   * Maybe it's an AutoYaST XML file.
   *
   * If so, limit the scope to lines in first '<info_file>' element.
   */
  for(f = f0; f; f = f->next) {
    if(f->key == key_none) {
      if(
        !strncmp(f->key_str, "<info_file", sizeof "<info_file" - 1) &&
        !strstr(f->key_str, "</info_file>") &&
        !strstr(f->key_str, "/>")
      ) {
        is_xml = 1;
        xml_section_start = f->next;
        break;
      }
      if(
        !strncmp(f->key_str, "<?xml", sizeof "<?xml" - 1) ||
        !strncmp(f->key_str, "<profile", sizeof "<profile" - 1) ||
        !strcmp(f->key_str, "</profile>")
      ) {
        is_xml = 1;
      }
    }
  }

  if(is_xml) f0 = xml_section_start;

  if(config.debug >= 2 && is_xml) {
    log_debug("  = xml file, applying only these lines =\n", flags);
  }

  for(f = f0; f; f = f->next) {
    if(
      is_xml &&
      f->key == key_none &&
      !strcmp(f->key_str, "</info_file>")
    ) {
      break;
    }

    if(config.debug >= 2 && is_xml) {
      log_debug("    key %d \"%s\", value %d \"%s\", raw \"%s\"\n", f->key, f->key_str, f->nvalue, f->value, f->unparsed ?: "");
    }

    switch(f->key) {
      case key_insmod:
      case key_insmod_pre:
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
        config.keymap_set = config.keymap ? 1 : 0;
        break;

      case key_hostip:
        str_copy(&config.ifcfg.manual->type, "static");
        config.ifcfg.manual->dhcp = 0;
        if(*f->value) str_copy(&config.ifcfg.manual->ip, f->value);
        break;

      case key_hostname:
        if(*f->value) str_copy(&config.net.realhostname, f->value);
        break;

      case key_netmask:
        i = netmask_to_prefix(f->value);
        if(i > 0) config.ifcfg.manual->netmask_prefix = i;
        break;

      case key_gateway:
        if(*f->value) str_copy(&config.ifcfg.manual->gw, f->value);
        break;

      case key_ptphost:
        if(*f->value) {
          config.ifcfg.manual->ptp = 1;
          str_copy(&config.ifcfg.manual->gw, f->value);
        }
        break;
      
      case key_nameserver:
        if(*f->value) {
          str_copy(&config.ifcfg.manual->ns, f->value);
          // normally space separated list
          // but allow comma separated list for compatibility
          char *s = config.ifcfg.manual->ns;
          do {
            if(*s == ',') *s = ' ';
          } while(*s++);
        }
        break;

      case key_proxy:
        url_free(config.url.proxy);
        config.url.proxy = url_set(f->value);
        if(
          config.url.proxy->scheme == inst_none ||
          config.url.proxy->scheme == inst_rel
        ) {
          sprintf(buf, "http://%s", f->value);
          url_free(config.url.proxy);
          config.url.proxy = url_set(buf);
        }
        break;

      case key_dhcptimeout:
        if(f->is.numeric) {
          config.net.dhcp_timeout = f->nvalue;
          config.net.dhcp_timeout_set = 1;
        }
        break;

      case key_tftptimeout:
        if(f->is.numeric) config.net.tftp_timeout = f->nvalue;
        break;

      case key_forcerootimage:
        config.download.instsys = f->nvalue;
        config.download.instsys_set = 1;
        break;

      case key_rebootwait:
        reboot_wait_ig = f->nvalue;
        break;

      case key_textmode:
        if(!*f->value) config.textmode = 1;
        if(f->is.numeric) config.textmode = f->nvalue;
        break;

      case key_username:
        str_copy(&config.net.user, *f->value ? f->value : NULL);
        break;

      case key_password:
        str_copy(&config.net.sshpassword, *f->value ? f->value : NULL);
        break;

      case key_passwordenc:
        str_copy(&config.net.sshpassword_enc, *f->value ? f->value : NULL);
        break;

      case key_workdomain:
        str_copy(&config.net.workgroup, f->value);
        break;

      case key_forceinsmod:
        config.forceinsmod = f->nvalue;
        break;

      case key_memlimit:
        if(f->is.numeric) config.memoryXXX.min_free = f->nvalue << 10;
        break;

      case key_memyast:
        if(f->is.numeric) config.memoryXXX.min_yast = f->nvalue << 10;
        break;

      case key_memloadimage:
        if(f->is.numeric) {
          config.memoryXXX.load_image = f->nvalue << 10;
          if(!config.download.instsys_set) {
            config.download.instsys = config.memoryXXX.free > config.memoryXXX.load_image ? 1 : 0;
          }
        }
        break;

      case key_netstop:
        if(f->is.numeric) config.netstop = f->nvalue;
        break;

      case key_testmode:
        if(f->is.numeric) config.test = f->nvalue;
        break;

      case key_debugwait:
        if(f->is.numeric) {
          config.debugwait = f->nvalue;
        }
        else if(*f->value) {
          config.debugwait = 1;
          slist_assign_values(&config.debugwait_list, f->value);
        }
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

      case key_domain:
        if(*f->value) str_copy(&config.ifcfg.manual->domain, f->value);
        break;

      case key_rootimage:
        str_copy(&config.rootimage, f->value);
        break;

      case key_rescueimage:
        str_copy(&config.rescueimage, f->value);
        break;

      case key_rescue:
        if(f->is.numeric) {
          config.rescue = f->nvalue;
          break;
        }
        else {
          config.rescue = 1;
        }

      case key_install:
        url_free(config.url.install);
        config.url.install = url_set(f->value);

        if(config.url.install->instsys) {
          url_free(config.url.instsys);
          config.url.instsys = url_set(config.url.install->instsys);
        }
        break;

      case key_instsys:
        str_copy(&config.url.instsys_default, *f->value ? f->value : NULL);
        break;

      case key_autoyast:
        config.url.autoyast = url_free(config.url.autoyast);
        if(strcmp(f->value, "default")) {
          config.url.autoyast = url_set(f->value);
          // if it's an SLP url, add 'autoyast' service query per default
          if(config.url.autoyast->scheme == inst_slp) {
            slist_setentry(&config.url.autoyast->query, "service", "autoyast", 0);
          }
        }
        config.manual = 0;
        break;

      case key_autoyast2:
        url_free(config.url.autoyast2);
        config.url.autoyast2 = url_set(f->value);
        break;

      case key_autoyast_parse:
        if(f->is.numeric) config.autoyast_parse = f->nvalue;
        break;

      case key_autoyast_passurl:
        if(f->is.numeric) config.autoyast_passurl = f->nvalue;
        break;

      case key_info:
        if(*f->value) slist_append_str(&config.info.file, f->value);
        break;

      case key_vnc:
        if(f->is.numeric) config.vnc = f->nvalue;
        if(config.vnc) {
          config.net.do_setup |= DS_VNC;
        }
        break;

      case key_usessh:
        config.sshd_only = 0;
        if(f->is.numeric) config.usessh = f->nvalue;
        if(config.usessh) {
          config.net.do_setup |= DS_SSH;
        }
        break;

      case key_sshd:
        config.sshd_only = 1;
        if(f->is.numeric) config.usessh = f->nvalue;
        if(config.usessh) {
          config.net.do_setup |= DS_SSH;
        }
        break;

      case key_vncpassword:
        str_copy(&config.net.vncpassword, *f->value ? f->value : NULL);
	/* do not enable vnc nor network ... this is done with vnc=1 */
        break;

      case key_displayip:
        str_copy(&config.net.displayip, *f->value ? f->value : NULL);
        break;
                                      
      case key_sshpassword:
        str_copy(&config.net.sshpassword, *f->value ? f->value : NULL);
	/* do not enable ssh nor network ... this is done with usessh=1 */
        break;

      case key_sshpasswordenc:
        str_copy(&config.net.sshpassword_enc, *f->value ? f->value : NULL);
        break;

      case key_sshkey:
        str_copy(&config.net.sshkey, *f->value ? f->value : NULL);
        break;

      case key_term:
        str_copy(&config.term, *f->value ? f->value : NULL);
        break;

      case key_addswap:
        if(f->is.numeric) {
          if(f->nvalue <= 0) {
            /* 0 -1 -2 > 0 2 1 */
            config.addswap = (3 + f->nvalue) % 3;
          }
          else {
            util_update_disk_list(NULL, 1);
            i = f->nvalue;
            for(sl = config.partitions; sl; sl = sl->next) {
              if(sl->key) {
                sprintf(buf, "/dev/%s", sl->key);
                t = util_fstype(buf, NULL);
                if(t && !strcmp(t, "swap")) {
                  if(!--i) {
                    char *argv[2] = { };

                    argv[1] = buf;
                    log_info("swapon %s\n", buf);
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
          t = util_fstype(s, NULL);
          if(t && !strcmp(t, "swap")) {
            argv[1] = s;
            log_info("swapon %s\n", s);
            util_swapon_main(2, argv);
          }
        }
        break;

      case key_exec:
        if(*f->value) lxrc_run_console(f->value);
        break;

      case key_usbwait:
        if(f->is.numeric) config.usbwait = f->nvalue;
        break;

      case key_nfsrsize:
        if(f->is.numeric) config.net.nfs.rsize = f->nvalue;
        break;

      case key_nfswsize:
        if(f->is.numeric) config.net.nfs.wsize = f->nvalue;
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

      case key_console:
        // just remember that it was used
        config.console_option = 1;
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
        if(*f->value) {
          if(
            !config.log.dest[1].name ||
            strcmp(config.log.dest[1].name, f->value)
          ) {
            str_copy(&config.log.dest[1].name, f->value);
            if(config.log.dest[1].f) fclose(config.log.dest[1].f);
            config.log.dest[1].f = NULL;
          }
        }
        break;

      case key_kbdtimeout:
        if(f->is.numeric) config.kbdtimeout = f->nvalue;
        break;

      case key_brokenmodules:
        slist_assign_values(&config.module.broken, f->value);
        if(!config.test) {
          if((w = fopen("/etc/modprobe.d/blacklist.conf", "w"))) {
            for(sl = config.module.broken; sl; sl = sl->next) {
              if(sl->key) fprintf(w, "blacklist %s\n", sl->key);
            }
            fclose(w);
          }
          if((w = fopen("/etc/modprobe.d/noload.conf", "w"))) {
            for(sl = config.module.broken; sl; sl = sl->next) {
              if(sl->key) fprintf(w, "install %s /bin/true\n", sl->key);
            }
            fclose(w);
          }
        }
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
        config.utf8 = config.linemode ? 0 : 1;
        break;

      case key_moduledelay:
        if(f->is.numeric) config.module.delay = f->nvalue;
        break;

      case key_updatedir:
        if(*f->value) str_copy(&config.update.dir, f->value);
        break;

      case key_scsirename:
        if(f->is.numeric) config.scsi_rename = f->nvalue;
        break;

      case key_doscsirename:
        if(f->is.numeric && f->nvalue) {
          config.scsi_rename = 1;
          scsi_rename();
        }
        break;

      case key_lxrcdebug:
        sl0 = slist_split(',', f->value);
        for(sl = sl0; sl; sl = sl->next) {
          if(*sl->key) {
            u = strtoul(sl->key, &t, 0);
            if(!*t) {
              config.debug = u;
            }
            else {
              s = sl->key;
              i = 1;
              if(*s == '+' || *s == '-') {
                if(*s == '-') i = 0;
                s++;
              }
              if(!strcmp(s, "wait")) config.debugwait = i;
              else if(!strcmp(s, "tmpfs")) config.tmpfs = i;
              else if(!strcmp(s, "udev.mods")) config.udev_mods = i;
              else if(!strcmp(s, "trace")) config.error_trace = i;
              else if(!strcmp(s, "bash")) config.early_bash = i;
              else if(!strcmp(s, "devtmpfs")) config.devtmpfs = i;
            }
          }
        }
        slist_free(sl0);

        config.log.dest[1].level =
          config.debug ? LOG_LEVEL_SHOW | LOG_LEVEL_INFO | LOG_LEVEL_DEBUG | LOG_TIMESTAMP : LOG_LEVEL_INFO;

        if(config.error_trace) {
          config.log.dest[2].level |= LOG_CALLER;
          if(config.debug) {
            config.log.dest[1].level |= LOG_CALLER;
          }
        }
        else {
          config.log.dest[2].level &= ~LOG_CALLER;
          if(config.debug) {
            config.log.dest[1].level &= ~LOG_CALLER;
          }
        }
        break;

      case key_linuxrc:
        slist_free(config.linuxrc);
        config.linuxrc = slist_split(',', f->value);
        if(slist_getentry(config.linuxrc, "nocmdline")) config.info.add_cmdline = 0;
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
        if(f->is.numeric) {
          config.update.ask = f->nvalue;
        }
        else if(*f->value) {
          slist_append_str(&config.update.urls, f->value);
        }
        break;

      case key_loglevel:
        if(f->is.numeric) config.loglevel = f->nvalue;
        break;

      case key_netsetup:
        config.net.do_setup |= DS_SETUP;

        // for compatibility, see below
        int do_all = 0;

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
            else if(!strcmp(s, "vlanid")) i = NS_VLANID;
            else if(!strcmp(s, "gateway")) i = NS_GATEWAY;
            else if(!strcmp(s, "display")) i = NS_DISPLAY;
            else if(!strcmp(s, "now")) i = NS_NOW;
            else if(!strcmp(s, "all")) do_all = 1;
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
            if(i == NS_NOW) {
              config.net.now = *sl->key == '-' ? 0 : 1;
            }
            else if(i) {
              if(*sl->key == '-') {
                config.net.setup &= ~i;
              }
              else {
                config.net.setup |= i;
              }
            }
          }

          slist_free(sl0);
        }
        /*
         * for compatibility with old versions:
         * make netsetup=dhcp,all an alias for ifcfg=*=dhcp
         */
        if(do_all && (config.net.setup & NS_DHCP)) {
          ifcfg_append(&config.ifcfg.list, ifcfg_parse("*=dhcp"));
          config.net.setup = 0;
          net_update_ifcfg(IFCFG_IFUP);
        }
        if(!config.net.setup) config.net.do_setup = 0;
        if(config.net.now) {
          auto2_user_netconfig();
          config.net.now = 0;
        }
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
        if(f->is.numeric) config.memoryXXX.ram_min = f->nvalue << 10;
        break;

#if defined(__s390__) || defined(__s390x__)
      case key_instnetdev:
        if(*f->value) config.hwp.type=file_sym2num(f->value);
        break;
      case key_iucvpeer:
        if(*f->value) str_copy(&config.hwp.userid, f->value);
        break;
      case key_portname:
        log_show("***\n"); 
        log_show("*** The Portname parameter is no longer needed. Please do not specify it.\n"); 
        log_show("***\n"); 
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
/*
 * The protocol attribute for CTCs is handled specially. We use IFNOTAUTO later
 * on in net.c, which equates a value of 0 to "not specified". So protocol must be
 * non-zero for even the "compatability" case where "protocol=0" in the parmfile.
 */
      case key_ctcprotocol:
        if(f->is.numeric) config.hwp.protocol = f->nvalue + 1;
        break;        
      case key_osainterface:
        if(*f->value) {
          config.hwp.interface=file_sym2num(f->value);
          if(config.hwp.interface != di_osa_lcs &&
             config.hwp.interface != di_osa_qdio) {
             log_show("\n*** The OsaInterface type of \"%s\" is not valid. Ignoring it.\n", f->value);
             config.hwp.interface=0;
          }
        }
        break;
/*
 * The layer2 attribute is handled specially. We use IFNOTAUTO later on in net.c,
 * which equates a value of 0 to "not specified". So layer2 must be non-zero
 * for both the layer 2 and layer 3 cases.
 */
      case key_layer2:
        if(f->is.numeric) config.hwp.layer2 = f->nvalue + 1;
        break;
/*
 * The portno attribute is handled specially. We use IFNOTAUTO later on in net.c,
 * which equates a value of 0 to "not specified". So portno must be non-zero
 * for both the port 0 and port 1 cases.
 */
      case key_portno:
        if(f->is.numeric) config.hwp.portno = f->nvalue + 1;
        else config.hwp.portno = 1;
        break;
      case key_osahwaddr:
        str_copy(&config.hwp.osahwaddr, f->value);
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

      case key_is_ptoption:
        // store option value
        for(sl = config.ptoptions; sl; sl = sl->next) {
          if(!strcasecmpignorestrich(sl->key, f->key_str)) {
            str_copy(&sl->value, f->value);
            break;
          }
          // If a pattern matches, create an entry with the actual option name.
          if(!fnmatch(sl->key, f->key_str, FNM_EXTMATCH + FNM_CASEFOLD)) {
            slist_setentry(&config.ptoptions, f->key_str, f->value, 1);
            break;
          }
        }
        break;

      case key_none:
        /*
         * Do interpretation of module.foo=bar in command line only once
         * (with kf_cmd_early) - the outcome would not change doing it again.
         */
        if(!(flags & kf_cmd_early)) break;

        /* it must not look like any of the linuxrc options */
        if(file_str2key(f->key_str, kf_all) != key_none) break;

        log_debug("parser: =0x%02x= =%d:%s= =%s= >%s<\n", flags, f->key, f->key_str, f->value, f->unparsed);

        /*
         * Assume it is a kernel module option if it can be parsed as
         * 'module.option'.
         *
         * Note: f->unparsed is only set when reading from cmdline/argv, not
         * from files.
         */
        if((s1 = f->unparsed)) {
          i = strlen(f->unparsed);
          if(s1[0] == '"' && s1[i - 1] == '"') {
            s1[i - 1] = 0;
            s1++;
          }
          s = strchr(s1, '.');
          t = strchr(s1, ' ');
          if(!s || (t && t < s)) break;	/* no spaces in module name */
          str_copy(&f->value, s1);
        }
        else {
          break;
        }

      /* fall through to key_options */

      case key_options:
        if(*f->value) {
	  /* allow both module.param and module=param */
	  s = f->value;
	  strsep(&s, ".=");
          if(s) {
	    sl = slist_getentry(config.module.options, f->value);
	    if(!sl) {
	      sl = slist_add(&config.module.options, slist_new());
	      sl->key = strdup(f->value);
	      sl->value = strdup(s);
	    }
	    else {
	      strprintf(&sl->value, "%s %s", sl->value, s);
	    }
            log_debug("options[%s] = \"%s\"\n", sl->key, sl->value);
          }
        }
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

      case key_withiscsi:
        if(f->is.numeric) config.withiscsi = f->nvalue;
        if(config.withiscsi && !config.net.do_setup) {
          config.net.do_setup |= DS_SETUP;
          config.net.setup |= NS_DEFAULT;
        }
        break;

      case key_withfcoe:
        if(f->is.numeric) config.withfcoe = f->nvalue;
        break;

      case key_usenbft:
        if(f->is.numeric) config.usenbft = f->nvalue;
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
          if(config.ifcfg.if_up) {
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

      case key_mediacheck:
        if(f->is.numeric) config.mediacheck = f->nvalue;
        break;

      case key_wlan_essid:
        str_copy(&config.net.wlan.essid, *f->value ? f->value : NULL);
        break;

      case key_wlan_wpa_psk:
        str_copy(&config.net.wlan.wpa_psk, *f->value ? f->value : NULL);
        config.net.wlan.auth = wa_wpa_psk;
        break;

      case key_wlan_wpa_id:
        str_copy(&config.net.wlan.wpa_identity, *f->value ? f->value : NULL);
        config.net.wlan.auth = wa_wpa_peap;
        break;

      case key_wlan_wpa_pass:
        str_copy(&config.net.wlan.wpa_password, *f->value ? f->value : NULL);
        config.net.wlan.auth = wa_wpa_peap;
        break;

      case key_wlan_auth:
        if(f->is.numeric) config.net.wlan.auth = f->nvalue;
        break;

      case key_wlan_device:
        if(*f->value) {
          slist_append_str(&config.net.wlan.devices, f->value);
          config.net.wlan.devices_fixed = 1;
        }
        break;

      case key_net_retry:
        if(f->is.numeric) config.net.retry = f->nvalue;
        break;

      case key_bootif:
        {
          /* handle both EUI-48 and EUI-64 both with and without
           * the ipappend value
           * EUI-48 w/o 17 : "01:23:45:67:89:0a"
           * EUI-48 w/  20 : "99-01:23:45:67:89:0a"
           * EUI-64 w/o 23 : "01:23:45:67:89:0a:bc:de"
           * EUI-64 w/  26 : "99-01:23:45:67:89:0a:bc:de"
           */
          int len = strlen(f->value);
          if ( len > 3 ) {
            str_copy(&config.ifcfg.manual->device, f->value + (len == 20 || len == 26 ? 3 : 0));
            for(s = config.ifcfg.manual->device; *s; s++) if(*s == '-') *s = ':';
          }
        break;
        }
      case key_swap_size:
        if(f->is.numeric) config.swap_file_size = f->nvalue;
        break;

      case key_ntfs_3g:
        if(f->is.numeric) config.ntfs_3g = f->nvalue;
        break;

      case key_hash:
        if(*f->value) {
          sl0 = slist_split(' ', f->value);
          if(sl0->key && sl0->next && sl0->next->next) {
            sl = slist_append_str(&config.digests.list, sl0->key);
            strprintf(&sl->key, "%s %s", sl->key, sl0->next->key);
            sl->value = strdup(sl0->next->next->key);
            // key = 'SHAXXX SHAXXX_DIGEST'
            // value = 'FILE_NAME'
          }
          slist_free(sl0);
        }
        break;

      case key_insecure:
        if(f->is.numeric && f->nvalue) {
          config.secure = 0;
          config.digests.failed = config.sig_failed = 0;
        }
        break;

      case key_sslcerts:
        if(f->is.numeric && !f->nvalue) {
          config.sslcerts = 0;
        }
        break;

      case key_kexec:
        if(f->is.numeric) config.kexec = f->nvalue;
        break;

      case key_nisdomain:
        str_copy(&config.net.nisdomain, f->value);
        break;

      case key_nomodprobe:
        if(f->is.numeric) config.nomodprobe = f->nvalue;
        break;

      case key_netdevice:
        str_copy(&config.ifcfg.manual->device, *f->value ? f->value : NULL);
        break;

      case key_partition:
      case key_device:
        str_copy(&config.device, short_dev(*f->value ? f->value : NULL));
        break;

      case key_nomdns:
        if(f->is.numeric && f->nvalue) {
          FILE *w;

          if((w = fopen("/etc/host.conf", "a"))) {
            fprintf(w, "mdns off\n");
            fclose(w);
          }
        }
        break;

      case key_yepurl:
        str_copy(&config.yepurl, f->value);
        break;

      case key_y2gdb:
        if(f->is.numeric) config.y2gdb = f->nvalue;
        break;

      case key_vga:
        str_copy(&config.vga, f->value);
        if(f->is.numeric) config.vga_mode = f->nvalue;
        break;

      case key_squash:
        if(f->is.numeric) config.squash = f->nvalue;
        break;

      case key_kexec_reboot:
        if(f->is.numeric) config.kexec_reboot = f->nvalue;
        break;

      case key_devbyid:
        if(f->is.numeric) config.device_by_id = f->nvalue;
        break;

      case key_braille:
        if(f->is.numeric) config.braille.check = f->nvalue;
        break;

      case key_nfsopts:
        if(*f->value) {
          str_copy(&config.net.nfs.opts, f->value);
          sl0 = slist_split(',', f->value);
          for(sl = sl0; sl; sl = sl->next) {
            if(sscanf(sl->key, "rsize=%u", &u) == 1) {
              config.net.nfs.rsize = u;
            }
            else if(sscanf(sl->key, "wsize=%u", &u) == 1) {
              config.net.nfs.wsize = u;
            }
            else if(sscanf(sl->key, "vers=%u", &u) == 1) {
              config.net.nfs.vers = u;
            }
            else if(!strcmp(sl->key, "udp")) {
              config.net.nfs.udp = 1;
            }
            else if(!strcmp(sl->key, "tcp")) {
              config.net.nfs.udp = 0;
            }
          }
          slist_free(sl0);
        }
        break;

      case key_ipv4:
        if(f->is.numeric) config.net.ipv4 = f->nvalue;
        break;

      case key_ipv4only:
        if(f->is.numeric) {
          config.net.ipv4 = f->nvalue;
          config.net.ipv6 = !config.net.ipv4;
        }
        break;

      case key_ipv6:
        if(f->is.numeric) config.net.ipv6 = f->nvalue;
        break;

      case key_ipv6only:
        if(f->is.numeric) {
          config.net.ipv6 = f->nvalue;
          config.net.ipv4 = !config.net.ipv6;
        }
        break;

      case key_efi:
        if(f->is.numeric) config.efi = f->nvalue;
        break;

      case key_supporturl:
        str_copy(&config.supporturl, f->value);
        break;

      case key_udevrule:
        if(*f->value && !slist_getentry(config.udevrules, f->value)) {
          slist_append_str(&config.udevrules, f->value);
        }
        break;

      case key_dhcpfail:
        str_copy(&config.net.dhcpfail, f->value);
        break;

      case key_namescheme:
        str_copy(&config.namescheme, f->value);
        break;

      case key_ptoptions:
        slist_assign_values(&config.ptoptions, f->value);
        break;

      case key_digests:
        slist_assign_values(&config.digests.supported, f->value);
        break;

      case key_plymouth:
        if(f->is.numeric) config.plymouth = f->nvalue;
        break;

      case key_restart:
        if(f->is.numeric && f->nvalue > 0) util_restart(SIGTERM);
        break;

      case key_restarted:
        if(f->is.numeric) config.restarted = f->nvalue;
        break;

      case key_withipoib:
        if(f->is.numeric) config.withipoib = f->nvalue;
        break;

      case key_upgrade:
        if(f->is.numeric) config.upgrade = f->nvalue;
        break;

      case key_media_upgrade:
        if(f->is.numeric) config.media_upgrade = f->nvalue;
        break;

      case key_ifcfg:
        if(*f->value) {
          ifcfg_t *ifcfg = ifcfg_parse(f->value);
          if(ifcfg) {
            // if 'try' flag is set, move to config.ifcfg.manual
            if(ifcfg->search) {
              ifcfg_copy(config.ifcfg.manual, ifcfg);
            }
            else {
              ifcfg_append(&config.ifcfg.list, ifcfg);
            }
          }
        }
        break;

      case key_defaultinstall:
        config.defaultrepo = slist_free(config.defaultrepo);
        if(*f->value) config.defaultrepo = slist_split(',', f->value);
        break;

      case key_nanny:
        if(f->is.numeric) {
          config.nanny = f->nvalue;
          config.nanny_set = 1;
        }
        else {
          config.nanny_set = 0;
        }
        break;

      case key_vlanid:
        if(f->is.numeric) {
          // set vlan id if it's a positive integer...
          if(f->nvalue > 0) {
            strprintf(&config.ifcfg.manual->vlan, "%d", f->nvalue);
          }
          config.net.setup |= NS_VLANID;
        }
        else {
          // ... else clear setting
          str_copy(&config.ifcfg.manual->vlan, NULL);
        }
        break;

      case key_systemboot:
        if(f->is.numeric) config.systemboot = f->nvalue;
        break;

      case key_sethostname:
        if(f->is.numeric) {
          config.net.sethostname = f->nvalue;
          config.net.sethostname_used = 1;
        }
        break;

      case key_debugshell:
        str_copy(&config.debugshell, *f->value ? f->value : NULL);
        break;

      case key_self_update:
        if (f->is.numeric) {
          config.self_update = f->nvalue;
        } else {
          str_copy(&config.self_update_url, f->value);
          config.self_update = 1;
        }
        break;

      case key_firmware_devices:
        slist_assign_values(&config.ifcfg.firmware, f->value);
        break;

      case key_firmware_types:
        slist_assign_values(&config.ifcfg.firmware_types, f->value);
        break;

      case key_linuxrc_core:
        str_copy(&config.core, *f->value ? f->value : NULL);
        break;

      case key_norepo:
        if(f->is.numeric) config.norepo = f->nvalue;
        break;

      case key_auto_assembly:
        if(f->is.numeric) config.auto_assembly = f->nvalue;
        break;

      case key_device_auto_config:
        if(f->is.numeric) config.device_auto_config = f->nvalue;
        break;

      case key_rd_zdev:
        if(!strcmp(f->value, "no-auto")) {
          config.device_auto_config = 0;
        }
        break;

      case key_zram:
        if(f->is.numeric) {
          if(f->nvalue) {
            str_copy(&config.zram.root_size, "1G");
            str_copy(&config.zram.swap_size, "1G");
          }
          else {
            str_copy(&config.zram.root_size, NULL);
            str_copy(&config.zram.swap_size, NULL);
          }
        }
        break;

      case key_zram_root:
        str_copy(&config.zram.root_size, *f->value ? f->value : NULL);
        break;

      case key_zram_swap:
        str_copy(&config.zram.swap_size, *f->value ? f->value : NULL);
        break;

      case key_extend:
        slist_assign_values(&config.extend_option, f->value);
        break;

      case key_switch_to_fb:
        if(f->is.numeric) config.switch_to_fb = f->nvalue;
        break;

      case key_edid:
        if(*f->value) util_parse_edid(f->value);
        break;

      case key_hypervisor:
        str_copy(&config.hwp.hypervisor, f->value);
        break;

      default:
        break;
    }
  }

  if((net_config_mask() & 3) == 3) {
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
        if(!f->is.numeric) str_copy(&config.new_root, f->value);
        config.restart_method = f->nvalue;
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


void file_write_num(FILE *f, file_key_t key, int64_t num)
{
  fprintf(f, "%s: %lld\n", file_key2str(key), (long long) num);
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


void file_write_install_inf(char *dir)
{
  FILE *f;
  char file_name[256], *s;
  slist_t *sl;
  file_t *ft0, *ft;
  url_t *url = config.url.install;

  if(!url) return;

  util_update_meminfo();

  strcat(strcpy(file_name, dir), INSTALL_INF_FILE);

  if(!(f = fopen(file_name, "w"))) {
    log_info("Cannot open yast info file\n");
    return;
  }

  // 'manual' has been added at times when hardware driver loading
  // could cause all kinds of side effects. This does not happen
  // anymore. So we are switching it off in general in install.inf.
  file_write_num(f, key_manual, 0);

  set_write_info(f);

  file_write_str(f, key_console, config.serial);

  // don't leave anything mounted unless we're low in memory and run yast
  // directly from the install medium
  file_write_num(f, key_sourcemounted, url->mount && !config.download.instsys ? 1 : 0);

  fprintf(f, "RepoURL: %s\n", url_print(url, 3));
  if(!config.norepo)   fprintf(f, "ZyppRepoURL: %s\n", url_print(url, 4));
  if(!config.sslcerts) fprintf(f, "ssl_verify: no\n");

  if(url->used.device && !url->is.network) fprintf(f, "Device: %s\n", short_dev(url->used.device));

  if(config.url.proxy) fprintf(f, "ProxyURL: %s\n", url_print(config.url.proxy, 1));

  if(config.net.realhostname) {
    file_write_str(f, key_hostname, config.net.realhostname);
  }
  file_write_num(f, key_sethostname, config.net.sethostname);
  fprintf(f, "SetHostnameUsed: %u\n", config.net.sethostname_used);

  LXRC_WAIT

  file_write_modparms(f);

  file_write_str(f, key_loghost, config.loghost);
  if(config.restart_method) file_write_num(f, key_reboot, config.restart_method);
  file_write_num(f, key_keyboard, 1);	/* we always have one - what's the point ??? */
  file_write_str(f, key_updatedir, config.update.dir);
  file_write_num(f, key_yast2update, config.update.ask || config.update.count ? 1 : 0);
  file_write_num(f, key_textmode, config.textmode);
  if(config.url.autoyast) {
    log_info("final autoyast url: %s\n", url_print(config.url.autoyast, 0));
    file_write_str(f, key_autoyast,
      config.autoyast_parse && !config.autoyast_passurl ? url_print(config.url.autoyast, 5) : config.url.autoyast->str
    );
  }
  /*
   * autoyast + upgrade = autoupgrade
   *
   * autoyast uses a different config var to trigger updates for historical reasons
   */
  if(config.url.autoyast && config.upgrade) {
    fprintf(f, "AutoUpgrade: 1\n");
  }
  file_write_num(f, key_memfree, config.memoryXXX.current >> 10);	// convention: in kB
  file_write_num(f, key_vnc, config.vnc);
  file_write_str(f, key_vncpassword, config.net.vncpassword);
  file_write_str(f, key_displayip, config.net.displayip);
  file_write_num(f, key_usessh, config.usessh & ~config.sshd_only);
  file_write_num(f, key_sshd, config.usessh);
  if(config.noshell) file_write_num(f, key_noshell, config.noshell);
  file_write_str(f, key_initrd_id, config.initrd_id);
  file_write_str(f, key_instsys_id, config.instsys_id);
  file_write_num(f, key_withiscsi, config.withiscsi);
  file_write_num(f, key_withfcoe, config.withfcoe);
  file_write_num(f, key_usenbft, config.usenbft);
  file_write_num(f, key_startshell, config.startshell);
  file_write_num(f, key_y2gdb, config.y2gdb);
  file_write_num(f, key_kexec_reboot, config.kexec_reboot);
  file_write_num(f, key_efi, config.efi >= 0 ? config.efi : config.efi_vars);
  file_write_num(f, key_insecure, !config.secure);
  file_write_str(f, key_zram_swap, config.zram.swap_size);
  if(config.upgrade) file_write_num(f, key_upgrade, config.upgrade);
  if(config.media_upgrade) file_write_num(f, key_media_upgrade, config.media_upgrade);
  if(config.self_update_url) {
    file_write_str(f, key_self_update, config.self_update_url);
  }
  else if (config.self_update == 0 || config.self_update == 1) {
    file_write_num(f, key_self_update, config.self_update);
  }

  if(
    config.rootpassword &&
    strcmp(config.rootpassword, "ask")
  ) file_write_str(f, key_rootpassword, config.rootpassword);

  if(config.module.broken) {
    file_write_str(f, key_brokenmodules, s = slist_join(",", config.module.broken));
    free(s);
  }

  if(config.braille.dev) {
    fprintf(f, "Braille: %s\n", config.braille.type);
    fprintf(f, "Brailledevice: %s\n", config.braille.dev);
  }

  for(sl = config.ptoptions; sl; sl = sl->next) {
    if(sl->value) fprintf(f, "%s: %s\n", sl->key, sl->value);
  }

  ft0 = file_read_cmdline(kf_cmd + kf_cmd_early + kf_boot);

  slist_t *parm_list = NULL;

  for(ft = ft0; ft; ft = ft->next) {
    if(
      ft->key == key_none ||
      ft->key == key_vga
    ) {
      slist_append_str(&parm_list, ft->unparsed);
    }
  }

#if defined(__s390x__)
  /*
   * bsc#1168036
   *
   * If device auto-config has been disabled pass this setting on to the target system.
   */
  if(config.device_auto_config == 0 && util_has_device_auto_config()) {
    slist_append_str(&parm_list, "rd.zdev=no-auto");
  }
#endif

  if(parm_list) {
    char *str = slist_join(" ", parm_list);
    fprintf(f, "Cmdline: %s\n", str);
    free(str);
  }

  slist_free(parm_list);

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

  file_write_str(f, key_yepurl, config.yepurl);

  file_write_str(f, key_supporturl, config.supporturl);

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
  size_t cmdline_len = 0;

  if(config.test) {
    if(!config.had_segv) {
      for(argv = config.argv, argv++; *argv; argv++) {
        strprintf(&cmdline, "%s \"%s\"", cmdline ?: "", *argv);
      }
    }
  }
  else {
    if(!(f = fopen(CMDLINE_FILE, "r"))) return NULL;
    getdelim(&cmdline, &cmdline_len, 0, f);
    fclose(f);
  }

  ft = file_parse_buffer(cmdline, flags);

  free(cmdline);

  return ft;
}


file_t *file_parse_buffer(char *buf, file_key_flag_t flags)
{
  file_t *ft0 = NULL, **ft = &ft0;
  char *current, *s, *s1, *t, *t1, sep = ' ';
  int i, quote;

  if(!buf) return NULL;

  if((flags & kf_comma)) sep = ',';

  current = buf;

  do {
    while(isspace(*current) || *current == sep) current++;
    for(quote = 0, s = current; *s && (quote || !(isspace(*s) || *s == sep)); s++) {
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
      (*ft)->key = file_str2key(t, flags);
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
  static file_t *cmdline = NULL, ft_buf;
  file_t *ft, *ft_ok = NULL;

  memset(&ft_buf, 0, sizeof ft_buf);

  if(!cmdline) cmdline = file_read_cmdline(kf_cmd + kf_cmd_early);

  for(ft = cmdline; ft; ft = ft->next) {
    if(ft->key == key) ft_ok = ft;
  }

  if(ft_ok) {
    memcpy(&ft_buf, ft_ok, sizeof ft_buf);
    ft_ok = &ft_buf;
    ft_ok->next = NULL;
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
    log_debug("%d: \"%s\" = \"%s\"\n", ft->key, ft->key_str, ft->value);
    if(ft->is.numeric) log_debug("  num = %d\n", ft->nvalue);
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
    log_debug("%s (%s:%s): \"%s\"\n",
      ml->name,
      config.module.type_name[ml->type],
      config.module.more_file[ml->type] ?: "-",
      ml->descr ?: ""
    );
    log_debug("  initrd = %s, show = %s, auto = %s, ask = %s\n",
      ml->initrd ? "yes" : "no",
      ml->descr ? "yes" : "no",
      ml->autoload ? "yes" : "no",
      ml->dontask ? "no" : "yes"
    );
    if(ml->param) log_debug("  param: \"%s\"\n", ml->param);
    if(ml->pre_inst) log_debug("  pre_inst: \"%s\"\n", ml->pre_inst);
    if(ml->post_inst) log_debug("  post_inst: \"%s\"\n", ml->post_inst);
  }
}

#endif


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

      log_info("new id: %s,%s,%s\n",
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

  log_show("\n%s waiting for connection on port %d\n", inet2print(&config.net.hostname), port);

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


/*
 * *Very* limited xml parsing.
 *
 * Returns a list of elements; 'key' is the (complete) attribute info
 * belonging to the element, 'value' is the element content.
 *
 * Elements without attribute cannot be matched.
 *
 * buf: buffer to parse
 * tag: elements name to look for
 */
slist_t *file_parse_xmllike_buf(char *buf, char *tag)
{
  slist_t *sl, *sl0 = NULL;
  char *tag_start = NULL, *tag_end = NULL;
  char *attr = NULL, *data = NULL;
  int i;
  char *ptr, *s0, *s1;

  if(!tag || !buf || !*buf) return sl0;

  // we're going to modify the buffer
  buf = strdup(buf);

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

  return sl0;
}


/*
 * Very limited xml parsing.
 *
 * Returns a list of elements; cf. file_parse_xmllike_buf().
 *
 * name: name of file to read
 * tag: elements name to look for
 */
slist_t *file_parse_xmllike(char *name, char *tag)
{
  slist_t *sl0 = NULL;
  FILE *f;
  char *buf = NULL;
  int buf_size = 0, buf_ptr = 0, i;

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

  sl0 = file_parse_xmllike_buf(buf, tag);

  free(buf);

  return sl0;
}


/*
 * Parse repomd data.
 *
 * - add file digest info to config.digests.list
 * - associate 'types' to file names (e.g. 'license' -> 'XXX-license.tar.gz'
 *   (stored in config.repomd_data)
 */
void file_parse_repomd(char *file)
{
  slist_t *repo, *sl, *sl_chk, *sl_digest, *sl_data;
  char buf_digest[256], buf_type[256], buf_loc[256], *s;

  repo = file_parse_xmllike(file, "data");

  for(sl = repo; sl; sl = sl->next) {
    if(sscanf(sl->key, "type=\"%255[^\"]\"", buf_type) == 1) {
      if((sl_chk = file_parse_xmllike_buf(sl->value, "checksum"))) {
        if(sscanf(sl_chk->key, "type=\"%255[^\"]\"", buf_digest) == 1) {
          if(
            (s = strstr(sl->value, "<location href=")) &&
            sscanf(s, "<location href=\"%255[^\"]\"", buf_loc) == 1
          ) {
            sl_digest = slist_append(&config.digests.list, slist_new());
            strprintf(&sl_digest->key, "%s %s", buf_digest, sl_chk->value);
            str_copy(&sl_digest->value, buf_loc);

            sl_data = slist_append_str(&config.repomd_data, buf_type);
            str_copy(&sl_data->value, buf_loc);
          }
        }
      }

      slist_free(sl_chk);
    }
  }

  slist_free(repo);
}


/*
 * Parse CHECKSUMS file.
 *
 * Add digest info to config.digests.list.
 *
 * File format: lines with
 *   SHA256 FILENAME
 */
void file_parse_checksums(char *file)
{
  FILE *fh;
  char *buf = NULL;
  size_t buf_size = 0;
  char sha256[65], name[256];
  slist_t *sl_digest;

  if(!(fh = fopen(file, "r"))) return;

  while(getline(&buf, &buf_size, fh) > 0) {
    if(sscanf(buf, "%64s %255s", sha256, name) == 2) {
      sl_digest = slist_append(&config.digests.list, slist_new());
      strprintf(&sl_digest->key, "sha256 %s", sha256);
      str_copy(&sl_digest->value, name);
    }
  }

  free(buf);

  fclose(fh);
}

