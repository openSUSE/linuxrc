/* Copyright (C) 1991, 1992, 1993 Free Software Foundation, Inc.
This file is part of the GNU C Library.

The GNU C Library is free software; you can redistribute it and/or
modify it under the terms of the GNU Library General Public License as
published by the Free Software Foundation; either version 2 of the
License, or (at your option) any later version.

The GNU C Library is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
Library General Public License for more details.

You should have received a copy of the GNU Library General Public
License along with the GNU C Library; see the file COPYING.LIB.  If
not, write to the Free Software Foundation, Inc., 675 Mass Ave,
Cambridge, MA 02139, USA.  */

#include <ansidecl.h>
#include <errno.h>
#ifdef __STDC__
#include <stddef.h>
#endif
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include "libioP.h"
#include "stdio.h"

extern char *__secure_getenv(const char *);

#ifndef _IO_open
#define _IO_open open
#endif

#ifndef _IO_close
#define _IO_close close
#endif

#ifndef _IO_stat
#define _IO_stat stat
#endif

#ifndef _IO_getpid
#define _IO_getpid getpid
#endif

#ifndef _IO_geteuid
#define _IO_geteuid geteuid
#endif

/* Return nonzero if DIR is an existent directory.  */
static int
DEFUN(diraccess, (dir), CONST char *dir)
{
  struct stat buf;
  _IO_uid_t euid;

  if (_IO_stat(dir, &buf) != 0 || !S_ISDIR(buf.st_mode)) return 0;

  /* That is going to be tough. */

  euid = _IO_geteuid ();
  
  /* super user */
  if (!euid) return 1;

  if (euid == buf.st_uid)
    return ((buf.st_mode & S_IWUSR) && (buf.st_mode & S_IXUSR));

  if (_IO_getegid () == buf.st_gid)
    return ((buf.st_mode & S_IWGRP) && (buf.st_mode & S_IXGRP));

  return ((buf.st_mode & S_IWOTH) && (buf.st_mode & S_IXOTH));
}


/* Return nonzero if FILE exists.  */
static int
DEFUN(exists, (file), CONST char *file)
{
  /* We can stat the file even if we can't read its data.  */
  struct stat st;
  int save = errno;
  if (_IO_stat (file, &st) == 0)
    return 1;
  else
    {
      /* We report that the file exists if stat failed for a reason other
	 than nonexistence.  In this case, it may or may not exist, and we
	 don't know; but reporting that it does exist will never cause any
	 trouble, while reporting that it doesn't exist when it does would
	 violate the interface of __stdio_gen_tempname.  */
      int exists = errno != ENOENT;
      errno = save;
      return exists;
    }
}


/* These are the characters used in temporary filenames.  */
static CONST char letters[] =
  "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";

/* Generate a temporary filename and return it (in a static buffer).  If
   STREAMPTR is not NULL, open a stream "w+b" on the file and set
   *STREAMPTR to it.  If DIR_SEARCH is nonzero, DIR and PFX are used as
   described for tempnam.  If not, a temporary filename in P_tmpdir with no
   special prefix is generated.  If LENPTR is not NULL, *LENPTR is set the
   to length (including the terminating '\0') of the resultant filename,
   which is returned.  This goes through a cyclic pattern of all possible
   filenames consisting of five decimal digits of the current pid and three
   of the characters in `letters'.  Data for tempnam and tmpnam is kept
   separate, but when tempnam is using P_tmpdir and no prefix (i.e, it is
   identical to tmpnam), the same data is used.  Each potential filename is
   tested for an already-existing file of the same name, and no name of an
   existing file will be returned.  When the cycle reaches its end
   (12345ZZZ), NULL is returned.  */
char *
DEFUN(_IO_gen_tempname, (dir, pfx, dir_search, lenptr, streamptr),
      CONST char *dir AND CONST char *pfx AND
      int dir_search AND _IO_size_t *lenptr AND
      _IO_FILE **streamptr)
{
  int saverrno = errno;
  static CONST char tmpdir[] = P_tmpdir;
  static struct
    {
      unsigned char digits [4];
    } infos[2], *info;
  static char buf[FILENAME_MAX];
  static _IO_pid_t oldpid = (pid_t) 0;
  _IO_pid_t pid = _IO_getpid();
  register _IO_size_t len, plen, dlen, flen;
  int i, carry;

  if (dir_search)
    {
      /* 
       * If this is not __secure_getenv you can probably clobber any
       * file on the system if you can find a setuid program calling 
       * this code. N.B., also: diraccess() isn't very safe.
       */
      register CONST char *d = __secure_getenv("TMPDIR");
      if (d != NULL && !diraccess(d))
	d = NULL;
      if (d == NULL && dir != NULL && diraccess(dir))
	d = dir;
      if (d == NULL && diraccess(tmpdir))
	d = tmpdir;
      if (d == NULL && diraccess("/tmp"))
	d = "/tmp";
      if (d == NULL)
	{
	  errno = ENOENT;
	  return NULL;
	}
      dir = d;
    }
  else
    dir = tmpdir;

  dlen = strlen (dir);

  /* Remove trailing slashes from the directory name.  */
  while (dlen > 1 && dir[dlen - 1] == '/')
    --dlen;

  if (pfx != NULL && *pfx != '\0')
    {
      plen = strlen(pfx);
      if (plen > 5)
	plen = 5;
    }
  else
    plen = 0;

  if (dir != tmpdir && !strcmp(dir, tmpdir))
    dir = tmpdir;
  info = &infos[(plen == 0 && dir == tmpdir) ? 1 : 0];

  if (pid != oldpid)
    {
      oldpid = pid;
      for (i = 0; i < sizeof (info->digits); i++)
	infos[0].digits[i] = infos[1].digits[i] = 0;
    }

  flen = dlen + 1 + plen + 5;
  for (;;)
    {
      if (info->digits [sizeof (info->digits) - 1])
	{
	  errno = EEXIST;
	  return NULL;
	}

      len = flen;
      if ((sizeof (buf) - sizeof (info->digits)) < len ||
		sprintf(buf, "%.*s/%.*s%.5d", (int) dlen, dir,
		(int) plen, pfx, pid % 100000) != (int) len)
	return NULL;

      /* Get the last part of string */
      for (i = 0; i < sizeof (info->digits) - 1; i++)
        buf [len++] = letters [info->digits [i]];
      buf [len] = '\0';

      /* Always return a unique string.  */
      carry = ++info->digits [0] / (sizeof (letters) - 1);
      info->digits [0] %= (sizeof (letters) - 1);
      for (i = 1; i < sizeof (info->digits); i++) {
	  info->digits [i] += carry;
	  carry = info->digits [i] / (sizeof (letters) - 1);
	  info->digits [i] %= (sizeof (letters) - 1);
      }

      if (streamptr != NULL)
	{
	  int fd = _IO_open (buf, O_RDWR|O_CREAT|O_EXCL, 0666);
	  if (fd >= 0)
	    {
	      *streamptr = _IO_fdopen (fd, "w+b");
	      if (*streamptr == NULL)
		{
		  int save = errno;
		  (void) _IO_close (fd);
		  errno = save;
		  return NULL;
		}
	      break;
	    }
	}
      else if (!exists (buf))
	break;
    }

  errno = saverrno;

  if (lenptr != NULL)
    *lenptr = len + 1;
  return buf;
}
