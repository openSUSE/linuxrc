#include "libioP.h"
#include "stdio.h"
#include <gnu-stabs.h>

#pragma weak ferror

int
ferror(fp)
     FILE* fp;
{
  CHECK_FILE(fp, EOF);
  return _IO_ferror(fp);
}

#undef _IO_ferror
elf_alias (ferror, _IO_ferror);
