#include "libioP.h"
#include "stdio.h"

FILE*
freopen( const char* filename, const char* mode, FILE* fp )
{
  FILE *ret;
  CHECK_FILE(fp, NULL);
#ifdef _LIBPTHREAD
  flockfile (fp);
#endif
  if (!(fp->_flags & _IO_IS_FILEBUF))
  {
#ifdef _LIBPTHREAD
    funlockfile (fp);
#endif
    return NULL;
  }
#ifdef _LIBPTHREAD
  __libc_lock_lock (__libc_libio_lock);
#endif
  ret = _IO_freopen(filename, mode, fp);
#ifdef _LIBPTHREAD
  __libc_lock_unlock (__libc_libio_lock);
  funlockfile (fp);
#endif
  return ret;
}

#ifndef _LIBPTHREAD
#ifdef __ELF__
#ifdef __GNUC__
FILE *freopen( const char* filename, const char* mode, FILE* fp )
	__attribute__ ((weak));

#else
#pragma weak freopen
#endif   /* __GNUC__ */
#endif   /* __ELF__ */
#endif   /* _LIBPTHREAD */
