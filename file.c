/*
 *
 * file.c        File access
 *
 * Copyright (c) 1996-2001  Hubert Mantel, SuSE GmbH  (mantel@suse.de)
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

#define YAST_INF_FILE		"/etc/yast.inf"
#define INSTALL_INF_FILE	"/etc/install.inf"
#define MTAB_FILE		"/etc/mtab"
#define CMDLINE_FILE		"/proc/cmdline"

// #define DEBUG_FILE

static char *file_key2str(file_key_t key);
static file_key_t file_str2key(char *value);
static int sym2index(char *sym);
static void parse_value(file_t *ft);

static char *file_read_info_file(char *file, char *file2);
void file_write_modparms(FILE *f);
static file_t *file_read_cmdline(void);
static void file_module_load (char *insmod_arg);
#ifdef DEBUG_FILE
static void file_dump_flist(file_t *ft);
static void file_dump_mlist(module2_t *ml);
#endif

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
  { key_start_pcmcia,   "start_pcmcia"     },
  { key_display,        "Display"          },
  { key_bootmode,       "Bootmode"         },
  { key_ip,             "IP"               },
  { key_netmask,        "Netmask"          },
  { key_gateway,        "Gateway"          },
  { key_server,         "Server"           },
  { key_dnsserver,      "Nameserver"       },
  { key_broadcast,      "Broadcast"        },
  { key_network,        "Network"          },
  { key_partition,      "Partition"        },
  { key_serverdir,      "Serverdir"        },
  { key_fstype,         "Fstyp"            },
  { key_netdevice,      "Netdevice"        },
  { key_livesrc,        "LiveSRC"          },
  { key_bootpwait,      "Bootpwait"        },
  { key_bootptimeout,   "BOOTP_TIMEOUT"    },
  { key_forcerootimage, "ForceRootimage"   },
  { key_rebootwait,     "WaitReboot"       },
  { key_sourcemounted,  "Sourcemounted"    },
  { key_cdrom,          "Cdrom"            },
  { key_pcmcia,         "PCMCIA"           },
  { key_haspcmcia,      "HasPCMCIA"        },
  { key_console,        "Console"          },
  { key_pliphost,       "PLIP-Host"        },
  { key_machine,        "Machinename"      },
  { key_domain,         "Domain"           },
  { key_ftpuser,        "FTP-User"         },
  { key_ftpproxy,       "FTP-Proxy"        },
  { key_ftpproxyport,   "FTP-Proxyport"    },
  { key_manual,         "Manual"           },
  { key_demo,           "Demo"             },
  { key_reboot,         "Reboot"           },
  { key_floppydisk,     "Floppydisk"       },
  { key_keyboard,       "Keyboard"         },
  { key_yast2update,    "YaST2update"      },
  { key_yast2serial,    "YaST2serial"      },
  { key_textmode,       "Textmode"         },
  { key_yast2autoinst,  "YaST2AutoInstall" },
  { key_usb,            "USB"              },
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
  { key_autoyast,       "autoyast"         },
  { key_linuxrc,        "linuxrc"          },
  { key_forceinsmod,    "ForceInsmod"      }
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
  { "Floppy",    BOOTMODE_FLOPPY    },
  { "CD",        BOOTMODE_CD        },
  { "Net",       BOOTMODE_NET       },
  { "Harddisk",  BOOTMODE_HARDDISK  },
  { "FTP",       BOOTMODE_FTP       },
  { "CDwithNET", BOOTMODE_CDWITHNET },
  { "SMB",       BOOTMODE_SMB       }
};

char *file_key2str(file_key_t key)
{
  int i;

  for(i = 0; i < sizeof keywords / sizeof *keywords; i++) {
    if(keywords[i].key == key) {
      return keywords[i].value;
    }
  }

  return "";
}


file_key_t file_str2key(char *str)
{
  int i;

  if(!str || !*str) return key_none;

  for(i = 0; i < sizeof keywords / sizeof *keywords; i++) {
    if(!strcasecmp(keywords[i].value, str)) {
      return keywords[i].key;
    }
  }

  return key_none;
}


int sym2index(char *sym)
{
  int i;

  for(i = 0; i < sizeof sym_constants / sizeof *sym_constants; i++) {
    if(!strcasecmp(sym_constants[i].name, sym)) return i;
  }

  return -1;
}


void parse_value(file_t *ft)
{
  char *s;
  int i;
  struct in_addr in;

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
    if(!ft->is.numeric) {
      if(!net_check_address(ft->value, &in)) {
        ft->ivalue = in;
        ft->is.inet = 1;
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
      (*ft)->key = file_str2key(s);
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
  char *file = NULL;

  if(config.win) {
    dia_info(&win_ri, txt_get(TXT_SEARCH_INFOFILE));
  }
  else {
    printf("%s...", txt_get(TXT_SEARCH_INFOFILE));
    fflush(stdout);
  }

  if(!config.infofile || !strcmp(config.infofile, "default")) {
    if(config.infofile || auto2_ig || auto_ig) {
      file = file_read_info_file("floppy:/suse/setup/descr/info", "floppy:/info");
    }
    if(!file) file = file_read_info_file("file:/info", NULL);
    if(!file) file = file_read_info_file("cmdline", NULL);
  }
  else {
    file = file_read_info_file(config.infofile, NULL);
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

  config.infoloaded = strdup(file ?: "");

  return file ? 0 : 1;
}


char *file_read_info_file(char *file, char *file2)
{
  char filename[MAX_FILENAME];
  int do_autoprobe = 0;
#if WITH_PCMCIA
  int start_pcmcia = 0;
#endif
  int i, mounted = 0;
  file_t *f0 = NULL, *f;

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
      if(!util_try_mount(config.floppy_dev[i], mountpoint_tg, MS_MGC_VAL | MS_RDONLY, 0)) break;
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

  valid_net_config_ig = 0;

  for(f = f0; f; f = f->next) {
    switch(f->key) {
      case key_insmod:
        file_module_load(f->value);
        break;

      case key_autoprobe:
        do_autoprobe = 1;
        break;

#if WITH_PCMCIA
      case key_start_pcmcia:
        start_pcmcia = 1;
        break;
#endif

      case key_language:
        i = set_langidbyname(f->value);
        if(i) config.language = i;
        break;

      case key_display:
        config.color = f->nvalue;
        if(config.color) disp_set_display();
        break;

      case key_keytable:
        if(config.keymap) free(config.keymap);
        config.keymap = *f->value ? strdup(f->value) : NULL;
        break;

      case key_bootmode:
        if(f->is.numeric) bootmode_ig = f->nvalue;
        break;

      case key_ip:
        if(f->is.inet) {
          ipaddr_rg = f->ivalue;
          valid_net_config_ig |= 1;
        }
        break;

      case key_netmask:
        if(f->is.inet) {
          netmask_rg = f->ivalue;
          valid_net_config_ig |= 2;
        }
        break;

      case key_gateway:
        if(f->is.inet) {
          gateway_rg = f->ivalue;
          valid_net_config_ig |= 4;
        }
        break;

      case key_server:
        if(f->is.inet) {
          nfs_server_rg = f->ivalue;
          valid_net_config_ig |= 8;
        }
        break;

      case key_dnsserver:
        if(f->is.inet) {
          nameserver_rg = f->ivalue;
          valid_net_config_ig |= 0x10;
        }
        break;

      case key_partition:
        strncpy(harddisk_tg, f->value, sizeof harddisk_tg);
        harddisk_tg[sizeof harddisk_tg - 1] = 0;
        break;

      case key_serverdir:
        strncpy(server_dir_tg, f->value, sizeof server_dir_tg);
        server_dir_tg[sizeof server_dir_tg - 1] = 0;
        valid_net_config_ig |= 0x20;
        break;

      case key_netdevice:
        strncpy(netdevice_tg, f->value, sizeof netdevice_tg);
        netdevice_tg[sizeof netdevice_tg - 1] = 0;
        break;

      case key_livesrc:
        strncpy(livesrc_tg, f->value, sizeof livesrc_tg);
        livesrc_tg[sizeof livesrc_tg - 1] = 0;
        if((valid_net_config_ig & 0x20)) bootmode_ig = BOOTMODE_NET;
        break;

      case key_bootpwait:
        bootp_wait_ig = f->nvalue;
        break;

      case key_bootptimeout:
        bootp_timeout_ig = f->nvalue;
        break;

      case key_forcerootimage:
        force_ri_ig = f->nvalue;
        break;

      case key_rebootwait:
        reboot_wait_ig = f->nvalue;
        break;

      case key_username:
        config.smb.user = strdup(f->value);
        break;

      case key_password:
        config.smb.password = strdup(f->value);
        break;

      case key_workdomain:
        config.smb.workgroup = strdup(f->value);
        break;

      case key_forceinsmod:
        config.forceinsmod = f->nvalue;
        break;

      default:
    }
  }

  if((valid_net_config_ig & 3) == 3) {
    broadcast_rg.s_addr = ipaddr_rg.s_addr | ~netmask_rg.s_addr;
    network_rg.s_addr = ipaddr_rg.s_addr & netmask_rg.s_addr;
  }

  file_free_file(f0);

  if(mounted) umount(mountpoint_tg);

  if(do_autoprobe) mod_autoload();

#if WITH_PCMCIA
//  if(start_pcmcia) pcmcia_load_core();
#endif

  return file;
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

      default:
    }
  }

  file_free_file(f0);

  return root ? 0 : -1;
}


void file_write_str(FILE *f, file_key_t key, char *str)
{
  fprintf(f, "%s: %s\n", file_key2str(key), str);
}


void file_write_num(FILE *f, file_key_t key, int num)
{
  fprintf(f, "%s: %d\n", file_key2str(key), num);
}


void file_write_sym(FILE *f, file_key_t key, char *base_sym, int num)
{
  int i;

  i = sym2index(base_sym);

  if(i < 0 || num < 0 || i + num >= sizeof sym_constants / sizeof *sym_constants) {
    file_write_num(f, key, num);
  }

  fprintf(f, "%s: %s\n", file_key2str(key), sym_constants[i + num].name);
}


void file_write_inet(FILE *f, file_key_t key, struct in_addr *inet)
{
  fprintf(f, "%s: %s\n", file_key2str(key), inet_ntoa(*inet));
}


void file_write_install_inf(char *dir)
{
  FILE *f;
  char file_name[256];
  int i;

  strcat(strcpy(file_name, dir), INSTALL_INF_FILE);

  if(!(f = fopen(file_name, "w"))) {
    fprintf(stderr, "Cannot open yast info file\n");
    return;
  }

  if(config.language) set_write_info(f);

  // is that really true???
  file_write_num(f, key_sourcemounted, ramdisk_ig ? 0 : 1);

  file_write_sym(f, key_display, "Undef", config.color);

  if(config.keymap && (!auto2_ig || yast_version_ig == 1)) {
    file_write_str(f, key_keytable, config.keymap);
  }

  if(*cdrom_tg) file_write_str(f, key_cdrom, cdrom_tg);
  if(*net_tg) {
    fprintf(f, "%s: %s %s\n", file_key2str(key_alias), netdevice_tg, net_tg);
  }

  if(*ppcd_tg) {
    fprintf(f, "post-install paride insmod %s\n", ppcd_tg);
    fprintf(f, "pre-remove paride rmmod %s\n", ppcd_tg);
  }

#if WITH_PCMCIA
  if(pcmcia_chip_ig == 1 || pcmcia_chip_ig == 2) {
    file_write_str(f, key_pcmcia, pcmcia_chip_ig == 1 ? "tcic" : "i82365");
  }
#endif

#ifdef USE_LIBHD
  file_write_num(f, key_haspcmcia, auto2_pcmcia() || pcmcia_chip_ig ? 1 : 0);
#endif

  if(serial_ig) file_write_str(f, key_console, console_parms_tg);

  i = bootmode_ig != BOOTMODE_CDWITHNET ?: BOOTMODE_CD;
  file_write_sym(f, key_bootmode, "Floppy", i);

  if(bootmode_ig == BOOTMODE_HARDDISK) {
    file_write_str(f, key_partition, harddisk_tg);
    file_write_str(f, key_fstype, fstype_tg);
    file_write_str(f, key_serverdir, server_dir_tg);
  }

  if(
    bootmode_ig == BOOTMODE_NET ||
    bootmode_ig == BOOTMODE_SMB ||
    bootmode_ig == BOOTMODE_FTP ||
    bootmode_ig == BOOTMODE_CDWITHNET
  ) {
    file_write_str(f, key_netdevice, netdevice_tg);
    file_write_inet(f, key_ip, &ipaddr_rg);

    if(bootmode_ig == BOOTMODE_CDWITHNET) {
      broadcast_rg.s_addr = ipaddr_rg.s_addr | ~netmask_rg.s_addr;
      network_rg.s_addr = ipaddr_rg.s_addr & netmask_rg.s_addr;
      file_write_inet(f, key_broadcast, &broadcast_rg);
      file_write_inet(f, key_network, &network_rg);
    }

    if(plip_host_rg.s_addr) {
      file_write_inet(f, key_pliphost, &plip_host_rg);
    }
    else {
      file_write_inet(f, key_netmask, &netmask_rg);
    }
            
    if(gateway_rg.s_addr) {
      file_write_inet(f, key_gateway, &gateway_rg);
    }

    if(nameserver_rg.s_addr) {
      file_write_inet(f, key_dnsserver, &nameserver_rg);
    }

    {
      struct in_addr *server_address = &nfs_server_rg;
      char *server_dir = server_dir_tg;

      if(bootmode_ig == BOOTMODE_FTP) {
        server_address = &ftp_server_rg;
      }
      else if(bootmode_ig == BOOTMODE_SMB) {
        server_address = &config.smb.server;
        server_dir     = config.smb.share;
      }

      file_write_inet(f, key_server, server_address);
      file_write_str(f, key_serverdir, server_dir);
    }

    if(*machine_name_tg) {
      file_write_str(f, key_machine, machine_name_tg);
    }

    if(*domain_name_tg) {
      file_write_str(f, key_domain, domain_name_tg);
    }
  }

  if(bootmode_ig == BOOTMODE_FTP) {
    if(*ftp_user_tg) {
      file_write_str(f, key_ftpuser, ftp_user_tg);
    }
    if(*ftp_proxy_tg) {
      file_write_str(f, key_ftpproxy, ftp_proxy_tg);
    }
    if(ftp_proxyport_ig != -1) {
      file_write_num(f, key_ftpproxyport, ftp_proxyport_ig);
    }
  }

  if(bootmode_ig == BOOTMODE_SMB) {
    if(config.smb.user) {
      file_write_str(f, key_username, config.smb.user);
    }
    if(config.smb.password) {
      file_write_str(f, key_password, config.smb.password);
    }
    if(config.smb.workgroup) {
      file_write_str(f, key_workdomain, config.smb.workgroup);
    }
  }

  file_write_modparms(f);

  file_write_num(f, key_manual, auto_ig || auto2_ig ? 0 : 1);
  file_write_num(f, key_demo, demo_ig);

  if(reboot_ig) file_write_num(f, key_reboot, reboot_ig);

#ifdef USE_LIBHD
  if(config.floppies) {
    file_write_str(f, key_floppydisk, config.floppy_dev[config.floppy]);
  }

  file_write_num(f, key_keyboard, has_kbd_ig);
  file_write_num(f, key_yast2update, yast2_update_ig || *driver_update_dir ? 1 : 0);

  file_write_num(f, key_yast2serial, yast2_serial_ig);
  file_write_num(f, key_textmode, text_mode_ig);

  if((action_ig & ACT_YAST2_AUTO_INSTALL)) {
    file_write_num(f, key_yast2autoinst, 1);
  }

  file_write_num(f, key_usb, usb_ig);

  if(yast2_color_ig) {
    fprintf(f, "%s: %06x\n", file_key2str(key_yast2color), yast2_color_ig);
  }

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


void file_write_mtab()
{
  char smb_mount_options[200];
  FILE *f;

  if(!(f = fopen(MTAB_FILE, "w"))) return;

  fprintf(f,
    "/dev/initrd / minix rw 0 0\n"
    "none /proc proc rw 0 0\n"
  );

  if(!ramdisk_ig) {
    switch(bootmode_ig) {
      case BOOTMODE_CD:
      case BOOTMODE_CDWITHNET:
        fprintf(f, "/dev/%s %s iso9660 ro 0 0\n", cdrom_tg, mountpoint_tg);
        break;

      case BOOTMODE_NET:
        fprintf(f,
          "%s:%s %s nfs ro 0 0\n",
          inet_ntoa(nfs_server_rg), server_dir_tg, mountpoint_tg
        );
        break;

      case BOOTMODE_SMB:
        net_smb_get_mount_options(smb_mount_options);
        fprintf(f,
          "//%s/%s %s smbfs ro,%s 0 0\n",
          inet_ntoa(config.smb.server), config.smb.share,
          mountpoint_tg, smb_mount_options
        );
        break;
    }
  }
  else {
    fprintf(f, "/dev/ram2 %s ext2 ro 0 0\n", inst_mountpoint_tg);
  }

  fclose(f);
}


void file_write_modparms(FILE *f)
{
  file_t *ft0, *ft;
  module2_t *ml;
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
      if(ml->initrd) {
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
      pl0 = slist_split(ml->pre_inst);
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
      pl0 = slist_split(ml->post_inst);
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
  file_t *ft0 = NULL, **ft = &ft0;
  char cmdline[1024], *current, *s, *s1, *t;
  int i, quote;

  if(!(f = fopen(CMDLINE_FILE, "r"))) return NULL;
  if(!fgets(cmdline, sizeof cmdline, f)) *cmdline = 0;
  fclose(f);

  current = cmdline;

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
      (*ft)->key = file_str2key(t);
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
  char module[64], params[256] /*, text[256] */;
//  window_t win;
  int i;

  i = sscanf(insmod_arg, "%63s %255[^\n]", module, params);

  if(i < 1) return;

  if(i == 1) *params = 0;

#if 0
  sprintf(text, txt_get(TXT_TRY_TO_LOAD), module);
  dia_info(&win, text);
#endif

  if(!mod_load_module(module, params)) {
    switch(mod_get_mod_type(module)) {
      case MOD_TYPE_SCSI:
        strcpy(scsi_tg, module);
        break;
      case MOD_TYPE_NET:
        strcpy(net_tg, module);
        break;
    }
  }

#if 0
  win_close(&win);
#endif
}


#ifdef DEBUG_FILE

void file_dump_flist(file_t *ft)
{
  for(; ft; ft = ft->next) {
    fprintf(stderr, "%d: \"%s\" = \"%s\"\n", ft->key, ft->key_str, ft->value);
    if(ft->is.numeric) fprintf(stderr, "  num = %d\n", ft->nvalue);
    if(ft->is.inet) fprintf(stderr, "  inet = %s\n", inet_ntoa(ft->ivalue));
  }
}

#endif


module2_t *file_read_modinfo(char *name)
{
  FILE *f;
  char buf[1024];
  char *s, *s1, *t, *current;
  module2_t *ml0 = NULL, **ml = &ml0, *ml1;
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
    while(*current && fields < sizeof field / sizeof *field);

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

            if(!config.module.scsi_type && !strcasecmp(s, "scsi")) {
              config.module.scsi_type = current_type;
            }
            if(!config.module.network_type && !strcasecmp(s, "network")) {
              config.module.network_type = current_type;
            }
            if(!config.module.cdrom_type && !strcasecmp(s, "cdrom")) {
              config.module.cdrom_type = current_type;
            }
            if(!config.module.pcmcia_type && !strcasecmp(s, "pcmcia")) {
              config.module.pcmcia_type = current_type;
            }
          }
        }
        free(field[--fields]);
      }
      else {
        if(!strncasecmp(field[0], "MoreModules", sizeof "MoreModules" - 1)) {
          s = field[0] + sizeof "MoreModules" - 1;
          while(*s == '=' || isspace(*s)) s++;
          if(!config.module.more_file[current_type]) free(config.module.more_file[current_type]);
          config.module.more_file[current_type] = strdup(s);
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
      if(fields > 2 && *field[2]) ml1->param = strdup(field[2]);
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

void file_dump_mlist(module2_t *ml)
{
  for(; ml; ml = ml->next) {
    fprintf(stderr, "%s (%s:%s): \"%s\"\n",
      ml->name,
      config.module.type_name[ml->type],
      config.module.more_file[ml->type] ?: "-",
      ml->descr ?: ""
    );
    fprintf(stderr, "  initrd = %s, show = %s, auto = %s\n",
      ml->initrd ? "yes" : "no",
      ml->descr ? "yes" : "no",
      ml->autoload ? "yes" : "no"
    );
    if(ml->param) fprintf(stderr, "  param: \"%s\"\n", ml->param);
    if(ml->pre_inst) fprintf(stderr, "  pre_inst: \"%s\"\n", ml->pre_inst);
    if(ml->post_inst) fprintf(stderr, "  post_inst: \"%s\"\n", ml->post_inst);
  }
}

#endif


