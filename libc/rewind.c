#include "stdio.h"
#include "libioP.h"

void
rewind( _IO_FILE *fp )
{
  CHECK_FILE(fp, );
#ifdef _LIBPTHREAD
  flockfile (fp);
#endif
  _IO_rewind(fp);
#ifdef _LIBPTHREAD
  funlockfile (fp);
#endif
}

#ifndef _LIBPTHREAD
#ifdef __ELF__
#ifdef __GNUC__
void rewind( _IO_FILE * ) __attribute__ ((weak));
#else
#pragma weak rewind
#endif   /* __GNUC__ */
#endif   /* __ELF__ */
#endif   /* _LIBPTHREAD */
