#include "libioP.h"
#include "stdio.h"

int
getw( FILE *fp )
{
  int w;
  _IO_size_t bytes_read;
  CHECK_FILE(fp, EOF);
#ifdef _LIBPTHREAD
  flockfile (fp);
#endif
  bytes_read = _IO_sgetn (fp, (char*)&w, sizeof(w));
#ifdef _LIBPTHREAD
  funlockfile (fp);
#endif
  return sizeof(w) == bytes_read ? w : EOF;
}

#ifndef _LIBPTHREAD
#ifdef __ELF__
#ifdef __GNUC__
int getw( FILE * ) __attribute__ ((weak));
#else
#pragma weak getw
#endif   /* __GNUC__ */
#endif   /* __ELF__ */
#endif   /* _LIBPTHREAD */
