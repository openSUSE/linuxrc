/* Misc utility functions.
   Copyright 2000 Keith Owens <kaos@ocs.com.au>

   This file is part of the Linux modutils.

   This program is free software; you can redistribute it and/or modify it
   under the terms of the GNU General Public License as published by the
   Free Software Foundation; either version 2 of the License, or (at your
   option) any later version.

   This program is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software Foundation,
   Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.  */

#include <stdlib.h>
#include <string.h>
#include "util.h"


/*======================================================================*/

char *
xstrcat(char *dest, const char *src, size_t size)
{
  int ldest = strlen(dest);
  int lsrc = strlen(src);
  if ((size - ldest - 1) < lsrc) {
    error("xstrcat: destination overflow");
    exit(1);
  }
  memcpy(dest+ldest, src, lsrc+1);
  return(dest);
}
