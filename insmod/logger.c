/* Error logging facilities.
   Copyright 1996, 1997 Linux International.

   Contributed by Richard Henderson <rth@tamu.edu>

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

#ident "$Id: logger.c,v 1.1 1999/12/14 12:38:12 snwint Exp $"

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <errno.h>
#include <syslog.h>

#include "util.h"


/*======================================================================*/

static int log;
static int silent;

int errors;
const char *error_file;
const char *program_name;

void
error(const char *fmt, ...)
{
  va_list args;

  if (silent)
    ;
  else if (log)
    {
      char buf[1024];
      int n;

      if (error_file)
        n = snprintf(buf, sizeof(buf), "%s: ", error_file);
      else
	n = 0;
      va_start(args, fmt);
      vsnprintf(buf+n, sizeof(buf)-n, fmt, args);
      va_end(args);

      syslog(LOG_ERR, "%s", buf);
    }
  else
    {
      if (error_file)
        fprintf(stderr, "%s: ", error_file);
      va_start(args, fmt);
      vfprintf(stderr, fmt, args);
      va_end(args);
      putc('\n', stderr);
    }

  errors++;
}

void
lprintf(const char *fmt, ...)
{
  va_list args;

  if (silent)
    ;
  else if (log)
    {
      char buf[1024];
      va_start(args, fmt);
      vsnprintf(buf, sizeof(buf), fmt, args);
      va_end(args);
      syslog(LOG_INFO, "%s", buf);
    }
  else
    {
      va_start(args, fmt);
      vfprintf(stdout, fmt, args);
      va_end(args);
      putchar('\n');
    }
}

void setsyslog(const char *program)
{
  openlog(program, LOG_CONS, LOG_DAEMON);
  log = 1;
}
