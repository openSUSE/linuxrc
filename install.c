/*
 *
 * install.c           Handling of installation
 *
 * Copyright (c) 1996-2000  Hubert Mantel, SuSE GmbH  (mantel@suse.de)
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <ctype.h>
#include <string.h>
#include <signal.h>
#include <netdb.h>
#include <errno.h>
#include <sys/mount.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <sys/swap.h>
#include <sys/reboot.h>
#include <arpa/inet.h>

#include "global.h"
#include "linuxrc.h"
#include "text.h"
#include "util.h"
#include "dialog.h"
#include "window.h"
#include "net.h"
#include "display.h"
#include "rootimage.h"
#include "module.h"
#include "keyboard.h"
#include "file.h"
#include "info.h"
#include "ftp.h"
#include "install.h"
#include "settings.h"

#define YAST_INFO_FILE  "/etc/yast.inf"
#define YAST2_COMMAND   "/lib/YaST2/bin/YaST2.start"
#define YAST1_COMMAND   "/sbin/YaST"

static char  inst_rootimage_tm [MAX_FILENAME];
static int   inst_rescue_im = FALSE;
static int   inst_loopmount_im = FALSE;
static char *inst_tmpmount_tm = "/tmp/loopmount";
static char  inst_rescuefile_tm [MAX_FILENAME];
static char *inst_demo_sys_tm = "/suse/images/cd-demo";

static int   inst_mount_harddisk      (void);
static int   inst_try_cdrom           (char *device_tv);
static int   inst_mount_cdrom         (void);
static int   inst_mount_nfs           (void);
static int   inst_start_install       (void);
static int   inst_start_rescue        (void);
static void  inst_start_shell         (char *tty_tv);
static int   inst_prepare             (void);
static int   inst_execute_yast        (void);
static int   inst_check_floppy        (void);
static int   inst_commit_install      (void);
static int   inst_choose_source       (void);
static int   inst_choose_source_cb    (int what_iv);
static int   inst_menu_cb             (int what_iv);
static int   inst_init_cache          (void);
static void  inst_umount              (void);
static int   inst_get_nfsserver       (void);
static int   inst_get_ftpserver       (void);
static int   inst_ftp                 (void);
static int   inst_get_ftpsetup        (void);
static int   inst_choose_yast_version (void);


int inst_auto_install (void)
    {
    int       rc_ii;


    if (!auto_ig)
        return (-1);

    inst_rescue_im = FALSE;

    switch (bootmode_ig)
        {
        case BOOTMODE_CD:
            rc_ii = inst_mount_cdrom ();
            break;
        case BOOTMODE_NET:
            rc_ii = inst_mount_nfs ();
            break;
        default:
            rc_ii = -1;
            break;
        }

    if (!rc_ii)
        rc_ii = inst_check_instsys ();

    if (rc_ii)
        {
        inst_umount ();
        return (-1);
        }

    if (ramdisk_ig)
        {
        rc_ii = root_load_rootimage (inst_rootimage_tm);
        inst_umount ();
        if (rc_ii)
            return (rc_ii);

        mkdir (inst_mountpoint_tg, 0777);
        rc_ii = util_try_mount (RAMDISK_2, inst_mountpoint_tg,
                                MS_MGC_VAL | MS_RDONLY, 0);
        if (rc_ii)
            return (rc_ii);
        }

    return (inst_execute_yast ());
    }


int inst_start_demo (void)
    {
    int    rc_ii;
    char   filename_ti [MAX_FILENAME];
    FILE  *file_pri;
    char   line_ti [MAX_X];
    int    test_ii = FALSE;

    if (!auto2_ig)
        {
        if (demo_ig)
            if (!info_eide_cd_exists ())
                {
                rc_ii = mod_auto (MOD_TYPE_SCSI);
                if (rc_ii || !info_scsi_cd_exists ())
                    (void) mod_auto (MOD_TYPE_OTHER);
                }

        if (strcmp (rootimage_tg, "test"))
            test_ii = FALSE;
        else
            test_ii = TRUE;

        if (test_ii)
            rc_ii = inst_mount_nfs ();
        else
            {
            if (!demo_ig)
                (void) dia_message (txt_get (TXT_INSERT_LIVECD), MSGTYPE_INFO);

            rc_ii = inst_mount_cdrom ();
            }

        if (rc_ii)
            return (rc_ii);
        }

    sprintf (filename_ti, "%s/%s", mountpoint_tg, inst_demo_sys_tm);
    if (!util_check_exist (filename_ti))
        {
        util_disp_init();
        dia_message (txt_get (TXT_RI_NOT_FOUND), MSGTYPE_ERROR);
        inst_umount ();
        return (-1);
        }

    rc_ii = root_load_rootimage (filename_ti);
    inst_umount ();

    if (rc_ii)
        return (rc_ii);

    if (util_try_mount (RAMDISK_2, mountpoint_tg, 0, 0))
        return (-1);

    sprintf (filename_ti, "%s/%s", mountpoint_tg, "etc/fstab");
    file_pri = fopen (filename_ti, "a");

    if (bootmode_ig == BOOTMODE_NET)
        sprintf (line_ti, "%s:%s /S.u.S.E. nfs ro 0 0\n",
                 inet_ntoa (nfs_server_rg), server_dir_tg);
    else
        sprintf (line_ti, "/dev/%s /S.u.S.E. iso9660 ro 0 0\n", cdrom_tg);

    fprintf (file_pri, line_ti);
    fclose (file_pri);
    inst_umount ();
    return (0);
    }


int inst_menu (void)
    {
    int     width_ii = 40;
    item_t  items_ari [4];
    int     nr_items_ii = sizeof (items_ari) / sizeof (items_ari [0]);
    int     choice_ii;
    int     i_ii;


    util_create_items (items_ari, nr_items_ii, width_ii);

    strcpy (items_ari [0].text, txt_get (TXT_START_INSTALL));
    strcpy (items_ari [1].text, txt_get (TXT_BOOT_SYSTEM));
    strcpy (items_ari [2].text, txt_get (TXT_START_RESCUE));
    strcpy (items_ari [3].text, txt_get (TXT_START_DEMO));
    for (i_ii = 0; i_ii < nr_items_ii; i_ii++)
        {
        util_center_text (items_ari [i_ii].text, width_ii);
        items_ari [i_ii].func = inst_menu_cb;
        }

    choice_ii = dia_menu (txt_get (TXT_MENU_START), items_ari,
                          nr_items_ii, 1);

    util_free_items (items_ari, nr_items_ii);

    if (choice_ii)
        return (0);
    else
        return (1);
    }


static int inst_menu_cb (int what_iv)
    {
    int  error_ii = FALSE;

    switch (what_iv)
        {
        case 1:
            error_ii = inst_start_install ();
            break;
        case 2:
            error_ii = root_boot_system ();
            break;
        case 3:
            error_ii = inst_start_rescue ();
            break;
        case 4:
            error_ii = inst_start_demo ();
            break;
        default:
            dia_message (txt_get (TXT_NOT_IMPLEMENTED), MSGTYPE_ERROR);
            error_ii = -1;
            break;
        }

    if (error_ii)
        return (what_iv);
    else
        return (0);
    }


static int inst_choose_source_cb (int what_iv)
    {
           int  error_ii = FALSE;
    static int  told_is = FALSE;
           char tmp_ti [200];


    switch (what_iv)
        {
        case 1:
            if (!told_is && !util_cd1_boot ())
                {
                sprintf (tmp_ti, txt_get (TXT_INSERT_CD), 1);
                (void) dia_message (tmp_ti, MSGTYPE_INFO);
                told_is = TRUE;
                }

            error_ii = inst_mount_cdrom ();
            break;
        case 2:
            error_ii = inst_mount_nfs ();
            break;
        case 3:
            error_ii = inst_ftp ();
            break;
        case 4:
            error_ii = inst_mount_harddisk ();
            break;
        case 5:
            error_ii = inst_check_floppy ();
            break;
        default:
            break;
        }

    if (!error_ii && what_iv != 1)
        {
        error_ii = inst_check_instsys ();
        if (error_ii)
            dia_message (txt_get (TXT_RI_NOT_FOUND), MSGTYPE_ERROR);
        }

    if (error_ii)
        {
        inst_umount ();
        return (what_iv);
        }
    else
        return (0);
    }


static int inst_choose_source (void)
    {
    int     width_ii = 20;
    item_t  items_ari [5];
    int     choice_ii;
    int     i_ii;
    int     nr_items_ii = sizeof (items_ari) / sizeof (items_ari [0]);


    inst_umount ();

    util_create_items (items_ari, nr_items_ii, width_ii);
    strncpy (items_ari [0].text, txt_get (TXT_CDROM), width_ii);
    strncpy (items_ari [1].text, txt_get (TXT_NFS), width_ii);
    strncpy (items_ari [2].text, txt_get (TXT_FTP), width_ii);
    strncpy (items_ari [3].text, txt_get (TXT_HARDDISK), width_ii);
    strncpy (items_ari [4].text, txt_get (TXT_FLOPPY), width_ii);
    for (i_ii = 0; i_ii < nr_items_ii; i_ii++)
        {
        util_center_text (items_ari [i_ii].text, width_ii);
        items_ari [i_ii].func = inst_choose_source_cb;
        }
    
    choice_ii = dia_menu (txt_get (TXT_CHOOSE_SOURCE), items_ari,
                          inst_rescue_im ? nr_items_ii : nr_items_ii - 1, 1);
        
    util_free_items (items_ari, nr_items_ii);

    if (choice_ii)
        return (0);
    else
        return (-1);
    }


static int inst_try_cdrom (char *device_tv)
    {
    char  path_device_ti [20];
    int   rc_ii;


    sprintf (path_device_ti, "/dev/%s", device_tv);
    rc_ii = mount (path_device_ti, mountpoint_tg, "iso9660", MS_MGC_VAL | MS_RDONLY, 0);
    return (rc_ii);
    }


static int inst_mount_cdrom (void)
    {
    static char  *device_tab_ats [] =
                          {
                          "hdb",   "hdc",    "hdd",     "scd0",   "scd1",
                          "scd2",  "scd3",   "scd4",    "scd5",   "scd6",
                          "scd7",  "scd8",   "scd9",    "scd10",  "scd11",
                          "scd12", "scd13",  "scd14",   "scd15",
                          "hda",   "hde",    "hdf",     "hdg",    "hdh",
                          "aztcd", "cdu535", "cm206cd", "gscd",   "sjcd",
                          "mcd",   "mcdx0",  "mcdx1",   "optcd",  "sonycd",
                          "sbpcd", "sbpcd1", "sbpcd2",  "sbpcd3", "pcd0",
                          "pcd1",  "pcd2",   "pcd3",
                          0
                          };
    int           rc_ii;
    int           i_ii = 0;
    char         *device_pci;
    window_t      win_ri;
    int           mount_success_ii = FALSE;


    bootmode_ig = BOOTMODE_CD;
    dia_info (&win_ri, txt_get (TXT_TRY_CD_MOUNT));

    if (cdrom_tg [0])
        device_pci = cdrom_tg;
    else
        device_pci = device_tab_ats [i_ii++];

    rc_ii = inst_try_cdrom (device_pci);
    if (!rc_ii)
        {
        mount_success_ii = TRUE;
        rc_ii = inst_check_instsys ();
        if (rc_ii)
            inst_umount ();
        }

    while (rc_ii < 0 && device_tab_ats [i_ii])
        {
        device_pci = device_tab_ats [i_ii++];
        rc_ii = inst_try_cdrom (device_pci);
        if (!rc_ii)
            {
            mount_success_ii = TRUE;
            rc_ii = inst_check_instsys ();
            if (rc_ii)
                inst_umount ();
            }
        }

    win_close (&win_ri);

    if (rc_ii < 0)
        dia_message (txt_get (mount_success_ii ? TXT_RI_NOT_FOUND :
                                                 TXT_ERROR_CD_MOUNT),
                     MSGTYPE_ERROR);
    else
        strcpy (cdrom_tg, device_pci);

    return (rc_ii);
    }


static int inst_mount_nfs (void)
    {
    int          rc_ii;
    char         server_ti [20];
    window_t     win_ri;
    char         text_ti [200 + MAX_FILENAME];


    bootmode_ig = BOOTMODE_NET;
    rc_ii = net_config ();
    if (rc_ii)
        return (rc_ii);

    if (!auto_ig)
        {
        if (!nfs_server_rg.s_addr)
            nfs_server_rg.s_addr = ipaddr_rg.s_addr & netmask_rg.s_addr;

        rc_ii = inst_get_nfsserver ();
        if (rc_ii)
            return (rc_ii);

        rc_ii = dia_input (txt_get (TXT_INPUT_DIR), server_dir_tg,
                           sizeof (server_dir_tg) - 1, 30);
        if (rc_ii)
            return (rc_ii);
        }
        
    util_truncate_dir (server_dir_tg);
    strcpy (server_ti, inet_ntoa (nfs_server_rg));
    sprintf (text_ti, txt_get (TXT_TRY_NFS_MOUNT), server_ti, server_dir_tg);
    dia_info (&win_ri, text_ti);
    system ("portmap");
    rc_ii = net_mount_nfs (server_ti, server_dir_tg);
    win_close (&win_ri);

    return (rc_ii);
    }


static int inst_mount_harddisk (void)
    {
            int   rc_ii;
    static  char *fs_types_ati [] = { "msdos", "hpfs", "ext2", 0 };
            int   i_ii;
            char *mountpoint_pci;


    bootmode_ig = BOOTMODE_HARDDISK;
    do
        {
        rc_ii = dia_input (txt_get (TXT_ENTER_PARTITION), harddisk_tg, 11, 11);
        if (rc_ii)
            return (rc_ii);

        if (!inst_rescue_im && !force_ri_ig)
            {
            mkdir (inst_tmpmount_tm, 0777);
            inst_loopmount_im = TRUE;
            mountpoint_pci = inst_tmpmount_tm;
            }
        else
            {
            inst_loopmount_im = FALSE;
            mountpoint_pci = mountpoint_tg;
            }

        i_ii = 0;
        do
            rc_ii = mount (harddisk_tg, mountpoint_pci, fs_types_ati [i_ii++],
                           MS_MGC_VAL | MS_RDONLY, 0);
        while (rc_ii && fs_types_ati [i_ii]);

        if (rc_ii)
            dia_message (txt_get (TXT_ERROR_HD_MOUNT), MSGTYPE_ERROR);
        else
            {
            fstype_tg = fs_types_ati [i_ii - 1];
            rc_ii = dia_input (txt_get (TXT_ENTER_HD_DIR), server_dir_tg,
                               sizeof (server_dir_tg) - 1, 30);
            if (rc_ii)
                {
                inst_umount ();
                return (rc_ii);
                }

            util_truncate_dir (server_dir_tg);
            }
        }
    while (rc_ii);

    return (0);
    }


int inst_check_instsys (void)
    {
    char  filename_ti [MAX_FILENAME];
    char *instsys_loop_ti = "/suse/setup/inst-img";


    if (memory_ig > 8000000)
        strcpy (inst_rescuefile_tm, "/suse/images/rescue");
    else
        strcpy (inst_rescuefile_tm, "/disks/rescue");

    switch (bootmode_ig)
        {
        case BOOTMODE_FLOPPY:
            ramdisk_ig = TRUE;
            strcpy (inst_rootimage_tm, "/dev/fd0");
            break;
        case BOOTMODE_HARDDISK:
            if (inst_loopmount_im)
                {
                ramdisk_ig = FALSE;
                sprintf (filename_ti, "%s%s%s", inst_tmpmount_tm,
                         server_dir_tg, instsys_loop_ti);
                if (util_mount_loop (filename_ti, mountpoint_tg))
                    return (-1);
                }
            else
                {
                ramdisk_ig = TRUE;
                sprintf (inst_rootimage_tm, "%s%s%s", mountpoint_tg, server_dir_tg,
                         inst_rescue_im == TRUE ? inst_rescuefile_tm : rootimage_tg);
                }
            break;
        case BOOTMODE_CD:
        case BOOTMODE_NET:
            ramdisk_ig = FALSE;
            sprintf (filename_ti, "%s%s", mountpoint_tg, installdir_tg);
            if (inst_rescue_im || force_ri_ig || !util_check_exist (filename_ti))
                ramdisk_ig = TRUE;
            sprintf (inst_rootimage_tm, "%s%s", mountpoint_tg,
                     inst_rescue_im == TRUE ? inst_rescuefile_tm : rootimage_tg);
            if (!util_check_exist (inst_rootimage_tm))
                {
                if (util_check_exist (filename_ti))
                    ramdisk_ig = FALSE;
                else
                    sprintf (inst_rootimage_tm, "%s/%s", mountpoint_tg, inst_demo_sys_tm);
                }
            break;
        case BOOTMODE_FTP:
            ramdisk_ig = TRUE;
            sprintf (inst_rootimage_tm, "%s%s", server_dir_tg,
                     inst_rescue_im == TRUE ? inst_rescuefile_tm : rootimage_tg);
            break;
        default:
            break;
        }

    if (bootmode_ig != BOOTMODE_FTP && ramdisk_ig &&
        !util_check_exist (inst_rootimage_tm))
        return (-1);
    else
        return (0);
    }


static int inst_start_install (void)
    {
    int       rc_ii;

    inst_rescue_im = FALSE;
    rc_ii = inst_choose_source ();
    if (rc_ii)
        return (rc_ii);

    if (ramdisk_ig)
        {
        rc_ii = root_load_rootimage (inst_rootimage_tm);
        fprintf (stderr, "Loading of rootimage returns %d\n", rc_ii);
        inst_umount ();
        if (rc_ii)
            return (rc_ii);

        mkdir (inst_mountpoint_tg, 0777);
        rc_ii = util_try_mount (RAMDISK_2, inst_mountpoint_tg,
                                MS_MGC_VAL | MS_RDONLY, 0);
        fprintf (stderr, "Mounting of inst-sys returns %d\n", rc_ii);
        if (rc_ii)
            return (rc_ii);
        }

    rc_ii = inst_execute_yast ();

    return (rc_ii);
    }


static int inst_start_rescue (void)
    {
    int   rc_ii;


    inst_rescue_im = TRUE;
    rc_ii = inst_choose_source ();
    if (rc_ii)
        return (rc_ii);

    rc_ii = root_load_rootimage (inst_rootimage_tm);
    inst_umount ();
    return (rc_ii);
    }


static void inst_start_shell (char *tty_tv)
    {
    char  *args_apci [] = { "bash", 0 };
    char  *env_pci [] =   { "TERM=linux",
                            "PS1=`pwd -P` # ",
                            "HOME=/",
                            "PATH=/lbin:/bin:/sbin:/usr/bin:/usr/sbin:/lib/YaST2/bin", 0 };
    int    fd_ii;


    if (!fork ())
        {
        fclose (stdin);
        fclose (stdout);
        fclose (stderr);
        setsid ();
        fd_ii = open (tty_tv, O_RDWR);
        ioctl (fd_ii, TIOCSCTTY, (void *)1);
        dup (fd_ii);
        dup (fd_ii);

        execve ("/bin/bash", args_apci, env_pci);
        fprintf (stderr, "Couldn't start shell (errno = %d)\n", errno);
        exit (-1);
        }
    }


static int inst_prepare (void)
    {
    char  *links_ati [] = {
                          "/etc/termcap",
                          "/etc/services",
                          "/etc/protocols",
                          "/etc/nsswitch.conf",
                          "/etc/passwd",
                          "/etc/group",
                          "/etc/shadow",
                          "/etc/gshadow",
                          "/etc/rpmrc",
                          "/etc/inputrc",
                          "/etc/ld.so.conf",
                          "/etc/ld.so.cache",
                          "/etc/host.conf",
                          "/etc/modules.conf",
                          "/bin",
                          "/boot",
                          "/root",
                          "/lib",
                          "/sbin",
                          "/usr"
                          };
    char   link_source_ti [MAX_FILENAME];
    int    i_ii;
    int    rc_ii = 0;

    mod_free_modules ();
    file_write_yast_info ();
    rename ("/bin", "/.bin");

    for (i_ii = 0; i_ii < sizeof (links_ati) / sizeof (links_ati [0]); i_ii++)
        {
        if (inst_loopmount_im)
            sprintf (link_source_ti, "%s%s", mountpoint_tg, links_ati [i_ii]);
        else
            {
            if (ramdisk_ig)
                sprintf (link_source_ti, "%s%s", inst_mountpoint_tg,
                         links_ati [i_ii]);
            else
                sprintf (link_source_ti, "%s%s%s", mountpoint_tg, installdir_tg,
                         links_ati [i_ii]);
            }
        unlink (links_ati [i_ii]);
        symlink (link_source_ti, links_ati [i_ii]);
        }

    setenv ("PATH", "/lbin:/bin:/sbin:/usr/bin:/usr/sbin:/lib/YaST2/bin", TRUE);
    if (serial_ig)
        {
        setenv ("TERM", "vt100", TRUE);
        setenv ("ESCDELAY", "1100", TRUE);
        }
    else
        {
        setenv ("TERM", "linux", TRUE);
        setenv ("ESCDELAY", "10", TRUE);
        }

    setenv ("YAST_DEBUG", "/debug/yast.debug", TRUE);

    if (!ramdisk_ig)
        rc_ii = inst_init_cache ();

    return (rc_ii);
    }


static int inst_execute_yast (void)
    {
    int       rc_ii;
    int       i_ii = 0;
    window_t  status_ri;
    char      command_ti [50];

    rc_ii = inst_prepare ();
    if (rc_ii)
        return (rc_ii);

    if (inst_choose_yast_version ())
        return (-1);

    if (!auto2_ig)
        dia_status_on (&status_ri, txt_get (TXT_START_YAST));
    system ("update");

    if (!auto2_ig)
        while (i_ii < 50)
            {
            dia_status (&status_ri, i_ii++);
            usleep (10000);
            }

    inst_start_shell ("/dev/tty2");
    if (memory_ig < MEM_LIMIT_SWAP_MSG)
        {
        if (!auto2_ig)
            dia_message (txt_get (TXT_LITTLE_MEM), MSGTYPE_ERROR);
        }
    else
        {
        inst_start_shell ("/dev/tty5");
        inst_start_shell ("/dev/tty6");
        }

    if (!auto2_ig)
        while (i_ii <= 100)
            {
            dia_status (&status_ri, i_ii++);
            usleep (10000);
            }

    if (!auto2_ig)
        win_close (&status_ri);
    disp_set_color (COL_WHITE, COL_BLACK);
    if (auto2_ig)
        disp_clear_screen ();
    fflush (stdout);

    lxrc_set_modprobe ("/sbin/modprobe");

    if (yast_version_ig == 2)
        sprintf (command_ti, "%s %s", YAST2_COMMAND,
                 auto_ig ? "--autofloppy" : "");
    else
        sprintf (command_ti, "%s%s", YAST1_COMMAND,
                 auto_ig ? " --autofloppy" : "");

    fprintf (stderr, "starting \"%s\"\n", command_ti);
    rc_ii = system (command_ti);
    fprintf (stderr, "%s return code is %d (errno = %d)\n",
             command_ti, rc_ii, rc_ii ? errno : 0);

#ifdef LXRC_DEBUG
    if((guru_ig & 1)) { printf("a shell for you...\n"); system("/bin/sh"); }
#endif

    lxrc_set_modprobe ("/etc/nothing");
    do_disp_init_ig = TRUE;

    sync ();
    if (!auto2_ig)
        disp_restore_screen ();
    disp_cursor_off ();
    kbd_reset ();

    yast_version_ig = 0;
    if (rc_ii)
        {
        if (auto2_ig)
            {
            auto2_ig = 0;
            util_disp_init();
            }
        
        dia_message (txt_get (TXT_ERROR_INSTALL), MSGTYPE_ERROR);
        }

    lxrc_killall (0);

    waitpid (-1, NULL, WNOHANG);

    (void) system ("rm -f /tmp/stp* > /dev/null 2>&1");
    (void) system ("rm -f /var/lib/YaST/* > /dev/null 2>&1");
    (void) system ("umount -a -tnoproc,nousbdevfs,nominix > /dev/null 2>&1");
    /* if the initrd has an ext2 fs, we've just made / read-only */
    mount(0, "/", 0, MS_MGC_VAL | MS_REMOUNT, 0);

    inst_umount ();
    if (ramdisk_ig)
        util_free_ramdisk ("/dev/ram2");

    if (!rc_ii)
        rc_ii = inst_commit_install ();

    unlink ("/bin");
    rename ("/.bin", "/bin");

    if(rc_ii) auto2_ig = FALSE;

    return (rc_ii);
    }


static int inst_check_floppy (void)
    {
    int  fd_ii;


    bootmode_ig = BOOTMODE_FLOPPY;
    fd_ii = dia_message (txt_get (TXT_INSERT_DISK), MSGTYPE_INFO);
    if (fd_ii)
        return (fd_ii);

    fd_ii = open ("/dev/fd0", O_RDONLY);
    if (fd_ii < 0)
        dia_message (txt_get (TXT_ERROR_READ_DISK), MSGTYPE_ERROR);
    else
        close (fd_ii);

    if (fd_ii < 0)
        return (fd_ii);
    else
        return (0);
    }


static int inst_commit_install (void)
    {
    FILE     *fd_pri;
    char      option_ti [30];
    char      value_ti [30];
    char      swap_ti [30];
    char      root_ti [30];
    int       live_cd_ii = 0;
    int       rc_ii = 0;
    window_t  win_ri;
    char      keymap[30], lang[30];
    char      command_ti [MAX_FILENAME];

    fd_pri = fopen (YAST_INFO_FILE, "r");
    if (!fd_pri)
        return (-1);

    *swap_ti = 0;
    *root_ti = 0;

    *keymap = 0;
    *lang = 0;

    while (fscanf (fd_pri, "%s %s", option_ti, value_ti) == 2)
        {
        fprintf (stderr, "%s %s\n", option_ti, value_ti);

        if (!strncasecmp (option_ti, "Swap", 4))
            strcpy (swap_ti, value_ti);

        if (!strncasecmp (option_ti, "Root", 4))
            strcpy (root_ti, value_ti);

        if (!strncasecmp (option_ti, "Live", 4))
            live_cd_ii = atoi (value_ti);

        if (!strncasecmp (option_ti, "Keytable", 8))
            strncpy (keymap, value_ti, sizeof keymap);

        if (!strncasecmp (option_ti, "Language", 8))
            strncpy (lang, value_ti, sizeof lang);
        }

    keymap[sizeof keymap - 1] = lang[sizeof lang - 1] = 0;

    fclose (fd_pri);

    if(*lang) {
      language_ig = set_langidbyname(lang);
      do_disp_init_ig = TRUE;
    }

    if(*keymap) {
      strcpy(keymap_tg, keymap);
      sprintf(command_ti, "loadkeys %s.map", keymap_tg);
      system(command_ti);
      do_disp_init_ig = TRUE;
    }

    if (*swap_ti)
        swapoff (swap_ti);

    if (*root_ti)
        {
        if ((!auto_ig && pcmcia_chip_ig)        ||
            !strncasecmp (root_ti, "reboot", 6) ||
            reboot_ig)
            {
            if(!auto_ig) {
              disp_clear_screen();
              util_disp_init();
              dia_message(txt_get(TXT_DO_REBOOT), MSGTYPE_INFO);
            }
            reboot (RB_AUTOBOOT);
            rc_ii = -1;
            }
        else
            {
            root_set_root (root_ti);
            if (live_cd_ii)
                {
                util_disp_init();
                (void) dia_message (txt_get (TXT_INSERT_LIVECD), MSGTYPE_INFO);
                }
            else
                {
                if (auto_ig)
                    {
                    util_disp_init();
                    dia_info (&win_ri, txt_get (TXT_INSTALL_SUCCESS));
                    sleep (2);
                    win_close (&win_ri);
                    }
                else
                    if(!auto2_ig)
                        {
                        util_disp_init();
                        dia_message (txt_get (TXT_INSTALL_SUCCESS), MSGTYPE_INFO);
                        }
                }
            }
        }
    else
        rc_ii = -1;

    return (rc_ii);
    }


static int inst_init_cache (void)
    {
    char     *files_ati [] = {
                             "/lib/libc.so.6",
                             "/lib/libc.so.5",
                             "/lib/ld.so",
                             "/bin/bash",
                             "/sbin/YaST"
                             };
    long      size_li;
    long      allsize_li;
    int       dummy_ii;
    char      buffer_ti [10240];
    window_t  status_ri;
    int       i_ii;
    int       fd_ii;
    int       percent_ii;
    int       old_percent_ii;
    int       read_ii;


    if (memory_ig < MEM_LIMIT_CACHE_LIBS)
        return (0);

    dia_status_on (&status_ri, txt_get (TXT_PREPARE_INST));
    allsize_li = 0;
    for (i_ii = 0; i_ii < sizeof (files_ati) / sizeof (files_ati [0]); i_ii++)
        if (!util_fileinfo (files_ati [i_ii], &size_li, &dummy_ii))
            allsize_li += size_li;

    if (allsize_li)
        {
        size_li = 0;
        old_percent_ii = 0;

        for (i_ii = 0; i_ii < sizeof (files_ati) / sizeof (files_ati [0]); i_ii++)
            {
            fd_ii = open (files_ati [i_ii], O_RDONLY);
            while ((read_ii = read (fd_ii, buffer_ti, sizeof (buffer_ti))) > 0)
                {
                size_li += (long) read_ii;
                percent_ii = (int) ((size_li * 100) / allsize_li);
                if (percent_ii != old_percent_ii)
                    {
                    dia_status (&status_ri, percent_ii);
                    old_percent_ii = percent_ii;
                    }
                }
            close (fd_ii);
            }
        }
    win_close (&status_ri);
    return (0);
    }


static void inst_umount (void)
    {
    if (inst_loopmount_im)
        {
        util_umount_loop (mountpoint_tg);
        umount (inst_tmpmount_tm);
        rmdir (inst_tmpmount_tm);
        inst_loopmount_im = FALSE;
        }
    else
        umount (mountpoint_tg);

    umount (inst_mountpoint_tg);
    }


static int inst_get_nfsserver (void)
    {
    char            input_ti [100];
    int             rc_ii;


    if (nfs_server_rg.s_addr)
        strcpy (input_ti, inet_ntoa (nfs_server_rg));
    else
        input_ti [0] = 0;

    do
        {
        rc_ii = dia_input (txt_get (TXT_INPUT_SERVER), input_ti,
                           sizeof (input_ti) - 1, 20);
        if (rc_ii)
            return (rc_ii);

        rc_ii = net_check_address (input_ti, &nfs_server_rg);
        if (rc_ii)
            (void) dia_message (txt_get (TXT_INVALID_INPUT), MSGTYPE_ERROR);
        }
    while (rc_ii);

    return (0);
    }


static int inst_get_ftpserver (void)
    {
    char            input_ti [100];
    int             rc_ii;


    if (ftp_server_rg.s_addr)
        strcpy (input_ti, inet_ntoa (ftp_server_rg));
    else if (inst_rescue_im)
        strcpy (input_ti, "209.81.41.5");
    else
        input_ti [0] = 0;

    do
        {
        rc_ii = dia_input (txt_get (TXT_INPUT_FTPSERVER), input_ti,
                           sizeof (input_ti) - 1, 20);
        if (rc_ii)
            return (rc_ii);

        rc_ii = net_check_address (input_ti, &ftp_server_rg);
        if (rc_ii)
            (void) dia_message (txt_get (TXT_INVALID_INPUT), MSGTYPE_ERROR);
        }
    while (rc_ii);

    return (0);
    }


static int inst_ftp (void)
    {
    int       rc_ii;
    window_t  win_ri;
    char msg[256];

    /* currently YaST1 only */
    yast_version_ig = 1;
    
    if (!inst_rescue_im && memory_ig <= (yast_version_ig == 1 ? MEM_LIMIT1_RAMDISK : MEM_LIMIT2_RAMDISK))
        {
        sprintf(msg, txt_get (TXT_NOMEM_FTP), (MEM_LIMIT1_RAMDISK >> 20) + 2);
        (void) dia_message (msg, MSGTYPE_ERROR);
        return (-1);
        }

    rc_ii = net_config ();
    if (rc_ii)
        return (rc_ii);

    do
        {
        rc_ii = inst_get_ftpserver ();
        if (rc_ii)
            return (rc_ii);

        rc_ii = inst_get_ftpsetup ();
        if (rc_ii)
            return (rc_ii);

        dia_info (&win_ri, txt_get (TXT_TRY_REACH_FTP));
        rc_ii = util_open_ftp (inet_ntoa (ftp_server_rg));
        win_close (&win_ri);

        if (rc_ii < 0)
            util_print_ftp_error (rc_ii);
        else
            {
            ftpClose (rc_ii);
            rc_ii = 0;
            }
        }
    while (rc_ii);

    if (inst_rescue_im)
        strcpy (server_dir_tg, "/pub/SuSE-Linux/current");

    if (dia_input (txt_get (TXT_INPUT_DIR), server_dir_tg,
                            sizeof (server_dir_tg) - 1, 30))
        return (-1);

    util_truncate_dir (server_dir_tg);

    bootmode_ig = BOOTMODE_FTP;
    return (0);
    }


static int inst_get_ftpsetup (void)
    {
    int             rc_ii;
    char            tmp_ti [MAX_FILENAME];
    struct in_addr  dummy_ri;
    int             i_ii;


    rc_ii = dia_yesno (txt_get (TXT_ANONYM_FTP), NO);
    if (rc_ii == ESCAPE)
        return (-1);

    if (rc_ii == NO)
        {
        strcpy (ftp_user_tg, "anonymous");
        strcpy (ftp_password_tg, "root@");
        }
    else
        {
        strcpy (tmp_ti, ftp_user_tg);
        rc_ii = dia_input (txt_get (TXT_ENTER_FTPUSER), tmp_ti,
                           sizeof (ftp_user_tg) - 1, 20);
        if (rc_ii)
            return (rc_ii);
        strcpy (ftp_user_tg, tmp_ti);

        strcpy (tmp_ti, ftp_password_tg);
        passwd_mode_ig = TRUE;
        rc_ii = dia_input (txt_get (TXT_ENTER_FTPPASSWD), tmp_ti,
                           sizeof (ftp_password_tg) - 1, 20);
        passwd_mode_ig = FALSE;
        if (rc_ii)
            return (rc_ii);
        strcpy (ftp_password_tg, tmp_ti);
        }


    rc_ii = dia_yesno (txt_get (TXT_WANT_FTPPROXY), NO);
    if (rc_ii == ESCAPE)
        return (-1);

    if (rc_ii == YES)
        {
        strcpy (tmp_ti, ftp_proxy_tg);
        do
            {
            rc_ii = dia_input (txt_get (TXT_ENTER_FTPPROXY), tmp_ti,
                               sizeof (ftp_proxy_tg) - 1, 30);
            if (rc_ii)
                return (rc_ii);

            if (isdigit (tmp_ti [0]))
                {
                rc_ii = net_check_address (tmp_ti, &dummy_ri);
                if (rc_ii)
                    (void) dia_message (txt_get (TXT_INVALID_INPUT),
                                        MSGTYPE_ERROR);
                }
            }
        while (rc_ii);
        strcpy (ftp_proxy_tg, tmp_ti);

        if (ftp_proxyport_ig == -1)
            tmp_ti [0] = 0;
        else
            sprintf (tmp_ti, "%d", ftp_proxyport_ig);

        do
            {
            rc_ii = dia_input (txt_get (TXT_ENTER_FTPPORT), tmp_ti,
                               6, 6);
            if (rc_ii)
                return (rc_ii);

            for (i_ii = 0; i_ii < strlen (tmp_ti); i_ii++)
                if (!isdigit (tmp_ti [i_ii]))
                    rc_ii = -1;

            if (rc_ii)
                (void) dia_message (txt_get (TXT_INVALID_INPUT), MSGTYPE_ERROR);
            }
        while (rc_ii);

        ftp_proxyport_ig = atoi (tmp_ti);
        }
    else
        {
        ftp_proxy_tg [0] = 0;
        ftp_proxyport_ig = -1;
        }

    return (0);
    }


static int inst_choose_yast_version (void)
    {
    item_t   items_ari [2];
    int      width_ii = 30;
    int      yast1_ii, yast2_ii;

    yast1_ii = util_check_exist (YAST1_COMMAND);
    yast2_ii = util_check_exist (YAST2_COMMAND);

    if (!yast_version_ig && auto_ig)
        yast_version_ig = 1;

    if (yast_version_ig == 1 && yast1_ii)
        return (0);

    if (yast_version_ig == 2 && yast2_ii)
        return (0);

    if (yast1_ii && !yast2_ii)
        {
        yast_version_ig = 1;
        return (0);
        }

    if (!yast1_ii && yast2_ii)
        {
        yast_version_ig = 2;
        return (0);
        }

    if (auto2_ig)
        {
        auto2_ig = 0;
        util_disp_init();
        }

    util_create_items (items_ari, 2, width_ii);
    strcpy (items_ari [0].text, txt_get (TXT_YAST1));
    strcpy (items_ari [1].text, txt_get (TXT_YAST2));
    util_center_text (items_ari [0].text, width_ii);
    util_center_text (items_ari [1].text, width_ii);
    yast_version_ig = dia_menu (txt_get (TXT_CHOOSE_YAST),
                                items_ari, 2, 2);
    util_free_items (items_ari, 2);

    if (!yast_version_ig)
        return (-1);
    else
        return (0);
    }

#ifdef USE_LIBHD

int inst_auto2_install()
{
  int i;

  deb_msg("going for automatic install");

  if(ramdisk_ig) {
    deb_msg("using RAM disk");   
   
    i = root_load_rootimage(inst_rootimage_tm);
    fprintf(stderr, "Loading of rootimage returns %d\n", i);
//    umount(mountpoint_tg);
//    umount(inst_mountpoint_tg);
    inst_umount();

    if(i || inst_rescue_im) return i;

    mkdir(inst_mountpoint_tg, 0777);
    i = util_try_mount(RAMDISK_2, inst_mountpoint_tg, MS_MGC_VAL | MS_RDONLY, 0);
    fprintf(stderr, "Mounting of %s returns %d\n", inst_mountpoint_tg, i);
    if(i) return i;
  }

  return inst_execute_yast();
}

#endif	/* USE_LIBHD */
