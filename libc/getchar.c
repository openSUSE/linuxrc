#include "libioP.h"
#include "stdio.h"

#undef getchar


int
getchar ( void )
{
#ifdef _LIBPTHREAD
  int ret;
  flockfile (stdin);
  ret = _IO_getc(stdin);
  funlockfile (stdin);
  return ret;
#else
  return _IO_getc (stdin);
#endif
}

#ifndef _LIBPTHREAD
#ifdef __ELF__
#ifdef __GNUC__
int getchar ( void ) __attribute__ ((weak));
#else
#pragma weak getchar
#endif   /* __GNUC__ */
#endif   /* __ELF__ */
#endif   /* _LIBPTHREAD */
