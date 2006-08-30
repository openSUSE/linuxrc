#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <hd.h>

/*
 * Rename SCSI devices (disk & CD-ROM) writing add/remove-single-device to
 * /proc/scsi/scsi so that USB & Firewire devices are last.
 */

#include "global.h"
#include "util.h"
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


str_list_t *reverse_str_list(str_list_t *list);
str_list_t *free_str_list(str_list_t *list);
str_list_t *read_dir(char *dir_name, int type);
char *hd_read_sysfs_link(char *base_dir, char *link_name);

void get_scsi_list()
{
  scsi_dev_t *scsi_dev;
  char *s;
  unsigned u;
  str_list_t *sf_class, *sf_class_e;
  char *sf_cdev = NULL, *sf_dev, *sf_cdev_name, *bus_id;

  scsi_list = calloc(MAX_SCSI_DEVS + 1, sizeof *scsi_list);

  sf_class = reverse_str_list(read_dir("/sys/block", 'd'));

  if(!sf_class) {
    if(config.debug) fprintf(stderr, "no block devices\n");
    return;
  }

  for(sf_class_e = sf_class; sf_class_e; sf_class_e = sf_class_e->next) {
    strprintf(&sf_cdev, "/sys/block/%s", sf_class_e->str);
    sf_dev = hd_read_sysfs_link(sf_cdev, "device");

    sf_cdev_name = strrchr(sf_cdev, '/');
    if(sf_cdev_name) sf_cdev_name++;

    if(sf_dev && sf_cdev_name) {
      bus_id = strrchr(sf_dev, '/');
      if(bus_id) bus_id++;

      scsi_dev = NULL;

      if(
        (
          !strncmp(sf_cdev_name, "sr", 2) ||
          !strncmp(sf_cdev_name, "sd", 2)
        ) &&
        scsi_list_len < MAX_SCSI_DEVS
      ) {
        scsi_dev = scsi_list[scsi_list_len++] = calloc(1, sizeof *scsi_dev);

        scsi_dev->name = strdup(sf_cdev_name);
        scsi_dev->bus_id = strdup(bus_id);
        for(s = scsi_dev->bus_id; *s; s++) if(*s == ':') *s = ' ';
        scsi_dev->path = strdup(sf_dev);
        if(
          strstr(sf_dev, "/usb") ||
          strstr(sf_dev, "/fw-host")
        ) {
          scsi_dev->hotplug = 1;
        }
      }
    }
  }

  free(sf_cdev);
  sf_class = free_str_list(sf_class);

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

  do_rename(disk_list, disk_list_len);
  do_rename(cdrom_list, cdrom_list_len);

  for(u = 0; u < scsi_list_len; u++) {
    if(scsi_list[u]->renamed) {
      if((s = hd_read_sysfs_link(scsi_list[u]->path, "block"))) {
        if((s = strrchr(s, '/'))) scsi_list[u]->new_name = strdup(s + 1);
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


