/*
 *
 * file.c        File access
 *
 * Copyright (c) 1996-2000  Hubert Mantel, SuSE GmbH  (mantel@suse.de)
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
#include "text.h"
#include "util.h"
#include "module.h"
#include "modparms.h"
#include "window.h"
#include "dialog.h"
#include "net.h"
#include "settings.h"
#include "smp.h"
#include "auto2.h"
#if WITH_PCMCIA
#include "pcmcia.h"
#endif

static const char  *file_infofile_tm           = "/etc/install.inf";

static const char  *file_txt_language_tm       = "Language:";
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
static const char  *file_txt_domain_name_tm    = "Domain:";
static const char  *file_txt_bootp_wait_tm     = "Bootpwait:";
static const char  *file_txt_ftp_user_tm       = "FTP-User:";
static const char  *file_txt_ftp_proxy_tm      = "FTP-Proxy:";
static const char  *file_txt_ftp_proxy_port_tm = "FTP-Proxyport:";
static const char  *file_txt_autoprobe_tm      = "autoprobe";
#if WITH_PCMCIA
static const char  *file_txt_start_pcmcia_tm   = "start_pcmcia";
#endif
static const char  *file_txt_console_tm        = "Console:";
#ifdef USE_LIBHD
static const char  *file_txt_mouse_dev_tm      = "Mousedevice:";
static const char  *file_txt_mouse_xf86_tm     = "MouseXF86:";
static const char  *file_txt_mouse_gpm_tm      = "MouseGPM:";
static const char  *file_txt_has_floppy_tm     = "Floppydisk:";
static const char  *file_txt_has_kbd_tm        = "Keyboard:";
static const char  *file_txt_yast2_update_tm   = "YaST2update:";
static const char  *file_txt_yast2_serial_tm   = "YaST2serial:";
static const char  *file_txt_text_mode_tm      = "Textmode:";
static const char  *file_txt_fb_mode_tm        = "Framebuffer:";
static const char  *file_txt_has_pcmcia_tm     = "HasPCMCIA:";
#endif

static void file_get_value   (char *input_tv, char *value_tr);
static void file_trim_buffer (char *buffer_tr);
static void file_module_load (char *command_tv);

void file_write_yast_info (void)
    {
    FILE  *file_pri;
    char   line_ti [200];


    file_pri = fopen (file_infofile_tm, "w");
    if (!file_pri)
        {
        fprintf (stderr, "Cannot open yast info file\n");
        return;
        }

    if (!auto2_ig)
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

    if (keymap_tg [0] && !auto2_ig)
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
    fprintf (file_pri, "%s %d\n", file_txt_has_pcmcia_tm, auto2_pcmcia());
#endif

    if (serial_ig)
        fprintf (file_pri, "%s %s\n", file_txt_console_tm, console_tg);

    strcpy (line_ti, file_txt_bootmode_tm);
    strcat (line_ti, " ");
    switch (bootmode_ig)
        {
        case BOOTMODE_FLOPPY:
            strcat (line_ti, file_txt_bootfloppy_tm);
            break;
        case BOOTMODE_CD:
            strcat (line_ti, file_txt_bootcd_tm);
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

    if (bootmode_ig == BOOTMODE_NET || bootmode_ig == BOOTMODE_FTP)
        {
        fprintf (file_pri, "%s %s\n", file_txt_netdevice_tm, netdevice_tg);

        fprintf (file_pri, "%s %s\n", file_txt_ip_tm, inet_ntoa (ipaddr_rg));

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
        fprintf (file_pri, "%s %s\n", file_txt_server_tm,
                 inet_ntoa (bootmode_ig == BOOTMODE_NET ? nfs_server_rg : ftp_server_rg));
        fprintf (file_pri, "%s %s\n", file_txt_serverdir_tm, server_dir_tg);

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

    if (detectSMP ())
        fprintf (file_pri, "SMP: 1\n");
    else
        fprintf (file_pri, "SMP: 0\n");

#ifdef USE_LIBHD
    if (mouse_dev_ig)
        fprintf (file_pri, "%s %s\n", file_txt_mouse_dev_tm, mouse_dev_ig);
    if (mouse_type_xf86_ig)
        fprintf (file_pri, "%s %s\n", file_txt_mouse_xf86_tm, mouse_type_xf86_ig);
    if (mouse_type_gpm_ig)
        fprintf (file_pri, "%s %s\n", file_txt_mouse_gpm_tm, mouse_type_gpm_ig);

    fprintf (file_pri, "%s %d\n", file_txt_has_floppy_tm, has_floppy_ig);
    fprintf (file_pri, "%s %d\n", file_txt_has_kbd_tm, has_kbd_ig);
    fprintf (file_pri, "%s %d\n", file_txt_yast2_update_tm, yast2_update_ig);
    fprintf (file_pri, "%s %d\n", file_txt_yast2_serial_tm, yast2_serial_ig);
    fprintf (file_pri, "%s %d\n", file_txt_text_mode_tm, text_mode_ig);

    if (frame_buffer_mode_ig)
        fprintf (file_pri, "%s 0x%04x\n", file_txt_fb_mode_tm, frame_buffer_mode_ig);
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
                sprintf (line_ti, "/dev/%s %s iso9660 ro 0 0\n", cdrom_tg, mountpoint_tg);
                break;
            case BOOTMODE_NET:
                sprintf (line_ti, "%s:%s %s nfs ro 0 0\n",
                                  inet_ntoa (nfs_server_rg),
                                  server_dir_tg, mountpoint_tg);
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


int file_read_info (void)
    {
    char      filename_ti [MAX_FILENAME] = "/info";
    FILE     *fd_pri;
    char      buffer_ti [MAX_X];
    window_t  win_ri;
    char      value_ti [MAX_X];
    int       do_autoprobe_ii = FALSE;
#if WITH_PCMCIA
    int       start_pcmcia_ii = FALSE;
#endif
    int       need_mount_ii = FALSE;


    if(auto2_ig)
        {
        printf("%s...", txt_get (TXT_SEARCH_INFOFILE));
        fflush(stdout);
        }
    else
        {
        dia_info (&win_ri, txt_get (TXT_SEARCH_INFOFILE));
        }

    fd_pri = fopen (filename_ti, "r");
    if (!fd_pri)
        {
        if (!has_floppy_ig || util_try_mount ("/dev/fd0", mountpoint_tg, MS_MGC_VAL | MS_RDONLY, 0))
            if (util_try_mount (floppy_tg, mountpoint_tg, MS_MGC_VAL | MS_RDONLY, 0))
                {
                if(auto2_ig) printf ("\n"); else win_close (&win_ri);
                return (-1);
                }

        sprintf (filename_ti, "%s/suse/setup/descr/info", mountpoint_tg);
        fd_pri = fopen (filename_ti, "r");
        if (!fd_pri)
            {
            sprintf (filename_ti, "%s/info", mountpoint_tg);
            fd_pri = fopen (filename_ti, "r");
            if (!fd_pri)
                {
                umount (mountpoint_tg);
                if(auto2_ig) printf ("\n"); else win_close (&win_ri);
                return (-1);
                }
            }

        need_mount_ii = TRUE;
        }

    valid_net_config_ig = 0;

    while (fgets (buffer_ti, sizeof (buffer_ti) - 1, fd_pri))
        {
        fprintf (stderr, "%s", buffer_ti);
        file_trim_buffer (buffer_ti);

        if (!strncasecmp (buffer_ti, "insmod", 6))
            file_module_load (buffer_ti);

        if (!strncasecmp (buffer_ti, file_txt_autoprobe_tm,
                      strlen (file_txt_autoprobe_tm)))
            do_autoprobe_ii = TRUE;

#if WITH_PCMCIA
        if (!strncasecmp (buffer_ti, file_txt_start_pcmcia_tm,
                      strlen (file_txt_start_pcmcia_tm)))
            start_pcmcia_ii = TRUE;
#endif

        file_get_value (buffer_ti, value_ti);

        if (!strncasecmp (file_txt_language_tm, buffer_ti,
                          strlen (file_txt_language_tm)))
            {
            if (!strncasecmp (value_ti, "english", 7))
                language_ig = LANG_ENGLISH;
            else if (!strncasecmp (value_ti, "german", 6))
                language_ig = LANG_GERMAN;
            else if (!strncasecmp (value_ti, "italian", 8))
                language_ig = LANG_ITALIAN;
            else if (!strncasecmp (value_ti, "french", 6))
                language_ig = LANG_FRENCH;
            }

        if (!strncasecmp (file_txt_display_tm, buffer_ti,
                          strlen (file_txt_display_tm)))
            {
            if (!strncasecmp (value_ti, "Color", 5))
                color_ig = TRUE;
            else if (!strncasecmp (value_ti, "Mono", 4))
                color_ig = FALSE;
            }

        if (!strncasecmp (file_txt_keymap_tm, buffer_ti,
                          strlen (file_txt_keymap_tm)))
            strncpy (keymap_tg, value_ti, sizeof (keymap_tg));

        if (!strncasecmp (file_txt_bootmode_tm, buffer_ti,
                          strlen (file_txt_bootmode_tm)))
            {
            if (!strncasecmp (value_ti, file_txt_bootcd_tm,
                              strlen (file_txt_bootcd_tm)))
                bootmode_ig = BOOTMODE_CD;
            else if (!strncasecmp (value_ti, file_txt_bootharddisk_tm,
                                   strlen (file_txt_bootharddisk_tm)))
                bootmode_ig = BOOTMODE_HARDDISK;
            else if (!strncasecmp (value_ti, file_txt_bootnet_tm,
                                   strlen (file_txt_bootnet_tm)))
                bootmode_ig = BOOTMODE_NET;
            }

        if (!strncasecmp (file_txt_ip_tm, buffer_ti,
                          strlen (file_txt_ip_tm)))
            if (!net_check_address (value_ti, &ipaddr_rg)) valid_net_config_ig |= 1;

        if (!strncasecmp (file_txt_netmask_tm, buffer_ti,
                          strlen (file_txt_netmask_tm)))
            if (!net_check_address (value_ti, &netmask_rg)) valid_net_config_ig |= 2;

        if (!strncasecmp (file_txt_gateway_tm, buffer_ti,
                          strlen (file_txt_gateway_tm)))
            if (!net_check_address (value_ti, &gateway_rg)) valid_net_config_ig |= 4;

        if (!strncasecmp (file_txt_server_tm, buffer_ti,
                          strlen (file_txt_server_tm)))
            if (!net_check_address (value_ti, &nfs_server_rg)) valid_net_config_ig |= 8;

        if (!strncasecmp (file_txt_dnsserver_tm, buffer_ti,
                          strlen (file_txt_dnsserver_tm)))
            if (!net_check_address (value_ti, &nameserver_rg)) valid_net_config_ig |= 0x10;

        if (!strncasecmp (file_txt_serverdir_tm, buffer_ti,
                          strlen (file_txt_serverdir_tm)))
            {
            strncpy (server_dir_tg, value_ti, sizeof (server_dir_tg));
            valid_net_config_ig |= 0x20;
            }

        if (!strncasecmp (file_txt_netdevice_tm, buffer_ti,
                          strlen (file_txt_netdevice_tm)))
            strncpy (netdevice_tg, value_ti, sizeof (netdevice_tg));

        if (!strncasecmp (file_txt_bootp_wait_tm, buffer_ti,
                          strlen (file_txt_bootp_wait_tm)))
            bootp_wait_ig = atoi (value_ti);
        }

    fclose (fd_pri);

    if (need_mount_ii)
        umount (mountpoint_tg);

    if (auto2_ig)
        printf ("\n");
    else
        win_close (&win_ri);

    if (do_autoprobe_ii)
        mod_autoload ();

#if WITH_PCMCIA
    if (start_pcmcia_ii)
        pcmcia_load_core ();
#endif

    return (0);
    }


static void file_get_value (char *input_tv, char *value_tr)
    {
    char  *tmp_pci;

    tmp_pci = input_tv;
    while (*tmp_pci && *tmp_pci != ' ' && *tmp_pci != ':')
        tmp_pci++;

    if (*tmp_pci == ':')
        tmp_pci++;

    while (*tmp_pci && isspace (*tmp_pci))
        tmp_pci++;

    if (*tmp_pci)
        strcpy (value_tr, tmp_pci);
    else
        value_tr [0] = 0;
    }


static void file_trim_buffer (char *buffer_tr)
    {
    char  *tmp_pci;

    tmp_pci = strchr (buffer_tr, '#');
    if (tmp_pci)
        *tmp_pci = 0;

    tmp_pci = buffer_tr + strlen (buffer_tr) - 1;

    while (tmp_pci > buffer_tr &&
           (isspace (*tmp_pci) || *tmp_pci == '\r' || *tmp_pci == '\n'))
        *(tmp_pci--) = 0;
    }


static void file_module_load (char *command_tv)
    {
    char      module_ti [30];
    char      params_ti [MAX_PARAM_LEN];
    char     *tmp_pci;
    int       i_ii = 0;
    char      text_ti [200];
    window_t  win_ri;


    tmp_pci = strchr (command_tv, ' ');
    if (!tmp_pci)
        return;

    while (*tmp_pci && (*tmp_pci == ' ' || *tmp_pci == 0x09))
        tmp_pci++;

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
