#include "libioP.h"
#include "stdio.h"

#undef putw

int
putw( int w, FILE *fp )
{
  _IO_size_t written;
  CHECK_FILE(fp, EOF);
#ifdef _LIBPTHREAD
  flockfile (fp);
#endif
  written = _IO_sputn(fp, (const char *)&w, sizeof(w));
#ifdef _LIBPTHREAD
  funlockfile (fp);
#endif
  return written == sizeof(w) ? 0 : EOF;
}

#ifndef _LIBPTHREAD
#ifdef __ELF__
#ifdef __GNUC__
int putw( int, FILE * ) __attribute__ ((weak));
#else
#pragma weak putw
#endif   /* __GNUC__ */
#endif   /* __ELF__ */
#endif   /* _LIBPTHREAD */
