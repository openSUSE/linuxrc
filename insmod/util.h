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


#ifndef MODUTILS_UTIL_H
#define MODUTILS_UTIL_H 1

#ident "$Id: util.h,v 1.1 1999/12/14 12:38:12 snwint Exp $"

void error(const char *, ...)
#ifdef __GNUC__
  __attribute__((format(printf, 1, 2)))
#endif
  ;

void *xmalloc(size_t);
void *xrealloc(void *, size_t);
char *xstrdup(const char *);

#endif /* util.h */
