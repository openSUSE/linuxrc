#include "libioP.h"
#include "stdio.h"

#ifndef _LIBPTHREAD
#ifdef __ELF__
#ifdef __GNUC__
void clearerr( FILE * ) __attribute__ ((weak));
#else
#pragma weak clearerr
#endif   /* __GNUC__ */
#endif   /* __ELF__ */
#endif   /* _LIBPTHREAD */


void
clearerr( FILE* fp )
{
  CHECK_FILE(fp, /*nothing*/);
#ifdef _LIBPTHREAD
  flockfile (fp);
#endif
  _IO_clearerr(fp);
#ifdef _LIBPTHREAD
  funlockfile (fp);
#endif
}

