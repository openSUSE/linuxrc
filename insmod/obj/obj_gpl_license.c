/* Return the type of license for a module.  0 for GPL, 1 for no license, 2 for
   non-GPL.  The license parameter is set to the license string or NULL.

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
   Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
  */

#include <string.h>
#include "obj.h"

/* This list must match *exactly* the list of allowable licenses in
 * linux/include/linux/module.h.  Checking for leading "GPL" will not
 * work, somebody will use "GPL sucks, this is proprietary".
 */
static const char *gpl_licenses[] = {
	"GPL",
	"GPL and additional rights",
	"Dual BSD/GPL",
	"Dual MPL/GPL",
};

int obj_gpl_license(struct obj_file *f, const char **license)
{
	struct obj_section *sec;
	if ((sec = obj_find_section(f, ".modinfo"))) {
		const char *value, *ptr, *endptr;
		ptr = sec->contents;
		endptr = ptr + sec->header.sh_size;
		while (ptr < endptr) {
			if ((value = strchr(ptr, '=')) && strncmp(ptr, "license", value-ptr) == 0) {
				int i;
				if (license)
					*license = value+1;
				for (i = 0; i < sizeof(gpl_licenses)/sizeof(gpl_licenses[0]); ++i) {
					if (strcmp(value+1, gpl_licenses[i]) == 0)
						return(0);
				}
				return(2);
			}
			if (strchr(ptr, '\0'))
				ptr = strchr(ptr, '\0') + 1;
			else
				ptr = endptr;
		}
	}
	return(1);
}
