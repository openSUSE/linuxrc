#include "libioP.h"
#include "stdio.h"
#include <gnu-stabs.h>

#undef putc

int
putc( int c, FILE *stream )
{
#ifdef _LIBPTHREAD
  int ret;
  flockfile (stream);
  ret = _IO_putc(c, stream);
  funlockfile (stream);
  return ret;
#else
  return _IO_putc(c, stream);
#endif
}

#ifndef _LIBPTHREAD
#ifdef __ELF__
#ifdef __GNUC__
int putc( int, FILE * ) __attribute__ ((weak));
#else
#pragma weak putc
#endif   /* __GNUC__ */
#endif   /* __ELF__ */
#endif   /* _LIBPTHREAD */

elf_alias (putc, _IO_putc);
