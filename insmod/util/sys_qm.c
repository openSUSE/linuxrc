/* Functions for the Linux module syscall interface.
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

#include <stdlib.h>
#include <errno.h>

#include "module.h"

/* Kernel headers before 2.1.mumble need this on the Alpha to get
   _syscall* defined.  */
#define __LIBRARY__

#include <asm/unistd.h>


/*======================================================================*/

/* I am fucking tired of the "this doesn't build on 2.0.x" questions.
   But if you ask, we still officially require 2.1.x to build.  */
#if !defined(__NR_query_module)
# if defined(__i386__)
#  define __NR_query_module 167
# elif defined(__alpha__)
#  define __NR_query_module 347
# elif defined(__sparc__)
#  define __NR_query_module 184
# elif defined(__mc68000__)
#  define __NR_query_module 167
# elif defined(__arm__)
#  define __NR_query_module (__NR_SYSCALL_BASE + 167)
# elif defined(__mips__)
#  define __NR_query_module 4187
# endif
#endif

_syscall5(int, query_module, const char *, name, int, which,
	  void *, buf, size_t, bufsize, size_t *, ret);
