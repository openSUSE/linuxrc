#include "libioP.h"
#include "stdio.h"


int
fgetc( FILE *fp )
{
#ifdef _LIBPTHREAD
  int ret;
  CHECK_FILE(fp, EOF);
  flockfile (fp);
  ret = _IO_getc(fp);
  funlockfile (fp);
  return ret;
#else
  CHECK_FILE(fp, EOF);
  return _IO_getc(fp);
#endif
}

#ifndef _LIBPTHREAD
#ifdef __ELF__
#ifdef __GNUC__
int fgetc( FILE * ) __attribute__ ((weak));
#else
#pragma weak fgetc
#endif
#endif
#endif
