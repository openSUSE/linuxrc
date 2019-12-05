typedef struct {
  char *name;
  char *new_name;
  char *bus_id; 
  char *path;
  unsigned hotplug:1;
  unsigned renamed:1;
  unsigned removed:1;
} scsi_dev_t;

extern scsi_dev_t **scsi_list;
extern scsi_dev_t **cdrom_list;
extern scsi_dev_t **disk_list;
extern unsigned scsi_list_len;
extern unsigned cdrom_list_len;
extern unsigned disk_list_len;

void get_scsi_list(void);
void free_scsi_list(void);
void rename_scsi_devs(void);

int scsi_rename_main(int argc, char **argv);
