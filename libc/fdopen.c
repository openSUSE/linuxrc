#include "libioP.h"

#ifdef __linux__
 
#undef HAVE_GNU_LD
#define HAVE_GNU_LD
#include <gnu-stabs.h>
  
weak_alias (_IO_fdopen, fdopen);
   
#else

_IO_FILE *
fdopen (fd, mode)
     int fd;
     const char *mode;
{
  return _IO_fdopen (fd, mode);
}

#endif
