#include "libioP.h"

#ifdef HAVE_GNU_LD

#include "stdio.h"
#include <unistd.h>
#include <ansidecl.h>
#include <gnu-stabs.h>

#if 1
function_alias(_cleanup, _IO_flush_all, void, (),
		DEFUN_VOID(_cleanup))
#else
/* We don't want this since we are building the shared library.
 * We want the shared library selfcontained. __libc_atexit will
 * ruin it unless we take exit () out of the shared library.
 */
text_set_element(__libc_atexit, _IO_cleanup);
#endif

#else

#if !defined (__linux__)

#if _G_HAVE_ATEXIT
#include <stdlib.h>

typedef void (*voidfunc) __P((void));

static void
DEFUN_VOID(_IO_register_cleanup)
{
  atexit ((voidfunc)_IO_cleanup);
  _IO_cleanup_registration_needed = 0;
}

void (*_IO_cleanup_registration_needed)() = _IO_register_cleanup;
#else
void (*_IO_cleanup_registration_needed)() = NULL;
#endif /* _G_HAVE_ATEXIT */

#endif

#endif
