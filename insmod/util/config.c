/*
 * Handle the configuration, including /etc/modules.conf
 *
 * Copyright 1994, 1995, 1996, 1997:
 *	Jacques Gelinas <jack@solucorp.qc.ca>
 *	Björn Ekwall <bj0rn@blox.se> February 1999
 *	Keith Owens <kaos@ocs.com.au> October 1999
 *
 * "kernelversion" idea from the Debian release via:
 *      Wichert Akkerman <wakkerma@cs.leidenuniv.nl>
 *
 * Björn, inspired by Richard Henderson <rth@twiddle.net>, cleaned up
 * the wildcard handling and started using ftw in March 1999
 * Cleanup of hardcoded arrays: Björn Ekwall <bj0rn@blox.se> March 1999
 * Many additional keywords: Björn Ekwall <bj0rn@blox.se> (C) March 1999
 * Standardize on /etc/modules.conf Keith Owens <kaos@ocs.com.au> October 1999
 *
 * Alpha typecast:Michal Jaegermann <michal@ellpspace.math.ualberta.ca>
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

/*
 * Specification: /etc/modules.conf / format
 *	Modules may be located at different places in the filesystem.
 *
 *	The file /etc/modules.conf contains different definitions to
 *	control the manipulation of modules.
 *
 *	Standard Unix style comments and continuation line are supported.
 *	Comments begin with a # and continue until the end of the line.
 *	A line continues on the next one if the last non-white character
 *	is a \.
 */
/* #Specification: /etc/modules.conf / format / official name */

#include <stdio.h>
#include <stdarg.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include <sys/stat.h>
#include <signal.h>
#include <limits.h>
#include <sys/param.h>
#include <errno.h>

#include "util.h"
#include "config.h"
#include "alias.h"

int flag_autoclean; /* set/used by modprobe and insmod */

struct utsname uts_info;

struct PATH_TYPE *modpath;
int nmodpath = 0;
static int maxpath = 0;

struct EXEC_TYPE *execs;
int nexecs = 0;
static int maxexecs = 0;

OPT_LIST *opt_list;
static int n_opt_list;

OPT_LIST *abovelist;
static int n_abovelist;

OPT_LIST *belowlist;
static int n_belowlist;

OPT_LIST *prunelist;
static int n_prunelist;

OPT_LIST *probe_list;
static int n_probe_list;

OPT_LIST *probeall_list;
static int n_probeall_list;

OPT_LIST *aliases;
static int n_aliases;

char *persistdir = "/var/lib/modules/persist";

const char symprefix[] = SYMPREFIX;

char *insmod_opt = NULL;
char *config_file = NULL;	/* Which file was actually used */
time_t config_mtime;
int root_check_off = CONFIG_ROOT_CHECK_OFF;	/* Default is modules must be owned by root */
static char *config_version;	/* Hack for config_add */
int quick = 0;			/* Option -A */

/* The initialization order must match the gen_file_enum order in config.h */
struct gen_files gen_file[] = {
	{"generic_string", NULL, 0},
	{"pcimap", NULL, 0},
	{"isapnpmap", NULL, 0},
	{"usbmap", NULL, 0},
	{"parportmap", NULL, 0},
	{"ieee1394map", NULL, 0},
	{"pnpbiosmap", NULL, 0},
	{"dep", NULL, 0},
};

const int gen_file_count = sizeof(gen_file)/sizeof(gen_file[0]);

int flag_verbose;

unsigned long safemode;

void verbose(const char *ctl,...)
{
	if (flag_verbose) {
		va_list list;
		va_start(list, ctl);
		vprintf(ctl, list);
		va_end(list);
		fflush(stdout);
	}
}


/*
 *	Check to see if the existing modules.xxx files need updating,
 *	based on the timestamps of the modules and the config file.
 */
static int check_update (const char *file, const struct stat *sb)
{
	int len = strlen(file);
	int i;

	if (!S_ISREG(sb->st_mode))
		return 0;
	for (i = 0; i < gen_file_count; ++i) {
		if (sb->st_mtime > gen_file[i].mtime)
			break;
	}
	if (i == gen_file_count)
		return 0;	/* All generated files are up to date */

	if (len > 2 && !strcmp(file + len - 2, ".o"))
		return 1;
	else if (len > 4 && !strcmp(file + len - 4, ".mod"))
		return 1;
#ifdef CONFIG_USE_ZLIB
	else if (len > 5 && !strcmp(file + len - 5, ".o.gz"))
		return 1;
#endif
	return 0;
}

static int need_update (const char *force_ver, const char *base_dir)
{
	struct stat tmp;
	char dep[PATH_MAX];
	int i;
	uname (&uts_info);
	if (!force_ver)
		force_ver = uts_info.release;

	if (strlen (force_ver) > 50)
		/* That's just silly. */
		return 1;

	for (i = 0; i < gen_file_count; ++i) {
		if (stat(gen_file[i].name, &tmp))
			return 1;	/* No dependency file yet, so we need to build it. */
		gen_file[i].mtime = tmp.st_mtime;
	}

	if (stat ("/etc/modules.conf", &tmp) &&
	    stat ("/etc/conf.modules", &tmp))
		return 1;

	for (i = 0; i < gen_file_count; ++i) {
		if (tmp.st_mtime > gen_file[i].mtime)
			return 1;	/* Config file is newer. */
	}

	snprintf (dep, sizeof(dep), "%s/lib/modules/%s", base_dir, force_ver);
	return xftw (dep, check_update);
}


/*
 *	Strip white char at the end of a string.
 *	Return the address of the last non white char + 1 (point on the '\0').
 */
static char *strip_end(char *str)
{
	int len = strlen(str);

	for (str += len - 1; len > 0 && (isspace(*str)); --len, --str)
		*str = '\0';
	return str + 1;
}

/*
 *	Read a line of a configuration file and process continuation lines.
 *	Return buf, or NULL if EOF.
 *	Blank at the end of line are always stripped.
 *	Everything on a line following comchar is a comment.
 *
 *	Continuation character is \
 *	Comment character is #
 */
char *fgets_strip(char *buf, int sizebuf, FILE * fin, int *lineno)
{
	int nocomment = 1; /* No comments found ? */
	int contline = 0;
	char *start = buf;
	char *ret = NULL;
	char comchar = '#';
	char contchar = '\\';

	*buf = '\0';

	while (fgets(buf, sizebuf, fin) != NULL) {
		char *end = strip_end(buf);
		char *pt = strchr(buf, comchar);

		if (pt != NULL) {
			nocomment = 0;
			*pt = '\0';
			end = strip_end(buf);
		}

		if (lineno != NULL)
			(*lineno)++;
		ret = start;
		if (contline) {
			char *pt = buf;

			while (isspace(*pt))
				pt++;
			if (pt > buf + 1) {
				strcpy(buf + 1, pt);	/* safe, backward copy */
				buf[0] = ' ';
				end -= (int) (pt - buf) - 1;
			} else if (pt == buf + 1) {
				buf[0] = ' ';
			}
		}
		if (end > buf && *(end - 1) == contchar) {
			if (end == buf + 1 || *(end - 2) != contchar) {
				/* Continuation */
				contline = 1;
				end--;
				*end = '\0';
				buf = end;
			} else {
				*(end - 1) = '\0';
				break;
			}
		} else {
			break;
		}
	}

	return ret;
}

static char *next_word(char *pt)
{
	char *match;
	char *pt2;

	/* find end of word */
	for (pt2 = pt; *pt2 && !(isspace(*pt2)); ++pt2) {
		if ((match = strchr("\"'`", *pt2)) != NULL) {
			for (++pt2; *pt2 && *pt2 != *match; ++pt2) {
				if (*pt2 == '\\' && *(pt2 + 1) == *match)
					++pt2;
			}
		}
	}

	/* skip leading whitespace before next word */
	if (*pt2) {
		*pt2++ = '\0'; /* terminate last word */
		while (*pt2 && isspace(*pt2))
			++pt2;
	}
	return pt2;
}

static GLOB_LIST *addlist(GLOB_LIST *orig, GLOB_LIST *add)
{
	if (!orig)
		return add;
	/* else */
	orig->pathv = (char **)xrealloc(orig->pathv,
					(orig->pathc + add->pathc + 1) *
					sizeof(char *));
	memcpy(orig->pathv + orig->pathc, add->pathv,
	       add->pathc * sizeof(char *));
	orig->pathc += add->pathc;
	orig->pathv[orig->pathc] = NULL;
	/*
	free(add->pathv);
	free(add);
	*/
	return orig;
}

static void decode_list(int *n, OPT_LIST **list, char *arg, int adding,
			char *version, int opts)
{
	GLOB_LIST *pg;
	GLOB_LIST *prevlist = NULL;
	int i, autoclean = 1;
	int where = *n;
	char *arg2 = next_word(arg);

	if (opts && !strcmp (arg, "-k")) {
		if (!*arg2)
			error("Missing module argument after -k\n");
		arg = arg2;
		arg2 = next_word(arg);
		autoclean = 0;
	}

	for (i = 0; i < *n; ++i) {
		if (strcmp((*list)[i].name, arg) == 0) {
			if (adding)
				prevlist = (*list)[i].opts;
			else
				free((*list)[i].opts);
			(*list)[i].opts = NULL;
			where = i;
			break;
		}
	}
	if (where == *n) {
		(*list) = (OPT_LIST *)xrealloc((*list),
			  (*n + 2) * sizeof(OPT_LIST));
		(*list)[*n].name = xstrdup(arg);
		(*list)[*n].autoclean = autoclean;
		*n += 1;
		memset(&(*list)[*n], 0, sizeof(OPT_LIST));
	} else if (!autoclean)
		(*list)[where].autoclean = 0;
	pg = (GLOB_LIST *)xmalloc(sizeof(GLOB_LIST));
	meta_expand(arg2, pg, NULL, version, ME_ALL);
	(*list)[where].opts = addlist(prevlist, pg);
}

static void decode_exec(char *arg, int type)
{
	char *arg2;

	execs[nexecs].when = type;
	arg2 = next_word(arg);
	execs[nexecs].module = xstrdup(arg);
	execs[nexecs].cmd = xstrdup(arg2);
	if (++nexecs >= maxexecs) {
		maxexecs += 10;
		execs = (struct EXEC_TYPE *)xrealloc(execs,
			maxexecs * sizeof(struct EXEC_TYPE));
	}
}

static int build_list(char **in, OPT_LIST **out, char *version, int opts)
{
	GLOB_LIST *pg;
	int i;

	for (i = 0; in[i]; ++i) {
		char *p = xstrdup(in[i]);
		char *pt = next_word(p);
		char *pn = p;

		*out = (OPT_LIST *)xrealloc(*out, (i + 2) * sizeof(OPT_LIST));
		(*out)[i].autoclean = 1;
		if (opts && !strcmp (p, "-k")) {
		    pn = pt;
		    pt = next_word(pn);
		    (*out)[i].autoclean = 0;
		}
		pg = (GLOB_LIST *)xmalloc(sizeof(GLOB_LIST));
		meta_expand(pt, pg, NULL, version, ME_ALL);
		(*out)[i].name = xstrdup(pn);
		(*out)[i].opts = pg;
		free(p);
	}
	memset(&(*out)[i], 0, sizeof(OPT_LIST));

	return i;
}

/* Environment variables can override defaults, testing only */
static void gen_file_env(struct gen_files *gf)
{
	if (!safemode) {
		char *e = xmalloc(strlen(gf->base)+5), *p1 = gf->base, *p2 = e;
		while ((*p2++ = toupper(*p1++))) ;
		strcpy(p2-1, "PATH");	/* safe, xmalloc */
		if ((p2 = getenv(e)) != NULL) {
			free(gf->name);
			gf->name = xstrdup(p2);
		}
		free(e);
	}
}

/* Read a config option for a generated filename */
static int gen_file_conf(struct gen_files *gf, int assgn, const char *parm, const char *arg)
{

	int l = strlen(gf->base);
	if (assgn &&
	    strncmp(parm, gf->base, l) == 0 &&
	    strcmp(parm+l, "file") == 0 &&
	    !gf->name) {
		gf->name = xstrdup(arg);
		return(0);
	}
	return(1);
}

/* Check we have a name for a generated file */
static int gen_file_check(struct gen_files *gf, GLOB_LIST *g,
			  char *base_dir, char *version)
{
	char tmp[PATH_MAX];
	int ret = 0;
	if (!gf->name) {
		/*
		 * Specification: config file / no xxxfile parameter
		 * The default value for generated filename xxx is:
		 *
		 * xxxfile=/lib/modules/`uname -r`/modules.xxx
		 *
		 * If the config file exists but lacks an xxxfile
		 * specification, the default value is used since
		 * the system can't work without one.
		 */
		snprintf(tmp, sizeof(tmp), "%s/lib/modules/%s/modules.%s",
			base_dir, version, gf->base);
		gf->name = xstrdup(tmp);
	} else { /* xxxfile defined in modules.conf */
		/*
		 * If we have a xxxfile definition in the configuration file
		 * we must resolve any shell meta-chars in its value.
		 */
		if (meta_expand(gf->name, g, base_dir, version, ME_ALL))
			ret = -1;
		else if (!g->pathv || g->pathv[0] == NULL)
			ret = -1;
		else {
			free(gf->name);
			gf->name = xstrdup(g->pathv[0]);
		}
	}
	return(ret);
}

/*
 *	Read the configuration file.
 *	If parameter "all" == 0 then ignore everything except path info
 *	Return -1 if any error.
 *	Error messages generated.
 */
static int do_read(int all, char *force_ver, char *base_dir, char *conf_file, int depth)
{
	#define MAX_LEVEL 20
	#define MAX_DEPTH 20
	FILE *fin;
	GLOB_LIST g;
	int i;
	int assgn;
	int drop_default_paths = 1;
	int lineno = 0;
	int ret = 0;
	int state[MAX_LEVEL + 1]; /* nested "if" */
	int level = 0;
	char buf[3000];
	char tmpline[PATH_MAX];
	char **pathp;
	char *envpath;
	char *version;
	char *type;
	char **glb;
	char old_name[] = "/etc/conf.modules";
	int conf_file_specified = 0;

	/*
	 * The configuration file is optional.
	 * No error is printed if it is missing.
	 * If it is missing the following content is assumed.
	 *
	 * path[boot]=/lib/modules/boot
	 *
	 * path[toplevel]=/lib/modules/`uname -r`
	 *
	 * path[toplevel]=/lib/modules/`kernelversion`
	 *   (where kernelversion gives the major kernel version: "2.0", "2.2"...)
	 *
	 * path[toplevel]=/lib/modules/default
	 *
	 * path[kernel]=/lib/modules/kernel
	 * path[fs]=/lib/modules/fs
	 * path[net]=/lib/modules/net
	 * path[scsi]=/lib/modules/scsi
	 * path[block]=/lib/modules/block
	 * path[cdrom]=/lib/modules/cdrom
	 * path[ipv4]=/lib/modules/ipv4
	 * path[ipv6]=/lib/modules/ipv6
	 * path[sound]=/lib/modules/sound
	 * path[fc4]=/lib/modules/fc4
	 * path[video]=/lib/modules/video
	 * path[misc]=/lib/modules/misc
	 * path[pcmcia]=/lib/modules/pcmcia
	 * path[atm]=/lib/modules/atm
	 * path[usb]=/lib/modules/usb
	 * path[ide]=/lib/modules/ide
	 * path[ieee1394]=/lib/modules/ieee1394
	 * path[mtd]=/lib/modules/mtd
	 *
	 * The idea is that modprobe will look first if the
	 * modules are compiled for the current release of the kernel.
	 * If not found, it will look for modules that fit for the
	 * general kernelversion (2.0, 2.2 and so on).
	 * If still not found, it will look into the default release.
	 * And if still not found, it will look in the other directories.
	 *
	 * The strategy should be like this:
	 * When you install a new linux kernel, the modules should go
	 * into a directory related to the release (version) of the kernel.
	 * Then you can do a symlink "default" to this directory.
	 *
	 * Each time you compile a new kernel, the make modules_install
	 * will create a new directory, but it won't change thee default.
	 *
	 * When you get a module unrelated to the kernel distribution
	 * you can place it in one of the last three directory types.
	 *
	 * This is the default strategy. Of course you can overide
	 * this in /etc/modules.conf.
	 *
	 * 2.3.15 added a new file tree walk algorithm which made it possible to
	 * point at a top level directory and get the same behaviour as earlier
	 * versions of modutils.  2.3.16 takes this one stage further, it
	 * removes all the individual directory names from most of the scans,
	 * only pointing at the top level directory.  The only exception is the
	 * last ditch scan, scanning all of /lib/modules would be a bad idea(TM)
	 * so the last ditch scan still runs individual directory names under
	 * /lib/modules.
	 *
	 * Additional syntax:
	 *
	 * [add] above module module1 ...
	 *	Specify additional modules to pull in on top of a module
	 *
	 * [add] below module module1 ...
	 *	Specify additional modules needed to be able to load a module
	 *
	 * [add] prune filename ...
	 *
	 * [add] probe name module1 ...
	 *	When "name" is requested, modprobe tries to install each
	 *	module in the list until it succeeds.
	 *
	 * [add] probeall name module1 ...
	 *	When "name" is requested, modprobe tries to install all
	 *	modules in the list.
	 *	If any module is installed, the command has succeeded.
	 *
	 * [add] options module option_list
	 *
	 * For all of the above, the optional "add" prefix is used to
	 * add to a list instead of replacing the contents.
	 *
	 * include FILE_TO_INCLUDE
	 *	This does what you expect. Include level is limited to 20.
	 *
	 * persistdir=persist_directory
	 *	Name the directory to save persistent data from modules.
	 *
	 * In the following WORD is a sequence if non-white characters.
	 * If ' " or ` is found in the string, all characters up to the
	 * matching ' " or ` will also be included, even whitespace.
	 * Every WORD will then be expanded w.r.t. meta-characters.
	 * If the expanded result gives more than one word, then only
	 * the first word of the result will be used.
	 *
	 *
	 * define CODE WORD
	 *		Do a putenv("CODE=WORD")
	 *
	 * EXPRESSION below can be:
	 *	WORD compare_op WORD
	 *		where compare_op is one of == != < <= >= >
	 *		The string values of the WORDs are compared
	 * or
	 *	-n WORD compare_op WORD
	 *		where compare_op is one of == != < <= >= >
	 *		The numeric values of the WORDs are compared
	 * or
	 *	WORD
	 *		if the expansion of WORD fails, or if the
	 *		expansion is "0" (zero), "false" or "" (empty)
	 *		then the expansion has the value FALSE.
	 *		Otherwise the expansion has the value TRUE
	 * or
	 *	-f FILENAME
	 *		Test if the file FILENAME exists
	 * or
	 *	-k
	 *		Test if "autoclean" (i.e. called from the kernel)
	 * or
	 *	! EXPRESSION
	 *		A negated expression is also an expression
	 *
	 * if EXPRESSION
	 *	any config line
	 *	...
	 * elseif EXPRESSION
	 *	any config line
	 *	...
	 * else
	 *	any config line
	 *	...
	 * endif
	 *
	 * The else and elseif keywords are optional.
	 * "if"-statements nest up to 20 levels.
	 */

	state[0] = 1;

	if (force_ver)
		version = force_ver;
	else
		version = uts_info.release;

	config_version = xstrdup(version);

	/* Only read the default entries on the first file */
	if (depth == 0) {
		maxpath = 100;
		modpath = (struct PATH_TYPE *)xmalloc(maxpath * sizeof(struct PATH_TYPE));
		nmodpath = 0;

		maxexecs = 10;
		execs = (struct EXEC_TYPE *)xmalloc(maxexecs * sizeof(struct EXEC_TYPE));
		nexecs = 0;

		/*
		 * Build predef options
		 */
		if (all && optlist[0])
			n_opt_list = build_list(optlist, &opt_list, version, 1);

		/*
		 * Build predef above
		 */
		if (all && above[0])
			n_abovelist = build_list(above, &abovelist, version, 0);

		/*
		 * Build predef below
		 */
		if (all && below[0])
			n_belowlist = build_list(below, &belowlist, version, 0);

		/*
		 * Build predef prune list
		 */
		if (prune[0])
			n_prunelist = build_list(prune, &prunelist, version, 0);

		/*
		 * Build predef aliases
		 */
		if (all && aliaslist[0])
			n_aliases = build_list(aliaslist, &aliases, version, 0);

		/* Order and priority is now: (MODPATH + modules.conf) || (predefs + modules.conf) */
		if ((envpath = getenv("MODPATH")) != NULL && !safemode) {
			size_t len;
			char *p;
			char *path;

			/* Make a copy so's we can mung it with strtok.  */
			len = strlen(envpath) + 1;
			p = alloca(len);
			memcpy(p, envpath, len);
			path = alloca(PATH_MAX);

			for (p = strtok(p, ":"); p != NULL; p = strtok(NULL, ":")) {
				len = snprintf(path, PATH_MAX, p, version);
				modpath[nmodpath].path = xstrdup(path);
				if ((type = strrchr(path, '/')) != NULL)
					type += 1;
				else
					type = "misc";
				modpath[nmodpath].type = xstrdup(type);
				if (++nmodpath >= maxpath) {
					maxpath += 100;
					modpath = (struct PATH_TYPE *)xrealloc(modpath,
						maxpath * sizeof(struct PATH_TYPE));
				}

			}
		} else {
			/*
			 * Build the default "path[type]" configuration
			 */
			int n;
			char *k;

			/* The first entry in the path list */
			modpath[nmodpath].type = xstrdup("boot");
			snprintf(tmpline, sizeof(tmpline), "%s/lib/modules/boot", base_dir);
			modpath[nmodpath].path = xstrdup(tmpline);
			++nmodpath;

			/* The second entry in the path list, `uname -r` */
			modpath[nmodpath].type = xstrdup("toplevel");
			snprintf(tmpline, sizeof(tmpline), "%s/lib/modules/%s", base_dir, version);
			modpath[nmodpath].path = xstrdup(tmpline);
			++nmodpath;

			/* The third entry in the path list, `kernelversion` */
			modpath[nmodpath].type = xstrdup("toplevel");
			for (n = 0, k = version; *k; ++k) {
				if (*k == '.' && ++n == 2)
					break;
			}
			snprintf(tmpline, sizeof(tmpline), "%s/lib/modules/%.*s", base_dir,
				(/* typecast for Alpha */ int)(k - version), version);
			modpath[nmodpath].path = xstrdup(tmpline);
			++nmodpath;

			/* The rest of the entries in the path list */
			for (pathp = tbpath; *pathp; ++pathp) {
				char **type;

				for (type = tbtype; *type; ++type) {
					char path[PATH_MAX];

					snprintf(path, sizeof(path), "%s%s/%s", base_dir, *pathp, *type);
					if (meta_expand(path, &g, NULL, version, ME_ALL))
						return -1;

					for (glb = g.pathv; glb && *glb; ++glb) {
						modpath[nmodpath].type = xstrdup(*type);
						modpath[nmodpath].path = *glb;
						if (++nmodpath >= maxpath) {
							maxpath += 100;
							modpath = (struct PATH_TYPE *)xrealloc(modpath,
								maxpath * sizeof(struct PATH_TYPE));
						}
					}
				}
			}
		}

		/* Environment overrides for testing only, undocumented */
		for (i = 0; i < gen_file_count; ++i)
			gen_file_env(gen_file+i);

	}	/* End of depth == 0 */

	if (conf_file ||
	    ((conf_file = getenv("MODULECONFIG")) != NULL && *conf_file && !safemode)) {
		if (!(fin = fopen(conf_file, "r"))) {
			error("Can't open %s", conf_file);
			return -1;
		}
		conf_file_specified = 1;
	} else {
		if (!(fin = fopen((conf_file = ETC_MODULES_CONF), "r"))) {
			/* Fall back to non-standard name */
			if ((fin = fopen((conf_file = old_name), "r"))) {
				fprintf(stderr,
					"Warning: modutils is reading from %s because\n"
					"         %s does not exist.  The use of %s is\n"
					"         deprecated, please rename %s to %s\n"
					"         as soon as possible.  Command\n"
					"         mv %s %s\n",
					old_name, ETC_MODULES_CONF,
					old_name, old_name, ETC_MODULES_CONF,
					old_name, ETC_MODULES_CONF);
			}
			/* So what... use the default configuration */
		}
	}

	if (fin) {
		struct stat statbuf1, statbuf2;
		if (fstat(fileno(fin), &statbuf1) == 0)
			config_mtime = statbuf1.st_mtime;
		config_file = xstrdup(conf_file);	/* Save name actually used */
		if (!conf_file_specified &&
		    stat(ETC_MODULES_CONF, &statbuf1) == 0 &&
		    stat(old_name, &statbuf2) == 0) {
			/* Both /etc files exist */
			if (statbuf1.st_dev == statbuf2.st_dev &&
			    statbuf1.st_ino == statbuf2.st_ino) {
				if (lstat(ETC_MODULES_CONF, &statbuf1) == 0 &&
				    S_ISLNK(statbuf1.st_mode))
					fprintf(stderr,
						"Warning: You do not need a link from %s to\n"
						"         %s.  The use of %s is deprecated,\n"
						"         please remove %s and rename %s\n"
						"         to %s as soon as possible.  Commands.\n"
						"           rm %s\n"
						"           mv %s %s\n",
						ETC_MODULES_CONF, old_name,
						old_name, ETC_MODULES_CONF, old_name, ETC_MODULES_CONF,
						ETC_MODULES_CONF,
						old_name, ETC_MODULES_CONF);
				else {
#ifndef NO_WARN_ON_OLD_LINK
					fprintf(stderr,
						"Warning: You do not need a link from %s to\n"
						"         %s.  The use of %s is deprecated,\n"
						"         please remove %s as soon as possible.  Command\n"
						"           rm %s\n",
						old_name, ETC_MODULES_CONF,
						old_name, old_name,
						old_name);
#endif
				}
			}
			else
				fprintf(stderr,
					"Warning: modutils is reading from %s and\n"
					"         ignoring %s.  The use of %s is deprecated,\n"
					"         please remove %s as soon as possible.  Command\n"
					"           rm %s\n",
					ETC_MODULES_CONF, old_name,
					old_name, old_name,
					old_name);
		}
	}

	/*
	 * Finally, decode the file
	 */
	while (fin && fgets_strip(buf, sizeof(buf) - 1, fin, &lineno) != NULL) {
		char *arg2;
		char *parm = buf;
		char *arg;
		int one_err = 0;
		int adding;

		while (isspace(*parm))
			parm++;

		if (strncmp(parm, "add", 3) == 0) {
			adding = 1;
			parm += 3;
			while (isspace(*parm))
				parm++;
		} else
			adding = 0;

		arg = parm;

		if (*parm == '\0')
			continue;

		one_err = 1;

		while (*arg > ' ' && *arg != '=')
			arg++;

		if (*arg == '=')
			assgn = 1;
		else
			assgn = 0;
		*arg++ = '\0';
		while (isspace(*arg))
			arg++;

		/*
		 * endif
		 */
		if (!assgn && strcmp(parm, "endif") == 0) {
			if (level > 0)
				--level;
			else {
				error("unmatched endif in line %d", lineno);
				return -1;
			}
			continue;
		}

		/*
		 * else
		 */
		if (!assgn && strcmp(parm, "else") == 0) {
			if (level <= 0) {
				error("else without if in line %d", lineno);
				return -1;
			}
			state[level] = !state[level];
			continue;
		}

		/*
		 * elseif
		 */
		if (!assgn && strcmp(parm, "elseif") == 0) {
			if (level <= 0) {
				error("elseif without if in line %d", lineno);
				return -1;
			}
			if (state[level] != 0) {
				/*
				 * We have already found a TRUE
				 * if statement in this "chain".
				 * That's what "2" means.
				 */
				state[level] = 2;
				continue;
			}
			/* else: No TRUE if has been found, cheat */
			/*
			 * The "if" handling increments level,
			 * but this is the _same_ level as before.
			 * So, compensate for it.
			 */
			--level;
			parm = "if";
			/* Fallthru to "if" */
		}

		/*
		 * if
		 */
		if (strcmp(parm, "if") == 0) {
			char *cmp;
			int not = 0;
			int numeric = 0;

			if (level >= MAX_LEVEL) {
				error("Too many nested if's in line %d\n", lineno);
				return -1;
			}
			state[++level] = 0; /* default false */

			if (*arg == '!') {
				not = 1;
				arg = next_word(arg);
			}

			if (strncmp(arg, "-k", 2) == 0) {
				state[level] = flag_autoclean;
				continue;
			}

			if (strncmp(arg, "-f", 2) == 0) {
				char *file = next_word(arg);
				meta_expand(file, &g, NULL, version, ME_ALL);
				if (access(g.pathc ? g.pathv[0] : file, R_OK) == 0)
					state[level] = !not;
				else
					state[level] = not;
				continue;
			}

			if (strncmp(arg, "-n", 2) == 0) {
				numeric = 1;
				arg = next_word(arg);
			}


			cmp = next_word(arg);
			if (*cmp) {
				GLOB_LIST g2;
				long n1 = 0;
				long n2 = 0;
				char *w1 = "";
				char *w2 = "";

				arg2 = next_word(cmp);

				meta_expand(arg, &g, NULL, version, ME_ALL);
				if (g.pathc && g.pathv[0])
					w1 = g.pathv[0];

				meta_expand(arg2, &g2, NULL, version, ME_ALL);
				if (g2.pathc && g2.pathv[0])
					w2 = g2.pathv[0];

				if (numeric) {
					n1 = strtol(w1, NULL, 0);
					n2 = strtol(w2, NULL, 0);
				}

				if (strcmp(cmp, "==") == 0 ||
				    strcmp(cmp, "=") == 0) {
					if (numeric)
					    state[level] = (n1 == n2);
					else
					    state[level] = strcmp(w1, w2) == 0;
				} else if (strcmp(cmp, "!=") == 0) {
					if (numeric)
					    state[level] = (n1 != n2);
					else
					    state[level] = strcmp(w1, w2) != 0;
				} else if (strcmp(cmp, ">=") == 0) {
					if (numeric)
					    state[level] = (n1 >= n2);
					else
					    state[level] = strcmp(w1, w2) >= 0;
				} else if (strcmp(cmp, "<=") == 0) {
					if (numeric)
					    state[level] = (n1 <= n2);
					else
					    state[level] = strcmp(w1, w2) <= 0;
				} else if (strcmp(cmp, ">") == 0) {
					if (numeric)
					    state[level] = (n1 > n2);
					else
					    state[level] = strcmp(w1, w2) > 0;
				} else if (strcmp(cmp, "<") == 0) {
					if (numeric)
					    state[level] = (n1 < n2);
					else
					    state[level] = strcmp(w1, w2) < 0;
				}
			} else { /* Check defined value, if any */
				/* undef or defined as
				 *	"" or "0" or "false" => false
				 *  defined => true
				 */
				if (!meta_expand(arg, &g, NULL, version, ME_ALL) &&
				    g.pathc > 0 &&
				    strcmp(g.pathv[0], "0") != 0 &&
				    strcmp(g.pathv[0], "false") != 0 &&
				    strlen(g.pathv[0]) != 0)
					state[level] = 1; /* true */
			}
			if (not)
				state[level] = !state[level];

			continue;
		}

		/*
		 * Should we bother?
		 */
		if (state[level] != 1)
			continue;

		/*
		 * define
		 */
		if (!assgn && strcmp(parm, "define") == 0) {
			char env[PATH_MAX];

			arg2 = next_word(arg);
			meta_expand(arg2, &g, NULL, version, ME_ALL);
			snprintf(env, sizeof(env), "%s=%s", arg, (g.pathc ? g.pathv[0] : ""));
			putenv(xstrdup(env));
			one_err = 0;
		}

		/*
		 * include
		 */
		if (!assgn && strcmp(parm, "include") == 0) {
                        if (depth > MAX_DEPTH) {
                                error("Too many include level in line %d\n", lineno);
                                return -1;
                        }
			meta_expand(arg, &g, NULL, version, ME_ALL);

			if (!do_read(all, version, base_dir, g.pathc ? g.pathv[0] : arg, depth+1))
				one_err = 0;
			else
				error("include %s failed\n", arg);
		}

		/*
		 * above
		 */
		else if (all && !assgn && strcmp(parm, "above") == 0) {
			decode_list(&n_abovelist, &abovelist, arg, adding, version, 0);
			one_err = 0;
		}

		/*
		 * below
		 */
		else if (all && !assgn && strcmp(parm, "below") == 0) {
			decode_list(&n_belowlist, &belowlist, arg, adding, version, 0);
			one_err = 0;
		}

		/*
		 * prune
		 */
		else if (!assgn && strcmp(parm, "prune") == 0) {
			decode_list(&n_prunelist, &prunelist, arg, adding, version, 0);
			one_err = 0;
		}

		/*
		 * probe
		 */
		else if (all && !assgn && strcmp(parm, "probe") == 0) {
			decode_list(&n_probe_list, &probe_list, arg, adding, version, 0);
			one_err = 0;
		}

		/*
		 * probeall
		 */
		else if (all && !assgn && strcmp(parm, "probeall") == 0) {
			decode_list(&n_probeall_list, &probeall_list, arg, adding, version, 0);
			one_err = 0;
		}

		/*
		 * options
		 */
		else if (all && !assgn && strcmp(parm, "options") == 0) {
			decode_list(&n_opt_list, &opt_list, arg, adding, version, 1);
			one_err = 0;
		}

		/*
		 * alias
		 */
		else if (all && !assgn && strcmp(parm, "alias") == 0) {
			/*
			 * Replace any previous (default) definitions
			 * for the same module
			 */
			decode_list(&n_aliases, &aliases, arg, 0, version, 0);
			one_err = 0;
		}

		/*
		 * Specification: /etc/modules.conf
		 * The format of the commands in /etc/modules.conf are:
		 *
		 *	pre-install module command
		 *	install module command
		 *	post-install module command
		 *	pre-remove module command
		 *	remove module command
		 *	post-remove module command
		 *
		 * The different words are separated by tabs or spaces.
		 */
		/*
		 * pre-install
		 */
		else if (all && !assgn && (strcmp(parm, "pre-install") == 0)) {
			decode_exec(arg, EXEC_PRE_INSTALL);
			one_err = 0;
		}

		/*
		 * install
		 */
		else if (all && !assgn && (strcmp(parm, "install") == 0)) {
			decode_exec(arg, EXEC_INSTALL);
			one_err = 0;
		}

		/*
		 * post-install
		 */
		else if (all && !assgn && (strcmp(parm, "post-install") == 0)) {
			decode_exec(arg, EXEC_POST_INSTALL);
			one_err = 0;
		}

		/*
		 * pre-remove
		 */
		else if (all && !assgn && (strcmp(parm, "pre-remove") == 0)) {
			decode_exec(arg, EXEC_PRE_REMOVE);
			one_err = 0;
		}

		/*
		 * remove
		 */
		else if (all && !assgn && (strcmp(parm, "remove") == 0)) {
			decode_exec(arg, EXEC_REMOVE);
			one_err = 0;
		}

		/*
		 * post-remove
		 */
		else if (all && !assgn && (strcmp(parm, "post-remove") == 0)) {
			decode_exec(arg, EXEC_POST_REMOVE);
			one_err = 0;
		}

		/*
		 * insmod_opt=
		 */
		else if (assgn && (strcmp(parm, "insmod_opt") == 0)) {
			insmod_opt = xstrdup(arg);
			one_err = 0;
		}

		/*
		 * keep
		 */
		else if (!assgn && (strcmp(parm, "keep") == 0)) {
			drop_default_paths = 0;
			one_err = 0;
		}

		/*
		 * path...=
		 */
		else if (assgn && strncmp(parm, "path", 4) == 0) {
			/*
			 * Specification: config file / path parameter
			 * The path parameter specifies a directory to
			 * search for modules.
			 * This parameter may be repeated multiple times.
			 *
			 * Note that the actual path may be defined using
			 * wildcards and other shell meta-chars, such as "*?`".
			 * For example:
			 *      path[misc]=/lib/modules/1.1.5?/misc
			 *
			 * Optionally the path keyword carries a tag.
			 * This tells us a little more about the purpose of
			 * this directory and allows some automated operations.
			 * A path is marked with a tag by adding the tag,
			 * enclosed in square brackets, to the path keyword:
			 * #
			 * path[boot]=/lib/modules/boot
			 * #
			 * This case identifies the path a of directory
			 * holding modules loadable a boot time.
			 */

			if (drop_default_paths) {
				int n;

				/*
				 * Specification: config file / path / default
				 *
				 * Whenever there is a path[] specification
				 * in the config file, all the default
				 * path are reset.
				 *
				 * If one instead wants to _add_ to the default
				 * set of paths, one has to have the option
				 *    keep
				 * before the first path[]-specification line
				 * in the configuration file.
				 */
				drop_default_paths = 0;
				for (n = 0; n < nmodpath; n++) {
					free(modpath[n].path);
					free(modpath[n].type);
				}
				nmodpath = 0;
			}

			/*
			 * Get (the optional) tag
			 * If the tag is missing, the word "misc"
			 * is assumed.
			 */
			type = "misc";

			if (parm[4] == '[') {
				char *pt_type = parm + 5;

				while (*pt_type != '\0' && *pt_type != ']')
					pt_type++;

				if (*pt_type == ']' && pt_type[1] == '\0') {
					*pt_type = '\0';
					type = parm + 5;
				} /* else CHECKME */
			}

			/*
			 * Handle the actual path description
			 */
			if (meta_expand(arg, &g, base_dir, version, ME_ALL))
				return -1;
			for (glb = g.pathv; glb && *glb; ++glb) {
				modpath[nmodpath].type = xstrdup(type);
				modpath[nmodpath].path = *glb;
				if (++nmodpath >= maxpath) {
					maxpath += 100;
					modpath = (struct PATH_TYPE *)xrealloc(modpath,
						maxpath * sizeof(struct PATH_TYPE));
				}
			}
			one_err = 0;
		}

		/*
		 * persistdir
		 */
		else if (assgn && strcmp(parm, "persistdir") == 0) {
			meta_expand(arg, &g, NULL, version, ME_ALL);
			persistdir = xstrdup(g.pathc ? g.pathv[0] : arg);
			one_err = 0;
		}

		/* Names for generated files in config file */
		for (i = 0; one_err && i < gen_file_count; ++i)
			one_err = gen_file_conf(gen_file+i, assgn, parm, arg);

		/*
		 * any errors so far?
		 */
		if (all == 0)
			one_err = 0;
		else if (one_err) {
			error("Invalid line %d in %s\n\t%s",
				     lineno, conf_file, buf);
			ret = -1;
		}
	}
	if (fin)
		fclose(fin);

	if (level) {
		error("missing endif at %s EOF", conf_file);
		ret = -1;
	}

	if (ret)
		return ret;
	/* else */

	if (depth == 0) {
		/* Check we have names for generated files */
		for (i = 0; !ret && i < gen_file_count; ++i)
			ret = gen_file_check(gen_file+i, &g, base_dir, version);
	}

	return ret;
}

int config_read(int all, char *force_ver, char *base_dir, char *conf_file)
{
	int r;
	if (modpath != NULL)
		return 0; /* already initialized */

	if (uname(&uts_info) < 0) {
		error("Failed to find kernel name information");
		return -1;
	}

	r = do_read(all, force_ver, base_dir, conf_file, 0);

	if (quick && !r && !need_update (force_ver, base_dir))
		exit (0);

	return r;
}

/****************************************************************************/
/*
 *	FIXME: Far too much global state.  KAO.
 */
static int found;
static int favail;
static int one_only;
static int meta_expand_type;
char **list;
static const char *filter_by_file;
static char *filter_by_dir;

/*
 *	Add a file name if it exist
 */
static int config_add(const char *file, const struct stat *sb)
{
	int i;
	int npaths = 0;
	char **paths = NULL;

	if (meta_expand_type) {
		GLOB_LIST g;
		char **p;
		char full[PATH_MAX];

		snprintf(full, sizeof(full), "%s/%s", file, filter_by_file);

		if (filter_by_dir && !strstr(full, filter_by_dir))
			return 0;

		if (meta_expand(full, &g, NULL, config_version, meta_expand_type))
			return 1;
		for (p = g.pathv; p && *p; ++p) {
			paths = (char **)xrealloc(paths,
					(npaths + 1) * sizeof(char *));
			paths[npaths++] = *p;
		}
	} else { /* normal path match or match with "*" */
		if (!S_ISREG(sb->st_mode))
			return 0;

		if (strcmp(filter_by_file, "*")) {
			char *p;

			if ((p = strrchr(file, '/')) == NULL)
				p = (char *)file;
			else
				p += 1;

			if (strcmp(p, filter_by_file))
				return 0;
		}
		if (filter_by_dir && !strstr(file, filter_by_dir))
			return 0;
		paths = (char **)xmalloc(sizeof(char **));
		*paths = xstrdup(file);
		npaths = 1;
	}

	for (i = 0; i < npaths; ++i) {
		struct stat sbuf;

		if (S_ISDIR(sb->st_mode)) {
			if (stat(paths[i], &sbuf) == 0)
				sb = &sbuf;
		}
		if (S_ISREG(sb->st_mode) && sb->st_mode & S_IRUSR) {
			int j;
			char **this;

			if (!root_check_off) {
				if (sb->st_uid != 0) {
					error("%s is not owned by root", paths[i]);
					continue;
				}
			}

			/* avoid duplicates */
			for (j = 0, this = list; j < found; ++j, ++this) {
				if (strcmp(*this, paths[i]) == 0) {
					free(paths[i]);
					goto next;
				}
			}

			list[found] = paths[i];
			if (++found >= favail)
				list = (char **)xrealloc(list,
				     (favail += 100) * sizeof(char *));

			if (one_only) {
				for (j = i + 1; j < npaths; ++j)
					free(paths[j]);
				free(paths);
				return 1; /* finish xftw */
			}
		}
	    next:
	}

	if (npaths > 0)
		free(paths);

	return 0;
}

/*
 * Find modules matching the name "match" in directory of type "type"
 * (type == NULL matches all)
 *
 * Return a pointer to the list of modules found (or NULL if error).
 * Update the counter (sent as parameter).
 */
GLOB_LIST *config_lstmod(const char *match, const char *type, int first_only)
{
	/*
	 * Note:
	 * There are _no_ wildcards remaining in the path descriptions!
	 */
	struct stat sb;
	int i;
	int ret = 0;
	char *path = NULL;
	char this[PATH_MAX];

	if (!match)
		match = "*";
	one_only = first_only;
	found = 0;
	filter_by_file = match;
	filter_by_dir = NULL;
	if (type) {
		char tmpdir[PATH_MAX];
		snprintf(tmpdir, sizeof(tmpdir), "/%s/", type);
		filter_by_dir = xstrdup(tmpdir);
	}
	/* In safe mode, the module name is always handled as is, without meta
	 * expansion.  It might have come from an end user via kmod and must
	 * not be trusted.  Even in unsafe mode, only apply globbing to the
	 * module name, not command expansion.  We trust config file input so
	 * applying command expansion is safe, we do not trust command line input.
	 * This assumes that the only time the user can specify -C config file
	 * is when they run under their own authority.  In particular all
	 * mechanisms that call modprobe as root on behalf of the user must
	 * run in safe mode, without letting the user supply a config filename.
	 */
	meta_expand_type = 0;
	if (strpbrk(match, SHELL_META) && strcmp(match, "*") && !safemode)
		meta_expand_type = ME_GLOB|ME_BUILTIN_COMMAND;

	list = (char **)xmalloc((favail = 100) * sizeof(char *));

	for (i = 0; i < nmodpath; i++) {
		path = modpath[i].path;
		/* Special case: insmod: handle single, non-wildcard match */
		if (first_only && strpbrk(match, SHELL_META) == NULL) {
			/* Fix for "2.1.121 syntax */
			snprintf(this, sizeof(this), "%s/%s/%s", path,
						  modpath[i].type, match);
			if (stat(this, &sb) == 0 &&
			    config_add(this, &sb))
				break;
			/* End fix for "2.1.121 syntax */

			snprintf(this, sizeof(this), "%s/%s", path, match);
			if (stat(this, &sb) == 0 &&
			    config_add(this, &sb))
				break;
		}

		/* Start looking */
		if ((ret = xftw(path, config_add))) {
			break;
		}
	}
	if (ret >= 0) {
		GLOB_LIST *g = (GLOB_LIST *)xmalloc(sizeof(GLOB_LIST));
		g->pathc = found;
		g->pathv = list;
		free(filter_by_dir);
		return g;
	}
	free(list);
	free(filter_by_dir);
	return NULL;
}

/* Given a bare module name, poke through the module path to find the file.  */
char *search_module_path(const char *base)
{
	GLOB_LIST *g;

	if (config_read(0, NULL, "", NULL) < 0)
		return NULL;
	/* else */
	g = config_lstmod(base, NULL, 1);
	if (g == NULL || g->pathc == 0) {
		char base_o[PATH_MAX];

		snprintf(base_o, sizeof(base_o), "%s.o", base);
		g = config_lstmod(base_o, NULL, 1);
#ifdef CONFIG_USE_ZLIB
		if (g == NULL || g->pathc == 0) {
			snprintf(base_o, sizeof(base_o), "%s.o.gz", base);
			g = config_lstmod(base_o, NULL, 1);
		}
#endif
	}
	if (g == NULL || g->pathc == 0)
		return NULL;
	/* else */
	return g->pathv[0];
}
