#include "libioP.h"
#include "stdio.h"

/* Define non-macro versions of stdin/stdout/stderr,
 * for use by debuggers. */

#undef stdin
#undef stdout
#undef stderr

#ifdef __SVR4_I386_ABI_L1__
FILE* stdin = (&__iob [0]);
FILE* stdout = (&__iob [1]);
FILE* stderr = (&__iob [2]);
#else
FILE* stdin = &_IO_stdin_.file;
FILE* stdout = &_IO_stdout_.file;
FILE* stderr = &_IO_stderr_.file;
#endif
