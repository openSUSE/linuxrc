/*
 *
 * info.c        System information
 *
 * Copyright (c) 1996-2000  Hubert Mantel, SuSE GmbH  (mantel@suse.de)
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <ctype.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/utsname.h>
#include <linux/hdreg.h>

#include "global.h"
#include "text.h"
#include "dialog.h"
#include "util.h"
#include "module.h"
#include "display.h"
#include "info.h"
#include "auto2.h"

static char *info_hwfile_tm = "/tmp/hwinfo";
#ifdef USE_LIBHD
static char *info_hwfile2_tm = "/tmp/hw.log";
#endif
typedef struct {
               int  text;
               char *file;
               } ifile_t;
static ifile_t info_file_arm [] = { { TXT_INFO_MODULES, "modules"    },
                                    { TXT_INFO_PCI,     "pci"        },
                                    { TXT_INFO_CPU,     "cpuinfo"    },
                                    { TXT_INFO_MEM,     "meminfo"    },
                                    { TXT_INFO_IOPORTS, "ioports"    },
                                    { TXT_INFO_IRQS,    "interrupts" },
                                    { TXT_INFO_DEVICES, "devices"    },
                                    { TXT_INFO_NETDEV,  "net/dev"    },
                                    { TXT_INFO_DMA,     "dma"        } };

static void info_hw_header (FILE *outfile_prv, char *text_tv);
static int  info_show_cb   (int what_iv);


void info_menu (void)
    {
#ifdef USE_LIBHD
    item_t  items_ari [12];
#else
    item_t  items_ari [11];
#endif
    int     i_ii;
    int     width_ii = 26;
    int     nr_items_ii = 0;

#ifdef USE_LIBHD
    util_create_items (items_ari, 12, width_ii);
#else
    util_create_items (items_ari, 11, width_ii);
#endif

#ifdef USE_LIBHD
    strncpy (items_ari [nr_items_ii++].text, "Hardware Autoprobing", width_ii);
#endif
    strncpy (items_ari [nr_items_ii++].text, txt_get (TXT_INFO_KERNEL), width_ii);
    strncpy (items_ari [nr_items_ii++].text, txt_get (TXT_DRIVES), width_ii);
    for (i_ii = 0;
         i_ii < sizeof (info_file_arm) / sizeof (info_file_arm [0]);
         i_ii++)
        strncpy (items_ari [nr_items_ii++].text,
                 txt_get (info_file_arm [i_ii].text), width_ii);

    for (i_ii = 0; i_ii < nr_items_ii; i_ii++)
        {
        util_center_text (items_ari [i_ii].text, width_ii);
        items_ari [i_ii].func = info_show_cb;
        }

    (void) dia_menu (txt_get (TXT_MENU_INFO), items_ari,
                     nr_items_ii, 1);

#ifdef USE_LIBHD
    util_free_items (items_ari, 12);
#else
    util_free_items (items_ari, 11);
#endif
    }


void info_init (void)
    {
    FILE              *fd_pri;
    char               line_ti [100];
    char               dummy_ti [20];
    char              *tmp_pci;
    struct utsname     utsinfo_ri;
    struct hd_driveid  driveinfo_ri;
    char               devname_ti [30];
    int                fd_ii;
    int                i_ii;


    line_ti [sizeof (line_ti) - 1] = 0;
    fd_pri = fopen ("/proc/cpuinfo", "r");
    if (!fd_pri)
        return;

    while (fgets (line_ti, sizeof (line_ti) - 1, fd_pri))
        {
        if (!strncmp (line_ti, "cpu family", 10))
            {
            tmp_pci = strchr (line_ti, ':');
            if (tmp_pci)
                cpu_ig = (int) *(tmp_pci + 2) - (int) '0';
            }

        if (strstr (line_ti, "Alpha"))
            cpu_ig = 5;

        if (!strncmp (line_ti, "bogomips", 8))
            {
            tmp_pci = strchr (line_ti, ':');
            if (tmp_pci)
                bogomips_ig = atoi (tmp_pci + 2);
            }
        }

    fclose (fd_pri);

    fd_pri = fopen ("/proc/meminfo", "r");
    if (!fd_pri)
        return;

    if (!fgets (line_ti, sizeof (line_ti) - 1, fd_pri))
        {
        fclose (fd_pri);
        return;
        }
    if (!fgets (line_ti, sizeof (line_ti) - 1, fd_pri))
        {
        fclose (fd_pri);
        return;
        }

    sscanf (line_ti, "%s %"PRId64, dummy_ti, &memory_ig);

    fclose (fd_pri);

    uname (&utsinfo_ri);
    if (*(utsinfo_ri.release + 2) > '0')
        old_kernel_ig = FALSE;

    fprintf (stderr, "CPU: %d, BogoMips: %d, Memory: %"PRId64", %skernel\n",
             cpu_ig, bogomips_ig, memory_ig, old_kernel_ig ? "Old " : "New ");

    /* Check for LS-120 */
    /* ---------------- */

    for (i_ii = 0; i_ii < 8; i_ii++)
        {
        sprintf (devname_ti, "/dev/hd%c", i_ii + 'a');
        fd_ii = open (devname_ti, O_RDONLY);
        if (fd_ii >= 0)
            if (!ioctl (fd_ii, HDIO_GET_IDENTITY, &driveinfo_ri))
                {
                fprintf (stderr, "%s: %s\n", devname_ti, driveinfo_ri.model);
                if (strstr (driveinfo_ri.model, "LS-120"))
                    {
                    fprintf (stderr, "Found LS-120 as %s\n", devname_ti);
                    strcpy (floppy_tg, devname_ti);
                    i_ii = 8;
                    }
                }

        close (fd_ii);
        }
    }


void info_show_hardware (void)
    {
    struct hd_driveid  driveinfo_ri;
    int                rc_ii;
    int                fd_ii;
    FILE              *outfile_pri;
    FILE              *infile_pri;
    int                i_ii;
    char               devname_ti [30];
    char               inline1_ti [MAX_X];
    char               inline2_ti [MAX_X];
    char               inline3_ti [MAX_X];
    char               outline_ti [MAX_X];
    int                found_ii = FALSE;
    int                nothing_ii = TRUE;
    int                wrote_header_ii = FALSE;


    outfile_pri = fopen (info_hwfile_tm, "w");
    if (!outfile_pri)
        return;

    for (i_ii = 0; i_ii < 8; i_ii++)
        {
        sprintf (devname_ti, "/dev/hd%c", i_ii + 'a');
        fd_ii = open (devname_ti, O_RDONLY);
        if (fd_ii >= 0)
            {
            if (!found_ii)
                {
                info_hw_header (outfile_pri, "(E)IDE");
                found_ii = TRUE;
                }

            rc_ii = ioctl (fd_ii, HDIO_GET_IDENTITY, &driveinfo_ri);
            if (!rc_ii)
                {
                nothing_ii = FALSE;
                if (driveinfo_ri.cyls)
                    fprintf (outfile_pri, "%-11s: ", txt_get (TXT_HARDDISK));
                else
                    fprintf (outfile_pri, "%-11s: ", txt_get (TXT_CDROM));

                fprintf (outfile_pri, "%s\n", driveinfo_ri.model);
                fprintf (outfile_pri, "%22s %.8s  SerialNo: %.8s\n",
                         "Firmware:", driveinfo_ri.fw_rev, driveinfo_ri.serial_no);
                if (driveinfo_ri.cyls)
                    fprintf (outfile_pri, "%22s: %d/%d/%d\n", "Geometry",
                             driveinfo_ri.cyls, driveinfo_ri.heads, driveinfo_ri.sectors);
                }

            close (fd_ii);
            }
        }

    infile_pri = fopen ("/proc/scsi/scsi", "r");
    if (infile_pri)
        {
        if (found_ii)
            fprintf (outfile_pri, "\n");

        while (fgets (inline1_ti, sizeof (inline1_ti) - 1, infile_pri))
            if (!strncmp (inline1_ti, "Host", 4))
                {
                if (!wrote_header_ii)
                    {
                    info_hw_header (outfile_pri, "SCSI");
                    wrote_header_ii = TRUE;
                    }

                found_ii = FALSE;
                fgets (inline2_ti, sizeof (inline2_ti) - 1, infile_pri);
                fgets (inline3_ti, sizeof (inline3_ti) - 1, infile_pri);
                if (strstr (inline3_ti, "Direct"))
                    {
                    fprintf (outfile_pri, "%-11s: ", txt_get (TXT_HARDDISK));
                    found_ii = TRUE;
                    }
                else if (strstr (inline3_ti, "CD-ROM"))
                    {
                    fprintf (outfile_pri, "%-11s: ", txt_get (TXT_CDROM));
                    found_ii = TRUE;
                    }

                if (found_ii)
                    {
                    nothing_ii = FALSE;
                    memset (outline_ti, 0, sizeof (outline_ti));
                    memcpy (outline_ti, inline2_ti + 10, 8);
                    memcpy (outline_ti + 8, inline2_ti + 26, 26);
                    strcat (outline_ti, "\n");
                    fprintf (outfile_pri, outline_ti);
                    memset (outline_ti, 0, sizeof (outline_ti));
                    memset (outline_ti, ' ', 20);
                    memcpy (outline_ti + 13, inline1_ti, 39);
                    fprintf (outfile_pri, outline_ti);
                    }
                }

        fclose (infile_pri);
        }

    fclose (outfile_pri);

    if (nothing_ii)
        dia_message (txt_get (TXT_NO_DRIVES), MSGTYPE_INFO);
    else
        (void) dia_show_file (txt_get (TXT_DRIVES), info_hwfile_tm, FALSE);

    unlink (info_hwfile_tm);
    }


int info_eide_cd_exists (void)
    {
    struct hd_driveid  driveinfo_ri;
    int                i_ii;
    int                fd_ii;
    int                rc_ii;
    char               devname_ti [30];
    int                found_ii;


    i_ii = 0;
    found_ii = FALSE;

    while (i_ii < 8 && !found_ii)
        {
        sprintf (devname_ti, "/dev/hd%c", i_ii + 'a');
        fd_ii = open (devname_ti, O_RDONLY);
        if (fd_ii >= 0)
            {
            rc_ii = ioctl (fd_ii, HDIO_GET_IDENTITY, &driveinfo_ri);
            if (!rc_ii && !driveinfo_ri.cyls)
                found_ii = TRUE;

            close (fd_ii);
            }

        i_ii++;
        }

    return (found_ii);
    }


int info_scsi_exists (void)
    {
    FILE  *infile_pri;
    int    found_ii;
    char   line_ti [MAX_X];


    found_ii = FALSE;

    infile_pri = fopen ("/proc/scsi/scsi", "r");
    if (infile_pri)
        {
        while (fgets (line_ti, sizeof (line_ti) - 1, infile_pri) && !found_ii)
            if (strstr (line_ti, "CD-ROM") ||
                strstr (line_ti, "Direct") ||
                strstr (line_ti, "Sequential"))
                found_ii = TRUE;

        fclose (infile_pri);
        }

    return (found_ii);
    }


int info_scsi_cd_exists (void)
    {
    FILE  *infile_pri;
    int    found_ii;
    char   line_ti [MAX_X];


    found_ii = FALSE;

    infile_pri = fopen ("/proc/scsi/scsi", "r");
    if (infile_pri)
        {
        while (fgets (line_ti, sizeof (line_ti) - 1, infile_pri) && !found_ii)
            if (strstr (line_ti, "CD-ROM"))
                found_ii = TRUE;

        fclose (infile_pri);
        }

    return (found_ii);
    }

/*
 *
 * Local functions
 *
 */

static void info_hw_header (FILE *outfile_prv, char *text_tv)
    {
    int  i_ii;
    char outline_ti [MAX_X];
    int  width_ii = 51;


    memset (outline_ti, 0, sizeof (outline_ti));
    for (i_ii = 0; i_ii < width_ii; i_ii++)
        outline_ti [i_ii] = '*';
    outline_ti [width_ii] = '\n';
    fprintf (outfile_prv, outline_ti);
    strcpy (outline_ti, text_tv);
    util_center_text (outline_ti, width_ii);
    strcat (outline_ti, "\n");
    fprintf (outfile_prv, outline_ti);
    for (i_ii = 0; i_ii < width_ii; i_ii++)
        outline_ti [i_ii] = '*';
    strcat (outline_ti, "\n\n");
    fprintf (outfile_prv, outline_ti);
    }


static int info_show_cb (int what_iv)
    {
    char  tmp_ti [30];
    int   rc_ii;


#ifndef USE_LIBHD
    what_iv++;
#endif

    if (what_iv == 1)
        {
#ifdef USE_LIBHD
        auto2_scan_hardware(info_hwfile2_tm);
        dia_show_file("Hardware Autoprobing Results", info_hwfile2_tm, TRUE);
#endif
        }
    else if (what_iv == 2)
        (void) dia_show_file (txt_get (TXT_INFO_KERNEL), kernellog_tg, FALSE);
    else if (what_iv == 3)
        info_show_hardware ();
    else if (what_iv > 3 &&
             what_iv <= sizeof (info_file_arm) / sizeof (info_file_arm [0]) + 3)
        {
        sprintf (tmp_ti, "/proc/%s", info_file_arm [what_iv - 4].file);
        rc_ii = dia_show_file (txt_get (info_file_arm [what_iv - 4].text),
                               tmp_ti, FALSE);
        if (rc_ii)
            (void) dia_message (txt_get (TXT_NO_INFO_AVAIL), MSGTYPE_INFO);
        }

    return (what_iv);
    }
