#include "stdio.h"
#include "libioP.h"


int
fseek( _IO_FILE* fp, long int offset, int whence )
{
#ifdef _LIBPTHREAD
  CHECK_FILE(fp, -1);
  flockfile (fp);
  whence = _IO_fseek (fp, offset, whence);
  funlockfile (fp);
  return whence;
#else
  CHECK_FILE(fp, -1);
  return _IO_fseek(fp, offset, whence);
#endif
}

#ifndef _LIBPTHREAD
#ifdef __ELF__
#ifdef __GNUC__
int fseek( _IO_FILE *, long int, int ) __attribute__ ((weak));
#else
#pragma weak fseek
#endif   /* __GNUC__ */
#endif   /* __ELF__ */
#endif   /* _LIBPTHREAD */
