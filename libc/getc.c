#include "libioP.h"
#include "stdio.h"
#include <gnu-stabs.h>

#undef getc

int getc( FILE * ) __attribute__ ((weak));

int
getc( FILE *stream )
{
#ifdef _LIBPTHREAD
  int ret;
  flockfile (stream);
  ret = _IO_getc(stream);
  funlockfile (stream);
  return ret;
#else
  return _IO_getc (stream);
#endif
}

#undef _IO_getc
elf_alias (getc, _IO_getc);

#if 0
#ifndef _LIBPTHREAD
#ifdef __ELF__
#pragma weak getc
#endif
#endif
#endif	/* #if 0 */
