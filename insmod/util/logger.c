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
   Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA
  */

#include <limits.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <errno.h>
#include <syslog.h>

#include "util.h"

/*======================================================================*/

int log;
static int silent;

int errors;
const char *error_file;
const char *program_name;

#define STOREMSG
#ifdef STOREMSG
struct cbuf {
	struct cbuf *next;
	int type;
	char *msg;
} *head, *tail;

static void savemsg(int type, char *msg)
{
	struct cbuf *me = (struct cbuf *)xmalloc(sizeof(struct cbuf));
	char *s = xstrdup(msg);

	me->next = NULL;
	me->type = type;
	me->msg = s;

	if (tail)
		tail->next = me;
	else
		head = me;
	tail = me;
}

static void dumpmsg(void)
{
	for (;head; head = head->next)
		syslog(head->type, "%s", head->msg);
}
#endif /* STOREMSG */

void error(const char *fmt,...)
{
	va_list args;

	if (silent)
		;
	else if (log) {
		char buf[2*PATH_MAX];
		int n;

		if (error_file)
			n = snprintf(buf, sizeof(buf), "%s: ", error_file);
		else
			n = 0;
		va_start(args, fmt);
		vsnprintf(buf + n, sizeof(buf) - n, fmt, args);
		va_end(args);
#ifdef STOREMSG
		savemsg(LOG_ERR, buf);
#else
		syslog(LOG_ERR, "%s", buf);
#endif
	} else {
		if (error_file)
			fprintf(stderr, "%s: ", error_file);
		va_start(args, fmt);
		vfprintf(stderr, fmt, args);
		va_end(args);
		putc('\n', stderr);
	}

	errors++;
}

void lprintf(const char *fmt,...)
{
	va_list args;

	if (silent);
	else if (log) {
		char buf[2*PATH_MAX];
		va_start(args, fmt);
		vsnprintf(buf, sizeof(buf), fmt, args);
		va_end(args);
#ifdef STOREMSG
		savemsg(LOG_INFO, buf);
#else
		syslog(LOG_INFO, "%s", buf);
#endif
	} else {
		va_start(args, fmt);
		vfprintf(stdout, fmt, args);
		va_end(args);
		putchar('\n');
	}
}

void setsyslog(const char *program)
{
	openlog(program, LOG_CONS, LOG_DAEMON);
#ifdef STOREMSG
	atexit(dumpmsg);
#endif
	log = 1;
}
