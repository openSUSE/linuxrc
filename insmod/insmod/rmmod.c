/*
 * Remove a module from a running kernel.
 *
 * Original author: Jon Tombs <jon@gtex02.us.es>,
 * extended by Björn Ekwall <bj0rn@blox.se> in 1994 (C).
 * New re-implementation by Björn Ekwall <bj0rn@blox.se> February 1999,
 * Generic kernel module info based on work by Richard Henderson <rth@tamu.edu>
 * Add ksymoops support by Keith Owens <kaos@ocs.com.au> August 1999,
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

#ident "$Id: rmmod.c,v 1.2 2000/11/22 15:45:22 snwint Exp $"

#include <errno.h>
#include <malloc.h>
#include <memory.h>
#include <unistd.h>
#include <getopt.h>

#include "module.h"
#include "version.h"
#include "util.h"
#include "obj.h"
#include "modstat.h"

#define WANT_TO_REMOVE 1
#define CAN_REMOVE 2

#ifdef COMBINE_rmmod
#define main rmmod_main
#endif

void rmmod_usage(void)
{
	fputs("Usage:\n"
	      "rmmod [-arshV] module ...\n"
	      "\n"
	      "  -a, --all     Remove all unused modules\n"
	      "  -r, --stacks  Remove stacks, starting at the named module\n"
	      "  -s, --syslog  Use syslog for error messages\n"
	      "  -h, --help    Print this message\n"
	      "  -V, --version Print the release version number\n"
	      ,stderr);
}

int main(int argc, char **argv)
{
	struct module_stat *m;
	int i;
	int j;
	int ret = 0;
	int recursive = 0;
	size_t n_module_names;
	char *module_names = NULL;

	struct option long_opts[] = {
		{"all", 0, 0, 'a'},
		{"stacks", 0, 0, 'r'},
		{"syslog", 0, 0, 's'},
		{"version", 0, 0, 'V'},
		{"help", 0, 0, 'h'},
		{0, 0, 0, 0}
	};

	error_file = "rmmod";

	/*
	 * Collect the loaded modules before deletion.  delete_module()
	 * gives no indication that any modules were deleted so we have to
	 * do it the hard way.
	 */
	get_kernel_info(0);
	module_names = xmalloc(l_module_name_list);
	memcpy(module_names, module_name_list, l_module_name_list);
	n_module_names = n_module_stat;

	while ((i = getopt_long(argc, argv, "arsVh",
				&long_opts[0], NULL)) != EOF)
		switch (i) {
		case 'a':
			/* Remove all unused modules and stacks.  */
			if (delete_module(NULL)) {
				perror("rmmod");
				snap_shot(module_names, n_module_names);
				free(module_names);
				return 1;
			}
			snap_shot(module_names, n_module_names);
			free(module_names);
			return 0;

		case 'r':
			/* Remove stacks, starting at named top module */
			recursive = 1;
			break;

		case 's':
			/* Start syslogging.  */
			setsyslog("rmmod");
			break;

		case 'V':
			fputs("rmmod version " MODUTILS_VERSION "\n", stderr);
			break;

		case 'h':
			rmmod_usage();
			return 0;
		default:
		usage:
			rmmod_usage();
			return 1;
		}

	if (optind >= argc)
		goto usage;

	if (!recursive) {
		for (i = optind; i < argc; ++i) {
			if (delete_module(argv[i]) < 0) {
				++ret;
				if (errno == ENOENT)
					error("module %s is not loaded", argv[i]);
				else
					perror(argv[i]);
			}
		}
		snap_shot(module_names, n_module_names);
		free(module_names);
		return ret ? 1 : 0;
	}

	/*
	 * Recursive removal
	 * Fetch all of the currently loaded modules and their dependencies.
	 */
	if (!get_kernel_info(K_INFO | K_REFS))
		return 1;

	/* Find out which ones we want to remove.  */
	for (i = optind; i < argc; ++i) {
		for (m = module_stat, j = 0; j < n_module_stat; ++j, ++m) {
			if (strcmp(m->name, argv[i]) == 0) {
				m->status = WANT_TO_REMOVE;
				break;
			}
		}
		if (j == n_module_stat) {
			error("module %s not loaded", argv[i]);
			ret = 1;
		}
	}

	/* Remove them if we can.  */
	for (m = module_stat, i = 0; i < n_module_stat; ++i, ++m) {
		struct module_stat **r;

		if (m->nrefs || (m->status & WANT_TO_REMOVE))
			m->status |= CAN_REMOVE;

		for (j = 0, r = m->refs;j < m->nrefs; ++j) {
			switch (r[j]->status) {
			case CAN_REMOVE:
			case WANT_TO_REMOVE | CAN_REMOVE:
				break;

			case WANT_TO_REMOVE:
				if (r[j]->nrefs == 0)
					break;
				/* else FALLTHRU */
			default:
				m->status &= ~CAN_REMOVE;
				break;
			}
		}

		switch (m->status) {
		case CAN_REMOVE:
		case WANT_TO_REMOVE | CAN_REMOVE:
			if (delete_module(m->name) < 0) {
				error("%s: %m", m->name);
				ret = 1;
			}
			break;

		case WANT_TO_REMOVE:
			error("%s is in use", m->name);
			ret = 1;
			break;
		}
	}

	snap_shot(module_names, n_module_names);
	free(module_names);
	return ret;
}
