#include "libioP.h"
#include "stdio.h"


int
fputc( int c, FILE *fp )
{
#ifdef _LIBPTHREAD
  int ret;
  CHECK_FILE(fp, EOF);
  flockfile (fp);
  ret = _IO_putc(c, fp);
  funlockfile (fp);
  return ret;
#else
  CHECK_FILE(fp, EOF);
  return _IO_putc(c, fp);
#endif
}

#ifndef _LIBPTHREAD
#ifdef __ELF__
#ifdef __GNUC__
int fputc( int, FILE * ) __attribute__ ((weak));
#else
#pragma weak fputc
#endif   /* __GNUC__ */
#endif   /* __ELF__ */
#endif   /* _LIBPTHREAD */
