/*
 * Handle expansion of meta charaters
 *
 * Copyright 1999 Björn Ekwall <bj0rn@blox.se>
 *
 * "kernelversion" idea from the Debian release via:
 *	Wichert Akkerman <wakkerma@cs.leidenuniv.nl>
 *
 * Use wordexp(): idea from Tim Waugh <tim@cyberelk.demon.co.uk>
 *
 * Alpha typecast: Michal Jaegermann <michal@ellpspace.math.ualberta.ca>
 *
 * This file is part of the Linux modutils.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#ifdef HAVE_WORDEXP
#undef HAVE_WORDEXP
#define HAVE_WORDEXP 0
#endif

#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <string.h>
#include <ctype.h>
#if HAVE_WORDEXP
#include <wordexp.h>
#elif HAVE_GLOB
#include <glob.h>
#endif
#include "util.h"

/*
 * Split into words delimited by whitespace,
 * handle remaining quotes though...
 * If strip_quotes != 0 then strip one level of quotes from the line.
 */
static void split_line(GLOB_LIST *g, char *line, int strip_quotes)
{
	int len;
	char *d;
	char *e;
	char *p;
	char tmpline[PATH_MAX];

	for (p = line; *p; p = e) {
		/* Skip leading whitespace */
		while (*p && isspace(*p))
			++p;

		/* find end of word */
		d = tmpline;
		for (e = p; *e && !(isspace(*e)); ++e) {
			char match;

			/* Quote handling */
			switch (*e) {
			case '\\':
				if (!strip_quotes)
					*d++ = *e;
				break;

			case '"':
			case '\'':
				match = *e;
				if (!strip_quotes)
					*d++ = *e;
				for (++e; *e && *e != match; ++e) {
					*d++ = *e;
					if (*e == '\\' && *(e + 1) == match)
						*d++ = *++e;
				}
				if (!strip_quotes)
					*d++ = *e;
				break;

			default:
				*d++ = *e;
				break;
			}
		}

		if ((len = (int)(d - tmpline)) > 0) {
			char *str = xmalloc(len + 1);
			strncpy(str, tmpline, len);
			str[len] = '\0';
			g->pathv = (char **)xrealloc(g->pathv,
				   (g->pathc + 2) * sizeof(char *));
			g->pathv[g->pathc++] = str;
		}
	}

	if (g->pathc)
		g->pathv[g->pathc] = NULL;
}

static int glob_it(char *pt, GLOB_LIST *g)
{
#if HAVE_WORDEXP
	wordexp_t w;

	memset(&w, 0, sizeof(w));
	if (wordexp(pt, &w, WRDE_UNDEF)) {
		/*
		error("wordexp %s failed", pt);
		*/
		return -1;
	}
	/* else */
	g->pathc = w.we_wordc;
	g->pathv = w.we_wordv;

	return 0;
#elif HAVE_GLOB /* but not wordexp */
	glob_t w;

	memset(&w, 0, sizeof(w));
	if (glob(pt, GLOB_NOSORT, NULL, &w)) {
		/*
		error("glob %s failed", pt);
		*/
		return -1;
	}
	/* else */
	if (w.gl_pathc && strpbrk(w.gl_pathv[0], SHELL_META)) {
		globfree(&w);
		return -1;
	}
	g->pathc = w.gl_pathc;
	g->pathv = w.gl_pathv;

	return 0;
#else /* Neither wordexp nor glob */
	return -1;
#endif
}

/*
 * Expand the string (including meta-character) to a list of matches
 *
 * Return 0 if OK else -1
 */
int meta_expand(char *pt, GLOB_LIST *g, char *base_dir, char *version)
{
	FILE *fin;
	int len = 0;
	char *line = NULL;
	char *p;
	char tmpline[PATH_MAX + 11];

	g->pathc = 0;
	g->pathv = NULL;

	/*
	 * Take care of version dependent expansions
	 * Needed for forced version handling
	 */
	if ((p = strchr(pt, '`')) != NULL) {
		do {
			char wrk[PATH_MAX + 1];
			char *s;

			for (s = p + 1; isspace(*s); ++s)
				;

			if (strncmp(s, "uname -r", 8) == 0) {
				while (*s && (*s != '`'))
					++s;
				if (*s == '`') {
					*p = '\0';
					sprintf(wrk, "%s%s%s",
						     pt,
						     version,
						     s + 1);
				}
				strcpy(tmpline, wrk);
				pt = tmpline;
			} else if (strncmp(s, "kernelversion", 13) == 0) {
				while (*s && (*s != '`'))
					++s;
				if (*s == '`') {
					int n;
					char *k;

					*p = '\0';
					for (n = 0, k = version; *k; ++k) {
						if (*k == '.' && ++n == 2)
							break;
					}
					sprintf(wrk, "%s%.*s%s",
						     pt,
						     /* typecast for Alpha */
						     (int)(k - version),
						     version,
						     s + 1);
					strcpy(tmpline, wrk);
					pt = tmpline;
				}
			} else
				break;
		} while ((p = strchr(pt, '`')) != NULL);
	}
	
	/*
	 * Any remaining meta-chars?
	 */
	if (strpbrk(pt, SHELL_META) == NULL) {
		/*
		 * No meta-chars.
		 * Split into words, delimited by whitespace.
		 */
		sprintf(tmpline, "%s%s", (base_dir ? base_dir : ""), pt);
		if ((p = strtok(tmpline, " \t\n")) != NULL) {
			while (p) {
				g->pathv = (char **)xrealloc(g->pathv,
					   (g->pathc + 2) * sizeof(char *));
				g->pathv[g->pathc++] = xstrdup(p);
				p = strtok(NULL, " \t\n");
			}
		}
		if (g->pathc)
			g->pathv[g->pathc] = NULL;
		return 0;
	}
	/* else */
	/*
	 * Handle remaining meta-chars
	 */

	/*
	 * Just plain quotes?
	 */
	if (strpbrk(pt, "&();|<>$`!{}[]~=+:?*") == NULL &&
	    (p = strpbrk(pt, "\"'\\"))) {
		split_line(g, pt, 1);
		return 0;
	}

	if (strpbrk(pt, "&();|<>$`\"'\\!{}~+:[]~?*") == NULL) {
		/* Only "=" remaining, should be module options */
		split_line(g, pt, 0);
		return 0;
	}

	/*
	 * If there are meta-characters and
	 * if they are only shell glob meta-characters: do globbing
	 */
#if HAVE_WORDEXP
	if (strpbrk(pt, "&();|<>`\"'\\!{}~=+:") == NULL &&
	    strpbrk(pt, "$[]~?*"))
#else
	if (strpbrk(pt, "&();|<>$`\"'\\!{}~=+:") == NULL &&
	    strpbrk(pt, "[]~?*"))
#endif
		if (glob_it(pt, g) == 0)
			return 0;

	if (strpbrk(pt, "&();|<>$`\"'\\!{}~+:[]~?*") == NULL) {
		/* Only "=" remaining, should be module options */
		split_line(g, pt, 0);
		return 0;
	}

	/*
	 * Last resort: Use "echo"
	 */
	sprintf(tmpline, "/bin/echo %s%s", (base_dir ? base_dir : ""), pt);
	if ((fin = popen(tmpline, "r")) == NULL) {
		error("Can't execute: %s", tmpline);
		return -1;
	}
	/* else */

	/*
	 * Collect the result
	 */
	while (fgets(tmpline, PATH_MAX, fin) != NULL) {
		int l = strlen(tmpline);

		line = (char *)xrealloc(line, len + l + 1);
		line[len] = '\0';
		strcat(line + len, tmpline);
		len += l;
	}
	pclose(fin);

	if (line) {
		split_line(g, line, 0);
		free(line);
	}

	return 0;
}
