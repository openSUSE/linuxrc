/* Copyright (C) 1994 Free Software Foundation, Inc.
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
not, write to the, 1992 Free Software Foundation, Inc., 675 Mass Ave,
Cambridge, MA 02139, USA.  */

/*
 *      ANSI Standard: 4.1.3 Errors     <errno.h>
 */

#ifndef _ERRNO_H
#define _ERRNO_H

#include <features.h>
#include <linux/errno.h>

#ifdef  __USE_BSD
extern int sys_nerr;
extern char *sys_errlist[];
#endif
#ifdef  __USE_GNU
extern int _sys_nerr;
extern char *_sys_errlist[];
#endif

__BEGIN_DECLS

extern void	perror __P ((__const char* __s));
extern char*	strerror __P ((int __errno));

#if defined(_POSIX_THREAD_SAFE_FUNCTIONS) || defined(_REENTRANT)
extern int*	__errno_location  __P((void));
#define errno	(*__errno_location ())
#else
extern int errno;
#endif

#if _MIT_POSIX_THREADS
#define pthread_errno(x)        pthread_run->error_p =(x)
#endif

__END_DECLS

#endif
