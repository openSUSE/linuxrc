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
#include "modparms.h"
#include "window.h"
#include "dialog.h"
#include "net.h"
#include "settings.h"
#include "auto2.h"
#if WITH_PCMCIA
#include "pcmcia.h"
#endif

static const char  *file_infofile_tm           = "/etc/install.inf";

static const char  *file_txt_keymap_tm         = "Keytable:";
static const char  *file_txt_sourcemount_tm    = "Sourcemounted:";
static const char  *file_txt_display_tm        = "Display:";
static const char  *file_txt_cdrom_tm          = "Cdrom:";
#if WITH_PCMCIA
static const char  *file_txt_pcmcia_tm         = "PCMCIA:";
#endif
static const char  *file_txt_bootmode_tm       = "Bootmode:";
static const char  *file_txt_bootfloppy_tm     = "Floppy";
static const char  *file_txt_bootcd_tm         = "CD";
static const char  *file_txt_bootnet_tm        = "Net";
static const char  *file_txt_bootsmb_tm        = "SMB";
static const char  *file_txt_bootharddisk_tm   = "Harddisk";
static const char  *file_txt_bootftp_tm        = "FTP";
static const char  *file_txt_partition_tm      = "Partition:";
static const char  *file_txt_fstyp_tm          = "Fstyp:";
static const char  *file_txt_serverdir_tm      = "Serverdir:";
static const char  *file_txt_ip_tm             = "IP:";
static const char  *file_txt_pliphost_tm       = "PLIP-Host:";
static const char  *file_txt_netmask_tm        = "Netmask:";
static const char  *file_txt_gateway_tm        = "Gateway:";
static const char  *file_txt_server_tm         = "Server:";
static const char  *file_txt_dnsserver_tm      = "Nameserver:";
static const char  *file_txt_netdevice_tm      = "Netdevice:";
static const char  *file_txt_machine_name_tm   = "Machinename:";
static const char  *file_txt_broadcast_tm      = "Broadcast:";
static const char  *file_txt_network_tm        = "Network:";
static const char  *file_txt_domain_name_tm    = "Domain:";
static const char  *file_txt_ftp_user_tm       = "FTP-User:";
static const char  *file_txt_ftp_proxy_tm      = "FTP-Proxy:";
static const char  *file_txt_ftp_proxy_port_tm = "FTP-Proxyport:";
static const char  *file_txt_manual_tm         = "Manual:";
static const char  *file_txt_demo_tm           = "Demo:";
static const char  *file_txt_reboot            = "Reboot:";
static const char  *file_txt_console_tm        = "Console:";
#ifdef USE_LIBHD
static const char  *file_txt_mouse_dev_tm      = "Mousedevice:";
static const char  *file_txt_mouse_xf86_tm     = "MouseXF86:";
static const char  *file_txt_mouse_gpm_tm      = "MouseGPM:";
static const char  *file_txt_has_floppy_tm     = "Floppydisk:";
static const char  *file_txt_has_kbd_tm        = "Keyboard:";
static const char  *file_txt_yast2_update_tm   = "YaST2update:";
static const char  *file_txt_yast2_serial_tm   = "YaST2serial:";
static const char  *file_txt_yast2_autoinst_tm = "YaST2AutoInstall:";
static const char  *file_txt_text_mode_tm      = "Textmode:";
static const char  *file_txt_has_pcmcia_tm     = "HasPCMCIA:";
static const char  *file_txt_usb_tm            = "USB:";
static const char  *file_txt_yast2_color_tm    = "YaST2color:";
static const char  *file_txt_boot_disk_tm      = "BootDisk:";
static const char  *file_txt_disks_tm          = "Disks:";
#endif

static file_key_t file_str2key(char *value);

static void file_module_load (char *insmod_arg);

static struct {
  file_key_t key;
  char *value;
} keywords[] = {
  { key_none,           ""               },
  { key_swap,           "Swap"           },
  { key_root,           "Root"           },
  { key_live,           "Live"           },
  { key_keytable,       "Keytable"       },
  { key_language,       "Language"       },
  { key_rebootmsg,      "RebootMsg"      },
  { key_insmod,         "insmod"         },
  { key_autoprobe,      "autoprobe"      },
  { key_start_pcmcia,   "start_pcmcia"   },
  { key_color,          "Color"          },
  { key_bootmode,       "Bootmode"       },
  { key_ip,             "IP"             },
  { key_netmask,        "Netmask"        },
  { key_gateway,        "Gateway"        },
  { key_server,         "Server"         },
  { key_dnsserver,      "Nameserver"     },
  { key_partition,      "Partition"      },
  { key_serverdir,      "Serverdir"      },
  { key_netdevice,      "Netdevice"      },
  { key_livesrc,        "LiveSRC"        },
  { key_bootpwait,      "Bootpwait"      },
  { key_bootptimeout,   "BOOTP_TIMEOUT"  },
  { key_forcerootimage, "ForceRootimage" },
  { key_rebootwait,     "WaitReboot"     }
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
  { "Mono",      0                  },
  { "Color",     1                  },
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


file_t *file_read_file(char *name)
{
  FILE *f;
  char buf1[256], buf2[256], *s, *t;
  int i, l;
  file_t *ft0 = NULL, **ft = &ft0;
  struct in_addr in;

  if(!(f = fopen(name, "r"))) return NULL;

  while((i = fscanf(f, "%255s %255[^\n]", buf1, buf2)) != EOF) {
    if(i) {
      *ft = calloc(1, sizeof **ft);

      l = strlen(buf1);
      if(l && buf1[l - 1] == ':') buf1[l - 1] = 0;

      (*ft)->key_str = strdup(buf1);	/* Maybe we should include ':'? */

      if(i == 2) {
        l = strlen(buf2);
        while(l && isspace(buf2[l - 1])) buf2[--l] = 0;
      }
      else {
        *buf2 = 0;
      }

      (*ft)->key = file_str2key(buf1);
      (*ft)->value = s = strdup(buf2);

      if(*s) {
        l = strtol(s, &t, 0);
        if(!*t) {
          (*ft)->nvalue = l;
          (*ft)->is.numeric = 1;
        }
        else {
          for(l = 0; l < sizeof sym_constants / sizeof *sym_constants; l++) {
            if(!strcasecmp(sym_constants[l].name, s)) {
              (*ft)->nvalue = sym_constants[l].value;
              (*ft)->is.numeric = 1;
              break;
            }
          }
        }
        if(!(*ft)->is.numeric) {
          if(!net_check_address(s, &in)) {
            (*ft)->ivalue = in;
            (*ft)->is.inet = 1;
          }
        }
      }

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
    if(file->value) free(file->value);
    free(file);
  }
}


void file_write_yast_info (char *file_name)
     /*
Bootmode: SMB
Server: <samba server>
Serverdir: <samba share>
Username: <samba user>  // falls nicht gesetzt, Anmeldung als 'guest'
Password: <samba password>
AsWorkgroup: <1 == workgroup, 0 == domain>
WorkDomain: <Workgroup name falls AsWorkgroup == 1, sonst Domainname>
     */
    {
    FILE  *file_pri;
    char   line_ti [200];


    file_pri = fopen (file_name ? file_name : file_infofile_tm, "w");
    if (!file_pri)
        {
        fprintf (stderr, "Cannot open yast info file\n");
        return;
        }

    if (language_ig != LANG_UNDEF)
        set_write_info (file_pri);

    strcpy (line_ti, file_txt_sourcemount_tm);
    if (!ramdisk_ig)
        strcat (line_ti, " 1\n");
    else
        strcat (line_ti, " 0\n");
    fprintf (file_pri, line_ti);

    strcpy (line_ti, file_txt_display_tm);
    if (colors_prg->has_colors)
        strcat (line_ti, " Color\n");
    else
        strcat (line_ti, " Mono\n");
    fprintf (file_pri, line_ti);

    if (keymap_tg [0] && (!auto2_ig || yast_version_ig == 1))
        fprintf (file_pri, "%s %s\n", file_txt_keymap_tm, keymap_tg);
    if (cdrom_tg [0])
        fprintf (file_pri, "%s %s\n", file_txt_cdrom_tm, cdrom_tg);
    if (net_tg [0])
        fprintf (file_pri, "alias %s %s\n", netdevice_tg, net_tg);
    if (ppcd_tg [0])
        {
        fprintf (file_pri, "post-install paride insmod %s\n", ppcd_tg);
        fprintf (file_pri, "pre-remove paride rmmod %s\n", ppcd_tg);
        }

#if WITH_PCMCIA
    if (pcmcia_chip_ig == 1 || pcmcia_chip_ig == 2)
        {
        strcpy (line_ti, file_txt_pcmcia_tm);
        if (pcmcia_chip_ig == 1)
            strcat (line_ti, " tcic\n");
        else
            strcat (line_ti, " i82365\n");
        fprintf (file_pri, line_ti);
        }
#endif
#ifdef USE_LIBHD
    fprintf (file_pri, "%s %d\n", file_txt_has_pcmcia_tm, auto2_pcmcia() || pcmcia_chip_ig ? 1 : 0);
#endif

    if (serial_ig)
        fprintf (file_pri, "%s %s\n", file_txt_console_tm, console_parms_tg);

    strcpy (line_ti, file_txt_bootmode_tm);
    strcat (line_ti, " ");
    switch (bootmode_ig)
        {
        case BOOTMODE_FLOPPY:
            strcat (line_ti, file_txt_bootfloppy_tm);
            break;
        case BOOTMODE_CD:
        case BOOTMODE_CDWITHNET:
            strcat (line_ti, file_txt_bootcd_tm);
            break;
        case BOOTMODE_SMB:
            strcat (line_ti, file_txt_bootsmb_tm);
            break;
        case BOOTMODE_NET:
            strcat (line_ti, file_txt_bootnet_tm);
            break;
        case BOOTMODE_HARDDISK:
            strcat (line_ti, file_txt_bootharddisk_tm);
            break;
        case BOOTMODE_FTP:
            strcat (line_ti, file_txt_bootftp_tm);
            break;
        default:
            strcat (line_ti, "unknown");
            break;
        }
    strcat (line_ti, "\n");
    fprintf (file_pri, line_ti);

    if (bootmode_ig == BOOTMODE_HARDDISK)
        {
        fprintf (file_pri, "%s %s\n", file_txt_partition_tm, harddisk_tg);
        fprintf (file_pri, "%s %s\n", file_txt_fstyp_tm, fstype_tg);
        fprintf (file_pri, "%s %s\n", file_txt_serverdir_tm, server_dir_tg);
        }

    if ( bootmode_ig == BOOTMODE_NET || 
	 bootmode_ig == BOOTMODE_SMB || 
	 bootmode_ig == BOOTMODE_FTP || 
	 bootmode_ig == BOOTMODE_CDWITHNET )
        {
        fprintf (file_pri, "%s %s\n", file_txt_netdevice_tm, netdevice_tg);
        fprintf (file_pri, "%s %s\n", file_txt_ip_tm, inet_ntoa (ipaddr_rg));

	if ( bootmode_ig == BOOTMODE_CDWITHNET ) {
            broadcast_rg.s_addr = ipaddr_rg.s_addr | ~netmask_rg.s_addr;
            network_rg.s_addr = ipaddr_rg.s_addr & netmask_rg.s_addr;
            fprintf (file_pri, "%s %s\n", file_txt_broadcast_tm, inet_ntoa ( broadcast_rg ) );
            fprintf (file_pri, "%s %s\n", file_txt_network_tm,   inet_ntoa ( network_rg ) );
  	}

        if (plip_host_rg.s_addr)
            fprintf (file_pri, "%s %s\n", file_txt_pliphost_tm,
                                          inet_ntoa (plip_host_rg));
        else
            fprintf (file_pri, "%s %s\n", file_txt_netmask_tm,
                                          inet_ntoa (netmask_rg));
            
        if (gateway_rg.s_addr)
            fprintf (file_pri, "%s %s\n", file_txt_gateway_tm,
                                          inet_ntoa (gateway_rg));
        if (nameserver_rg.s_addr)
            fprintf (file_pri, "%s %s\n", file_txt_dnsserver_tm,
                                          inet_ntoa (nameserver_rg));
	{
	    struct in_addr *server_address = 0;
	    char           *server_dir = server_dir_tg;
	    switch (bootmode_ig) {
	    case BOOTMODE_SMB:
		server_address = &config.smb.server;
		server_dir     = config.smb.share;
		break;
	    case BOOTMODE_NET:
		server_address = &nfs_server_rg;
		break;
	    case BOOTMODE_FTP:
		server_address = &ftp_server_rg;
		break;
	    }
	    fprintf (file_pri, "%s %s\n", file_txt_server_tm,
		     inet_ntoa (*server_address));
	    fprintf (file_pri, "%s %s\n", file_txt_serverdir_tm, server_dir);
	}

        if (machine_name_tg [0])
            fprintf (file_pri, "%s %s\n", file_txt_machine_name_tm,
                                          machine_name_tg);
        if (domain_name_tg [0])
            fprintf (file_pri, "%s %s\n", file_txt_domain_name_tm,
                                          domain_name_tg);
        }

    if (bootmode_ig == BOOTMODE_FTP)
        {
        if (ftp_user_tg [0])
            fprintf (file_pri, "%s %s\n", file_txt_ftp_user_tm,
                                          ftp_user_tg);
        if (ftp_proxy_tg [0])
            fprintf (file_pri, "%s %s\n", file_txt_ftp_proxy_tm,
                                          ftp_proxy_tg);
        if (ftp_proxyport_ig != -1)
            fprintf (file_pri, "%s %d\n", file_txt_ftp_proxy_port_tm,
                                          ftp_proxyport_ig);
        }

    mpar_write_modparms (file_pri);

    fprintf (file_pri, "%s %d\n", file_txt_manual_tm, auto_ig || auto2_ig ? 0 : 1);
    fprintf (file_pri, "%s %d\n", file_txt_demo_tm, demo_ig);
    if(reboot_ig)
        fprintf (file_pri, "%s 1\n", file_txt_reboot);

#ifdef USE_LIBHD
    if (mouse_dev_ig)
        fprintf (file_pri, "%s %s\n", file_txt_mouse_dev_tm, mouse_dev_ig);
    if (mouse_type_xf86_ig)
        fprintf (file_pri, "%s %s\n", file_txt_mouse_xf86_tm, mouse_type_xf86_ig);
    if (mouse_type_gpm_ig)
        fprintf (file_pri, "%s %s\n", file_txt_mouse_gpm_tm, mouse_type_gpm_ig);

    if(config.floppies)
        fprintf(file_pri, "%s %s\n", file_txt_has_floppy_tm, config.floppy_dev[config.floppy]);
    fprintf (file_pri, "%s %d\n", file_txt_has_kbd_tm, has_kbd_ig);
    fprintf(file_pri, "%s %d\n",
      file_txt_yast2_update_tm,
      (yast2_update_ig || *driver_update_dir) ? 1 : 0
    );
    fprintf (file_pri, "%s %d\n", file_txt_yast2_serial_tm, yast2_serial_ig);
    fprintf (file_pri, "%s %d\n", file_txt_text_mode_tm, text_mode_ig);
    if ((action_ig & ACT_YAST2_AUTO_INSTALL))
        fprintf (file_pri, "%s %d\n", file_txt_yast2_autoinst_tm, 1);

    fprintf (file_pri, "%s %d\n", file_txt_usb_tm, usb_ig);

    if(yast2_color_ig) {
      fprintf (file_pri, "%s %06x\n", file_txt_yast2_color_tm, yast2_color_ig);
    }

    {
      char *s;
      int boot_disk;

      s = auto2_disk_list(&boot_disk);
      if(*s) {
        fprintf (file_pri, "%s %d\n", file_txt_boot_disk_tm, boot_disk ? 1 : 0);
        fprintf (file_pri, "%s %s\n", file_txt_disks_tm, s);
      }
    }

#endif

    fclose (file_pri);

    file_pri = fopen ("/etc/mtab", "w");
    if (!file_pri)
        return;

    fprintf (file_pri, "/dev/initrd / minix rw 0 0\n");
    fprintf (file_pri, "none /proc proc rw 0 0\n");

    if (!ramdisk_ig)
        {
        switch (bootmode_ig)
            {
            case BOOTMODE_CD:
            case BOOTMODE_CDWITHNET:
                sprintf (line_ti, "/dev/%s %s iso9660 ro 0 0\n", cdrom_tg, mountpoint_tg);
                break;
		// TODO: +=SMB
            case BOOTMODE_NET:
                sprintf (line_ti, "%s:%s %s nfs ro 0 0\n",
                                  inet_ntoa (nfs_server_rg),
                                  server_dir_tg, mountpoint_tg);
                break;
            case BOOTMODE_SMB: {
		char smb_mount_options[200];
		net_smb_get_mount_options(smb_mount_options);
                sprintf (line_ti, 
			 "//%s/%s %s smbfs ro,%s 0 0\n",
			 inet_ntoa (config.smb.server),
			 config.smb.share,
			 mountpoint_tg,
			 smb_mount_options);
	        }
	        break;
            default:
                line_ti [0] = 0;
                break;
            }
        }
    else
        sprintf (line_ti, "/dev/ram2 %s ext2 ro 0 0\n", inst_mountpoint_tg);

    fprintf (file_pri, line_ti);

    fclose (file_pri);
    }


int file_read_info()
{
  char filename_ti[MAX_FILENAME] = "/info";
  window_t win_ri;
  int do_autoprobe = FALSE;
#if WITH_PCMCIA
  int start_pcmcia = FALSE;
#endif
  int i, mounted = FALSE;
  file_t *f0, *f;

  if(auto2_ig) {
    printf("%s...", txt_get(TXT_SEARCH_INFOFILE));
    fflush(stdout);
  }
  else {
    dia_info(&win_ri, txt_get(TXT_SEARCH_INFOFILE));
  }

  f0 = file_read_file(filename_ti);

  if(!f0) {
    for(i = 0; i < config.floppies; i++) {
      if(!util_try_mount(config.floppy_dev[i], mountpoint_tg, MS_MGC_VAL | MS_RDONLY, 0)) break;
    }
    if(i < config.floppies) {
      config.floppy = i;	// remember currently used floppy

      mounted = TRUE;

      util_chk_driver_update(mountpoint_tg);

      sprintf(filename_ti, "%s/suse/setup/descr/info", mountpoint_tg);
      f0 = file_read_file(filename_ti);
      if(!f0) {
        sprintf(filename_ti, "%s/info", mountpoint_tg);
        f0 = file_read_file(filename_ti);
      }
    }
  }

  if(!f0) {
    if(mounted) umount(mountpoint_tg);
    if(auto2_ig) printf ("\n"); else win_close (&win_ri);
    return -1;
  }

  valid_net_config_ig = 0;

  for(f = f0; f; f = f->next) {
    switch(f->key) {
      case key_insmod:
        file_module_load(f->value);
        break;

      case key_autoprobe:
        do_autoprobe = TRUE;
        break;

#if WITH_PCMCIA
      case key_start_pcmcia:
        start_pcmcia = TRUE;
        break;
#endif

      case key_language:
        i = set_langidbyname(f->value);
        if(i != LANG_UNDEF) language_ig = i;
        break;

      case key_color:
        color_ig = f->nvalue;
        break;

      case key_keytable:
        strncpy(keymap_tg, f->value, sizeof keymap_tg);
        keymap_tg[sizeof keymap_tg - 1] = 0;
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

      default:
    }
  }

  if((valid_net_config_ig & 3) == 3) {
    broadcast_rg.s_addr = ipaddr_rg.s_addr | ~netmask_rg.s_addr;
    network_rg.s_addr = ipaddr_rg.s_addr & netmask_rg.s_addr;
  }

  file_free_file(f0);

  if(mounted) umount(mountpoint_tg);

  if(auto2_ig) printf("\n"); else win_close(&win_ri);

  if(do_autoprobe) mod_autoload();

#if WITH_PCMCIA
  if(start_pcmcia) pcmcia_load_core();
#endif

  return 0;
}


void file_module_load(char *insmod_arg)
{
  char module[64], params[256], text[256];
  window_t win;
  int i;

  i = sscanf(insmod_arg, "%63s %255[^\n]", module, params);

  if(i < 1) return;

  if(i == 1) *params = 0;

  sprintf(text, txt_get(TXT_TRY_TO_LOAD), module);
  dia_info(&win, text);

  if(!mod_load_module(module, params)) {
    mpar_save_modparams(module, params);
    switch(mod_get_mod_type(module)) {
      case MOD_TYPE_SCSI:
        strcpy(scsi_tg, module);
        break;
      case MOD_TYPE_NET:
        strcpy(net_tg, module);
        break;
    }
  }

  win_close(&win);
}

#if 0
static void file_module_load (char *command_tv)
    {
    char      module_ti [30];
    char      params_ti [MAX_PARAM_LEN];
    char     *tmp_pci;
    int       i_ii = 0;
    char      text_ti [200];
    window_t  win_ri;


    tmp_pci = command_tv;
#if 0
    tmp_pci = strchr (command_tv, ' ');
    if (!tmp_pci)
        return;

    while (*tmp_pci && (*tmp_pci == ' ' || *tmp_pci == 0x09))
        tmp_pci++;
#endif

    while (*tmp_pci && *tmp_pci != ' ' && *tmp_pci != 0x09)
        module_ti [i_ii++] = *tmp_pci++;

    module_ti [i_ii] = 0;

    while (*tmp_pci && (*tmp_pci == ' ' || *tmp_pci == 0x09))
        tmp_pci++;

    strcpy (params_ti, tmp_pci);

    sprintf (text_ti, txt_get (TXT_TRY_TO_LOAD), module_ti);
    dia_info (&win_ri, text_ti);
    if (!mod_load_module (module_ti, params_ti))
        {
        mpar_save_modparams (module_ti, params_ti);
        switch (mod_get_mod_type (module_ti))
            {
            case MOD_TYPE_SCSI:
                strcpy (scsi_tg, module_ti);
                break;
            case MOD_TYPE_NET:
                strcpy (net_tg, module_ti);
                break;
            default:
                break;
            }
        }

    win_close (&win_ri);
    }
#endif
