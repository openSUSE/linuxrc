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
#include <hd.h>

#include "global.h"
#include "text.h"
#include "dialog.h"
#include "util.h"
#include "module.h"
#include "display.h"
#include "info.h"
#include "auto2.h"

static int info_show_cb(dia_item_t di);

static dia_item_t di_info_menu_last = di_none;
static char *pr_dev_num(hd_dev_num_t *d);

void info_menu()
{
  dia_item_t items[] = {
    di_info_kernel,
    di_info_drives,
    di_info_modules,
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
      break;
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

    if(!config.had_segv) fprintf (stderr, "CPU: %d, Memory: %"PRId64"\n",
             cpu_ig, memory_ig);

#ifndef __powerpc__
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
#endif
    }


void info_show_hardware()
{
  slist_t *sl0 = NULL;
  char buf[256];
  hd_data_t *hd_data;
  hd_t *hd, *hd0;
  hd_hw_item_t hw_items[] = { hw_cdrom, hw_disk, 0 };
  hd_res_t *res;

  hd_data = calloc(1, sizeof *hd_data);

  hd0 = hd_list2(hd_data, hw_items, 1);

  for(hd = hd0; hd; hd = hd->next) {
    if(!hd->unix_dev_name) continue;
    if(sl0) slist_append_str(&sl0, "");
    sprintf(buf, "%s (%s", hd->unix_dev_name, hd_is_hw_class(hd, hw_cdrom) ? "cdrom" : "disk");
    if(hd->unix_dev_num.type == 'b') {
      sprintf(buf + strlen(buf), ", dev %s", pr_dev_num(&hd->unix_dev_num));
    }
    sprintf(buf + strlen(buf), ")");
    slist_append_str(&sl0, buf);

    if(hd->model) {
      sprintf(buf, "  Model: \"%s\"", hd->model);
      slist_append_str(&sl0, buf);
    }

    if(hd->revision.name && *hd->revision.name) {
      sprintf(buf, "  Revision: \"%s\"", hd->revision.name);
      slist_append_str(&sl0, buf);
    }

    if(hd->serial && *hd->serial) {
      sprintf(buf, "  Serial: \"%s\"", hd->serial);
      slist_append_str(&sl0, buf);
    }

    if(hd->drivers) {
      sprintf(buf, "  Driver: \"%s\"", hd->drivers->str);
      slist_append_str(&sl0, buf);
    }

    for(res = hd->res; res; res = res->next) {
      switch(res->any.type) {
        case res_size:
          if(res->size.unit == size_unit_sectors && res->size.val1) {
            sprintf(buf, "  Size: %"PRIu64" sectors", res->size.val1);
            if(res->size.val1 >= (1 << 21)) {
              sprintf(buf + strlen(buf), " (%"PRIu64" GB)", ((res->size.val1 >> 20) + 1) >> 1);
            }
            else if(res->size.val1 >= (1 << 11)) {
              sprintf(buf + strlen(buf), " (%"PRIu64" MB)", ((res->size.val1 >> 10) + 1) >> 1);
            }
            slist_append_str(&sl0, buf);
          }
          break;

        case res_disk_geo:
          sprintf(buf,
            "  Geometry (%s): CHS %u/%u/%u",
            res->disk_geo.logical ? "Logical" : "Physical",
            res->disk_geo.cyls, res->disk_geo.heads, res->disk_geo.sectors
          );
          slist_append_str(&sl0, buf);
          break;

        default:
          break;
      }
    }

  }

  if(!sl0) slist_append_str(&sl0, txt_get(TXT_NO_DRIVES));

  dia_show_lines2(txt_get(TXT_DRIVES), sl0, 60);

  slist_free(sl0);

  hd_free_hd_data(hd_data);
  free(hd_data);
}


char *pr_dev_num(hd_dev_num_t *d)
{
  static char *buf = NULL;

  if(d->type) {
    if(d->range > 1) {
      strprintf(&buf, "%u:%u-%u:%u",
        d->major, d->minor,
        d->major, d->minor + d->range - 1
      );
    }
    else {
      strprintf(&buf, "%u:%u",
        d->major, d->minor
      );
    }
  }
  else {
    strprintf(&buf, "%s", "");
  }

  return buf;
}

