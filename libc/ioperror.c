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

#include "libioP.h"
#include <errno.h>
#ifndef errno
extern int errno;
#endif

#if NLS
#include "nl_types.h"
#endif

/* Also glibc? */
#ifdef __linux__

/* Print a line on stderr consisting of the text in S, a colon, a space,
   a message describing the meaning of the contents of `errno' and a newline.
   If S is NULL or "", the colon and space are omitted.  */
void
DEFUN(_IO_perror, (s), register CONST char *s)
{
  int errnum = errno;
  CONST char *colon;

#if NLS
	libc_nls_init();
#endif
  if (s == NULL || *s == '\0')
    s = colon = "";
  else
    colon = ": ";

  if (errnum >= 0 && errnum < _sys_nerr)
#if NLS
    (void) _IO_fprintf(_IO_stderr, "%s%s%s\n", s, colon,
	   catgets(_libc_cat, ErrorListSet, errnum +1, (char *) _sys_errlist[errnum]));
#else
    (void) _IO_fprintf(_IO_stderr, "%s%s%s\n", s, colon, _sys_errlist[errnum]);
#endif
  else
#if NLS
    (void) _IO_fprintf(_IO_stderr, "%s%s%s %d\n", s, colon,
		   catgets(_libc_cat, ErrorListSet, 1,  "Unknown error"),
		   errnum);
#else
    (void) _IO_fprintf(_IO_stderr, "%s%sUnknown error %d\n", s, colon, errnum);
#endif
}

#else

#include <string.h>

#ifndef _IO_strerror
extern char* _IO_strerror __P((int));
#endif

void
DEFUN(_IO_perror, (s),
      const char *s)
{
  char *error = _IO_strerror (errno);

  if (s != NULL && *s != '\0')
    _IO_fprintf (_IO_stderr, "%s:", s);

  _IO_fprintf (_IO_stderr, "%s\n", error ? error : "");

}
#endif

#if defined(__ELF__) || defined(__GNU_LIBRARY__)
#include <gnu-stabs.h>
#ifdef weak_alias
weak_alias (_IO_perror, perror);
#endif
#endif
