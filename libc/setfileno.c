/* Some known programs (xterm, pdksh?) non-portably change the _file
   field of s struct _iobuf.  This kludge allows the same "functionality".
   This code is an undocumented feature for iostream/stdio. Use it at
   your own risk. */

#include "libioP.h"
#include "stdio.h"

void setfileno( _IO_FILE *, int );

void setfileno(_IO_FILE* fp, int fd) _IO_attr_weak;

void
setfileno( _IO_FILE *fp, int fd )
{
  CHECK_FILE(fp, );
#ifdef _LIBPTHREAD
  flockfile (fp);
#endif
  if ((fp->_flags & _IO_IS_FILEBUF) != 0)
    fp->_fileno = fd;
#ifdef _LIBPTHREAD
  funlockfile (fp);
#endif
}

#ifndef _LIBPTHREAD
#ifdef __ELF__
#ifdef __GNUC__
void setfileno( _IO_FILE *, int ) __attribute__ ((weak));
#else
#pragma weak setfileno
#endif   /* __GNUC__ */
#endif   /* __ELF__ */
#endif   /* _LIBPTHREAD */
