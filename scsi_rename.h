typedef struct {
  char *name;
  char *new_name;
  char *bus_id; 
  char *path;
  unsigned hotplug:1;
  unsigned renamed:1;
  unsigned removed:1;
} scsi_dev_t;

scsi_dev_t **scsi_list;
scsi_dev_t **cdrom_list;
scsi_dev_t **disk_list;
unsigned scsi_list_len;
unsigned cdrom_list_len;
unsigned disk_list_len;

void get_scsi_list(void);
void free_scsi_list(void);
void rename_scsi_devs(void);

int scsi_rename_main(int argc, char **argv);
