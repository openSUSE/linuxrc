#ifndef MODUTILS_UTIL_H
#define MODUTILS_UTIL_H 1

/* Miscelaneous utility functions.
   Copyright 1996, 1997 Linux International.

   Contributed by Richard Henderson <rth@tamu.edu>

   This file is part of the Linux modutils.

   This program is free software; you can redistribute it and/or modify it
   under the terms of the GNU General Public License as published by the
   Free Software Foundation; either version 2 of the License, or (at your
   option) any later version.

   This program is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software Foundation,
   Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.  */


#include <stdio.h>
#include <sys/stat.h>

#define SHELL_META "&();|<>$`\"'\\!{}[]~=+:?*" /* Sum of bj0rn and Debian */

void *xmalloc(size_t);
void *xrealloc(void *, size_t);
char *xstrdup(const char *);
char *xstrcat(char *, const char *, size_t);
int   xsystem(const char *, char *const[]);
int   arch64(void);

typedef int (*xftw_func_t)(const char *, const struct stat *);
extern int xftw(const char *directory, xftw_func_t);

/* Error logging */
extern int log;
extern int errors;
extern const char *error_file;

extern int flag_verbose;
extern void verbose(const char *ctl,...);

void error(const char *fmt, ...)
#ifdef __GNUC__
  __attribute__((format(printf, 1, 2)))
#endif
  ;

void lprintf(const char *fmt, ...)
#ifdef __GNUC__
  __attribute__((format(printf, 1, 2)))
#endif
  ;

void setsyslog(const char *program);

/*
 * Generic globlist <bj0rn@blox.se>
 */
typedef struct {
	int pathc;       /* Count of paths matched so far  */
	char **pathv;    /* List of matched pathnames.  */
} GLOB_LIST;
int meta_expand(char *pt, GLOB_LIST *g, char *base_dir, char *version, int type);
#define ME_BUILTIN_COMMAND	1
#define ME_SHELL_COMMAND	2
#define ME_GLOB			4
#define ME_ALL			(ME_GLOB|ME_SHELL_COMMAND|ME_BUILTIN_COMMAND)

extern void snap_shot(const char *module_name, int number);
extern void snap_shot_log(const char *fmt,...);

#ifdef CONFIG_USE_ZLIB
int gzf_open(const char *name, int mode);
int gzf_read(int fd, void *buf, size_t count);
off_t gzf_lseek(int fd, off_t offset, int whence);
void gzf_close(int fd);

#else /* ! CONFIG_USE_ZLIB */

#include <unistd.h>

#define gzf_open	open
#define gzf_read	read
#define gzf_lseek	lseek
#define gzf_close	close

#endif /* CONFIG_USE_ZLIB */

#define SYMPREFIX "__insmod_";
extern const char symprefix[10];	/* Must be sizeof(SYMPREFIX), including nul */

#endif /* util.h */
