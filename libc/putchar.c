#include "libioP.h"
#include "stdio.h"
#undef putchar

int
putchar( int c )
{
#ifdef _LIBPTHREAD
  int ret;
  flockfile (stdout);
  ret = _IO_putc(c, stdout);
  funlockfile (stdout);
  return ret;
#else
  return _IO_putc(c, stdout);
#endif
}

#ifndef _LIBPTHREAD
#ifdef __ELF__
#ifdef __GNUC__
int putchar( int ) __attribute__ ((weak));
#else
#pragma weak  putchar
#endif   /* __GNUC__ */
#endif   /* __ELF__ */
#endif   /* _LIBPTHREAD */
