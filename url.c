#define _GNU_SOURCE	/* strnlen, getline */

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
static int url_mount_disk(url_t *url, char *dir, int (*test_func)(url_t *));
static int url_progress(url_data_t *url_data, int stage);
static int url_setup_device(url_t *url);
static int url_setup_interface(url_t *url);
static void url_parse_instsys_config(char *file);
static slist_t *url_instsys_lookup(char *key, slist_t **sl_ll);
static char *url_instsys_config(char *path);
static char *url_config_get_path(char *entry);
static slist_t *url_config_get_file_list(char *entry);
static hd_t *sort_a_bit(hd_t *hd_list);
static int link_detected(hd_t *hd);
static char *url_print_zypp(url_t *url);


void url_read(url_data_t *url_data)
{
  CURL *c_handle;
  int i;
  FILE *f;
  char *buf, *s, *proxy_url = NULL;
  sighandler_t old_sigpipe = signal(SIGPIPE, SIG_IGN);
  unsigned char sha1[20];

  sha1_init_ctx(&url_data->sha1_ctx);

  c_handle = curl_easy_init();
  // fprintf(stderr, "curl handle = %p\n", c_handle);

  // curl_easy_setopt(c_handle, CURLOPT_VERBOSE, 1);

  curl_easy_setopt(c_handle, CURLOPT_WRITEFUNCTION, url_write_cb);
  curl_easy_setopt(c_handle, CURLOPT_WRITEDATA, url_data);
  curl_easy_setopt(c_handle, CURLOPT_ERRORBUFFER, url_data->curl_err_buf);
  curl_easy_setopt(c_handle, CURLOPT_FAILONERROR, 1);
  curl_easy_setopt(c_handle, CURLOPT_FOLLOWLOCATION, 1);
  curl_easy_setopt(c_handle, CURLOPT_MAXREDIRS, 10);
  curl_easy_setopt(c_handle, CURLOPT_SSL_VERIFYPEER, 0);

  curl_easy_setopt(c_handle, CURLOPT_PROGRESSFUNCTION, url_progress_cb);
  curl_easy_setopt(c_handle, CURLOPT_PROGRESSDATA, url_data);
  curl_easy_setopt(c_handle, CURLOPT_NOPROGRESS, 0);

  if(config.net.ipv6) {
    curl_easy_setopt(c_handle, CURLOPT_IPRESOLVE, CURL_IPRESOLVE_V6);
  }

  url_data->err = curl_easy_setopt(c_handle, CURLOPT_URL, url_data->url->str);
  // fprintf(stderr, "curl opt url = %d\n", url_data->err);

  if(config.debug >= 2) fprintf(stderr, "url_read(%s)\n", url_data->url->str);

  str_copy(&proxy_url, url_print(config.url.proxy, 1));
  if(proxy_url) {
    if(config.debug >= 2) fprintf(stderr, "using proxy %s\n", proxy_url);
    name2inet(&config.url.proxy->used.server, config.url.proxy->server);
    if(net_check_address(&config.url.proxy->used.server, 1)) {
      snprintf(url_data->err_buf, url_data->err_buf_len, "invalid proxy address: %s", config.url.proxy->used.server.name);
      fprintf(stderr, "%s\n", url_data->err_buf);
      url_data->err = 105;
    }
    else {
      curl_easy_setopt(c_handle, CURLOPT_PROXY, proxy_url);
      if(config.debug >= 2) fprintf(stderr, "proxy: %s\n", proxy_url);
    }
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

  // fprintf(stderr, "curl perform = %d (%s)\n", url_data->err, url_data->err_buf);

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
        snprintf(url_data->err_buf, url_data->err_buf_len, "gzip: command terminated");
      }
      // fprintf(stderr, "close = %d\n", i);
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

  if(!url_data->err) {
    memset(sha1, 0, 16);
    sha1_finish_ctx(&url_data->sha1_ctx, sha1);
    url_data->sha1 = calloc(2 * sizeof sha1 + 1, 1);
    for(i = 0; i < sizeof sha1; i++) {
      sprintf(url_data->sha1 + 2 * i, "%02x", sha1[i]);
    }
  }
}


size_t url_write_cb(void *buffer, size_t size, size_t nmemb, void *userp)
{
  url_data_t *url_data = userp;
  size_t z1, z2;
  int i, fd, fd1, fd2, tmp;
  struct cramfs_super_block *cramfs_sb;
  off_t off;

#if 0
  fprintf(stderr,
    "buffer = %p, size = %d, nmemb = %d, userp = %p\n",
    buffer, size, nmemb, userp
  );
#endif

  z1 = size * nmemb;

  if(z1) sha1_process_bytes(buffer, z1, &url_data->sha1_ctx);

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
    if(
      url_data->unzip &&
      url_data->buf.data[0] == 0x1f &&
      url_data->buf.data[1] == 0x8b
    ) {
      url_data->gzip = 1;

      if((url_data->buf.data[3] & 0x08)) {
        i = strnlen((char *) url_data->buf.data + 10, url_data->buf.len - 10);
        if(i < url_data->buf.len - 10) {
          url_data->orig_name = strdup((char *) url_data->buf.data + 10);
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

#if 0
    fprintf(stderr,
      "gzip = %d, cramfs = %d, >%s<\n",
      url_data->gzip, url_data->cramfs, url_data->orig_name
    );
#endif
  }

  if(url_data->buf.len == url_data->buf.max || url_data->flush) {
    if(!url_data->file_opened) {
      url_data->file_opened = 1;
      if(url_data->gzip) {
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
            url_data->f = popen("gzip -dc", "w");
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
 * scheme://domain;user:password@server:port/path?query
 *
 * smb: path = share/path
 * disk: path = [device/]path
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

    i = file_sym2num(s0);
    url->scheme = i >= 0 ? i : inst_none;

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
    // FIXME: should always be 'rel'
    i = file_sym2num(str);
    if(i >= 0) {
      url->scheme = i;
      url->path = strdup("");
    }
    else if(i == -1) {
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

  /* smb: first path element is share */
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

  /* disk/cdrom: allow path to begin with device name */
  if(
    (
      url->scheme == inst_disk ||
      url->scheme == inst_cdrom ||
      url->scheme == inst_dvd ||
      url->scheme == inst_floppy ||
      url->scheme == inst_hd
    ) && url->path
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
  }

  if((sl = slist_getentry(url->query, "all"))) {
    url->search_all = strtoul(sl->value, NULL, 0);
  }

  if((sl = slist_getentry(url->query, "quiet"))) {
    url->quiet = strtoul(sl->value, NULL, 0);
  }

  if(
    url->scheme == inst_file ||
    url->scheme == inst_nfs ||
    url->scheme == inst_smb ||
    url->scheme == inst_cdrom ||
    url->scheme == inst_floppy ||
    url->scheme == inst_hd ||
    url->scheme == inst_disk ||
    url->scheme == inst_dvd ||
    url->scheme == inst_exec
    ) {
    url->is.mountable = 1;
  }

  if(
    url->scheme == inst_slp ||
    url->scheme == inst_nfs ||
    url->scheme == inst_ftp ||
    url->scheme == inst_smb ||
    url->scheme == inst_http ||
    url->scheme == inst_tftp
    ) {
    url->is.network = 1;
  }

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

  if(config.debug >= 1) {
    fprintf(stderr, "url = %s\n", url->str);
    if(config.debug >= 2) {
      fprintf(stderr, "  scheme = %s (%d)", get_instmode_name(url->scheme), url->scheme);
      if(url->server) fprintf(stderr, ", server = \"%s\"", url->server);
      if(url->port) fprintf(stderr, ", port = %u", url->port);
      if(url->path) fprintf(stderr, ", path = \"%s\"", url->path);
      fprintf(stderr, "\n");

      if(url->user || url->password) {
        i = 0;
        if(url->user) fprintf(stderr, "%c user = \"%s\"", i++ ? ',' : ' ', url->user);
        if(url->password) fprintf(stderr, "%c password = \"%s\"", i++ ? ',' : ' ', url->password);
        fprintf(stderr, "\n");
      }

      if(url->share || url->domain || url->device) {
        i = 0;
        if(url->share) fprintf(stderr, "%c share = \"%s\"", i++ ? ',' : ' ', url->share);
        if(url->domain) fprintf(stderr, "%c domain = \"%s\"", i++ ? ',' : ' ', url->domain);
        if(url->device) fprintf(stderr, "%c device = \"%s\"", i++ ? ',' : ' ', url->device);
        fprintf(stderr, "\n");
      }

      fprintf(stderr,
        "  network = %u, mountable = %u, file = %u, all = %u, quiet = %u\n",
        url->is.network, url->is.mountable, url->is.file, url->search_all, url->quiet
      );

      if(url->instsys) fprintf(stderr, "  instsys = %s\n", url->instsys);

      if(url->query) {
        fprintf(stderr, "  query:\n");
        for(sl = url->query; sl; sl = sl->next) {
          fprintf(stderr, "    %s = \"%s\"\n", sl->key, sl->value);
        }
      }
    }
  }

  return url;
}


/*
 * Print url to string.
 *
 * scheme://domain;user:password@server:port/path?query
 *
 * format:
 *   0: for logging
 *   1: without query part
 *   2: with device
 *   3: like 2, but remove 'rel:' scheme
 *   4: in zypp format
 */
char *url_print(url_t *url, int format)
{
  static char *buf = NULL, *s;
  int q = 0;

  // printf("start buf = %p\n", buf);
  // LXRC_WAIT

  str_copy(&buf, NULL);

  if(!url) return buf;

  if(format == 4) return url_print_zypp(url);

  if(format != 3 || url->scheme != inst_rel) {
    strprintf(&buf, "%s:", get_instmode_name(url->scheme));
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

  if(format == 0 || format == 2 || format == 3) {
    if((s = url->used.device) || (s = url->device)) {
      strprintf(&buf, "%s%cdevice=%s", buf, q++ ? '&' : '?', short_dev(s));
    }
    if(url->file_list) {
      strprintf(&buf, "%s%clist=%s", buf, q++ ? '&' : '?', s = slist_join(",", url->file_list));
      free(s);
    }
  }

  if(format == 0) {
    if(config.debug >= 2 && url->used.hwaddr) {
      strprintf(&buf, "%s%chwaddr=%s", buf, q++ ? '&' : '?', url->used.hwaddr);
    }
  }

  s = buf;

  if(format == 3 && *s == '/') s++;

  // printf("end buf = %p\n", buf);
  // LXRC_WAIT

  return s;
}


/*
 * according to zypp/media/MediaManager.h
 */
char *url_print_zypp(url_t *url)
{
  static char *buf = NULL, *s;
  char *path = NULL, *file = NULL;
  int q = 0, scheme;

  // printf("start buf = %p\n", buf);
  // LXRC_WAIT

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

  if(scheme == inst_disk) {
    scheme = url->is.cdrom ? inst_cdrom : inst_hd;
  }

  if(
    scheme != inst_hd &&
    scheme != inst_cdrom &&
    scheme != inst_file &&
    scheme != inst_ftp &&
    scheme != inst_http &&
    scheme != inst_nfs &&
    scheme != inst_smb &&
    scheme != inst_tftp
  ) return buf;

  strprintf(&buf, "%s:", get_instmode_name(scheme));

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

  if(url->is.file && file) {
    strprintf(&buf, "iso:/?iso=%s&url=%s", file, buf);
  }

  str_copy(&path, NULL);

  // printf("end buf = %p\n", buf);
  // LXRC_WAIT

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
    free(url->used.server.name);

    slist_free(url->query);
    slist_free(url->file_list);

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
    if(err) fprintf(stderr, "curl init = %d\n", err);
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
  free(url_data->sha1);

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
          url_data->label_shown && !url_data->percent >= 0 ? "\x08\x08\x08\x08\x08\x08\x08\x08\x08\x08\x08\x08" : "",
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
 */
void url_umount(url_t *url)
{
  if(!url) return;

  // FIXME: this is wrong!

  if(url->mount && util_umount(url->mount)) {
    if(config.debug) fprintf(stderr, "%s: url umount failed\n", url->mount);
  }
  str_copy(&url->mount, NULL);

  if(url->tmp_mount && util_umount(url->tmp_mount)) {
    if(config.debug) fprintf(stderr, "%s: url umount failed\n", url->tmp_mount);
  }
  str_copy(&url->tmp_mount, NULL);
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

  fprintf(stderr, "url mount: trying %s\n", url_print(url, 0));
  if(url->used.model) fprintf(stderr, "(%s)\n", url->used.model);

  if(
    !url ||
    !url->scheme ||
    !url->path ||
    (!url->used.device && url->scheme != inst_file)
  ) return 0;

  url_umount(url);
  str_copy(&url->tmp_mount, NULL);
  str_copy(&url->mount, NULL);

  if(!url_setup_device(url)) return 0;

  if(!url->is.network) {
    /* local device */

    /* we might need an extra mountpoint */
    if(url->scheme != inst_file && strcmp(url->path, "/")) {
      str_copy(&url->tmp_mount, new_mountpoint());
      ok = util_mount_ro(url->used.device, url->tmp_mount, url->file_list) ? 0 : 1;

      if(!ok) {
        fprintf(stderr, "disk: %s: mount failed\n", url->used.device);
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

        if(config.debug) fprintf(stderr, "[server = %s]\n", inet2print(&url->used.server));

        if(!url->is.file) {
          err = net_mount_nfs(url->mount, &url->used.server, url->path, url->port);
          fprintf(stderr, "nfs: %s -> %s (%d)\n", url->path, url->mount, err);
        }
        else {
          fprintf(stderr, "nfs: %s: is file, mounting one level up\n", url->path);
        }

        if(err == ENOTDIR || err == ENOENT || url->is.file) {
          str_copy(&url->mount, NULL);
          str_copy(&buf, url->path);

          if((s = strrchr(buf, '/')) && s != buf && s[1]) {
            *s++ = 0;
            str_copy(&url->tmp_mount, new_mountpoint());

            if(config.debug) fprintf(stderr, "[server = %s]\n", inet2print(&url->used.server));

            err = net_mount_nfs(url->tmp_mount, &url->used.server, buf, url->port);
            fprintf(stderr, "nfs: %s -> %s (%d)\n", buf, url->tmp_mount, err);
    
            if(err) {
              fprintf(stderr, "nfs: %s: mount failed\n", url->used.device);
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

        if(config.debug) fprintf(stderr, "[server = %s]\n", inet2print(&url->used.server));

        err = net_mount_smb(s, &url->used.server, url->share, url->user, url->password, url->domain);
        fprintf(stderr, "smb: %s -> %s (%d)\n", url->share, s, err);
        if(err) {
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
      case inst_ftp:
      case inst_tftp:
        break;

      default:
        fprintf(stderr, "%s: unsupported scheme\n", get_instmode_name(url->scheme));
        err = 1;
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
            URL_FLAG_PROGRESS + URL_FLAG_UNZIP + URL_FLAG_NOSHA1
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
    fprintf(stderr, "disk: mount ok but test failed\n");
  }

  if(!ok) {
    fprintf(stderr, "url mount: %s failed\n", url_print(url, 0));

    util_umount(url->mount);
    util_umount(url->tmp_mount);

    str_copy(&url->tmp_mount, NULL);
    str_copy(&url->mount, NULL);
  }
  else {
    fprintf(stderr, "url mount: %s", url_print(url, 0));
    if(url->mount) fprintf(stderr, " @ %s", url->mount);
    fprintf(stderr, "\n");
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
  hd_hw_item_t hw_item = hw_network_ctrl;
  str_list_t *sl;
  char *url_device;

  if(!url || !url->scheme) return 1;

  update_device_list(0);

  if(!config.hd_data) return 1;

  if(
    url->scheme == inst_file ||
    url->used.device
  ) {
    return url_mount_disk(url, dir, test_func) ? 0 : 1;
  }

  if(!url->is.network) {
    switch(url->scheme) {
      case inst_cdrom:
        hw_item = hw_cdrom;
        break;

      case inst_floppy:
        hw_item = hw_floppy;
        break;

      default:
        hw_item = hw_block;
        break;
    }
  }

  url_device = url->device;
  if(!url_device) url_device = url->is.network ? config.netdevice : config.device;

  for(found = 0, hd = sort_a_bit(fix_device_names(hd_list(config.hd_data, hw_item, 0, NULL))); hd; hd = hd->next) {
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
      (hd_is_hw_class(hd, hw_block) && hd->child_ids) ||	/* skip whole block device if there are partitions */
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

    url->is.wlan = hd->is.wlan;

    if((ok = url_mount_disk(url, dir, test_func))) {
      found++;
      if(hd_is_hw_class(hd, hw_cdrom)) url->is.cdrom = 1;
      if(ok == 1 || config.sig_failed || config.sha1_failed) break;
    }
    else {
      err = 1;
    }
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

  return found ? 0 : 1;
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
 */
int url_read_file(url_t *url, char *dir, char *src, char *dst, char *label, unsigned flags)
{
  int err, win, i;
  char *src_sig = NULL, *dst_sig = NULL, *buf = NULL, *old_path = NULL, *s;

  str_copy(&old_path, url->path);

  if(!(flags & URL_FLAG_CHECK_SIG)) {
    err = url_read_file_nosig(url, dir, src, dst, label, flags);
    str_copy(&url->path, old_path);
    free(old_path);

    return err;
  }

  flags |= URL_FLAG_NOSHA1;

  err = url_read_file_nosig(url, dir, src, dst, label, flags);
  str_copy(&url->path, old_path);

  if(err) {
    free(old_path);
    return err;
  }

  config.sig_failed = 0;

  if(!config.secure) {
    free(old_path);
    return err;
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
    "gpg --homedir /root/.gnupg --batch --no-default-keyring --keyring /installkey.gpg --ignore-valid-from --ignore-time-conflict --verify %s >/dev/null%s",
    dst_sig, config.debug < 2 ? " 2>&1" : ""
  );

  err = url_read_file_nosig(url, dir, src_sig, dst_sig, NULL, flags);
  str_copy(&url->path, old_path);

  s = url_print2(url, src);

  if(!err) {
    if(system(buf)) {
      fprintf(stderr, "%s: signature check failed\n", s);
      config.sig_failed = 2;
    }
    else {
      fprintf(stderr, "%s: signature ok\n", s);
      config.sig_failed = 0;
    }
  }
  else {
    fprintf(stderr, "%s: no signature\n", s);
  }

  if(config.sig_failed) {
    strprintf(&buf,
      "%s: %s\n\n%s",
      s,
      txt_get(config.sig_failed == 1 ? TXT_NO_SIGNATURE : TXT_INVALID_SIGNATURE),
      txt_get(config.sig_failed == 1 ? TXT_INSECURE_REPO2 : TXT_INSTALL_ABORT)
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

  free(buf);
  free(dst_sig);
  free(src_sig);
  free(old_path);

  return err;
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
  int keep_mounted = 0, real_err = 0;

  int test_and_copy(url_t *url)
  {
    int ok = 0, new_url = 0, i, j, k, win;
    char *old_path, *buf = NULL;
    url_data_t *url_data;
    slist_t *sl;

    if(!url) return 0;

    if(url->is.mountable && url->scheme != inst_file) {
      if(!url->mount) return 0;
      ok = util_check_exist2(url->mount, src) == 'r' ? 1 : 0;
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
      (i && old_path[i - 1] == '/') || !*old_path || !*src || *src == '/' ? "" : "/",
      strcmp(src, "/") ? src : ""
    );
    if(url->path[0] == '/' && url->path[1] == '/') str_copy(&url->path, url->path + 1);

    if(config.debug >= 3) fprintf(stderr, "path: \"%s\" + \"%s\" = \"%s\"\n", old_path, src, url->path);

    str_copy(&buf, url_print(url, 1));
    url_data->url = url_set(buf);

    free(url->path);
    url->path = old_path;

    url_data->file_name = strdup(dst);

    if((flags & URL_FLAG_OPTIONAL)) url_data->optional = 1;
    if((flags & URL_FLAG_UNZIP)) url_data->unzip = 1;
    if((flags & URL_FLAG_PROGRESS)) url_data->progress = url_progress;
    str_copy(&url_data->label, label);

    fprintf(stderr, "loading %s -> %s\n", url_print(url_data->url, 0), url_data->file_name);

    url_read(url_data);

    if(url_data->err) {
      fprintf(stderr, "error %d: %s%s\n", url_data->err, url_data->err_buf, url_data->optional ? " (ignored)" : "");
    }
    else {
      ok = 1;
      if(config.secure) {
        fprintf(stderr, "sha1 %s\n", url_data->sha1);
        if((flags & URL_FLAG_NOSHA1)) {
          fprintf(stderr, "sha1 not checked\n");
        }
        else {
          k = 0;
          sl = slist_getentry(config.sha1, url_data->sha1);
          if(sl && url_data->url->path) {
            i = strlen(sl->value);
            j = strlen(url_data->url->path);
            if(i <= j && !strcmp(url_data->url->path + j - i, sl->value)) k = 1;
          }
          if(sl) {	/* if(k) to verify filenames as well */
            fprintf(stderr, "sha1 ok\n");
          }
          else {
            fprintf(stderr, "sha1 check failed\n");
            config.sha1_failed = 1;
            strprintf(&buf,
              "%s: %s\n\n%s",
              url_print2(url_data->url, NULL),
              txt_get(TXT_SHA1_FAILED),
              txt_get(TXT_INSECURE_REPO2)
            );
            if(!(win = config.win)) util_disp_init();
            i = dia_okcancel(buf, NO);
            if(!win) util_disp_done();
            if(i == YES) {
              config.secure = 0;
              config.sha1_failed = 0;
            }
            else {
              ok = 0;
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

  if(!dst) return 1;
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
    fprintf(stderr, "url read: %s: failed to create directories\n", dst);

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
  char *url_device;

  if(!url || !url->is.network || url->used.device) return url_read_file(url, dir, src, dst, label, flags);

  LXRC_WAIT

  if(config.net.configured) {
    str_copy(&url->used.device, config.net.device);
    str_copy(&url->used.hwaddr, config.net.hwaddr);
    str_copy(&url->used.model, config.net.cardname);
    str_copy(&url->used.unique_id, config.net.unique_id);

    LXRC_WAIT

  }
  else {
    update_device_list(0);

    LXRC_WAIT

    if(config.hd_data) {
      url_device = url->device ?: config.netdevice;

      for(found = 0, hd = sort_a_bit(hd_list(config.hd_data, hw_network_ctrl, 0, NULL)); hd; hd = hd->next) {
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

        url->is.wlan = hd->is.wlan;

        url_setup_device(url);

        if(!url_read_file(url, dir, src, dst, label, flags)) {
          found++;
          break;
        }
        if(config.sig_failed || config.sha1_failed) break;
      }

      if(!found) {
        str_copy(&url->used.device, NULL);
        str_copy(&url->used.model, NULL); 
        str_copy(&url->used.hwaddr, NULL);
        str_copy(&url->used.unique_id, NULL);
      }

      LXRC_WAIT
 
      return found ? 0 : 1;
    }
  }

  if(url->used.device) url_setup_device(url);

  err = url_read_file(url, dir, src, dst, label, flags);

  LXRC_WAIT

  return err;
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

  /*
   * 0: failed, 1: ok, 2: ok but continue search
   */
  int test_is_repo(url_t *url)
  {
    int ok = 0, i, opt, parts, part;
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
      config.sha1 = slist_free(config.sha1);
      config.sha1_failed = 0;

      strprintf(&buf, "/%s", config.zen ? config.zenconfig : "content");
      strprintf(&buf2, "file:%s", buf);

      if(
        url_read_file(url, NULL, buf, buf, NULL,
          URL_FLAG_NOSHA1 + (config.secure ? URL_FLAG_CHECK_SIG : 0)
        )
      ) return 0;

      if(!config.sig_failed) {
        file_read_info_file(buf2, config.zen ? kf_cont + kf_cfg : kf_cont);
      }

      str_copy(&buf, NULL);
      str_copy(&buf2, NULL);
    }

    if(config.url.instsys->scheme != inst_rel || config.kexec) return 1;

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
          if(config.sha1_failed) return 1;
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
            fprintf(stderr, "instsys missing: %s (optional)\n", t);
          }
          else {
            fprintf(stderr, "instsys missing: %s\n", t);
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
        (util_is_mountable(buf) || !util_check_exist(buf)) &&
        !config.rescue &&
        (!config.download.instsys || util_check_exist(buf) == 'd')
      ) {
        if(!util_check_exist(buf) && opt) {
          fprintf(stderr, "mount %s -> %s failed (ignored)\n", buf, sl->value);
        }
        else {
          fprintf(stderr, "mount %s -> %s\n", buf, sl->value);

          i = util_mount_ro(buf, sl->value, url->file_list) ? 0 : 1;
          ok &= i;
          if(!i) fprintf(stderr, "instsys mount failed: %s\n", sl->value);
        }
      }
      else {
        if(parts > 1) {
          strprintf(&buf2, "%s (%d/%d)",
            txt_get(config.rescue ? TXT_LOADING_RESCUE : TXT_LOADING_INSTSYS), part, parts
          );
        }
        else {
          str_copy(&buf2, txt_get(config.rescue ? TXT_LOADING_RESCUE : TXT_LOADING_INSTSYS));
        }

        if(!url_read_file(url,
          NULL,
          t,
          file_name = strdup(new_download()),
          buf2,
          URL_FLAG_PROGRESS + URL_FLAG_UNZIP + opt * URL_FLAG_OPTIONAL
        )) {
          fprintf(stderr, "mount %s -> %s\n", file_name, sl->value);

          i = util_mount_ro(file_name, sl->value, url->file_list) ? 0 : 1;
          ok &= i;
          if(!i) fprintf(stderr, "instsys mount failed: %s\n", sl->value);
        }
        else {
          fprintf(stderr, "download failed: %s%s\n", sl->value, opt ? " (ignored)" : "");
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

  fprintf(stderr, "repository: looking for %s\n", url_print(url, 0));

  err = url_mount(url, dir, test_is_repo);

  if(err) {
    fprintf(stderr, "repository: not found\n");
  }
  else {
    fprintf(stderr, "repository: using %s", url_print(url, 0));
    if(url->mount) fprintf(stderr, " @ %s", url->mount);
    fprintf(stderr, "\n");
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
  int opt, part, parts, ok, i;
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
      // fprintf(stderr, "XXX %d %s\n", url->is.mountable, url->mount);
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
          fprintf(stderr, "instsys missing: %s (optional)\n", t);
        }
        else {
          fprintf(stderr, "instsys missing: %s\n", t);
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
        (util_is_mountable(buf) || !util_check_exist(buf)) &&
        !config.rescue &&
        (!config.download.instsys || util_check_exist(buf) == 'd')
      ) {
        if(!util_check_exist(buf) && opt) {
          fprintf(stderr, "mount %s -> %s failed (ignored)\n", buf, sl->value);
        }
        else {
          fprintf(stderr, "mount %s -> %s\n", buf, sl->value);

          i = util_mount_ro(buf, sl->value, url->file_list) ? 0 : 1;
          ok &= i;
          if(!i) fprintf(stderr, "instsys mount failed: %s\n", sl->value);
        }
      }
      else {
        if(parts > 1) {
          strprintf(&buf2, "%s (%d/%d)",
            txt_get(config.rescue ? TXT_LOADING_RESCUE : TXT_LOADING_INSTSYS), part, parts
          );
        }
        else {
          str_copy(&buf2, txt_get(config.rescue ? TXT_LOADING_RESCUE : TXT_LOADING_INSTSYS));
        }

        if(!url_read_file(url,
          NULL,
          *t ? t : NULL,
          file_name = strdup(new_download()),
          buf2,
          URL_FLAG_PROGRESS + URL_FLAG_UNZIP + opt * URL_FLAG_OPTIONAL
        )) {
          fprintf(stderr, "mount %s -> %s\n", file_name, sl->value);

          i = util_mount_ro(file_name, sl->value, url->file_list) ? 0 : 1;
          ok &= i;
          if(!i) fprintf(stderr, "instsys mount failed: %s\n", sl->value);
        }
        else {
          fprintf(stderr, "download failed: %s%s\n", sl->value, opt ? " (ignored)" : "");
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

  // fprintf(stderr, "*** url_setup_device(%s)\n", url_print(url, 0));

  if(!url) return 0;

  if(url->scheme == inst_file) return 1;

  if(!url->used.device) return 0;

  // fprintf(stderr, "*** url_setup_device(dev = %s)\n", url->used.device);

  if(!url->is.network) {
    /* load fs module if necessary */

    type = util_fstype(url->used.device, NULL);
    if(type && strcmp(type, "swap")) ok = 1;
  }
  else {
    ok = url_setup_interface(url);

    if(ok) {
      name2inet(&url->used.server, url->server);

      if(net_check_address(&url->used.server, 1)) {
        fprintf(stderr, "invalid server address: %s\n", url->used.server.name);
        config.net.configured = nc_none;

        ok = 0;
      }
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
  int i;
  url_t *tmp_url;

  // fprintf(stderr, "*** url_setup_interface(dev = %s)\n", url->used.device);

  /* setup interface */

  if(
    config.net.configured &&
    config.net.device &&
    !strcmp(url->used.device, config.net.device)
  ) {
    // fprintf(stderr, "*** url_setup_interface(dev = %s): already up\n", url->used.device);

    return 1;
  }

  if(
    !strncmp(url->used.device, "lo", sizeof "lo" - 1) ||
    !strncmp(url->used.device, "sit", sizeof "sit" - 1)
  ) return 0;

  /* if(!getenv("PXEBOOT")) */ net_stop();

  str_copy(&config.net.device, url->used.device);
  str_copy(&config.net.hwaddr, url->used.hwaddr);
  str_copy(&config.net.cardname, url->used.model);
  str_copy(&config.net.unique_id, url->used.unique_id);

// --- net_config2() start ---

  config.net.configured = nc_none;

  fprintf(stderr, "interface setup: %s\n", url->used.device);

  if(url->is.wlan && wlan_setup()) return 0;

  if((config.net.do_setup & DS_SETUP)) auto2_user_netconfig();

  if(config.net.configured == nc_none) config.net.configured = nc_static;

  /* we need at least ip & netmask for static network config */
  if((net_config_mask() & 3) != 3) {
    printf(
      "Sending %s request to %s...\n",
      config.net.use_dhcp ? config.net.ipv6 ? "DHCP6" : "DHCP" : "BOOTP",
      url->used.device
    );
    fflush(stdout);
    fprintf(stderr,
      "sending %s request to %s... ",
      config.net.use_dhcp ? config.net.ipv6 ? "DHCP6" : "DHCP" : "BOOTP",
      url->used.device
    );

    config.net.use_dhcp ? net_dhcp() : net_bootp();

    if(
      !config.test &&
      !config.net.ipv6 &&
      (
        !config.net.hostname.ok ||
        !config.net.netmask.ok ||
        !config.net.broadcast.ok
      )
    ) {
      fprintf(stderr, "no/incomplete answer.\n");
      config.net.configured = nc_none;

      return 0;
    }
    fprintf(stderr, "ok.\n");

    config.net.configured = config.net.use_dhcp ? nc_dhcp : nc_bootp;
  }

  if(net_activate_ns()) {
    fprintf(stderr, "network setup failed\n");
    config.net.configured = nc_none;

    return 0;
  }
  else {
    fprintf(stderr, "%s activated\n", url->used.device);
  }

// --- net_config2() end ---

  if(url->scheme == inst_slp) {
    tmp_url = url_set(slp_get_install(url));
    if(!tmp_url->scheme) {
      fprintf(stderr, "%s: SLP failed\n", url->used.device);
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

    fprintf(stderr, "slp: using %s\n", url_print(url, 0));
  }

  net_ask_password();

  fprintf(stderr, "hostip: %s\n", inet2print(&config.net.hostname));
  if(config.net.gateway.ok) {
    fprintf(stderr, "gateway: %s\n", inet2print(&config.net.gateway));
  }
  for(i = 0; i < sizeof config.net.nameserver / sizeof *config.net.nameserver; i++) {
    if(config.net.nameserver[i].ok) {
      fprintf(stderr, "nameserver %d: %s\n", i, inet2print(&config.net.nameserver[i]));
    }
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

  if(config.debug) {
    fprintf(stderr, "instsys deps:\n");
    for(sl = config.url.instsys_deps; sl; sl = sl->next) {
      fprintf(stderr, "  %s: %s\n", sl->key, sl->value);
    }
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

  for(sl = config.url.instsys_list; sl; sl = sl->next) {
    s = sl->key;
    if(*s == '?') s++;
    strprintf(&sl->key, "%s%s%s", s == sl->key ? "" : "?", base, s);
  }

  if(read_list && (f = fopen("/etc/instsys.parts", "r"))) {
    while(getline(&lbuf, &lbuf_size, f) > 0) {
      sl = slist_split(' ', lbuf);
      // fprintf(stderr, ">%s< >%s<\n", sl->key, lbuf);
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

  if(config.debug) {
    fprintf(stderr, "instsys list:\n");
    for(sl = config.url.instsys_list; sl; sl = sl->next) {
      fprintf(stderr, "  %s\n", sl->key);
    }
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
 * Re-sort hardware list to make some people happy.
 */
hd_t *sort_a_bit(hd_t *hd_list)
{
  hd_t *hd;
  unsigned hds = 0;

  for(hd = hd_list; hd; hd = hd->next) hds++;

  if(hds) {
    hd_t *hd_array[hds + 1];
    unsigned u = 0;

    /* cards with link first */

    for(hd = hd_list; hd; hd = hd->next) {
      if(link_detected(hd)) hd_array[u++] = hd;
    }
    for(hd = hd_list; hd; hd = hd->next) {
      if(!link_detected(hd)) hd_array[u++] = hd;
    }
    hd_array[hds] = NULL;
    for(u = 0; u < hds; u++) hd_array[u]->next = hd_array[u + 1];
    hd_list = hd_array[0];

    /* wlan cards last */

    for(u = 0, hd = hd_list; hd; hd = hd->next) {
      if(!hd->is.wlan) hd_array[u++] = hd;
    }
    for(hd = hd_list; hd; hd = hd->next) {
      if(hd->is.wlan) hd_array[u++] = hd;
    }
    hd_array[hds] = NULL;
    for(u = 0; u < hds; u++) hd_array[u]->next = hd_array[u + 1];
    hd_list = hd_array[0];
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

