/* Misc utility functions.
   Copyright 1996, 1997 Linux International.
   Written by Keith Owens <kaos@ocs.com.au>

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
#include <sys/utsname.h>
#include "util.h"

/*======================================================================*/

/* Indicate if the current machine uses 64 bit architecture */
int arch64(void)
{
	struct utsname uts;
	char *uname_m;
	if (uname(&uts))
		return(0);
	if ((uname_m = getenv("UNAME_MACHINE"))) {
		int l = strlen(uname_m);
		if (l >= sizeof(uts.machine))
			l = sizeof(uts.machine)-1;
		memcpy(uts.machine, uname_m, l);
		uts.machine[l] = '\0';
	}
	return(strstr(uts.machine, "64") != NULL);
}
