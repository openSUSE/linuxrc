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
static void info_show_hardware(void);
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
    uint64_t           memory_ig;
    int cpu_ig = 0;


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

    }


void info_show_hardware()
{
  slist_t *sl0 = NULL;
  char buf[256], *s;
  hd_data_t *hd_data;
  hd_t *hd, *hd0;
  hd_hw_item_t hw_items[] = { hw_cdrom, hw_disk, 0 };
  hd_res_t *res;
  static char *geo_type_str[] = { "Physical", "Logical", "BIOS EDD", "BIOS Legacy" };
  uint64_t size;

  hd_data = calloc(1, sizeof *hd_data);

  hd0 = fix_device_names(hd_list2(hd_data, hw_items, 1));

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
            size = (res->size.val1 * res->size.val2 + 512*1024) >> 20;
            if(size >= 1024) {
              sprintf(buf + strlen(buf), " (%"PRIu64" GB)", (size + 512) >> 10);
            }
            else if(size) {
              sprintf(buf + strlen(buf), " (%"PRIu64" MB)", size);
            }
            slist_append_str(&sl0, buf);
          }
          break;

        case res_disk_geo:
          s = res->disk_geo.geotype < sizeof geo_type_str / sizeof *geo_type_str ?
            geo_type_str[res->disk_geo.geotype] : "";
          sprintf(buf,
            "  Geometry (%s): CHS %u/%u/%u",
            s, res->disk_geo.cyls, res->disk_geo.heads, res->disk_geo.sectors
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

