/*
 *
 * info.c        System information
 *
 * Copyright (c) 1996-2002  Hubert Mantel, SuSE Linux AG  (mantel@suse.de)
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

static void info_hw_header(FILE *outfile_prv, char *text_tv);
static int info_show_cb(dia_item_t di);

static dia_item_t di_info_menu_last = di_none;

void info_menu()
{
  dia_item_t items[] = {
    di_info_kernel,
    di_info_drives,
    di_info_modules,
    di_info_pci,
    di_info_cpu,
    di_info_mem,
    di_info_ioports,
    di_info_interrupts,
    di_info_devices,
    di_info_netdev,
    di_info_dma,
    di_none
  };

  dia_menu2(txt_get(TXT_MENU_INFO), 26, info_show_cb, items, di_info_menu_last);
}


/*
 * return values:
 * -1    : abort (aka ESC)
 *  0    : ok
 *  other: stay in menu
 */
int info_show_cb(dia_item_t di)
{
  char buf[30] = "/proc/", *s;
  int i;

  di_info_menu_last = di;

  s = NULL;
  switch(di) {
    case di_info_kernel:
      dia_show_file(txt_get(TXT_INFO_KERNEL), kernellog_tg, FALSE);
      break;

    case di_info_drives:
      info_show_hardware();
      break;

    case di_info_modules:
      s = "modules";
      break;

    case di_info_pci:
      s = "pci";
      break;

    case di_info_cpu:
      s = "cpuinfo";
      break;

    case di_info_mem:
      s = "meminfo";
      break;

    case di_info_ioports:
      s = "ioports";
      break;

    case di_info_interrupts:
      s = "interrupts";
      break;

    case di_info_devices:
      s = "devices";
      break;

    case di_info_netdev:
      s = "net/dev";
      break;

    case di_info_dma:
      s = "dma";
      break;

    default:
  }

  if(s) {
    strcat(buf, s);
    i = dia_show_file(dia_get_text(di), buf, FALSE);
    if(i) dia_message(txt_get(TXT_NO_INFO_AVAIL), MSGTYPE_INFO);
  }

  return 1;
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
    uint64_t           memory_ig;


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

    fprintf (stderr, "CPU: %d, Memory: %"PRId64", %skernel\n",
             cpu_ig, memory_ig, old_kernel_ig ? "Old " : "New ");

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
                    config.floppies = 1;
                    config.floppy_dev[0] = strdup(devname_ti);
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

