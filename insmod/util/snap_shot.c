/* Take a snap shot of ksyms and modules for Oops debugging
   Copyright 1999 Linux International.

   Contributed by Keith Owens <kaos@ocs.com.au>

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
   Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA
  */

#include <errno.h>
#include <stdio.h>
#include <stdarg.h>
#include <time.h>
#include <unistd.h>
#include <string.h>

#include "module.h"
#include "obj.h"
#include "modstat.h"
#include "util.h"

static char snap_dir[] = "/var/log/ksymoops";

/* If snap_dir exists, take a snap shot of ksyms and modules to snap_dir.
 * Prefix the files with the equivalent of
 * date +%Y%m%d%T%M%S | sed -e 's/://g'
 */
void snap_shot(const char *module_names, int n_module_names)
{
	char file[] = "ccyymmddhhmmss.modules", buffer[4096];
	static char *infile[] = { "/proc/ksyms", "/proc/modules" };
	static char *suffix[] = {       "ksyms",       "modules" };
	struct tm *local;
	time_t t;
	int i, l;
	FILE *in, *out;

	if (module_names) {
		/* Only snap shot if the list of modules has changed.
		 * Otherwise auto cleanup takes a snap shot every time
		 * and ends up with a large snap shot directory.
		 */
		char *new_module_names;
		size_t n_new_module_names;
		get_kernel_info(0);
		new_module_names = module_name_list;
		n_new_module_names = n_module_stat;
		if (n_module_names && n_new_module_names == n_module_names) {
			while (n_module_names) {
				if (strcmp(module_names, new_module_names))
					break;	/* difference detected */
				i = strlen(module_names) + 1;
				module_names += i;
				new_module_names += i;
				--n_module_names;
			}
		}
		if (!n_module_names)
			return;	/* no difference, no need for snap shot */
	}

	if (chdir(snap_dir))
		return;
	t = time(NULL);
	local = localtime(&t);
	for (i = 0; i < sizeof(infile)/sizeof(infile[0]); ++i) {
		snprintf(file, sizeof(file), "%04d%02d%02d%02d%02d%02d.%s",
			local->tm_year+1900,
			local->tm_mon + 1,
			local->tm_mday,
			local->tm_hour,
			local->tm_min,
			local->tm_sec,
			suffix[i]);
		out = fopen(file, "w");
		if (!out) {
			error("cannot create %s/%s %m", snap_dir, file);
			return;
		}
		in = fopen(infile[i], "r");
		if (!in) {
			error("cannot open %s %m", infile[i]);
			return;
		}
		while ((l = fread(buffer, 1, sizeof(buffer), in)) > 0) {
			if (fwrite(buffer, l, 1, out) != 1) {
				error("unable to write to %s %m", file);
				fclose(in);
				fclose(out);
				return;
			}
		}
		if (ferror(in))
			error("unable to read from %s %m", infile[i]);
		fclose(in);
		fflush(out);
		fdatasync(fileno(out));
		fclose(out);
	}
}

/* If snap_dir exists, log a message to snap_dir.  The log file is called the
 * equivalent of date +%Y%m%d | sed -e 's/://g'.  Each line is prefixed with
 * timestamp down to seconds and followed by a newline.
 */
void snap_shot_log(const char *fmt,...)
{
	char date[] = "ccyymmdd", file[] = "ccyymmdd.log", stamp[] = "ccyymmdd hhmmss";
	struct tm *local;
	time_t t;
	FILE *log;
	va_list args;
	int save_errno = errno;

	if (chdir(snap_dir))
		return;
	t = time(NULL);
	local = localtime(&t);
	snprintf(date, sizeof(date), "%04d%02d%02d",
			local->tm_year+1900,
			local->tm_mon + 1,
			local->tm_mday);
	snprintf(file, sizeof(file), "%s.log", date);
	log = fopen(file, "a");
	if (!log) {
		error("cannot create %s/%s %m", snap_dir, file);
		return;
	}
	snprintf(stamp, sizeof(stamp), "%s %02d%02d%02d",
		date,
		local->tm_hour,
		local->tm_min,
		local->tm_sec);
	fprintf(log, "%s ", stamp);
	va_start(args, fmt);
	errno = save_errno;	/* fmt may use %m */
	vfprintf(log, fmt, args);
	va_end(args);
	fprintf(log, "\n");
	fflush(log);
	fdatasync(fileno(log));
	fclose(log);
}
