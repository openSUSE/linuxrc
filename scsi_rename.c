#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sysfs/dlist.h>
#include <sysfs/libsysfs.h>

/*
 * Rename SCSI devices (disk & CD-ROM) writing add/remove-single-device to
 * /proc/scsi/scsi so that USB & Firewire devices are last.
 */

#include "global.h"
#include "scsi_rename.h"

#define MAX_SCSI_DEVS	0x110

scsi_dev_t **scsi_list = NULL;
scsi_dev_t **cdrom_list = NULL;
scsi_dev_t **disk_list = NULL;
unsigned scsi_list_len = 0;
unsigned cdrom_list_len = 0;
unsigned disk_list_len = 0;

static int cmp_func(const void *p0, const void *p1);
static void do_rename(scsi_dev_t **list, int len);
static void write_proc_scsi(scsi_dev_t *sd, int add);

int scsi_rename_main(int argc, char **argv)
{
  argc--; argv++;

  if(argc && !strcmp(argv[0], "-v")) {
    config.debug = 1;
    argc--; argv++;
  }

  if(argc) {
    fprintf(stderr, "usage: scsi_rename [-v]\n");
    return 1;
  }

  get_scsi_list();
  rename_scsi_devs();
  free_scsi_list();

  return 0;
}


void get_scsi_list()
{
  scsi_dev_t *scsi_dev;
  char *s;
  unsigned u;

  struct sysfs_class *sf_class;
  struct sysfs_class_device *sf_cdev;
  struct sysfs_device *sf_dev;
  struct dlist *sf_cdev_list;

  scsi_list = calloc(MAX_SCSI_DEVS + 1, sizeof *scsi_list);

  sf_class = sysfs_open_class("block");

  if(!sf_class) {
    if(config.debug) fprintf(stderr, "no block devices\n");
    return;
  }

  sf_cdev_list = sysfs_get_class_devices(sf_class);
  if(sf_cdev_list) dlist_for_each_data(sf_cdev_list, sf_cdev, struct sysfs_class_device) {
    sf_dev = sysfs_get_classdev_device(sf_cdev);

    if(sf_dev) {
      scsi_dev = NULL;

      if(
        (
          !strncmp(sf_cdev->name, "sr", 2) ||
          !strncmp(sf_cdev->name, "sd", 2)
        ) &&
        scsi_list_len < MAX_SCSI_DEVS
      ) {
        scsi_dev = scsi_list[scsi_list_len++] = calloc(1, sizeof *scsi_dev);

        scsi_dev->name = strdup(sf_cdev->name);
        scsi_dev->bus_id = strdup(sf_dev->bus_id);
        for(s = scsi_dev->bus_id; *s; s++) if(*s == ':') *s = ' ';
        scsi_dev->path = strdup(sf_dev->path);
        if(
          strstr(sf_dev->path, "/usb") ||
          strstr(sf_dev->path, "/fw-host")
        ) {
          scsi_dev->hotplug = 1;
        }
      }

    }
  }

  sysfs_close_class(sf_class);

  if(scsi_list_len) {
    qsort(scsi_list, scsi_list_len, sizeof *scsi_list, cmp_func);
  }

  for(u = 0; u < scsi_list_len; u++) {
    if(scsi_list[u]->name[1] == 'r') {
      cdrom_list = scsi_list + u;
      cdrom_list_len = scsi_list_len - u;
      break;
    }
  }

  if(scsi_list_len && scsi_list[0]->name[1] == 'd') {
    disk_list = scsi_list;
    disk_list_len = scsi_list_len - cdrom_list_len;
  }

  for(u = 0; u < scsi_list_len; u++) {
    scsi_dev = scsi_list[u];
    if(config.debug) fprintf(stderr,
      "%c %s >%s< %s\n",
      scsi_dev->hotplug ? '*' : ' ',
      scsi_dev->name,
      scsi_dev->bus_id,
      scsi_dev->path
    );
  }

#if 0
  if(config.debug) {
    if(disk_list && disk_list_len) {
      fprintf(stderr, "disks: %u at %u\n", disk_list_len, disk_list - scsi_list);
    }

    if(cdrom_list && cdrom_list_len) {
      fprintf(stderr, "  cds: %u at %u\n", cdrom_list_len, cdrom_list - scsi_list);
    }
  }
#endif

}


/* for qsort */
int cmp_func(const void *p0, const void *p1)
{
  scsi_dev_t **sd0, **sd1;

  sd0 = (scsi_dev_t **) p0;
  sd1 = (scsi_dev_t **) p1;

  return strcmp((*sd0)->name, (*sd1)->name);
}


void do_rename(scsi_dev_t **list, int len)
{
  int i;
  int hotplug_idx = -1, do_it = 0;
  scsi_dev_t *sd;

  if(!len || !list) return;

  for(i = 0; i < len; i++) {
    sd = list[i];
    if(sd->hotplug && hotplug_idx == -1) hotplug_idx = i;
    if(!sd->hotplug && hotplug_idx >= 0) {
      do_it = 1;
      break;
    }
  }

  if(!do_it) return;

  for(i = hotplug_idx; i < len; i++) {
    write_proc_scsi(list[i], 0);
    list[i]->renamed = 1;
    list[i]->removed = 1;
  }

  for(i = hotplug_idx; i < len; i++) {
    if(!list[i]->hotplug) {
      write_proc_scsi(list[i], 1);
      list[i]->removed = 0;
    }
  }

  for(i = hotplug_idx; i < len; i++) {
    if(list[i]->removed) write_proc_scsi(list[i], 1);
  }
}


void rename_scsi_devs()
{
  unsigned u;
  char *s;

  struct sysfs_directory *sf_dir;
  struct sysfs_link *sf_link;

  do_rename(disk_list, disk_list_len);
  do_rename(cdrom_list, cdrom_list_len);

  for(u = 0; u < scsi_list_len; u++) {
    if(scsi_list[u]->renamed) {
      sf_dir = sysfs_open_directory(scsi_list[u]->path);
      if(sf_dir) {
        sysfs_read_directory(sf_dir);
        sf_link = sysfs_get_directory_link(sf_dir, "block");
        if(sf_link) {
          if((s = strrchr(sf_link->target, '/')))
          scsi_list[u]->new_name = strdup(s + 1);
        }
        sysfs_close_directory(sf_dir);
      }
    }
  }

  for(u = 0; u < scsi_list_len; u++) {
    if(scsi_list[u]->new_name) {
      fprintf(stderr, "%s -> %s\n", scsi_list[u]->name, scsi_list[u]->new_name);
    }
  }

}


void write_proc_scsi(scsi_dev_t *sd, int add)
{
  FILE *f;

  if(config.debug) fprintf(stderr, "scsi %s-single-device %s\n", add ? "add" : "remove", sd->bus_id);

  if((f = fopen("/proc/scsi/scsi", "w"))) {
    fprintf(f, "scsi %s-single-device %s\n", add ? "add" : "remove", sd->bus_id);
    fclose(f);
  }
}


void free_scsi_list()
{
  unsigned u;
  scsi_dev_t *sd;

  for(u = 0; u < scsi_list_len; u++) {
    sd = scsi_list[u];

    free(sd->name);
    free(sd->new_name);
    free(sd->bus_id);
    free(sd->path);
    free(sd);
  }

  free(scsi_list);

  scsi_list = cdrom_list = disk_list = NULL;
  scsi_list_len = cdrom_list_len = disk_list_len = 0;
}


