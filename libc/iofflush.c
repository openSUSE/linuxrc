/* 
Copyright (C) 1993 Free Software Foundation

This file is part of the GNU IO Library.  This library is free
software; you can redistribute it and/or modify it under the
terms of the GNU General Public License as published by the
Free Software Foundation; either version 2, or (at your option)
any later version.

This library is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this library; see the file COPYING.  If not, write to the Free
Software Foundation, 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

As a special exception, if you link this library with files
compiled with a GNU compiler to produce an executable, this does not cause
the resulting executable to be covered by the GNU General Public License.
This exception does not however invalidate any other reasons why
the executable file might be covered by the GNU General Public License. */

#include "libioP.h"

#ifdef _LIBPTHREAD
#define _IO_fflush fflush
#endif

int
DEFUN(_IO_fflush, (fp),
      register _IO_FILE *fp)
{
  int result;
  if (fp == NULL)
  {
#ifdef _LIBPTHREAD
    __libc_lock_lock (__libc_libio_lock);
#endif
    result = _IO_flush_all();
#ifdef _LIBPTHREAD
    __libc_lock_unlock (__libc_libio_lock);
#endif
  }
  else
    {
      CHECK_FILE(fp, EOF);
#ifdef _LIBPTHREAD
      flockfile (fp); 
#endif
      result = _IO_SYNC (fp) ? EOF : 0;
#ifdef _LIBPTHREAD
      funlockfile (fp); 
#endif
    }
  return result;
}

#ifndef _LIBPTHREAD
#if defined(__ELF__) || defined(__GNU_LIBRARY__)
#include <gnu-stabs.h>
#ifdef weak_alias
weak_alias (_IO_fflush, fflush);
#endif
#endif
#endif
