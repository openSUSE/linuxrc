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

#if defined(__i386__) || defined(__m68k__) || defined(__arm__)

#define __NR__create_module  __NR_create_module
static inline _syscall2(long, _create_module, const char *, name, size_t, size)

unsigned long create_module(const char *name, size_t size)
{
  /* Why all this fuss?

     In linux 2.1, the address returned by create module point in
     kernel space which is now mapped at the top of user space (at
     0xc0000000 on i386). This looks like a negative number for a
     long. The normal syscall macro of linux 2.0 (and all libc compile
     with linux 2.0 or below) consider that the return value is a
     negative number and consider it is an error number (A kernel
     convention, return value are positive or negative, indicating the
     error number).

     By checking the value of errno, we know if we have been fooled by
     the syscall2 macro and we fix it.  */

  long ret = _create_module(name, size);
  if (ret == -1 && errno > 125)
    {
      ret = -errno;
      errno = 0;
    }
  return ret;
}

#elif defined(__alpha__)

/* Alpha doesn't have the same problem, exactly, but a bug in older
   kernels fails to clear the error flag.  Clear it here explicitly.  */

#define __NR__create_module  __NR_create_module
static inline _syscall4(unsigned long, _create_module, const char *, name,
			size_t, size, size_t, dummy, size_t, err);

unsigned long create_module(const char *name, size_t size)
{
  return _create_module(name, size, 0, 0);
}

#else

/* Sparc, MIPS, (and Alpha, but that's another problem) don't mistake
   return values for errors due to the nature of the system call.  */

_syscall2(unsigned long, create_module, const char *, name, size_t, size)

#endif
