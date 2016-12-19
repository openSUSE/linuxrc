#include "md5.h"
#include "sha1.h"
#include "sha256.h"
#include "sha512.h"

#define MAX_DIGEST_SIZE SHA512_DIGEST_SIZE

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
  unsigned cramfs:1;
  unsigned file_opened:1;
  unsigned unzip:1;
  unsigned label_shown:1;
  unsigned optional:1;
  char *compressed;		///< program name used for compression, if any
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
  struct {
    struct {
      struct md5_ctx md5;
      struct sha1_ctx sha1;
      struct sha256_ctx sha224;
      struct sha256_ctx sha256;
      struct sha512_ctx sha384;
      struct sha512_ctx sha512;
    } ctx;
    char md5[MD5_DIGEST_SIZE * 2 + 1];
    char sha1[SHA1_DIGEST_SIZE * 2 + 1];
    char sha224[SHA224_DIGEST_SIZE * 2 + 1];
    char sha256[SHA256_DIGEST_SIZE * 2 + 1];
    char sha384[SHA384_DIGEST_SIZE * 2 + 1];
    char sha512[SHA512_DIGEST_SIZE * 2 + 1];
  } digest;
} url_data_t;

#define URL_FLAG_UNZIP		(1 << 0)
#define URL_FLAG_PROGRESS	(1 << 1)
#define URL_FLAG_NODIGEST	(1 << 2)
#define URL_FLAG_NOUNLINK	(1 << 3)
#define URL_FLAG_KEEP_MOUNTED	(1 << 4)
#define URL_FLAG_OPTIONAL	(1 << 5)
#define URL_FLAG_CHECK_SIG	(1 << 6)

void url_read(url_data_t *url_data);
url_t *url_set(char *str);
url_t *url_free(url_t *url);
void url_cleanup(void);
url_data_t *url_data_new(void);
void url_data_free(url_data_t *url_data);
void url_umount(url_t *url);
int url_mount(url_t *url, char *dir, int (*test_func)(url_t *));
int url_read_file(url_t *url, char *dir, char *src, char *dst, char *label, unsigned flags);
int url_read_file_anywhere(url_t *url, char *dir, char *src, char *dst, char *label, unsigned flags);
int url_find_repo(url_t *url, char *dir);
int url_find_instsys(url_t *url, char *dir);
char *url_print(url_t *url, int format);
char *url_print2(url_t *url, char *file);
char *url_instsys_base(char *path);
void url_build_instsys_list(char *instsys, int read_list);

unsigned url_is_mountable(instmode_t scheme);
unsigned url_is_network(instmode_t scheme);
unsigned url_is_blockdev(instmode_t scheme);
unsigned url_is_nopath(instmode_t scheme);
unsigned url_is_auth(instmode_t scheme);
instmode_t url_scheme2id(char *scheme);
char *url_scheme2name(instmode_t scheme_id);
char *url_scheme2name_upper(instmode_t scheme_id);
void url_register_schemes(void);
