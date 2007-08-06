#include "sha1.h"

typedef struct url_data_s {
  url_t *url;
  char *file_name;
  FILE *f;
  int err;
  char *err_buf;
  char *curl_err_buf;
  unsigned err_buf_len;
  unsigned p_now, p_total;
  unsigned zp_now, zp_total;
  unsigned z_progress:1;
  unsigned flush:1;
  unsigned gzip:1;
  unsigned cramfs:1;
  unsigned file_opened:1;
  unsigned unzip:1;
  unsigned label_shown:1;
  char *label;
  int percent;
  int pipe_fd;
  char *orig_name;
  unsigned image_size;
  char *tmp_file;
  struct {
    unsigned len, max;
    unsigned char *data;
  } buf;
  int (*progress)(struct url_data_s *, int);
  struct sha1_ctx sha1_ctx;
  char *sha1;
} url_data_t;

#define URL_FLAG_UNZIP		1
#define URL_FLAG_PROGRESS	2

void url_read(url_data_t *url_data);
url_t *url_set(char *str);
url_t *url_free(url_t *url);
void url_cleanup(void);
url_data_t *url_data_new(void);
void url_data_free(url_data_t *url_data);
void url_umount(url_t *url);
int url_mount(url_t *url, char *dir, int (*test_func)(url_t *));
int url_read_file(url_t *url, char *dir, char *src, char *dst, char *label, unsigned flags);
int url_find_repo(url_t *url, char *dir);
int url_find_instsys(url_t *url, char *dir);
char *url_print(url_t *url, int format);

