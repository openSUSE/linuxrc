/*
 * Configuration file management
 *
 * Copyright 1994, 1995, 1996, 1997:
 *	Jacques Gelinas <jack@solucorp.qc.ca>
 *	Björn Ekwall <bj0rn@blox.se> February, March 1999
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

#ifndef _CONFIG_H
#define _CONFIG_H

#include <sys/utsname.h>

#define ETC_MODULES_CONF	"/etc/modules.conf"

#define EXEC_PRE_INSTALL 0
#define EXEC_POST_INSTALL 1
#define EXEC_PRE_REMOVE 2
#define EXEC_POST_REMOVE 3
#define EXEC_INSTALL 4
#define EXEC_REMOVE 5

struct PATH_TYPE {
	char *type;
	char *path;
};

struct EXEC_TYPE {
	int when;
	char *module;
	char *cmd;
};

typedef struct {
	char *name;
	GLOB_LIST *opts;
} OPT_LIST;

/* config.c */
extern int flag_autoclean;
extern struct utsname uts_info;
extern char *aliaslist[];
extern struct PATH_TYPE *modpath;
extern int nmodpath;
extern struct EXEC_TYPE *execs;
extern int nexecs;
extern char *insmod_opt;
extern char *depfile;
extern char *config_file;
extern char *optlist[];
extern OPT_LIST *opt_list;
extern OPT_LIST *abovelist;
extern OPT_LIST *belowlist;
extern OPT_LIST *probe_list;
extern OPT_LIST *probeall_list;
extern OPT_LIST *aliases;
extern time_t config_mtime;
extern int root_check_off;	/* Check modules are owned by root? */

char *fgets_strip(char *buf, int sizebuf, FILE * fin, int *lineno);
int config_read(int all, char *force_ver, char *base_dir, char *conf_file);
GLOB_LIST *config_lstmod(char *match, const char *type, int first_only);
char *search_module_path(char *base);

#endif /* _CONFIG_H */
