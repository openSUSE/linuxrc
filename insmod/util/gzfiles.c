/*
 * This simple library intends to make it transparent to read gzipped and/or
 * standard files. This is simple enough to fit modutils' needs, but may be
 * easily adapted to anyone's needs. It's completely free, do what you want
 * with it .  - Willy Tarreau <willy@meta-x.org> - 2000/05/05 -
 */

#ifdef CONFIG_USE_ZLIB

#include <stdio.h>
#include <zlib.h>
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>

/* redefinition of gz_stream which isn't exported by zlib */
typedef struct gz_stream {
    z_stream stream;
    int      z_err;   /* error code for last stream operation */
    int      z_eof;   /* set if end of input file */
    FILE     *file;   /* .gz file */
    Byte     *inbuf;  /* input buffer */
    Byte     *outbuf; /* output buffer */
    uLong    crc;     /* crc32 of uncompressed data */
    char     *msg;    /* error message */
    char     *path;   /* path name for debugging only */
    int      transparent; /* 1 if input file is not a .gz file */
    char     mode;    /* 'w' or 'r' */
    long     startpos; /* start of compressed data in file (header skipped) */
} gz_stream;

/* maximum number of simultaneous open files, also greater file descriptor number */
#define MAXFD	64

/* this static list is assumed to be filled with NULLs at runtime */
static gzFile gzf_fds[MAXFD];

/* returns the filedesc of the opened file. */
int gzf_open(const char *name, int mode) {
    int fd;
    gzFile g;

    if ((g=gzopen(name, "rb")) != NULL) {
	fd=fileno(((gz_stream*)g)->file);
	gzf_fds[fd]=g;
    }
    else if ((fd=open(name, mode)) != -1) {
	gzf_fds[fd]=NULL; /* NULL means not GZ mode */
    }
    return fd;
}

off_t gzf_lseek(int fd, off_t offset, int whence) {
    if (fd<0 || fd>=MAXFD || gzf_fds[fd]==NULL)
	return lseek(fd, offset, whence);
    else
	return gzseek(gzf_fds[fd], offset, whence);
}

int gzf_read(int fd, void *buf, size_t count) {
    if (fd<0 || fd>=MAXFD || gzf_fds[fd]==NULL)
	return read(fd, buf, count);
    else
	return gzread(gzf_fds[fd], buf, count);
}

void gzf_close(int fd) {
    if (fd<0 || fd>=MAXFD || gzf_fds[fd]==NULL)
	close(fd);
    else
	gzclose(gzf_fds[fd]);
}
#endif

