#define _GNU_SOURCE	/* strnlen, getline, strcasestr, strverscmp, fnmatch */

/*

known issues:

- slp: path = NULL does not work - why?

 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <dirent.h>
#include <fnmatch.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mount.h>

#include <curl/curl.h>

#include "global.h"
#include "file.h"
#include "util.h"
#include "module.h"
#include "net.h"
#include "slp.h"
#include "dialog.h"
#include "display.h"
#include "auto2.h"
#include "url.h"

#define CRAMFS_SUPER_MAGIC	0x28cd3d45
#define CRAMFS_SUPER_MAGIC_BIG	0x453dcd28

struct cramfs_super_block {
  unsigned magic;
  unsigned size;
  unsigned flags;
  unsigned future;
  unsigned char signature[16];
  unsigned crc;
  unsigned edition;
  unsigned blocks;
  unsigned files;
  unsigned char name[16];
};

static size_t url_write_cb(void *buffer, size_t size, size_t nmemb, void *userp);
static int url_progress_cb(void *clientp, double dltotal, double dlnow, double ultotal, double ulnow);

static int url_read_file_nosig(url_t *url, char *dir, char *src, char *dst, char *label, unsigned flags);
static int url_mount_really(url_t *url, char *device, char *dir);
static int url_mount_disk(url_t *url, char *dir, int (*test_func)(url_t *));
static int url_progress(url_data_t *url_data, int stage);
static int url_setup_device(url_t *url);
static int url_setup_interface(url_t *url);
static int url_setup_slp(url_t *url);
static void fixup_url_rel(url_t *url);
static void fixup_url_usb(url_t *url);
static void fixup_url_disk(url_t *url);
static void fixup_url_label(url_t *url);
static void skip_slashes(char **str);
static void skip_not_slashes(char **str);
static void url_parse_instsys_config(char *file);
static slist_t *url_instsys_lookup(char *key, slist_t **sl_ll);
static char *url_instsys_config(char *path);
static char *url_config_get_path(char *entry);
static slist_t *url_config_get_file_list(char *entry);
static hd_t *find_parent_in_list(hd_t *hd_list, hd_t *hd);
static int same_device_name(hd_t *hd1, hd_t *hd2);
static hd_t *relink_array(hd_t *hd_array[]);
static void log_hd_list(char *label, hd_t *hd);
static int cmp_hd_entries_by_name(const void *p0, const void *p1);
static hd_t *sort_a_bit(hd_t *hd_list);
static int link_detected(hd_t *hd);
static char *url_print_zypp(url_t *url);
static char *url_print_autoyast(url_t *url);
static void digests_init(url_data_t *url_data);
static void digests_done(url_data_t *url_data);
static void digests_process(url_data_t *url_data, void *buffer, size_t len);
static int digests_match(url_data_t *url_data, char *digest_name, char *digest_value);
static int digests_verify(url_data_t *url_data, char *file_name);
static void digests_log(url_data_t *url_data);
static int warn_signature_failed(char *file_name);
static int is_gpg_signed(char *file);
static int is_rpm_signed(char *file);
static int is_signed(char *file, int check);
static unsigned url_scheme_attr(instmode_t scheme, char *attr_name);
static void url_add_query_string(char **buf, int n, url_t *url);
static char *url_replace_vars(char *url);
static void url_replace_vars_with_backup(char **str, char **backup);


// mapping of URL schemes to internal constants
static struct {
  char *name;
  instmode_t value;
} url_schemes[] = {
  { "no scheme", inst_none          },
  { "file",      inst_file          },
  { "nfs",       inst_nfs           },
  { "ftp",       inst_ftp           },
  { "smb",       inst_smb           },
  { "http",      inst_http          },
  { "https",     inst_https         },
  { "tftp",      inst_tftp          },
  { "cd",        inst_cdrom         },
  { "floppy",    inst_floppy        },
  { "hd",        inst_hd            },
  { "dvd",       inst_dvd           },
  { "cdwithnet", inst_cdwithnet     },
  { "net",       inst_net           },
  { "slp",       inst_slp           },
  { "exec",      inst_exec          },
  { "rel",       inst_rel           },
  { "disk",      inst_disk          },
  { "usb",       inst_usb           },
  { "label",     inst_label         },
  /* add new inst modes _here_! (before "extern") */
  { "extern",    inst_extern        },
  /* the following are just aliases */
  { "harddisk",  inst_hd            },
  { "cdrom",     inst_cdrom         },
  { "cifs",      inst_smb           },
  { "device",    inst_disk          },
  { "relurl",    inst_rel           },
};


void url_read(url_data_t *url_data)
{
  CURL *c_handle;
  int i;
  FILE *f;
  char *buf, *s, *proxy_url = NULL;
  sighandler_t old_sigpipe = signal(SIGPIPE, SIG_IGN);

  digests_init(url_data);

  c_handle = curl_easy_init();
  // log_info("curl handle = %p\n", c_handle);

  // curl_easy_setopt(c_handle, CURLOPT_VERBOSE, 1);

  curl_easy_setopt(c_handle, CURLOPT_WRITEFUNCTION, url_write_cb);
  curl_easy_setopt(c_handle, CURLOPT_WRITEDATA, url_data);
  curl_easy_setopt(c_handle, CURLOPT_ERRORBUFFER, url_data->curl_err_buf);
  curl_easy_setopt(c_handle, CURLOPT_FAILONERROR, 1);
  curl_easy_setopt(c_handle, CURLOPT_FOLLOWLOCATION, 1);
  curl_easy_setopt(c_handle, CURLOPT_MAXREDIRS, 10);
  curl_easy_setopt(c_handle, CURLOPT_SSL_VERIFYPEER, config.sslcerts ? 1 : 0);
  curl_easy_setopt(c_handle, CURLOPT_SSL_VERIFYHOST, config.sslcerts ? 2 : 0);
  curl_easy_setopt(c_handle, CURLOPT_HTTPAUTH, CURLAUTH_ANY);

  curl_easy_setopt(c_handle, CURLOPT_PROGRESSFUNCTION, url_progress_cb);
  curl_easy_setopt(c_handle, CURLOPT_PROGRESSDATA, url_data);
  curl_easy_setopt(c_handle, CURLOPT_NOPROGRESS, 0);

  if(config.net.ipv6 && !config.net.ipv4) {
    curl_easy_setopt(c_handle, CURLOPT_IPRESOLVE, CURL_IPRESOLVE_V6);
  }
  else if(config.net.ipv4 && !config.net.ipv6) {
    curl_easy_setopt(c_handle, CURLOPT_IPRESOLVE, CURL_IPRESOLVE_V4);
  }
  else {
    curl_easy_setopt(c_handle, CURLOPT_IPRESOLVE, CURL_IPRESOLVE_WHATEVER);
  }

  url_data->err = curl_easy_setopt(c_handle, CURLOPT_URL, url_data->url->str);

  if(config.debug >= 2) log_debug("curl opt url = %d (%s)\n", url_data->err, url_data->curl_err_buf);
  if(config.debug >= 2) log_debug("url_read(%s)\n", url_data->url->str);

  str_copy(&proxy_url, url_print(config.url.proxy, 1));
  if(proxy_url) {
    if(config.debug >= 2) log_debug("using proxy %s\n", proxy_url);
    curl_easy_setopt(c_handle, CURLOPT_PROXY, proxy_url);
    curl_easy_setopt(c_handle, CURLOPT_PROXYAUTH, CURLAUTH_ANY);
    if(config.debug >= 2) log_debug("proxy: %s\n", proxy_url);
  }

  if(url_data->progress) url_data->progress(url_data, 0);

  if(!url_data->err) {
    i = curl_easy_perform(c_handle);
    if(!url_data->err) url_data->err = i;
  }

  if(!url_data->err) {
    url_data->flush = 1;
    url_write_cb(NULL, 0, 0, url_data);
  }

  if(config.debug >= 2) log_debug("curl perform = %d (%s)\n", url_data->err, url_data->curl_err_buf);

  if(url_data->f) {
    i = url_data->pipe_fd >= 0 ? pclose(url_data->f) : fclose(url_data->f);
    url_data->f = NULL;

    if(url_data->pipe_fd >= 0) {
      i = WIFEXITED(i) ? WEXITSTATUS(i) : 0;
      if(i && i != 2) {
        if(url_data->tmp_file) {
          buf = malloc(url_data->err_buf_len);
          f = fopen(url_data->tmp_file, "r");
          i = fread(buf, 1, url_data->err_buf_len - 1, f);
          fclose(f);
          buf[i] = 0;
          i = strlen(buf) - 1;
          while(i > 0 && isspace(buf[i])) buf[i--] = 0;
          s = buf;
          while(isspace(*s)) s++;
          if(!url_data->err) strcpy(url_data->err_buf, s);
          free(buf);
        }
        url_data->err = 103;
        snprintf(url_data->err_buf, url_data->err_buf_len, "%s: command terminated", url_data->compressed);
      }
      // log_info("close = %d\n", i);
    }
    else {
      if(i && !url_data->err) url_data->err = 104;
    }
  }

  /* to get progress bar at 100% when uncompressing */
  url_data->flush = 0;
  url_write_cb(NULL, 0, 0, url_data);

  if(url_data->pipe_fd >= 0) close(url_data->pipe_fd);

  if(url_data->tmp_file) unlink(url_data->tmp_file);

  if(!*url_data->err_buf) {
    memcpy(url_data->err_buf, url_data->curl_err_buf, url_data->err_buf_len);
    *url_data->curl_err_buf = 0;
  }

  if(url_data->progress) url_data->progress(url_data, 2);

  curl_easy_cleanup(c_handle);

  str_copy(&proxy_url, NULL);

  signal(SIGPIPE, old_sigpipe);
}


size_t url_write_cb(void *buffer, size_t size, size_t nmemb, void *userp)
{
  url_data_t *url_data = userp;
  size_t z1, z2;
  int i, fd, fd1, fd2, tmp;
  struct cramfs_super_block *cramfs_sb;
  off_t off;

  z1 = size * nmemb;

  digests_process(url_data, buffer, z1);

  if(url_data->buf.len < url_data->buf.max && z1) {
    z2 = url_data->buf.max - url_data->buf.len;
    if(z2 > z1) z2 = z1;
    memcpy(url_data->buf.data + url_data->buf.len, buffer, z2);
    url_data->buf.len += z2;
    z1 -= z2;
    buffer += z2;
  }

  if(
    (url_data->buf.len == url_data->buf.max || url_data->flush) &&
    url_data->buf.len >= 11
  ) {
    if(url_data->unzip) {
      str_copy(&url_data->compressed, compress_type(url_data->buf.data));
    }

    if(url_data->compressed) {
      if(!strcmp(url_data->compressed, "gzip")) {
        if((url_data->buf.data[3] & 0x08)) {
          i = strnlen((char *) url_data->buf.data + 10, url_data->buf.len - 10);
          if(i < url_data->buf.len - 10) {
            url_data->orig_name = strdup((char *) url_data->buf.data + 10);
          }
        }
      }
    }
    else if(url_data->buf.len > sizeof *cramfs_sb) {
      cramfs_sb = (struct cramfs_super_block *) url_data->buf.data;
      if(
        cramfs_sb->magic == CRAMFS_SUPER_MAGIC ||
        cramfs_sb->magic == CRAMFS_SUPER_MAGIC_BIG
      ) {
        url_data->orig_name = calloc(1, sizeof cramfs_sb->name + 1);
        memcpy(url_data->orig_name, cramfs_sb->name, sizeof cramfs_sb->name);
        url_data->cramfs = 1;
      }
    }

    i = 0;
    if(
      url_data->orig_name &&
      sscanf(url_data->orig_name, "%*s %d", &i) >= 1 &&
      i > 0
    ) {
      url_data->image_size = i;
    }
  }

  if(url_data->buf.len == url_data->buf.max || url_data->flush) {
    if(!url_data->file_opened) {
      url_data->file_opened = 1;
      if(url_data->compressed) {
        url_data->tmp_file = strdup("/tmp/foo_XXXXXX");
        tmp = mkstemp(url_data->tmp_file);
        if(tmp > 0) {
          fd = open(url_data->file_name, O_LARGEFILE | O_WRONLY | O_CREAT | O_TRUNC, 0644);
          if(fd >= 0) {
            fd1 = dup(1);
            fd2 = dup(2);
            dup2(fd, 1);
            dup2(tmp, 2);
            url_data->pipe_fd = fd;
            char cmd[64];
            snprintf(cmd, sizeof cmd, "%s -dc", url_data->compressed);
            url_data->f = popen(cmd, "w");
            dup2(fd1, 1);
            dup2(fd2, 2);
            url_data->zp_total = url_data->image_size << 10;
          }
          else {
            url_data->err = 101;
            snprintf(url_data->err_buf, url_data->err_buf_len, "open: %s: %s", url_data->file_name, strerror(errno));
          }
          close(tmp);
        }
        else {
          url_data->err = 1;
          snprintf(url_data->err_buf, url_data->err_buf_len, "mkstemp: %s", strerror(errno));
        }
      }
      else {
        url_data->f = fopen(url_data->file_name, "w");
        if(!url_data->f) {
          url_data->err = 101;
          snprintf(url_data->err_buf, url_data->err_buf_len, "open: %s: %s", url_data->file_name, strerror(errno));
        }
      }
    }

    if(url_data->f && url_data->buf.len) {
      fwrite(url_data->buf.data, url_data->buf.len, 1, url_data->f);
      url_data->p_now += url_data->buf.len;
    }

    if(url_data->f && z1) {
      fwrite(buffer, z1, 1, url_data->f);
      url_data->p_now += z1;
    }

    if(url_data->buf.max) {
      url_data->buf.len = url_data->buf.max = 0;
      free(url_data->buf.data);
      url_data->buf.data = NULL;
    }
  }

  if(url_data->pipe_fd >= 0) {
    off = lseek(url_data->pipe_fd, 0, SEEK_CUR);
    if(off != -1) url_data->zp_now = off;
  }

  if(url_data->p_total || url_data->zp_total) {
    if(url_data->progress) {
      if(url_data->progress(url_data, 1) && !url_data->err) url_data->err = 102;
    }
  }

  return url_data->err ? 0 : size * nmemb;
}


int url_progress_cb(void *clientp, double dltotal, double dlnow, double ultotal, double ulnow)
{
  url_data_t *url_data = clientp;

  if(!url_data->p_total) url_data->p_total = dltotal;

  return url_data->progress ? url_data->progress(url_data, 1) : 0;
}


/*
 * Parse URL string.
 *
 * Return a freshly allocated url_t structure.
 *
 * A URL looks like: scheme://domain;user:password@server:port/path?query.
 *
 * For a general URL syntax overview, see
 *   - https://tools.ietf.org/html/rfc3986#section-3
 *   - https://tools.ietf.org/html/rfc1738 (older, with more examples)
 *
 * Notes:
 *   - 'path' is prefixed with the share name ('share/path') for SMB/CIFS
 *   - 'path' may optionally be prefixed with the device name ('device/path')
 *     for URLs referring to local block devices
 *   - 'domain' (aka workgroup) and 'share' are only relevant for SMB/CIFS
 *
 * Special AutoYaST URL schemes (note the *two* slashes ('//'):
 *   - device://dev/path
 *     -> is mapped to disk:/path?device=dev
 *   - relurl://path
 *     -> is mapped to rel:path
 *   - usb://path
 *     -> is mapped to disk:/path?device=disk/\*usb*
 *   - label://label/path
 *     -> is mapped to disk:/path?device=disk/by-label/label
 *
 *   Additionally, zero up to three consecutive slashes are acceptable at
 *   the beginning of the URL as long as the meaning is unambiguous (e.g.
 *   'label:foo').
 */
url_t *url_set(char *str)
{
  url_t *url = calloc(1, sizeof *url);
  char *s0, *s1, *s2;
  char *tmp = NULL;
  int i;
  unsigned u;
  struct stat sbuf;
  slist_t *sl;

  if(!str) return url;

  url->str = strdup(str);
  s0 = str = strdup(str);

  if((s1 = strchr(s0, ':'))) {
    *s1++ = 0;

    url->scheme = url_scheme2id(s0);

    if(url->scheme) {
      s0 = s1;

      if(s0[0] == '/' && s0[1] == '/') {
        i = strcspn(s0 + 2, "/?");
        if(i) {
          tmp = strdup(s0 + 2);
          tmp[i] = 0;
        }
        s0 += i + 2;
      }

      if((s1 = strchr(s0, '?'))) {
        *s1++ = 0;
        url->query = slist_split('&', s1);
        for(sl = url->query; sl; sl = sl->next) {
          if((s2 = strchr(sl->key, '='))) {
            *s2++ = 0;
            sl->value = strdup(s2);
          }
        }
      }

      url->path = strdup(*s0 == '/' ? s0 + 1 : s0);
    }
  }
  else {
    i = url_scheme2id(str);
    if(i > 0) {
      url->scheme = i;
      url->path = strdup("");
    }
    else {
      url->scheme = inst_rel;
      url->path = strdup(str);
    }
  }

  free(str);

  if(tmp) {
    s0 = tmp;

    if((s1 = strchr(s0, ';'))) {
      *s1++ = 0;
      url->domain = strdup(s0);
      s0 = s1;
    }

    if((s1 = strchr(s0, '@'))) {
      *s1++ = 0;
      if((s2 = strchr(s0, ':'))) {
        *s2++ = 0;
        url->password = strdup(s2);
      }
      url->user = strdup(s0);
      s0 = s1;
    }

    s1 = s0;
    if(*s0 == '[') s1 = strchr(s0, ']') ?: s0;

    if((s1 = strchr(s1, ':'))) {
      *s1++ = 0;
      if(*s1) {
        u = strtoul(s1, &s1, 0);
        if(!*s1) url->port = u;
      }
    }

    s1 = s0 + strlen(s0);

    if(*s0 == '[' && s1[-1] == ']') {
      s1[-1] = 0;
      url->server = strdup(s0 + 1);
    }
    else {
      url->server = strdup(s0);
    }

    free(tmp);
    tmp = NULL;
  }

  /* cifs: first path element is share */
  if(url->scheme == inst_smb && url->path) {
    url->share = url->path;
    url->path = NULL;

    if((s1 = strchr(url->share, '/'))) {
      *s1++ = 0;
      url->path = strdup(s1);
    }
  }

  /* unescape strings */
  {
    char **str[] = {
      &url->server, &url->share, &url->path, &url->user,
      &url->password, &url->domain
    };

    for(i = 0; i < sizeof str / sizeof *str; i++) {
      if(*str[i]) {
        s0 = *str[i];
        *str[i] = curl_easy_unescape(NULL, s0, 0, NULL);
        free(s0);
      }
    }
  }

  url->orig.scheme = url->scheme;

  /* adjust some url schemes to support autoyast syntax */
  fixup_url_disk(url);
  fixup_url_rel(url);
  fixup_url_usb(url);
  fixup_url_label(url);

  url->is.blockdev = url_is_blockdev(url->scheme);

  /* local storage device: allow path to begin with device name */
  if(
    url->is.blockdev &&
    url->path
  ) {
    tmp = malloc(strlen(url->path) + 6);
    strcpy(tmp, "/");

    if(
      strncmp(url->path, "dev", 3) ||
      (url->path[3] != 0 && url->path[3] != '/')
    ) {
      strcat(tmp, "dev/");
    }

    strcat(tmp, url->path);

    s0 = tmp;
    do {
      if((s0 = strchr(s0 + 1, '/'))) *s0 = 0;

      if(stat(tmp, &sbuf)) break;
      if(S_ISBLK(sbuf.st_mode)) {
        url->device = strdup(short_dev(tmp));
        free(url->path);
        url->path = s0 ? strdup(s0 + 1) : NULL;
      }
      if(!S_ISDIR(sbuf.st_mode)) break;
    }
    while(s0 && (*s0 = '/'));

    free(tmp);
    tmp = NULL;
  }

  if((sl = slist_getentry(url->query, "device"))) {
    s0 = short_dev(sl->value);
    str_copy(&url->device, *s0 ? s0 : NULL);
  }

  if((sl = slist_getentry(url->query, "instsys"))) {
    str_copy(&url->instsys, sl->value);
  }

  if((sl = slist_getentry(url->query, "list"))) {
    url->file_list = slist_split(',', sl->value);
  }

  if((sl = slist_getentry(url->query, "type"))) {
    url->is.file = strcmp(sl->value, "file") ? 0 : 1;
    url->is.dir = strcmp(sl->value, "dir") ? 0 : 1;
  }

  if((sl = slist_getentry(url->query, "all"))) {
    url->search_all = strtoul(sl->value, NULL, 0);
  }

  if((sl = slist_getentry(url->query, "quiet"))) {
    url->quiet = strtoul(sl->value, NULL, 0);
  }

  url->is.mountable = url_is_mountable(url->scheme);

  url->is.network = url_is_network(url->scheme);

  url->is.nodevneeded = !(url->is.network || url->is.blockdev);

  if(
    url->scheme == inst_cdrom ||
    url->scheme == inst_dvd
    ) {
    url->is.cdrom = 1;
  }

  /* ensure leading "/" if mountable */
  if(url->is.mountable) {
    if(url->path) {
      if(*url->path != '/') {
        strprintf(&url->path, "/%s", url->path);
      }
    }
    else {
      url->path = strdup("/");
    }
  }

  /* if URL path ends with '/', assume directory */
  if(!url->is.file) {
    if(url->path && *url->path && url->path[strlen(url->path) - 1] == '/') {
      url->is.dir = 1;
    }
  }

  url_replace_vars_with_backup(&url->server, &url->orig.server);
  url_replace_vars_with_backup(&url->share, &url->orig.share);
  url_replace_vars_with_backup(&url->path, &url->orig.path);
  url_replace_vars_with_backup(&url->instsys, &url->orig.instsys);

  if(config.debug >= 2) {
    url_log(url);
  }
  else {
    log_debug("url = %s\n", url->str);
  }

  return url;
}


/*
 * Detailed log of parsed url components.
 */
void url_log(url_t *url)
{
  int i;
  slist_t *sl;

  if(!url) return;

  log_debug("url = %s\n", url->str);

  log_debug(
    "  scheme = %s (%d), orig scheme = %s (%d)",
    url_scheme2name(url->scheme), url->scheme,
    url_scheme2name(url->orig.scheme), url->orig.scheme
  );
  if(url->server) log_debug("  server = \"%s\"", url->server);
  if(url->orig.server) log_debug("  server (orig) = \"%s\"", url->orig.server);
  if(url->port) log_debug("  port = %u", url->port);
  if(url->path) log_debug("  path = \"%s\"", url->path);
  if(url->orig.path) log_debug("  path (orig) = \"%s\"", url->orig.path);

  if(url->user || url->password) {
    i = 0;
    if(url->user) log_debug("%c user = \"%s\"", i++ ? ',' : ' ', url->user);
    if(url->password) log_debug("%c password = \"%s\"", i++ ? ',' : ' ', url->password);
    log_debug("\n");
  }

  if(url->share || url->domain || url->device) {
    if(url->share) log_debug("  share = \"%s\"", url->share);
    if(url->orig.share) log_debug("  share (orig) = \"%s\"", url->orig.share);
    if(url->domain) log_debug("  domain = \"%s\"", url->domain);
    if(url->device) log_debug("  device = \"%s\"", url->device);
  }

  log_debug(
    "  network = %u, blockdev = %u, mountable = %u, file = %u, dir = %u, all = %u, quiet = %u\n",
    url->is.network, url->is.blockdev, url->is.mountable, url->is.file, url->is.dir,
    url->search_all, url->quiet
  );

  if(url->instsys) log_debug("  instsys = %s\n", url->instsys);
  if(url->orig.instsys) log_debug("  instsys (orig) = %s\n", url->orig.instsys);

  if(url->query) {
    log_debug("  query:\n");
    for(sl = url->query; sl; sl = sl->next) {
      log_debug("    %s = \"%s\"\n", sl->key, sl->value);
    }
  }

  log_debug("  mount = %s, tmp_mount = %s\n", url->mount, url->tmp_mount);

  log_debug("  url (zypp format) = %s\n", url_print(url, 4));
  log_debug("  url (ay format) = %s\n", url_print(url, 5));
}


/*
 * Fix up autoyast 'rel' url scheme.
 *
 *   - relurl://foo/bar
 *
 * Note the '//'.
 */
void fixup_url_rel(url_t *url)
{
  if(
    url->scheme == inst_rel &&
    url->server &&
    url->path
  ) {
    if(!*url->path) {
      free(url->path);
      url->path = url->server;
      url->server = NULL;
    }
    else {
      strprintf(&url->path, "%s/%s", url->server, url->path);
      str_copy(&url->server, NULL);
    }
  }
}


/*
 * Fix up autoyast 'usb' url scheme.
 *
 *   - usb://foo/bar
 *
 * Note the '//'.
 *
 * 'usb' is translated to the 'disk' url scheme with a suitable '?device=XXX'
 * query parameter added to take care of matching only usb devices.
 */
void fixup_url_usb(url_t *url)
{
  if(url->scheme != inst_usb) return;

  url->scheme = inst_disk;
  slist_setentry(&url->query, "device", "disk/*usb*", 1);

  fixup_url_disk(url);
}


/*
 * Fix up autoyast usage of 'device'/'disk' url scheme.
 *
 *   - disk://some_dev/foo/bar
 *
 * Note the '//'.
 *
 * The server part of the url is prepended to path.
 */
void fixup_url_disk(url_t *url)
{
  if(url->scheme != inst_disk) return;

  if(url->server && url->path) {
    if(!*url->path) {
      free(url->path);
      url->path = url->server;
      url->server = NULL;
    }
    else {
      strprintf(&url->path, "%s/%s", url->server, url->path);
      str_copy(&url->server, NULL);
    }
  }
}


/*
 * Handle autoyast 'label' scheme.
 *
 *   - label://label/foo/bar
 *
 * 'label' is translated to the 'disk' url scheme with a suitable '?device=XXX'
 * query parameter added to take care of matching the label.
 */
void fixup_url_label(url_t *url)
{
  if(url->scheme != inst_label) return;

  url->scheme = inst_disk;

  if(url->server) {
    slist_t *sl = slist_setentry(&url->query, "device", NULL, 1);
    strprintf(&sl->value, "disk/by-label/%s", url->server);
    slist_setentry(&url->query, "label", url->server, 1);
    str_copy(&url->server, NULL);
  }
  else if(url->path) {
    char *s = url->path;
    skip_slashes(&s);
    if(*s) {
      char *t = s;
      skip_not_slashes(&t);
      if(t != s) {
        if(*t) *t++ = 0;
        slist_t *sl = slist_setentry(&url->query, "device", NULL, 1);
        strprintf(&sl->value, "disk/by-label/%s", s);
        slist_setentry(&url->query, "label", s, 1);
        str_copy(&url->path, t);
      }
    }
  }
}


/*
 * Skip sequence of slashes ('/').
 */
void skip_slashes(char **str)
{
  while(**str && **str == '/') (*str)++;
}


/*
 * Skip sequence of non-slashes (not '/').
 */
void skip_not_slashes(char **str)
{
  while(**str && **str != '/') (*str)++;
}


/*
 * (Re-)append arbitrary query parameters to url.
 *
 *  buf: buffer pointer, will be updated as needed
 *    n: parameter position (starting at 0)
 *  url: url to process
 */
void url_add_query_string(char **buf, int n, url_t *url)
{
  slist_t *sl;

  if(
    url->scheme != inst_ftp &&
    url->scheme != inst_tftp &&
    url->scheme != inst_http &&
    url->scheme != inst_https
  ) return;

  for(sl = url->query; sl; sl = sl->next) {
    // skip parameters handled by linuxrc
    if(fnmatch("@(device|instsys|list|type|all|quiet|label|service|descr|proxy*)", sl->key, FNM_EXTMATCH)) {
      strprintf(buf, "%s%c%s=%s", *buf, n++ ? '&' : '?', sl->key, sl->value);
    }
  }
}


/*
 * Print url to string.
 *
 * scheme://domain;user:password@server:port/path?query
 *
 * format:
 *   0: for logging
 *   1: with non-standard query part
 *   2: with device
 *   3: like 2, but remove 'rel:' scheme
 *   4: in zypp format
 *   5: in autoyast format
 */
char *url_print(url_t *url, int format)
{
  static char *buf = NULL, *s;
  int q = 0;

  // log_info("start buf = %p\n", buf);
  // LXRC_WAIT

  str_copy(&buf, NULL);

  if(!url) return buf;

  if(format == 4) return url_print_zypp(url);

  if(format == 5) return url_print_autoyast(url);

  if(format != 3 || url->scheme != inst_rel) {
    strprintf(&buf, "%s:", url_scheme2name(url->scheme));
  }
  else {
    str_copy(&buf, "");
  }

  if(url->domain || url->user || url->password || url->server || url->port) {
    strprintf(&buf, "%s//", buf);
    if(url->domain) strprintf(&buf, "%s%s;", buf, url->domain);
    if(url->user) {
      s = curl_easy_escape(NULL, url->user, 0);
      strprintf(&buf, "%s%s", buf, s);
      curl_free(s);
    }
    if(url->password) {
      s = curl_easy_escape(NULL, url->password, 0);
      strprintf(&buf, "%s:%s", buf, s);
      curl_free(s);
    }
    if(url->user || url->password) strprintf(&buf, "%s@", buf);
    if(url->server) {
      if(strchr(url->server, ':')) {
        strprintf(&buf, "%s[%s]", buf, url->server);
      }
      else {
        strprintf(&buf, "%s%s", buf, url->server);
      }
    }
    if(url->port) strprintf(&buf, "%s:%u", buf, url->port);
  }

  if(url->share) strprintf(&buf, "%s/%s", buf, url->share);
  if(url->path && (url->scheme != inst_slp || *url->path)) {
    strprintf(&buf, "%s/%s%s",
      buf,
      url->scheme == inst_ftp && *url->path == '/' ? "%2F" : "",
      *url->path == '/' ? url->path + 1 : url->path
    );
  }

  if(format == 1) {
    // add the non-standard query parameters
    url_add_query_string(&buf, q, url);
  }

  if(format == 0 || format == 2 || format == 3) {
    if((s = url->used.device) || (s = url->device)) {
      strprintf(&buf, "%s%cdevice=%s", buf, q++ ? '&' : '?', short_dev(s));
    }
    if(url->file_list) {
      strprintf(&buf, "%s%clist=%s", buf, q++ ? '&' : '?', s = slist_join(",", url->file_list));
      free(s);
    }

    url_add_query_string(&buf, q, url);
  }

  if(format == 0) {
    // basically for the slp scheme
    static char *params[] = { "service", "descr" };
    slist_t *sl;
    for (int i = 0; i < sizeof params / sizeof *params; i++) {
      if((sl = slist_getentry(url->query, params[i]))) {
        strprintf(&buf, "%s%c%s=%s", buf, q++ ? '&' : '?', sl->key, sl->value);
      }
    }
    if(config.debug >= 2 && url->used.hwaddr) {
      strprintf(&buf, "%s%chwaddr=%s", buf, q++ ? '&' : '?', url->used.hwaddr);
    }
  }

  s = buf;

  if(format == 3 && *s == '/') s++;

  // log_info("end buf = %p\n", buf);
  // LXRC_WAIT

  return s;
}


/*
 * Construct URL suitable for zypp.
 *
 * See
 *   - https://doc.opensuse.org/projects/libzypp/HEAD/classzypp_1_1media_1_1MediaManager.html#MediaAccessUrl
 * for URL scheme documentation.
 *
 * This builds a URL without zypp variables replaced.
 */
char *url_print_zypp(url_t *url)
{
  static char *buf = NULL, *s;
  char *path = NULL, *file = NULL;
  int q = 0;

  // log_info("start buf = %p\n", buf);
  // LXRC_WAIT

  if(url->rewrite_for_zypp) {
    str_copy(&buf, url->rewrite_for_zypp);

    return buf;
  }

  str_copy(&buf, NULL);
  int scheme = url->scheme;

  /* prefer original values (without zypp variables replaced) */
  char *server = url->orig.server ?: url->server;
  char *share = url->orig.share ?: url->share;
  str_copy(&path, url->orig.path ?: url->path);

  if(url->is.file && path) {
    if((file = strrchr(path, '/')) && *file) {
      *file++ = 0;
    }
    else {
      file = NULL;
    }
  }

  if(scheme == inst_disk) {
    scheme = url->is.cdrom ? inst_cdrom : inst_hd;
  }

  if(
    scheme != inst_hd &&
    scheme != inst_cdrom &&
    scheme != inst_file &&
    scheme != inst_ftp &&
    scheme != inst_http &&
    scheme != inst_https &&
    scheme != inst_nfs &&
    scheme != inst_smb &&
    scheme != inst_tftp
  ) return buf;

  strprintf(&buf, "%s:", url_scheme2name(scheme));

  if(url->domain || url->user || url->password || server || url->port) {
    strprintf(&buf, "%s//", buf);
    if(url->domain) strprintf(&buf, "%s%s;", buf, url->domain);
    if(url->user) {
      s = curl_easy_escape(NULL, url->user, 0);
      strprintf(&buf, "%s%s", buf, s);
      curl_free(s);
    }
    if(url->password) {
      s = curl_easy_escape(NULL, url->password, 0);
      strprintf(&buf, "%s:%s", buf, s);
      curl_free(s);
    }
    if(url->user || url->password) strprintf(&buf, "%s@", buf);
    if(server) {
      if(strchr(server, ':')) {
        strprintf(&buf, "%s[%s]", buf, server);
      }
      else {
        strprintf(&buf, "%s%s", buf, server);
      }
    }
    if(url->port) strprintf(&buf, "%s:%u", buf, url->port);
  }

  if(share) strprintf(&buf, "%s/%s", buf, share);
  if(path) {
    strprintf(&buf, "%s/%s%s",
      buf,
      url->scheme == inst_ftp && *path == '/' ? "%2F" : "",
      *path == '/' ? path + 1 : path
    );
  }

  if(url->scheme == inst_hd) {
    if((s = url->used.device) || (s = url->device)) {
      strprintf(&buf, "%s%cdevice=%s", buf, q++ ? '&' : '?', long_dev(s));
    }
  }

  if(url->scheme == inst_cdrom) {
    if((s = url->used.device) || (s = url->device)) {
      strprintf(&buf, "%s%cdevices=%s", buf, q++ ? '&' : '?', long_dev(s));
    }
  }

  if(
    config.url.proxy &&
    config.url.proxy->server && (
      url->scheme == inst_http ||
      url->scheme == inst_https ||
      url->scheme == inst_ftp ||
      url->scheme == inst_tftp
    )
  ) {
    strprintf(&buf, "%s%cproxy=%s", buf, q++ ? '&' : '?', config.url.proxy->server);
    if(config.url.proxy->port) strprintf(&buf, "%s%cproxyport=%u", buf, q++ ? '&' : '?', config.url.proxy->port);
    if(config.url.proxy->user) strprintf(&buf, "%s%cproxyuser=%s", buf, q++ ? '&' : '?', config.url.proxy->user);
    if(config.url.proxy->password) strprintf(&buf, "%s%cproxypass=%s", buf, q++ ? '&' : '?', config.url.proxy->password);
  }

  url_add_query_string(&buf, q, url);

  if(url->is.file && file) {
    strprintf(&buf, "iso:/?iso=%s&url=%s", file, buf);
  }

  str_copy(&path, NULL);

  // log_info("end buf = %p\n", buf);
  // LXRC_WAIT

  return buf;
}


/*
 * Convert URL to AutoYAST format.
 *
 * url scheme doc
 *   - https://doc.opensuse.org/projects/autoyast/#Commandline.ay
 *
 * actual implementation
 *   - https://github.com/yast/yast-installation/blob/master/src/lib/transfer/file_from_url.rb
 *   - https://github.com/yast/yast-autoinstallation/blob/master/src/modules/AutoinstConfig.rb
 */
char *url_print_autoyast(url_t *url)
{
  static char *buf = NULL, *s;
  char *path = NULL, *file = NULL;
  int scheme;

  str_copy(&buf, NULL);

  str_copy(&path, url->path);

  if(url->is.file && path) {
    if((file = strrchr(path, '/')) && *file) {
      *file++ = 0;
    }
    else {
      file = NULL;
    }
  }

  scheme = url->scheme;

  if(url->is.blockdev) {
    strprintf(&buf, "device://");
    if(url->used.device) {
      strprintf(&buf, "%s%s", buf, short_dev(url->used.device));
    }
    else if(url->orig.scheme == inst_usb) {
      strprintf(&buf, "usb:/");
    }
    else if(url->orig.scheme == inst_label) {
      slist_t *sl;
      strprintf(&buf, "label://");
      if((sl = slist_getentry(url->query, "label"))) {
        strprintf(&buf, "%s%s", buf, sl->value);
      }
    }
  }
  else if(scheme == inst_rel) {
    strprintf(&buf, "relurl:/");
  }
  else if(scheme == inst_file) {
    strprintf(&buf, "file://");
  }
  else if(scheme == inst_slp) {
    strprintf(&buf, "slp");
  }
  else {
    strprintf(&buf, "%s:", url_scheme2name(scheme));
  }

  if(url->domain || url->user || url->password || url->server || url->port) {
    strprintf(&buf, "%s//", buf);
    if(url->domain) strprintf(&buf, "%s%s;", buf, url->domain);
    if(url->user) {
      s = curl_easy_escape(NULL, url->user, 0);
      strprintf(&buf, "%s%s", buf, s);
      curl_free(s);
    }
    if(url->password) {
      s = curl_easy_escape(NULL, url->password, 0);
      strprintf(&buf, "%s:%s", buf, s);
      curl_free(s);
    }
    if(url->user || url->password) strprintf(&buf, "%s@", buf);
    if(url->server) {
      if(strchr(url->server, ':')) {
        strprintf(&buf, "%s[%s]", buf, url->server);
      }
      else {
        strprintf(&buf, "%s%s", buf, url->server);
      }
    }
    if(url->port) strprintf(&buf, "%s:%u", buf, url->port);
  }

  if(url->share) strprintf(&buf, "%s/%s", buf, url->share);

  if(path && url->scheme != inst_slp) {
    strprintf(&buf, "%s/%s%s",
      buf,
      url->scheme == inst_ftp && *path == '/' ? "%2F" : "",
      *path == '/' ? path + 1 : path
    );
  }

  str_copy(&path, NULL);

  url_add_query_string(&buf, 0, url);

  return buf;
}


char *url_print2(url_t *url, char *file)
{
  static char *buf = NULL, *s = "";
  int i;

  str_copy(&buf, url_print(url, 1));

  if(!file) return buf;

  i = strlen(buf);
  if(i && buf[i - 1] == '/' && *file == '/') file++;

  if(i && buf[i - 1] != '/' && *file != '/') s = "/";

  strprintf(&buf, "%s%s%s", buf, s, file);

  return buf;
}


url_t *url_free(url_t *url)
{
  if(url) {
    free(url->str);
    free(url->server);
    free(url->share);
    free(url->path);
    free(url->user);
    free(url->password);
    free(url->domain);
    free(url->device);
    free(url->mount);
    free(url->tmp_mount);

    free(url->used.device);
    free(url->used.hwaddr);
    free(url->used.model);
    free(url->used.unique_id);

    slist_free(url->query);
    slist_free(url->file_list);

    free(url->orig.server);
    free(url->orig.share);
    free(url->orig.path);
    free(url->orig.instsys);

    free(url);
  }

  return NULL;
}


url_data_t *url_data_new()
{
  static int curl_init = 0;
  int err;
  url_data_t *url_data = calloc(1, sizeof *url_data);

  url_data->err_buf_len = CURL_ERROR_SIZE;
  *(url_data->err_buf = malloc(CURL_ERROR_SIZE)) = 0;
  *(url_data->curl_err_buf = malloc(CURL_ERROR_SIZE)) = 0;

  url_data->buf.data = malloc(url_data->buf.max = 256);
  url_data->buf.len = 0;

  url_data->pipe_fd = -1;
  url_data->percent = -1;

  if(!curl_init) {
    curl_init = 1;
    err = curl_global_init(CURL_GLOBAL_ALL);
    if(err) log_info("curl init = %d\n", err);
  }

  return url_data;
}


void url_data_free(url_data_t *url_data)
{
  url_free(url_data->url);

  free(url_data->file_name);
  free(url_data->err_buf);
  free(url_data->curl_err_buf);
  free(url_data->orig_name);
  free(url_data->tmp_file);
  free(url_data->buf.data);
  free(url_data->label);
  free(url_data->compressed);

  free(url_data);
}


void url_cleanup()
{
  curl_global_cleanup();
}


/*
 * Default progress indicator.
 *   stage: 0 = init, 1 = update, 2 = done
 *
 * return:
 *   0: ok
 *   1: abort download
 *
 * Note: it's possible that 'done' follows 'init' without any 'update' in between.
 */
int url_progress(url_data_t *url_data, int stage)
{
  int percent = -1, with_win;
  char *buf = NULL;

  with_win = config.win && !config.linemode;

  /* init */
  if(stage == 0) {
    if(!with_win) {
      if(url_data->label) {
        strprintf(&buf, "%s", url_data->label);
      }
      else {
        strprintf(&buf, "Loading %s", url_print(url_data->url, 0));
      }

      printf("%s", buf);
      fflush(stdout);
    }

    return 0;
  }

  /* done */
  if(stage == 2) {
    if(with_win) {
      dia_status_off(&config.progress_win);
      if(url_data->err && !url_data->optional) {
        strprintf(&buf, "error %d: %s\n", url_data->err, url_data->err_buf);
        dia_message(buf, MSGTYPE_ERROR);
      }
    }
    else {
      if(url_data->err) {
        printf("%s - %s\n",
          url_data->label_shown && url_data->percent < 0 ? "\x08\x08\x08\x08\x08\x08\x08\x08\x08\x08\x08\x08" : "",
          url_data->optional ? "missing (optional)" : "failed"
        );
        if(config.debug && !url_data->optional) printf("error %d: %s\n", url_data->err, url_data->err_buf);
      }
      else {
        printf("\n");
      }

      fflush(stdout);
    }

    return 0;
  }

  /* update */

  if(url_data->p_total) {
    percent = (100 * (uint64_t) url_data->p_now) / url_data->p_total;
  }
  else if(url_data->zp_total) {
    percent = (100 * (uint64_t) url_data->zp_now) / url_data->zp_total;
  }

  if(percent > 100) percent = 100;

  if(!url_data->label_shown) {
    if(with_win) {
      if(url_data->label) {
        strprintf(&buf, "%s", url_data->label);
      }
      else {
        strprintf(&buf, "Loading %s", url_print(url_data->url, 0));
      }
      if(percent >= 0) {
        strprintf(&buf, "%s (%u kB)",
          buf,
          ((url_data->zp_total ?: url_data->p_total) + 1023) >> 10
        );
      }

      dia_status_on(&config.progress_win, buf);
    }
    else {
      if(percent >= 0) {
        strprintf(&buf,
          " (%u kB) -     ",
          ((url_data->zp_total ?: url_data->p_total) + 1023) >> 10
        );
      }
      else {
        strprintf(&buf, " -          ");
      }
      printf("%s", buf);
    }

    url_data->label_shown = 1;
  }

  if(percent >= 0) {
    if(percent != url_data->percent) {
      if(with_win) {
        dia_status(&config.progress_win, percent);
      }
      else {
        printf("\x08\x08\x08\x08%3d%%", percent);
      }

      url_data->percent = percent;
    }
  }
  else {
    percent = (url_data->zp_now ?: url_data->p_now) >> 10;
    if(percent > url_data->percent + 100 || url_data->flush) {
      if(with_win) {
        strprintf(&buf, "%6u kB", percent);
        disp_gotoxy(
          (config.progress_win.x_left + config.progress_win.x_right)/2 - 3,
          config.progress_win.y_right - 2
        );
        disp_write_string(buf);
      }
      else {
        printf("\x08\x08\x08\x08\x08\x08\x08\x08\x08%6u kB", percent);
      }
      url_data->percent = percent;
    }
  }

  fflush(stdout);

  str_copy(&buf, NULL);

  return 0;
}


/*
 * Unmounts volumes used by 'url'.
 *
 * Return error code (0 if ok).
 */
int url_umount(url_t *url)
{
  int err = 0;
  if(!url) return 0;

  if(url->mount && util_umount(url->mount)) {
    log_debug("%s: url umount failed\n", url->mount);
    err = 1;
  }
  else {
    str_copy(&url->mount, NULL);
  }

  if(url->tmp_mount && util_umount(url->tmp_mount)) {
    log_debug("%s: url umount failed\n", url->tmp_mount);
    err = 1;
  }
  else {
    str_copy(&url->tmp_mount, NULL);
  }

  return err;
}


/*
 * Really mount 'device' to 'dir' (do an actual mount() call).
 *
 * Get additionally needed params out of 'url'.
 *
 * Return 0 if ok, else some error code.
 */
int url_mount_really(url_t *url, char *device, char *dir)
{
  int err = -1;

  slist_t *options_sl = slist_getentry(url->query, "options");
  char *options = options_sl ? options_sl->value : NULL;

  if(!url->is.mountable) return err;

  if(url->scheme >= inst_extern) {
    // run external script
    char *cmd = NULL;
    char *zypp_file = "/tmp/zypp.url";

    strprintf(&cmd, "/scripts/url/%s/mount", url_scheme2name(url->scheme));

    if(device) setenv("url_device", device, 1);
    if(dir) setenv("url_dir", dir, 1);
    if(options) setenv("url_options", options, 1);
    if(url->server) setenv("url_server", url->server, 1);
    if(url->path) setenv("url_path", url->path, 1);
    if(url->user) setenv("url_user", url->user, 1);
    if(url->password) setenv("url_password", url->password, 1);

    // we don't need rewritten urls for 'extend'
    if(!config.extend_running) setenv("url_zypp", zypp_file, 1);

    err = lxrc_run(cmd);

    if(!err) {
      char *s = util_get_attr(zypp_file);
      if(*s) {
        str_copy(&url->rewrite_for_zypp, s);
        log_info("url for zypp: %s", url->rewrite_for_zypp);
      }
    }

    unlink(zypp_file);

    unsetenv("url_device");
    unsetenv("url_dir");
    unsetenv("url_options");
    unsetenv("url_server");
    unsetenv("url_path");
    unsetenv("url_rewrite");
    unsetenv("url_user");
    unsetenv("url_password");
    unsetenv("url_zypp");

    str_copy(&cmd, NULL);
  }
  else {
    if(!url->is.network) {
      err = util_mount_ro(device, dir, url->file_list);
    }
    else {
      switch(url->scheme) {
        case inst_nfs:
          err = net_mount_nfs(dir, url->server, device, url->port, options);
          break;

        case inst_smb:
          err = net_mount_cifs(dir, url->server, device, url->user, url->password, url->domain, options);
          break;

        default:
          log_info("%s: unsupported scheme\n", url_scheme2name(url->scheme));
          break;
      }
    }
  }

  log_info("%s: %s -> %s (%d)\n", url_scheme2name(url->scheme), device ?: "", dir, err);

  return err;
}


/*
 * Mount url to dir; if dir is NULL, assign temporary mountpoint.
 *
 * If test_func() is set it must return:
 *   0: failed
 *   1: ok
 *   2: ok, but continue search
 *
 * url->used.device must be set
 *
 * return:
 *   0: failed
 *   1: ok
 *   2: ok, but continue search
 *
 * *** Note: inverse return value compared to url_mount(). ***
 */
int url_mount_disk(url_t *url, char *dir, int (*test_func)(url_t *))
{
  int ok = 0, file_type, err = 0;
  char *path = NULL, *buf = NULL, *s;
  url_t *tmp_url;

  log_info("url mount: trying %s (path = %s)\n", url_print(url, 0), url->path ?: "");
  if(url->used.model) log_info("(%s)\n", url->used.model);

  if(
    !url ||
    !url->scheme ||
    !url->path ||
    !(url->used.device || url->is.nodevneeded)
  ) return 0;

  url_umount(url);
  str_copy(&url->tmp_mount, NULL);
  str_copy(&url->mount, NULL);

  if(!url_setup_device(url)) return 0;

  if(!url->is.network) {
    /* local device */

    /* we might need an extra mountpoint */
    if(
      (url->scheme != inst_file && strcmp(url->path, "/")) ||
      url->scheme >= inst_extern
    ) {
      str_copy(&url->tmp_mount, new_mountpoint());
      ok = url_mount_really(url, url->used.device, url->tmp_mount) ? 0 : 1;

      if(!ok) {
        log_info("disk: %s: mount failed\n", url->used.device);
        str_copy(&url->tmp_mount, NULL);

        return ok;
      }
    }

    if(url->scheme == inst_file) {
      str_copy(&path, url->path);
    }
    else if(url->tmp_mount) {
      strprintf(&path, "%s%s", url->tmp_mount, url->path);
    }
    else {
      str_copy(&path, url->used.device);
    }
  }
  else {
    /* network device */

    switch(url->scheme) {
      case inst_nfs:
        str_copy(&url->mount, dir ?: new_mountpoint());

        log_debug("[server = %s]\n", url->server ?: "");

        if(!url->is.file) {
          err = url_mount_really(url, url->path, url->mount);
        }
        else {
          log_info("nfs: %s: is file, mounting one level up\n", url->path);
        }

        if(err || url->is.file) {
          str_copy(&url->mount, NULL);
          str_copy(&buf, url->path);

          if((s = strrchr(buf, '/')) && s != buf && s[1]) {
            *s++ = 0;
            str_copy(&url->tmp_mount, new_mountpoint());

            log_debug("[server = %s]\n", url->server ?: "");

            err = url_mount_really(url, buf, url->tmp_mount);

            if(err) {
              log_info("nfs: %s: mount failed\n", url->used.device);
              str_copy(&url->tmp_mount, NULL);
            }
            else {
              strprintf(&path, "%s/%s", url->tmp_mount, s);
            }
          }

          str_copy(&buf, NULL);
        }
        else if(!err) {
          str_copy(&path, url->mount);
        }
        break;

      case inst_smb:
        if(strcmp(url->path, "/")) {
          str_copy(&url->tmp_mount, new_mountpoint());
          s = url->tmp_mount;
        }
        else {
          str_copy(&url->mount, dir ?: new_mountpoint());
          s = url->mount;
        }

        log_debug("[server = %s]\n", url->server ?: "");

        err = url_mount_really(url, url->share, s);

        if(err) {
          log_info("cifs: %s: mount failed\n", url->used.device);
          str_copy(&url->tmp_mount, NULL);
          str_copy(&url->mount, NULL);
        }
        else {
          if(url->mount) {
            str_copy(&path, url->mount);
          }
          else {
            strprintf(&path, "%s%s", url->tmp_mount, url->path);
          }
        }
        break;

      case inst_http:
      case inst_https:
      case inst_ftp:
      case inst_tftp:
        break;

      default:
        if(url_is_mountable(url->scheme)) {
          str_copy(&url->tmp_mount, new_mountpoint());
          err = url_mount_really(url, url->used.device, url->tmp_mount);

          if(err) {
            log_info("disk: %s: mount failed\n", url->used.device);
            str_copy(&url->tmp_mount, NULL);
          }
          else {
            strprintf(&path, "%s%s", url->tmp_mount, url->path);
          }
        }
        else {
          log_info("%s: unsupported scheme\n", url_scheme2name(url->scheme));
          err = 1;
        }
        break;
    }
  }

  if(!err) {
    if(url->is.mountable) {
      file_type = util_check_exist(path);

      url->is.file = file_type == 'r' ? 1 : 0;

      if(file_type) {
        if(
          (file_type == 'r' || file_type == 'b') &&
          (url->download || !util_is_mountable(path))
        ) {

          str_copy(&url->mount, dir ?: new_mountpoint());

          tmp_url = url_set("file:/");

          ok = url_read_file(tmp_url,
            NULL,
            path,
            s = strdup(new_download()),
            NULL,
            URL_FLAG_PROGRESS + URL_FLAG_UNZIP + URL_FLAG_NODIGEST
          ) ? 0 : 1;

          if(ok) ok = util_mount_ro(s, url->mount, url->file_list) ? 0 : 1;
          if(!ok) unlink(s);

          free(s);
          url_free(tmp_url);
        }
        else {
          if(!url->mount) {
            str_copy(&url->mount, dir ?: new_mountpoint());
            ok = util_mount_ro(path, url->mount, url->file_list) ? 0 : 1;
          }
          else {
            ok = 1;
          }
        }
      }
      else {
        ok = 0;
      }
    }
    else {
      ok = 1;
    }
  }

  if(ok && test_func && !(ok = test_func(url))) {
    log_info("disk: mount ok but test failed\n");
  }

  if(!ok) {
    log_info("url mount: %s failed\n", url_print(url, 0));

    util_umount(url->mount);
    util_umount(url->tmp_mount);

    str_copy(&url->tmp_mount, NULL);
    str_copy(&url->mount, NULL);
  }
  else {
    log_info("url mount: %s", url_print(url, 0));
    if(url->mount) log_info(" @ %s", url->mount);
    log_info("\n");
  }

  str_copy(&path, NULL);

  return ok;
}


/*
 * Mount url to dir; if dir is NULL, assign temporary mountpoint.
 *
 * If url->used.device is not set, try all appropriate devices.
 *
 * return:
 *   0: ok
 *   1: failed
 *   sets url->used.device, url->mount, url->tmp_mount (if ok)
 *
 * *** Note: inverse return value compared to url_mount_disk(). ***
 */
int url_mount(url_t *url, char *dir, int (*test_func)(url_t *))
{
  int err = 0, ok, found, matched;
  hd_t *hd;
  hd_res_t *res;
  char *hwaddr;
  hd_hw_item_t hw_items[3] = { hw_network_ctrl, hw_network, 0 };
  str_list_t *sl;
  char *url_device = NULL;

  if(!url || !url->scheme) return 1;

  update_device_list(0);

  if(!config.hd_data) return 1;

  // we either don't need to setup/select a device or have already done so
  if(url->is.nodevneeded || url->used.device) {
    return url_mount_disk(url, dir, test_func) ? 0 : 1;
  }

  if(!url->is.network) {
    switch(url->scheme) {
      case inst_cdrom:
        hw_items[0] = hw_cdrom;
        break;

      case inst_floppy:
        hw_items[0] = hw_floppy;
        break;

      default:
        hw_items[0] = hw_block;
        break;
    }
    hw_items[1] = 0;
  }

  str_copy(&url_device, url->device);
  if(!url_device) str_copy(&url_device, url->is.network ? config.ifcfg.manual->device : config.device);

  config.lock_device_list++;

  for(found = 0, hd = sort_a_bit(fix_device_names(hd_list2(config.hd_data, hw_items, 0))); hd; hd = hd->next) {
    for(hwaddr = NULL, res = hd->res; res; res = res->next) {
      if(res->any.type == res_hwaddr) {
        hwaddr = res->hwaddr.addr;
        break;
      }
    }

    if(
      (	/* hd: neither floppy nor cdrom */
        url->scheme == inst_hd &&
        (
          hd_is_hw_class(hd, hw_floppy) ||
          hd_is_hw_class(hd, hw_cdrom)
        )
      ) ||
      (hd_is_hw_class(hd, hw_block) && hd->child_ids && hd->child_ids->next) ||	/* skip whole block device if it has > 1 partition */
      !hd->unix_dev_name
    ) continue;

    matched = url_device ? match_netdevice(short_dev(hd->unix_dev_name), hwaddr, url_device) : 1;

    for(sl = hd->unix_dev_names; !matched && sl; sl = sl->next) {
      matched = match_netdevice(short_dev(sl->str), NULL, url_device);
    }

    if(!matched) continue;

    str_copy(&url->used.unique_id, hd->unique_id);
    str_copy(&url->used.device, hd->unix_dev_name);
    str_copy(&url->used.hwaddr, hwaddr);

    if(hd->model && !strcmp(hd->model, "Partition")) {
      strprintf(&url->used.model, "%s: %s", hd->model, blk_ident(url->used.device));
    }
    else {
      str_copy(&url->used.model, hd->model);
    }

    if(hd->is.wlan) util_set_wlan(hd->unix_dev_name);

    if((ok = url_mount_disk(url, dir, test_func))) {
      found++;
      if(hd_is_hw_class(hd, hw_cdrom)) url->is.cdrom = 1;
      if(ok == 1 || config.sig_failed || config.digests.failed) break;
    }
    else {
      err = 1;
    }
  }

  config.lock_device_list--;

  if(!found) {
    log_info("device not found (err = %d): %s\n", err, url_device ?: "");
  }

  /*
   * should not happen, but anyway: device name was not in our list
   *
   * - not for network interfaces (bnc #429518)
   * - Maybe drop code completely?
   */
  if(!err && !found && !url->used.device && url_device && !url->is.network) {
    str_copy(&url->used.device, long_dev(url_device));
    str_copy(&url->used.model, NULL);
    str_copy(&url->used.hwaddr, NULL);
    str_copy(&url->used.unique_id, NULL);
    err = url_mount_disk(url, dir, test_func) ? 0 : 1;
    if(!err) found = 1;
  }

  if(!found) {
    str_copy(&url->used.device, NULL);
    str_copy(&url->used.model, NULL);
    str_copy(&url->used.hwaddr, NULL);
    str_copy(&url->used.unique_id, NULL);
  }

  str_copy(&url_device, NULL);

  return found ? 0 : 1;
}


/*
 * Warn if signature check failed and ask user what to do.
 *
 * Return 0 if it's ok to continue or 1 if we should report en error.
 */
int warn_signature_failed(char *file_name)
{
  int i, win, err = 0;
  char *buf = NULL;

  if(config.sig_failed && config.secure_always_fail) {
    log_info("%s: file not signed or invalid signature\n", file_name);
    return 1;
  }

  if(config.sig_failed) {
    strprintf(&buf,
      "%s: %s\n\n%s",
      file_name,
      config.sig_failed == 1 ? "File not signed." : "Invalid signature.",
      config.sig_failed == 1 ? "If you really trust your repository, you may continue in an insecure mode." : "Installation aborted."
    );
    if(!(win = config.win)) util_disp_init();
    if(config.sig_failed == 1) {
      i = dia_okcancel(buf, NO);
    }
    else {
      dia_message(buf, MSGTYPE_ERROR);
      i = NO;
    }
    if(!win) util_disp_done();
    if(i == YES) {
      config.secure = 0;
      config.sig_failed = 0;
      err = 0;
    }
    else {
      err = 1;
    }
  }

  str_copy(&buf, NULL);

  return err;
}


/*
  Test if 'file' is a gpg signed file.
  If so, unpack it (replacing 'file') and verify signature.

  Return values:
    -1: file or gpg not found
     0: file gpg format, sig ok
     1: file gpg format, sig wrong
     2: file not gpg format
*/
int is_gpg_signed(char *file)
{
  char *cmd = NULL, *buf = NULL;
  int err = -1, is_sig = 0, sig_ok = 0;
  size_t len = 0;
  FILE *f;

  if(util_check_exist(file) != 'r') {
    log_debug("%s: gpg check = %d\n", file, err);

    return err;
  }

  strprintf(&cmd,
    "gpg --homedir /root/.gnupg --batch --no-default-keyring --keyring /installkey.gpg "
    "--ignore-valid-from --ignore-time-conflict --output '%s.unpacked' '%s' 2>&1",
    file,
    file
  );

  if((f = popen(cmd, "r"))) {
    while(getline(&buf, &len, f) > 0) {
      if(config.debug >= 2) log_debug("%s", buf);
      if(strncmp(buf, "gpg: Signature made", sizeof "gpg: Signature made" - 1)) is_sig = 1;
      if(strncmp(buf, "gpg: Good signature", sizeof "gpg: Good signature" - 1)) sig_ok = 1;
    }
    err = pclose(f) ? 1 : 0;
    if(config.debug >= 2) log_debug("gpg returned %s\n", err ? "an error" : "ok");
  }

  strprintf(&buf, "%s.unpacked", file);

  if(is_sig && rename(buf, file)) is_sig = 0;

  unlink(buf);

  str_copy(&cmd, NULL);
  free(buf);

  if(err != -1) {
    if(is_sig) {
      err = !err && sig_ok ? 0 : 1;
    }
    else {
      err = 2;
    }
  }

  if(err == 0 || err == 1) {
    log_info("%s: gpg signature %s\n", file, err ? "failed" : "ok");
  }

  log_debug("%s: gpg check = %d\n", file, err);

  return err;
}


/*
  Test if 'file' is a signed rpm.
  If so, verify signature.

  Return values:
    -1: file or 'rpmkeys' not found
     0: file rpm format, sig ok
     1: file rpm format, sig wrong
     2: file not rpm format or not signed
*/
int is_rpm_signed(char *file)
{
  char *cmd = NULL, *buf = NULL;
  int err = -1, is_sig = 0, sig_ok = 0;
  size_t len = 0;
  FILE *f;

  if(util_check_exist(file) != 'r') {
    log_debug("%s: rpm sig check = %d\n", file, err);

    return err;
  }

  char *type = util_fstype(file, NULL);
  if(!type || strcmp(type, "rpm")) return 2;

  strprintf(&cmd, "rpmkeys --checksig --define '%%_keyringpath /pubkeys' '%s' 2>&1", file);

  if((f = popen(cmd, "r"))) {
    while(getline(&buf, &len, f) > 0) {
      char *s = strrchr(buf, ':') ?: buf;

      if(config.debug >= 2) log_debug("%s", buf);

      if(strcasestr(s, " pgp ") || strcasestr(s, " gpg ")) is_sig = 1;
      if(strstr(s, " pgp ") || strstr(s, " gpg ")) sig_ok = 1;
    }
    err = pclose(f) ? 1 : 0;
    if(config.debug >= 2) log_debug("rpmkeys returned %s\n", err ? "an error" : "ok");
  }

  str_copy(&cmd, NULL);
  free(buf);

  if(err != -1) {
    if(is_sig) {
      err = !err && sig_ok ? 0 : 1;
    }
    else {
      err = 2;
    }
  }

  if(err == 0 || err == 1) {
    log_info("%s: rpm signature %s\n", file, err ? "failed" : "ok");
  }

  log_debug("%s: rpm sig check = %d\n", file, err);

  return err;
}


/*
  Test if 'file' is a (non-detached) signed file.
  Verify signature and, if necessary (gpg), unpack it,
  replacing original 'file'.

  If 'check' is set, update config.sig_failed and show warning to user.

  Return values:
    -1: file or checking command not found
     0: file has signature, sig ok
     1: file has signature, sig wrong
     2: file not signed
*/
int is_signed(char *file, int check)
{
  int err;

  // first, maybe it's an rpm
  err = is_rpm_signed(file);

  // if not, maybe gpg signed
  if(!(err == 0 || err == 1)) err = is_gpg_signed(file);

  if(check && config.secure && err == 1) {
    config.sig_failed = 2;
    err = warn_signature_failed(file);
  }

  log_debug("%s: sig check = %d\n", file, err);

  return err;
}


/*
 * Read file 'src' relative to 'url' and write it to 'dst'. If 'dir' is set,
 * mount 'url' at 'dir' if necessary.
 *
 * Note: does *NOT* modify url->path.
 *
 * return:
 *   0: ok
 *   1: failed
 *
 * This function also sets config.sig_failed:
 *   0: signature ok or config.secure == 0
 *   1: no signature
 *   2: wrong signature
 */
int url_read_file(url_t *url, char *dir, char *src, char *dst, char *label, unsigned flags)
{
  int err, gpg;
  char *src_sig = NULL, *dst_sig = NULL, *buf = NULL, *old_path = NULL, *s;

  str_copy(&old_path, url->path);

  if(!(flags & URL_FLAG_CHECK_SIG)) {
    err = url_read_file_nosig(url, dir, src, dst, label, flags);
    str_copy(&url->path, old_path);
    free(old_path);

    return err;
  }

  flags |= URL_FLAG_NODIGEST;

  err = url_read_file_nosig(url, dir, src, dst, label, flags);
  str_copy(&url->path, old_path);

  if(err) {
    free(old_path);
    return err;
  }

  config.sig_failed = 0;

  if(!config.secure) {
    is_signed(dst, 0);
    free(old_path);
    return err;
  }

  gpg = is_signed(dst, 1);

  if(gpg != 2) {
    free(old_path);
    return gpg ? 1 : 0;
  }

  config.sig_failed = 1;

  if(!(src || (url && url->path)) || !dst) {
    free(old_path);
    return err;
  }

  if(src) {
    strprintf(&src_sig, "%s.asc", src);
  }
  else {
    strprintf(&url->path, "%s.asc", old_path);
  }
  strprintf(&dst_sig, "%s.asc", dst);
  strprintf(&buf,
    "gpg --homedir /root/.gnupg --batch --no-default-keyring --keyring /installkey.gpg --ignore-valid-from --ignore-time-conflict --verify '%s' '%s'",
    dst_sig, dst
  );

  err = url_read_file_nosig(url, dir, src_sig, dst_sig, NULL, flags);
  str_copy(&url->path, old_path);

  s = url_print2(url, src);

  if(!err) {
    if(lxrc_run(buf)) {
      log_info("%s: signature check failed\n", s);
      config.sig_failed = 2;
    }
    else {
      log_info("%s: signature ok\n", s);
      config.sig_failed = 0;
    }
  }
  else {
    log_info("%s: no signature\n", s);
  }

  err = warn_signature_failed(s);

  free(buf);
  free(dst_sig);
  free(src_sig);
  free(old_path);

  return err;
}

static char *tc_src, *tc_dst;
static int real_err = 0;
static int keep_mounted = 0;
static unsigned int tc_flags;
static char *tc_label;

static int test_and_copy(url_t *url)
{
  int ok = 0, new_url = 0, i, win;
  char *old_path, *buf = NULL;
  url_data_t *url_data;

  if(!url) return 0;

  if(url->is.mountable && url->scheme != inst_file) {
    if(!url->mount) return 0;
    ok = util_check_exist2(url->mount, tc_src) == 'r' ? 1 : 0;
    if(!ok) {
      real_err = 1;

      return keep_mounted ? 1 : 0;
    }
    strprintf(&buf, "file:%s", url->mount);
    url = url_set(buf);
    new_url = 1;
  }

  url_data = url_data_new();

  old_path = url->path;
  url->path = NULL;

  /* there is probably an easier way... */
  i = strlen(old_path);
  strprintf(&url->path, "%s%s%s",
    old_path,
    (i && old_path[i - 1] == '/') || !*old_path || !*tc_src || *tc_src == '/' ? "" : "/",
    strcmp(tc_src, "/") ? tc_src : ""
  );
  if(url->path[0] == '/' && url->path[1] == '/') str_copy(&url->path, url->path + 1);

  if(config.debug >= 3) log_debug("path: \"%s\" + \"%s\" = \"%s\"\n", old_path, tc_src, url->path);

  str_copy(&buf, url_print(url, 1));
  url_data->url = url_set(buf);

  free(url->path);
  url->path = old_path;

  url_data->file_name = strdup(tc_dst);

  if((tc_flags & URL_FLAG_OPTIONAL)) url_data->optional = 1;
  if((tc_flags & URL_FLAG_UNZIP)) url_data->unzip = 1;
  if((tc_flags & URL_FLAG_PROGRESS)) url_data->progress = url_progress;
  str_copy(&url_data->label, tc_label);

  log_info("loading %s -> %s\n", url_print(url_data->url, 0), url_data->file_name);

  url_read(url_data);

  if(url_data->err) {
    log_info("error %d: %s%s\n", url_data->err, url_data->err_buf, url_data->optional ? " (ignored)" : "");
  }
  else {
    ok = 1;
    if(config.secure) {
      digests_log(url_data);

      if((tc_flags & URL_FLAG_NODIGEST)) {
        log_info("digest not checked\n");
      }
      else {
        if(digests_verify(url_data, url_data->url->path)) {
          log_info("digest ok\n");
        }
        else {
          log_info("digest check failed\n");
          config.digests.failed = 1;
          if(config.secure_always_fail) {
            ok = 0;
          }
          else {
            strprintf(&buf,
              "%s: %s\n\n%s",
              url_print2(url_data->url, NULL),
              "Digest verification failed.",
              "If you really trust your repository, you may continue in an insecure mode."
            );
            if(!(win = config.win)) util_disp_init();
            i = dia_okcancel(buf, NO);
            if(!win) util_disp_done();
            if(i == YES) {
              config.secure = 0;
              config.digests.failed = 0;
            }
            else {
              ok = 0;
            }
          }
        }
      }
    }
  }

  str_copy(&buf, NULL);

  if(new_url) url_free(url);

  url_data_free(url_data);

  return ok;
}

/*
 * Parameters as for url_read_file().
 *
 * return:
 *   0: ok
 *   1: failed
 */
int url_read_file_nosig(url_t *url, char *dir, char *src, char *dst, char *label, unsigned flags)
{
  int err = 0, free_src = 0;
  char *buf1 = NULL, *s, *t;

  // don't assign tc_src yet, src may get modified
  tc_dst = dst;
  tc_flags = flags;
  tc_label = label;

  if(!dst || !url->scheme) return 1;
  if(!(flags & URL_FLAG_NOUNLINK)) unlink(dst);

  /* create missing directories */
  str_copy(&buf1, dst);
  for(s = buf1; (t = strchr(s, '/')) && !err; s = t + 1) {
    *t = 0;
    if(*buf1 && util_check_exist(buf1) != 'd') err = mkdir(buf1, 0755);
    *t = '/';
  }
  str_copy(&buf1, NULL);

  if(err) {
    log_info("url read: %s: failed to create directories\n", dst);

    return 1;
  }

  if(!src && url->mount) return 1;

  if(!src) {
    if(url->scheme == inst_nfs) {
      s = strrchr(url->path, '/');
      if(!s) return 1;
      str_copy(&src, s + 1);
      *s = 0;
    }
    else {
      str_copy(&src, url->path);
      str_copy(&url->path, url->is.mountable ? "/" : "");
    }
    free_src = 1;
  }

  tc_src = src;

  // it is expected that tc_src holds a non-NULL pointer
  // (else there's no source to read from)
  if(!tc_src) return 1;

  if(url->mount) {
    strprintf(&buf1, "file:%s", url->mount);
    url = url_set(buf1);
    err = test_and_copy(url) ? 0 : 1;
    str_copy(&buf1, NULL);
    url_free(url);
  }
  else {
    if(url->is.mountable && url->scheme != inst_file) {
      keep_mounted = flags & URL_FLAG_KEEP_MOUNTED;
      err = url_mount(url, dir, test_and_copy);
      if(keep_mounted && !err) err = real_err;
    }
    else {
      err = test_and_copy(url) ? 0 : 1;
    }
  }

  if(free_src) str_copy(&src, NULL);

  return err;
}


/*
 * Like url_read_file() but setup network if necessary.
 *
 * return:
 *   0: ok
 *   1: failed
 */
int url_read_file_anywhere(url_t *url, char *dir, char *src, char *dst, char *label, unsigned flags)
{
  int err, found, matched;
  hd_t *hd;
  hd_res_t *res;
  char *hwaddr;
  str_list_t *sl;
  char *url_device = NULL;
  hd_hw_item_t hw_items[] = { hw_network_ctrl, hw_network, 0 };

  if(!url || !url->is.network || config.ifcfg.if_up) return url_read_file(url, dir, src, dst, label, flags);

  LXRC_WAIT

#if defined(__s390__) || defined(__s390x__)
  net_activate_s390_devs();
#endif

  update_device_list(0);

  LXRC_WAIT

  if(config.hd_data) {
    str_copy(&url_device, url->device ?: config.ifcfg.manual->device);

    config.lock_device_list++;

    for(found = 0, hd = sort_a_bit(hd_list2(config.hd_data, hw_items, 0)); hd; hd = hd->next) {
      for(hwaddr = NULL, res = hd->res; res; res = res->next) {
        if(res->any.type == res_hwaddr) {
          hwaddr = res->hwaddr.addr;
          break;
        }
      }

      if(!hd->unix_dev_name) continue;

      matched = url_device ? match_netdevice(short_dev(hd->unix_dev_name), hwaddr, url_device) : 1;

      for(sl = hd->unix_dev_names; !matched && sl; sl = sl->next) {
        matched = match_netdevice(short_dev(sl->str), NULL, url_device);
      }

      if(!matched) continue;

      str_copy(&url->used.unique_id, hd->unique_id);
      str_copy(&url->used.device, hd->unix_dev_name);
      str_copy(&url->used.hwaddr, hwaddr);
      str_copy(&url->used.model, hd->model);

      if(hd->is.wlan) util_set_wlan(hd->unix_dev_name);

      url_setup_device(url);

      if(!url_read_file(url, dir, src, dst, label, flags)) {
        found++;
        break;
      }
      if(config.sig_failed || config.digests.failed) break;
    }

    config.lock_device_list--;

    if(!found) {
      str_copy(&url->used.device, NULL);
      str_copy(&url->used.model, NULL);
      str_copy(&url->used.hwaddr, NULL);
      str_copy(&url->used.unique_id, NULL);
    }

    str_copy(&url_device, NULL);

    LXRC_WAIT

    return found ? 0 : 1;
  }

  if(url->used.device) url_setup_device(url);

  err = url_read_file(url, dir, src, dst, label, flags);

  LXRC_WAIT

  return err;
}


/*
 * 0: failed, 1: ok, 2: ok but continue search
 */
static int test_is_repo(url_t *url)
{
  int ok = 0, i, opt, copy, parts, part;
  char *buf = NULL, *buf2 = NULL, *file_name, *s, *t;
  char *instsys_config;
  slist_t *sl, *file_list, *old_file_list;
  FILE *f;

  if(
    !url ||
    (!url->mount && url->is.mountable) ||
    !config.url.instsys ||
    !config.url.instsys->scheme
  ) return 0;

  if(!config.keepinstsysconfig) {
    config.digests.failed = 0;

    // Check for '/content' (SUSE tags repo), '/repodata/repomd.xml' (RPM-MD repo)
    // or '/media.1/products' (multi-repository medium) as indication we have a SUSE repo.
    // The file must be validly signed (because we parse it).
    // zenworks has a different approach ('settings.txt') - they don't have a repo.

    strprintf(&buf, "/%s", config.zen ? config.zenconfig : "content");
    strprintf(&buf2, "file:%s", buf);

    config.repomd = 0;
    config.repomd_data = slist_free(config.repomd_data);

    if(
      url_read_file(url, NULL, buf, buf, NULL,
        URL_FLAG_NODIGEST + (config.secure ? URL_FLAG_CHECK_SIG : 0)
      )
    ) {
      if(config.zen) return 0;

      config.repomd = 1;

      if(!config.norepo) {
        // no content file -> download repomd.xml
        int read_failed = url_read_file(
          url, NULL, "/repodata/repomd.xml", "/repomd.xml", NULL,
          URL_FLAG_NODIGEST + (config.secure ? URL_FLAG_CHECK_SIG : 0)
        );

        if(read_failed) {
          // no repomd.xml, check if it is a multi-repository medium,
          // do not check the signatures, that file is not signed
          read_failed = url_read_file(
            url, NULL, "/media.1/products", "/products", NULL, URL_FLAG_NODIGEST
          );

          if(read_failed)
            return 0;
          else
            log_info("found a multi product medium\n");
        }
        else
          file_parse_repomd("/repomd.xml");
      }

      // download CHECKSUMS ...
      int read_failed = url_read_file(
        url, NULL, "/CHECKSUMS", "/CHECKSUMS", NULL,
        URL_FLAG_NODIGEST + (config.secure ? URL_FLAG_CHECK_SIG : 0)
      );

      if(read_failed && config.norepo) return 0;

      // ... and parse it
      if(!read_failed) file_parse_checksums("/CHECKSUMS");
    }

    if(!config.sig_failed && util_check_exist(buf)) {
      file_read_info_file(buf2, config.zen ? kf_cont + kf_cfg : kf_cont);
    }

    str_copy(&buf, NULL);
    str_copy(&buf2, NULL);
  }

  if(config.url.instsys->scheme != inst_rel || config.kexec == 1) return 1;

  if(!config.keepinstsysconfig) {
    instsys_config = url_instsys_config(config.url.instsys->path);

    file_name = NULL;

    if(url->is.mountable) {
      if(util_check_exist2(url->mount, instsys_config)) {
        strprintf(&file_name, "%s/%s", url->mount, instsys_config);
      }
    }
    else {
      if(url_read_file(url,
        NULL,
        instsys_config,
        file_name = strdup(new_download()),
        NULL,
        0
      )) {
        free(file_name);
        file_name = NULL;
        if(config.digests.failed) return 1;
      }
    }

    url_parse_instsys_config(file_name);

    free(file_name);
  }

  url_build_instsys_list(config.url.instsys->path, 1);

  if(url->is.mountable) {
    for(sl = config.url.instsys_list; sl; sl = sl->next) {
      opt = *(s = sl->key) == '?' && s++;
      t = url_config_get_path(s);

      if(!util_check_exist2(url->mount, t)) {
        if(opt) {
          log_info("instsys missing: %s (optional)\n", t);
        }
        else {
          log_info("instsys missing: %s\n", t);
          free(t);
          return 0;
        }
      }
      free(t);
    }
  }

  for(parts = 0, sl = config.url.instsys_list; sl; sl = sl->next) parts++;

  for(ok = 1, part = 1, sl = config.url.instsys_list; ok && sl; sl = sl->next, part++) {
    opt = *(s = sl->key) == '?' && s++;
    copy = strstr(s, "?copy=1") ? 1 : 0;
    t = url_config_get_path(s);
    file_list = url_config_get_file_list(s);

    old_file_list = url->file_list;
    url->file_list = file_list;

    if(url->is.mountable) strprintf(&buf, "%s/%s", url->mount, t);

    // sl->value = strdup(parts > 1 ? new_mountpoint() : config.mountpoint.instsys);
    sl->value = strdup(new_mountpoint());

    if((f = fopen("/etc/instsys.parts", "a"))) {
      fprintf(f, "%s %s\n", s, sl->value);
      fclose(f);
    }

    if(
      url->is.mountable &&
      (copy || util_is_mountable(buf) || !util_check_exist(buf)) &&
      !config.rescue &&
      (!config.download.instsys || util_check_exist(buf) == 'd')
    ) {
      if(!util_check_exist(buf) && opt) {
        log_info("%s %s -> %s failed (ignored)\n", copy ? "copy" : "mount", buf, sl->value);
      }
      else {
        if(copy) {
          char *dst = strrchr(t, '/') ?: t;
          log_info("copy %s -> %s\n", buf, dst);
          i = !util_cp_main(3, (char *[]) {0, buf, dst});
          ok &= i;
          if(!i) log_info("adding %s to instsys failed\n", dst);
        }
        else {
          log_info("mount %s -> %s\n", buf, sl->value);
          i = util_mount_ro(buf, sl->value, url->file_list) ? 0 : 1;
          ok &= i;
          if(!i) log_info("instsys mount failed: %s\n", sl->value);
        }
      }
    }
    else {
      if(parts > 1) {
        strprintf(&buf2, "%s (%d/%d)",
          config.rescue ? "Loading Rescue System" : "Loading Installation System", part, parts
        );
      }
      else {
        str_copy(&buf2, config.rescue ? "Loading Rescue System" : "Loading Installation System");
      }

      if(!url_read_file(url,
        NULL,
        t,
        file_name = strdup(new_download()),
        buf2,
        URL_FLAG_PROGRESS + URL_FLAG_UNZIP + opt * URL_FLAG_OPTIONAL
      )) {
        if(copy) {
          char *dst = strrchr(t, '/') ?: t;
          log_info("mv %s -> %s\n", file_name, dst);
          i = !rename(file_name, dst);
          ok &= i;
          if(!i) log_info("adding %s to instsys failed\n", dst);
        }
        else {
          log_info("mount %s -> %s\n", file_name, sl->value);
          i = util_mount_ro(file_name, sl->value, url->file_list) ? 0 : 1;
          ok &= i;
          if(!i) log_info("instsys mount failed: %s\n", sl->value);
        }
      }
      else {
        log_info("download failed: %s%s\n", sl->value, opt ? " (ignored)" : "");
        if(!opt) ok = 0;
      }

      free(file_name);
    }

    url->file_list = old_file_list;
    slist_free(file_list);
    free(t);
  }

  if(ok) {
    str_copy(&config.url.instsys->mount, config.mountpoint.instsys);
    mkdir(config.url.instsys->mount, 0755);
  }

  str_copy(&buf, NULL);
  str_copy(&buf2, NULL);

  return ok;
}
/*
 * Find repository (and mount at 'dir' if possbile).
 * Mount instsys, too, if it is a relative url.
 *
 * return:
 *   0: ok
 *   1: failed
 */
int url_find_repo(url_t *url, char *dir)
{
  int err = 0;

  log_info("repository: looking for %s\n", url_print(url, 0));

  err = url_mount(url, dir, test_is_repo);

  if(err) {
    log_info("repository: not found\n");
  }
  else {
    log_info("repository: using %s", url_print(url, 0));
    if(url->mount) log_info(" @ %s", url->mount);
    log_info("\n");
  }

  return err;
}


/*
 * Find instsys (and mount at 'dir' if possbile).
 *
 * return:
 *   0: ok
 *   1: failed
 */
int url_find_instsys(url_t *url, char *dir)
{
  int opt, copy, part, parts, ok, i;
  char *s, *t;
  char *file_name = NULL, *buf = NULL, *buf2 = NULL, *url_path = NULL;
  slist_t *sl, *file_list, *old_file_list;
  FILE *f;

  if(
    !url ||
    !url->scheme ||
    url->scheme == inst_rel ||
    !url->path
  ) return 1;

  if(config.download.instsys || config.rescue) url->download = 1;

  str_copy(&url_path, url->path);

  str_copy(&url->path, url_instsys_base(url->path));

  if(!config.keepinstsysconfig) {
    if(
      url_read_file(url,
        NULL,
        "/config",
        file_name = strdup(new_download()),
        NULL,
        URL_FLAG_KEEP_MOUNTED
      )
    ) {
      // log_info("XXX %d %s\n", url->is.mountable, url->mount);
      if(!(url->is.mountable && url->mount)) {
        str_copy(&url->path, url_path);
      }

      str_copy(&file_name, NULL);
    }

    url_parse_instsys_config(file_name);
  }

  str_copy(&file_name, NULL);

  s = url_path + strlen(url->path);
  if(*s == '/') s++;
  url_build_instsys_list(s, 1);

  ok = 1;

  if(url->is.mountable && url->mount) {
    for(sl = config.url.instsys_list; sl; sl = sl->next) {
      opt = *(s = sl->key) == '?' && s++;
      t = url_config_get_path(s);

      if(!util_check_exist2(url->mount, t)) {
        if(opt) {
          log_info("instsys missing: %s (optional)\n", t);
        }
        else {
          log_info("instsys missing: %s\n", t);
          ok = 0;

          break;
        }
      }

      free(t);
    }
  }

  if(ok) {
    for(parts = 0, sl = config.url.instsys_list; sl; sl = sl->next) parts++;

    for(part = 1, sl = config.url.instsys_list; ok && sl; sl = sl->next, part++) {
      opt = *(s = sl->key) == '?' && s++;
      copy = strstr(s, "?copy=1") ? 1 : 0;
      t = url_config_get_path(s);
      file_list = url_config_get_file_list(s);

      old_file_list = url->file_list;
      url->file_list = file_list;

      if(url->is.mountable) strprintf(&buf, "%s/%s", url->mount, t);

      // sl->value = strdup(parts > 1 ? new_mountpoint() : config.mountpoint.instsys);
      sl->value = strdup(new_mountpoint());

      if((f = fopen("/etc/instsys.parts", "a"))) {
        fprintf(f, "%s %s\n", s, sl->value);
        fclose(f);
      }

      if(
        url->is.mountable &&
        (copy || util_is_mountable(buf) || !util_check_exist(buf)) &&
        !config.rescue &&
        (!config.download.instsys || util_check_exist(buf) == 'd')
      ) {
        if(!util_check_exist(buf) && opt) {
          log_info("mount %s -> %s failed (ignored)\n", buf, sl->value);
        }
        else {
          if(copy) {
            char *dst = strrchr(t, '/') ?: t;
            log_info("copy %s -> %s\n", buf, dst);
            i = !util_cp_main(3, (char *[]) {0, buf, dst});
            ok &= i;
            if(!i) log_info("adding %s to instsys failed\n", dst);
          }
          else {
            log_info("mount %s -> %s\n", buf, sl->value);
            i = util_mount_ro(buf, sl->value, url->file_list) ? 0 : 1;
            ok &= i;
            if(!i) log_info("instsys mount failed: %s\n", sl->value);
          }
        }
      }
      else {
        if(parts > 1) {
          strprintf(&buf2, "%s (%d/%d)",
            config.rescue ? "Loading Rescue System" : "Loading Installation System", part, parts
          );
        }
        else {
          str_copy(&buf2, config.rescue ? "Loading Rescue System" : "Loading Installation System");
        }

        if(!url_read_file(url,
          NULL,
          *t ? t : NULL,
          file_name = strdup(new_download()),
          buf2,
          URL_FLAG_PROGRESS + URL_FLAG_UNZIP + opt * URL_FLAG_OPTIONAL
        )) {
          if(copy) {
            char *dst = strrchr(t, '/') ?: t;
            log_info("mv %s -> %s\n", file_name, dst);
            i = !rename(file_name, dst);
            ok &= i;
            if(!i) log_info("adding %s to instsys failed\n", dst);
          }
          else {
            log_info("mount %s -> %s\n", file_name, sl->value);
            i = util_mount_ro(file_name, sl->value, url->file_list) ? 0 : 1;
            ok &= i;
            if(!i) log_info("instsys mount failed: %s\n", sl->value);
          }
        }
        else {
          log_info("download failed: %s%s\n", sl->value, opt ? " (ignored)" : "");
          if(!opt) ok = 0;
        }

        str_copy(&file_name, NULL);
      }

      url->file_list = old_file_list;
      slist_free(file_list);
      free(t);
    }
  }

  if(ok) {
    str_copy(&config.url.instsys->mount, config.mountpoint.instsys);
    mkdir(config.url.instsys->mount, 0755);
  }

  str_copy(&buf, NULL);
  str_copy(&buf2, NULL);

  str_copy(&url->path, NULL);
  url->path = url_path;

  return ok ? 0 : 1;
}


/*
 * Load fs module or setup network interface.
 *
 * return:
 *   0: failed
 *   1: ok
 */
int url_setup_device(url_t *url)
{
  int ok = 0;
  char *type;

  log_info("url_setup_device: %s\n", url_print(url, 0));

  if(!url) return 0;

  if(url->is.nodevneeded) return 1;

  if(!url->used.device) return 0;

  // log_info("*** url_setup_device(dev = %s)\n", url->used.device);

  if(!url->is.network) {
    /* load fs module if necessary */

    type = util_fstype(url->used.device, NULL);
    if(type && strcmp(type, "swap")) ok = 1;
  }
  else {
    ok = url_setup_interface(url);
    if(ok) {
      net_ask_password();	// FIXME: strange location to put it here...
      ok = url_setup_slp(url);
    }
  }

  return ok;
}


/*
 * Setup network interface.
 *
 * return:
 *   0: failed
 *   1: ok
 */
int url_setup_interface(url_t *url)
{
  // the interface has already been configured
  if(slist_getentry(config.ifcfg.if_up, url->used.device)) {
    log_info("setup_interface: %s already up\n", url->used.device);

    return 1;
  }

  if(
    !strncmp(url->used.device, "lo", sizeof "lo" - 1) ||
    !strncmp(url->used.device, "sit", sizeof "sit" - 1)
  ) {
    log_info("setup_interface: %s ignored\n", url->used.device);

    return 0;
  }

  net_stop();

  str_copy(&config.ifcfg.manual->device, url->used.device);

  log_info("interface setup: %s\n", net_get_ifname(config.ifcfg.manual));

  if((config.net.do_setup & DS_SETUP)) auto2_user_netconfig();

  if(!slist_getentry(config.ifcfg.if_up, net_get_ifname(config.ifcfg.manual))) {
    check_ptp(config.ifcfg.manual->device);

    if(config.ifcfg.manual->dhcp && !config.ifcfg.manual->ptp) {
      net_dhcp();
    }
    else {
      net_static();
    }
  }

  if(!slist_getentry(config.ifcfg.if_up, net_get_ifname(config.ifcfg.manual))) {
    log_info("%s network setup failed\n", net_get_ifname(config.ifcfg.manual));
    return 0;
  }
  else {
    log_info("%s activated\n", net_get_ifname(config.ifcfg.manual));
  }

  return 1;
}


/*
 * Get SLP data.
 *
 * return:
 *   0: failed
 *   1: ok
 */
int url_setup_slp(url_t *url)
{
  url_t *tmp_url;

  if(url->scheme == inst_slp) {
    tmp_url = url_set(slp_get_install(url));
    if(!tmp_url->scheme) {
      log_info("SLP failed\n");
      url_free(tmp_url);

      return 0;
    }

    url->scheme = tmp_url->scheme;
    url->port = tmp_url->port;
    str_copy(&url->str, tmp_url->str);
    str_copy(&url->path, tmp_url->path);
    str_copy(&url->server, tmp_url->server);
    str_copy(&url->share, tmp_url->share);
    str_copy(&url->path, tmp_url->path);
    str_copy(&url->user, tmp_url->user);
    str_copy(&url->password, tmp_url->password);
    str_copy(&url->domain, tmp_url->domain);
    str_copy(&url->device, tmp_url->device);
    str_copy(&url->instsys, tmp_url->instsys);

    url_free(tmp_url);

    log_info("slp: using %s\n", url_print(url, 0));
  }

  return 1;
}


void url_parse_instsys_config(char *file)
{
  file_t *f0, *f;
  slist_t *sl;

  config.url.instsys_deps = slist_free(config.url.instsys_deps);

  if(!file) return;

  f0 = file_read_file(file, kf_none);
  for(f = f0; f; f = f->next) {
    if(
      f->key_str &&
      *f->key_str &&
      *f->key_str != '#' &&
      !slist_getentry(config.url.instsys_deps, f->key_str)
    ) {
      sl = slist_append_str(&config.url.instsys_deps, f->key_str);
      str_copy(&sl->value, f->value);
    }
  }
  file_free_file(f0);

  log_debug("instsys deps:\n");
  for(sl = config.url.instsys_deps; sl; sl = sl->next) {
    log_debug("  %s: %s\n", sl->key, sl->value);
  }
}


slist_t *url_instsys_lookup(char *key, slist_t **sl_ll)
{
  slist_t *sl0 = NULL, *sl1, *sl2, *sl;
  int opt = 0;
  char *s, *lkey = NULL;

  if(!key) return NULL;

  /* make it long enough */
  lkey = calloc(1, strlen(key) + 32);

  if(*key == '?') {
    opt = 1;
    key++;
  }

  if((s = strstr(key, "<lang>"))) {
    memcpy(lkey, key, s - key);
    strcat(lkey, current_language()->trans_id);
    strcat(lkey, s + sizeof "<lang>" - 1);
  }
  else {
    strcpy(lkey, key);
  }

  if(!slist_getentry(*sl_ll, lkey)) {
    if((sl = slist_getentry(config.url.instsys_deps, lkey))) {
      sl2 = slist_split(' ', sl->value);
      for(sl = sl2; sl; sl = sl->next) {
        sl1 = slist_new();
        strprintf(&sl1->key, "%s%s", opt && *sl->key != '?' ? "?" : "", sl->key);
        slist_append(&sl0, sl1);
      }
      slist_free(sl2);
    }
    else {
      sl0 = slist_new();
      strprintf(&sl0->key, "%s%s", opt ? "?" : "", lkey);
    }

    slist_append_str(sl_ll, lkey);
  }

  free(lkey);

  return sl0;
}


void url_build_instsys_list(char *image, int read_list)
{
  char *s, *base = NULL, *name = NULL, *buf = NULL, *lbuf = NULL;
  slist_t *sl, *sl1, *sl2, *sl_ll = NULL, *list = NULL, *parts = NULL;
  size_t lbuf_size = 0;
  FILE *f;

  if(!image) return;

  str_copy(&base, image);
  s = strrchr(base, '/');
  s = s ? s + 1 : base;
  str_copy(&name, s);
  *s = 0;

  config.url.instsys_list = slist_free(config.url.instsys_list);

  list = url_instsys_lookup(name, &sl_ll);

  for(sl = list; sl; sl = sl->next) {
    sl1 = url_instsys_lookup(sl->key, &sl_ll);
    if(!sl1) continue;
    sl2 = sl->next;
    str_copy(&sl->key, NULL);
    sl->next = sl1;
    slist_append(&list, sl2);
  }

  for(sl = list; sl; sl = sl->next) {
    if(!sl->key) continue;
    s = sl->key;
    if(*s == '?') s++;
    strprintf(&buf, "?%s", s);
    if(
      !slist_getentry(config.url.instsys_list, s) &&
      !slist_getentry(config.url.instsys_list, buf)
    ) {
      slist_append_str(&config.url.instsys_list, sl->key);
    }
  }

  // if user specified extra parts to add via 'extend' option, add them here
  for(sl = config.extend_option; sl; sl = sl->next) {
    slist_append_str(&config.url.instsys_list, sl->key);
  }

  for(sl = config.url.instsys_list; sl; sl = sl->next) {
    s = sl->key;
    if(*s == '?') s++;
    strprintf(&sl->key, "%s%s%s", s == sl->key ? "" : "?", base, s);
  }

  if(read_list && (f = fopen("/etc/instsys.parts", "r"))) {
    while(getline(&lbuf, &lbuf_size, f) > 0) {
      sl = slist_split(' ', lbuf);
      // log_info(">%s< >%s<\n", sl->key, lbuf);
      if(*sl->key != '#') slist_append_str(&parts, sl->key);
      sl = slist_free(sl);
    }
    fclose(f);
  }

  list = slist_free(list);

  for(sl = config.url.instsys_list; sl; sl = sl->next) {
    s = sl->key;
    if(*s == '?') s++;
    if(!slist_getentry(parts, s)) slist_append_str(&list, sl->key);
  }

  slist_free(config.url.instsys_list);
  config.url.instsys_list = list;
  list = NULL;

  log_debug("instsys list:\n");
  for(sl = config.url.instsys_list; sl; sl = sl->next) {
    log_debug("  %s\n", sl->key);
  }

  free(lbuf);
  free(base);
  free(name);
  free(buf);

  slist_free(sl_ll);
}


char *url_instsys_config(char *path)
{
  static char *config = NULL;
  char *s;

  if(!path) return NULL;

  str_copy(&config, NULL);

  config = calloc(1, strlen(path) + sizeof "config");

  strcpy(config, path);
  s = strrchr(config, '/');
  strcpy(s ? s + 1 : config, "config");

  return config;
}


char *url_instsys_base(char *path)
{
  static char *base = NULL;
  char *s;

  if(!path) return NULL;

  str_copy(&base, path);

  s = strrchr(base, '/');
  if(s) *s = 0;

  return base;
}


char *url_config_get_path(char *entry)
{
  char *s = NULL, *t;

  if(!entry) return NULL;

  str_copy(&s, entry);

  if((t = strchr(s, '?'))) *t = 0;

  return s;
}


slist_t *url_config_get_file_list(char *entry)
{
  slist_t *sl = NULL;

  if(!entry) return NULL;

  if(entry && (entry = strstr(entry, "?list="))) {
    sl = slist_split(',', entry + sizeof "?list=" - 1);
  }

  return sl;
}


/*
 * Look up the parent device of hd in hd_list.
 *
 * Return NULL if none exists.
 */
hd_t *find_parent_in_list(hd_t *hd_list, hd_t *hd)
{
  unsigned idx;

  if(!hd || !hd_list) return NULL;

  for(idx = hd->attached_to; hd_list; hd_list = hd_list->next) {
    if(hd_list->idx == idx) return hd_list;
  }

  return NULL;
}


/*
 * Compare device names of two hardware items.
 *
 * Return 1 if hd1 and hd2 are both not NULL and have the same unix_dev_name
 * entry, else 0.
 */
int same_device_name(hd_t *hd1, hd_t *hd2)
{
  if(!hd1 || !hd2) return 0;

  if(!hd1->unix_dev_name || !hd2->unix_dev_name) return 0;

  return !strcmp(hd1->unix_dev_name, hd2->unix_dev_name);
}


/*
 * Turn hd_array elements into a linked list, in order.
 *
 * Last element in hd_array must be NULL.
 *
 * Return linked list.
 */
hd_t *relink_array(hd_t *hd_array[])
{
  hd_t **hdp = hd_array;

  for(; *hdp; hdp++) (*hdp)->next = hdp[1];

  return *hd_array;
}


/*
 * Log hardware list in abbreviated form (just device name + class).
 */
void log_hd_list(char *label, hd_t *hd)
{
  log_info("hd list (%s)\n", label);
  for(; hd; hd = hd->next) {
    log_info("  %s (%s)\n", hd->unix_dev_name, hd_hw_item_name(hd->hw_class));
  }
}


/*
 * Compare two hardware items by name using strverscmp().
 */
int cmp_hd_entries_by_name(const void *p0, const void *p1)
{
  hd_t **hd0, **hd1;
  char *name0, *name1;

  hd0 = (hd_t **) p0;
  hd1 = (hd_t **) p1;

  name0 = (*hd0)->unix_dev_name;
  name1 = (*hd1)->unix_dev_name;

  // either string might be NULL
  return strverscmp(name0 ?: "", name1 ?: "");
}


/*
 * Re-sort hardware list to make some people happy.
 */
hd_t *sort_a_bit(hd_t *hd_list)
{
  hd_t *hd;
  unsigned hds = 0;

  for(hd = hd_list; hd; hd = hd->next) hds++;

  if(hds) {
    hd_t *hd_array[hds + 1];
    unsigned u;

    if(config.debug >= 2) log_hd_list("before", hd_list);

    /* 1. drop network interfaces if there's also a corresponding card  */
    for(u = 0, hd = hd_list; hd; hd = hd->next) {
      if(
        !(
          hd->hw_class == hw_network &&
          same_device_name(hd, find_parent_in_list(hd_list, hd))
        )
      ) {
        hd_array[u++] = hd;
      }
    }

    // remember correct item count
    hds = u;
    hd_array[hds] = NULL;

    hd_list = relink_array(hd_array);

    /* 2. sort list by name */

    qsort(hd_array, hds, sizeof *hd_array, cmp_hd_entries_by_name);

    hd_list = relink_array(hd_array);

    /* 3. cards with link first */

    for(u = 0, hd = hd_list; hd; hd = hd->next) {
      if(link_detected(hd)) hd_array[u++] = hd;
    }
    for(hd = hd_list; hd; hd = hd->next) {
      if(!link_detected(hd)) hd_array[u++] = hd;
    }
    hd_array[u] = NULL;

    hd_list = relink_array(hd_array);

    /* 4. wlan cards last */

    for(u = 0, hd = hd_list; hd; hd = hd->next) {
      if(!hd->is.wlan) hd_array[u++] = hd;
    }
    for(hd = hd_list; hd; hd = hd->next) {
      if(hd->is.wlan) hd_array[u++] = hd;
    }
    hd_array[u] = NULL;

    hd_list = relink_array(hd_array);

    /* 5. network interfaces last */

    for(u = 0, hd = hd_list; hd; hd = hd->next) {
      if(hd->hw_class != hw_network) hd_array[u++] = hd;
    }
    for(hd = hd_list; hd; hd = hd->next) {
      if(hd->hw_class == hw_network) hd_array[u++] = hd;
    }
    hd_array[u] = NULL;

    hd_list = relink_array(hd_array);

    if(config.debug >= 1) log_hd_list("after", hd_list);
  }

  return hd_list;
}


int link_detected(hd_t *hd)
{
  hd_res_t *res;

  for(res = hd->res; res; res = res->next) {
    if(res->any.type == res_link && res->link.state) return 1;
  }

  return 0;
}


/*
 * Initialize all relevant digests.
 *
 * config.digests.supported: list of supported (potentially needed digests)
 *
 * url_data->digest.list: fixed size array, should hold up to max_digests digests; if there
 *   are more, those are ignored
 *
 */
void digests_init(url_data_t *url_data)
{
  int i;
  slist_t *sl;
  int max_digests = sizeof url_data->digest.list / sizeof *url_data->digest.list;

  digests_done(url_data);

  for(sl = config.digests.supported, i = 0; sl && i < max_digests; sl = sl->next, i++) {
    url_data->digest.list[i] = mediacheck_digest_init(sl->key, NULL);
  }
}


/*
 * Free all digests.
 */
void digests_done(url_data_t *url_data)
{
  int i;
  int max_digests = sizeof url_data->digest.list / sizeof *url_data->digest.list;

  for(i = 0; i < max_digests; i++) {
    mediacheck_digest_done(url_data->digest.list[i]);
  }

  memset(url_data->digest.list, 0, sizeof url_data->digest.list);
}


/*
 * Calculate all digests.
 *
 * Note: since we don't know which digest type to compare against later,
 * calculate all expected types in parallel.
 */
void digests_process(url_data_t *url_data, void *buffer, size_t len)
{
  int i;
  int max_digests = sizeof url_data->digest.list / sizeof *url_data->digest.list;

  if(!len) return;

  for(i = 0; i < max_digests; i++) {
    mediacheck_digest_process(url_data->digest.list[i], buffer, len);
  }
}


/*
 * Check f there's a matching digest.
 *
 * Return 1 if yes, else 0.
 *
 * Find the digest indicated by digest_name and compare its hex value with digest_value.
 *
 * Note: since we don't know which digest type to compare against later,
 * calculate all expected types in parallel.
 */
int digests_match(url_data_t *url_data, char *digest_name, char *digest_value)
{
  int i;
  int max_digests = sizeof url_data->digest.list / sizeof *url_data->digest.list;

  for(i = 0; i < max_digests; i++) {
    if(!strcasecmp(digest_name, mediacheck_digest_name(url_data->digest.list[i]))) {
      if(!strcasecmp(digest_value, mediacheck_digest_hex(url_data->digest.list[i]))) return 1;
    }
  }

  return 0;
}


/*
 * Find and compare digest for file_name.
 *
 * Return 1 if a match was found, else 0.
 */
int digests_verify(url_data_t *url_data, char *file_name)
{
  slist_t *sl, *sl0;
  int len, file_name_len, ok = 0;

  // match only last path element
  if(file_name) {
    char *s = strrchr(file_name, '/');
    if(s) file_name = s + 1;
  }

  file_name_len = file_name ? strlen(file_name) : 0;

  for(sl = config.digests.list; sl; sl = sl->next) {
    // first check file name
    if(file_name_len) {
      len = strlen(sl->value);
      if(len < file_name_len || strcmp(file_name, sl->value + len - file_name_len)) continue;
    }

    // compare digest
    sl0 = slist_split(' ', sl->key);
    if(sl0->next && digests_match(url_data, sl0->key, sl0->next->key)) ok = 1;
    slist_free(sl0);

    if(ok) break;
  }

  return ok;
}


/*
 * Log all currently calculated digests.
 *
 * Log only the first 32 bytes of each to keep the log lines readable.
 */
void digests_log(url_data_t *url_data)
{
  int i;
  int max_digests = sizeof url_data->digest.list / sizeof *url_data->digest.list;

  for(i = 0; i < max_digests; i++) {
    if(mediacheck_digest_valid(url_data->digest.list[i])) {
      log_info(
        "%-6s %.32s\n",
        mediacheck_digest_name(url_data->digest.list[i]),
        mediacheck_digest_hex(url_data->digest.list[i])
      );
    }
  }
}


/*
 * Return 1 if we can mount the url.
 */
unsigned url_is_mountable(instmode_t scheme)
{
  if(
    scheme == inst_file ||
    scheme == inst_nfs ||
    scheme == inst_smb ||
    scheme == inst_cdrom ||
    scheme == inst_floppy ||
    scheme == inst_hd ||
    scheme == inst_disk ||
    scheme == inst_dvd ||
    scheme == inst_exec
  ) {
    return 1;
  }

  return url_scheme_attr(scheme, "mount");
}


/*
 * Return 1 if we need network for this url scheme.
 */
unsigned url_is_network(instmode_t scheme)
{
  if(
    scheme == inst_slp ||
    scheme == inst_nfs ||
    scheme == inst_ftp ||
    scheme == inst_smb ||
    scheme == inst_http ||
    scheme == inst_https ||
    scheme == inst_tftp
  ) {
    return 1;
  }

  return url_scheme_attr(scheme, "network");
}


/*
 * Return 1 if url scheme needs a local block device.
 *
 * (In this case, the path component of the url may optionally start the
 * device name.)
 *
 */
unsigned url_is_blockdev(instmode_t scheme)
{
  if(
    scheme == inst_disk ||
    scheme == inst_cdrom ||
    scheme == inst_dvd ||
    scheme == inst_floppy ||
    scheme == inst_hd
  ) {
    return 1;
  }

  return url_scheme_attr(scheme, "blockdev");
}


/*
 * Return 1 if url scheme does not have a path component.
 *
 */
unsigned url_is_nopath(instmode_t scheme)
{
  if(
    scheme == inst_slp
  ) {
    return 1;
  }

  return url_scheme_attr(scheme, "nopath");
}


/*
 * Return 1 if url scheme may need user/password.
 *
 */
unsigned url_is_auth(instmode_t scheme)
{
  if(
    scheme == inst_ftp ||
    scheme == inst_smb ||
    scheme == inst_http ||
    scheme == inst_https
  ) {
    return 1;
  }

  return url_scheme_attr(scheme, "auth");
}


/*
 * Check for presence of file 'attr_name' in URL directory.
 */
unsigned url_scheme_attr(instmode_t scheme, char *attr_name)
{
  int ok = 0;

  if(scheme >= inst_extern && attr_name && *attr_name) {
    char *path = NULL;

    strprintf(&path, "/scripts/url/%s/%s", url_scheme2name(scheme), attr_name);
    ok = util_check_exist(path) ? 1 : 0;

    str_copy(&path, NULL);
  }

  return ok;
}


/*
 * Convert URL scheme to internal number.
 *
 * Note: if the URL scheme is an externally supported one (via "/scripts/url")
 * it is registered and gets assigned an id.
 */
instmode_t url_scheme2id(char *scheme)
{
  slist_t *sl;
  instmode_t i;

  if(!scheme || !*scheme) return inst_none;

  for(unsigned u = 0; u < sizeof url_schemes / sizeof *url_schemes; u++) {
    if(!strcasecmp(url_schemes[u].name, scheme)) return url_schemes[u].value;
  }

  if(util_check_exist2("/scripts/url", scheme) == 'd') {
    char *attr = NULL, *attr_val;
    strprintf(&attr, "/scripts/url/%s/menu", scheme);
    attr_val = util_get_attr(attr);
    str_copy(&attr, NULL);
    if(!slist_getentry(config.extern_scheme, scheme)) {
      log_info("registering url scheme: %s: %s\n", scheme, attr_val);
    }
    slist_setentry(&config.extern_scheme, scheme, *attr_val ? attr_val : NULL, 1);
  }

  for(sl = config.extern_scheme, i = inst_extern; sl; sl = sl->next, i++) {
    if(!strcmp(sl->key, scheme)) return i;
  }

  return inst_none;
}


/*
 * String representing the URL scheme.
 *
 * Returns NULL if scheme is unknown.;
 */
char *url_scheme2name(instmode_t scheme_id)
{
  char *s = NULL;
  slist_t *sl;
  instmode_t i;

  if(scheme_id < inst_extern) {
    for(unsigned u = 0; u < sizeof url_schemes / sizeof *url_schemes; u++) {
      if(url_schemes[u].value == scheme_id) {
        s = url_schemes[u].name;
        break;
      }
    }
  }
  else {
    for(sl = config.extern_scheme, i = inst_extern; sl; sl = sl->next, i++) {
      if(i == scheme_id) {
        s = sl->key;
        break;
      }
    }
  }

  return s;
}


/*
 * Uppercase variant of URL scheme; used in messages.
 */
char *url_scheme2name_upper(instmode_t scheme_id)
{
  static char *name = NULL;
  int i;

  str_copy(&name, url_scheme2name(scheme_id));

  if(name) {
    for(i = 0; name[i]; i++) name[i] = toupper(name[i]);
  }

  return name;
}


/*
 * Register all user-defined schemes.
 *
 * Each scheme is auto-registered on first use. But to get the menus in
 * manual mode right, we register all in one go.
 */
void url_register_schemes()
{
  struct dirent *de;
  DIR *d;

 if((d = opendir("/scripts/url"))) {
    while((de = readdir(d))) {
      if(de->d_name[0] != '.') {
        url_scheme2id(de->d_name);
      }
    }
    closedir(d);
  }
}


/*
 * Replace zypp variables in string.
 *
 * Return new string.
 *
 * The returned value is a copy of the passed argument and must be freed later.
 *
 * Currently only $releasever and ${releasever} are replaced.
 */
char *url_replace_vars(char *str)
{
  static char *vars[] = { "${releasever}", "$releasever" };
  char *res = NULL;

  str_copy(&res, str);

  if(!config.releasever) return res;

  for(int i = 0; i < sizeof vars / sizeof *vars; i++) {
    int len = strlen(vars[i]);
    char *s;
    if((s = strstr(res, vars[i]))) {
      s[len - 1] = 0;
      s[0] = 0;
      strprintf(&res, "%s%s%s", res, config.releasever, s + len);
    }
  }

  return res;
}


/*
 * Replace zypp variables and store a backup of the original string.
 */
void url_replace_vars_with_backup(char **str, char **backup)
{
  if(!*str) return;

  char *new_str = url_replace_vars(*str);

  if(!strcmp(new_str, *str)) {
    free(new_str);
  }
  else {
    *backup = *str;
    *str = new_str;
  }
}
