/*
 * Remove a module from a running kernel.
 *
 * Original author: Jon Tombs <jon@gtex02.us.es>,
 * extended by Björn Ekwall <bj0rn@blox.se> in 1994 (C).
 * New re-implementation by Björn Ekwall <bj0rn@blox.se> February 1999,
 * Generic kernel module info based on work by Richard Henderson <rth@tamu.edu>
 * Add ksymoops support by Keith Owens <kaos@ocs.com.au> August 1999,
 * Add persistent data.  Keith Owens <kaos@ocs.com.au> November 2000.
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

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <malloc.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <getopt.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/utsname.h>

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

struct module_parm {
	struct obj_symbol *sym;
	int max;
	char type;
	char written;
	unsigned int size;
};

static void rmmod_usage(void)
{
	fputs("Usage:\n"
	      "rmmod [-aehrsvV] module ...\n"
	      "\n"
	      "  -a, --all     Remove all unused modules\n"
	      "  -e, --persist Save persistent data, do not unload module\n"
	      "  -r, --stacks  Remove stacks, starting at the named module\n"
	      "  -s, --syslog  Use syslog for error messages\n"
	      "  -h, --help    Print this message\n"
	      "  -v, --verbose Be verbose\n"
	      "  -V, --version Print the release version number\n"
	      ,stderr);
}

/* Scan the exported symbols for a module, looking for "__insmod_"
 * module_name "_" type.  Return a copy of the text immediately after type.
 */
static char *find_insmod_symbol(const char *module, const char *type)
{
	struct module_stat *mod;
	struct module_symbol *sym;
	int nmod, nsym, l = strlen(module);
	const char *p;

	for (nmod = 0, mod = module_stat; nmod < n_module_stat; ++nmod, ++mod) {
		for (nsym = 0, sym=mod->syms; nsym < mod->nsyms; ++nsym, ++sym) {
			p = (char *)sym->name;
			if (strncmp(p, symprefix, sizeof(symprefix)-1))
				continue;
			p += sizeof(symprefix)-1;
			if (strncmp(p, module, l))
				continue;
			p += l;
			if (*p != '_')
				continue;
			++p;
			if (strlen(p) < strlen(type) || strncmp(p, type, strlen(type)))
				continue;
			return(xstrdup(p+strlen(type)));
		}
	}
	return(NULL);
}

/* Scan the exported symbols for a module, looking for "__insmod_"
 * module_name "_S" section_name.  Locate the corresponding section
 * entry in the object file and set the sh_addr field so we know where
 * the section was originally loaded.
 */
static int find_insmod_sections(const char *module, struct obj_file *f)
{
	struct module_stat *mod;
	struct module_symbol *sym;
	int nmod, nsym, l = strlen(module), i, secname_len;
	unsigned long len;
	const char *p, *secname, *obj_secname;

	for (nmod = 0, mod = module_stat; nmod < n_module_stat; ++nmod, ++mod) {
		for (nsym = 0, sym=mod->syms; nsym < mod->nsyms; ++nsym, ++sym) {
			p = (char *)sym->name;
			if (strncmp(p, symprefix, sizeof(symprefix)-1))
				continue;
			p += sizeof(symprefix)-1;
			if (strncmp(p, module, l))
				continue;
			p += l;
			if (strncmp(p, "_S", 2))
				continue;
			secname = (p += 2);
			p = strstr(p, "_L");
			if (!p) {
				error("Cannot find length in %s", (char *)sym->name);
				return(1);
			}
			secname_len = p - secname;
			len = strtoul(p+2, (char **)&p, 10);
			if (*p) {
				error("Invalid length in %s", (char *)sym->name);
				return(1);
			}
			for (i = 0; i < f->header.e_shnum; ++i) {
				obj_secname = f->sections[i]->name;
				if (obj_secname &&
				    strncmp(obj_secname, secname, secname_len) == 0 &&
				    !obj_secname[secname_len]) {
					if (len != f->sections[i]->header.sh_size) {
						error("Length mismatch %s vs %d",
							(char *)sym->name,
							f->sections[i]->header.sh_size);
						return(1);
					}
					f->sections[i]->header.sh_addr = sym->value;
					break;
				}
			}
			if (i > f->header.e_shnum) {
				error("Cannot find object section for %s", (char *)sym->name);
				return(1);
			}
		}
	}
	return(0);
}

/* Check if a module parameter supports persistent data. */
static int spd_parm(const char *pname, const char *type,
		    const char *obj_name, struct obj_file *f,
		    struct module_parm **mpp, int *n_mp)
{
	struct obj_symbol *sym;
	struct module_parm *mp;

	verbose("  MODULE_PARM(%s, \"%s\");\n", pname, type);
	sym = obj_find_symbol(f, pname);
	if (sym == NULL) {
		error("Symbol for parameter %s not found in %s", pname, obj_name);
		return(1);
	}
	*mpp = xrealloc(*mpp, sizeof(**mpp)*++*n_mp);
	mp = *mpp+*n_mp-1;
	memset(mp, 0, sizeof(*mp));
	mp->max = 1;
	mp->sym = sym;
	if (isdigit(*type)) {
		mp->max = strtoul(type, (char **)&type, 10);
		if (*type == '-')
			mp->max = strtoul(type + 1, (char **)&type, 10);
	}
	mp->type = *type;
	switch (*type) {
	case 'c':
		if (!isdigit(*(type+1))) {
			error("Parameter %s has no size after type 'c'", pname);
			return(1);
		}
		if (!(mp->size = strtoul(type + 1, (char **)&type, 10))) {
			error("Parameter %s has zero size after type 'c'", pname);
			return(1);
		}
		if (type)
			--type;	/* point to char before suffix */
		break;
	case 'b':	/* drop through */
		mp->size = sizeof(char);
		break;
	case 'h':
		mp->size = sizeof(short);
		break;
	case 'i':
		mp->size = sizeof(int);
		break;
	case 'l':
		mp->size = tgt_sizeof_long;
		break;
	default:
		error("Parameter %s has unknown format character '%c'", pname, *type);
		return(1);
	}
	if (*type)
		++type;
	switch (*type) {
	case 'p':
		if (*(type-1) == 's') {
			error("Parameter %s is invalid persistent string", pname);
			return(1);
		}
		break;
	case '\0':
		--*n_mp;	/* Not persistent, discard data */
		break;
	default:
		error("Parameter %s has unknown format modifier '%c'", pname, *type);
		return(1);
	}
	return(0);
}

/* Print one persistent parameter, return true if data extracted. */
static int print_persistent_parm(struct obj_file *f, FILE *fw,
				  struct module_parm *mp, const char *module)
{
	int datasize = mp->size*mp->max;
	struct {
		struct module m;
		union {
			/* bless gcc dynamic arrays :) */
			char c[mp->size][mp->max];
			char b[mp->max];
			short h[mp->max];
			int i[mp->max];
			tgt_long l[mp->max];
		} data;
	} read_parm;
	int j, k;
	char comma[2] = { '\0', '\0' };

	memset(&read_parm, 0, sizeof(read_parm));
	read_parm.m.size_of_struct = -sizeof(read_parm.m);      /* -ve size => read, not write */
	read_parm.m.read_start = mp->sym->value;
	read_parm.m.read_end = read_parm.m.read_start + datasize;
	if (sys_init_module(module, (struct module *) &read_parm)) {
		int old_errors = errors;
		error("has persistent data but the kernel is too old to support it.");
		errors = old_errors;
		return(0);
	}

	fprintf(fw, "%s=", mp->sym->name);
	for (j = 0; j < mp->max; ++j) {
		switch (mp->type) {
		case 'c':
			fprintf(fw, "%s\"", comma);
			for (k = 0; k < mp->size; ++k) {
				if (read_parm.data.c[j][k] == '\0')
					break;
				if (read_parm.data.c[j][k] == '\n') {
					fprintf(fw, "\\n");
					continue;
				}
				if (read_parm.data.c[j][k] == '"' ||
				    read_parm.data.c[j][k] == '\\')
					fprintf(fw, "\\");
				fprintf(fw, "%c", read_parm.data.c[j][k]);
			}
			fprintf(fw, "\"");
			*comma = ',';
			break;
		case 'b':
			fprintf(fw, "%s%d", comma, read_parm.data.b[j]);
			*comma = ',';
			break;
		case 'h':
			fprintf(fw, "%s%d", comma, read_parm.data.h[j]);
			*comma = ',';
			break;
		case 'i':
			fprintf(fw, "%s%d", comma, read_parm.data.i[j]);
			*comma = ',';
			break;
		case 'l':
			fprintf(fw, "%s%" tgt_long_fmt "d", comma, read_parm.data.l[j]);
			*comma = ',';
			break;
		default:
			error("Parameter %s has unknown format character '%c'",
				mp->sym->name, mp->type);
			fprintf(fw, "??\n");
			return(0);
		}
	}
	fprintf(fw, "\n");
	return(1);
}

/* Extract the persistent data from the kernel and update the text file. */
static void update_persistent_data(struct obj_file *f, const char *persist_name,
				   struct module_parm *mp, int n_mp, const char *module)
{
	FILE *fr, *fw;
	int i, fd, len;
	char *newname = NULL, *newdir = NULL, *p, *name, *value, *uname_m;
	char line[4096];	/* Grossly oversized for any reasonable text */
	struct utsname uts;
	time_t clock;
	struct tm *gmt;

	len = strlen(persist_name)+1;

	/* Make the target directory, including any intermediate paths */
	newdir = xmalloc(len);
	strcpy(newdir, persist_name);
	*(strrchr(newdir, '/')) = '\0';	/* guaranteed to contain at least one '/' */
	i = 1;
	while (1) {
		if ((p = strchr(newdir+i, '/')))
			*p = '\0';
		if (mkdir(newdir, 0755)) {
			if (errno != EEXIST) {
				error("Cannot mkdir %s for persistent data: %m", newdir);
				goto err1;
			}
			else
				verbose("mkdir %s: %m\n", newdir);
		}
		else
			verbose("mkdir %s\n", newdir);
		if (!p)
			break;
		*p = '/';
		i = p - newdir + 1;
	}

	newname = xmalloc(len+20);	/* for getpid() */
	snprintf(newname, len+20, "%s.%d", persist_name, getpid());
	unlink(newname);
	if ((fd = open(newname, O_WRONLY|O_CREAT|O_EXCL, 0644)) < 0) {
		error("Cannot create %s: %m", newname);
		goto err1;
	}
	if (uname(&uts)) {
		error("uname failed: %m");
		goto err2;
	}
	if ((uname_m = getenv("UNAME_MACHINE"))) {
		int l = strlen(uname_m);
		if (l >= sizeof(uts.machine))
			l = sizeof(uts.machine)-1;
		memcpy(uts.machine, uname_m, l);
		uts.machine[l] = '\0';
	}

	fr = fopen(persist_name, "r");
	fw = fdopen(fd, "w");

	fprintf(fw, "#%% Kernel %s %s %s %s %s\n",
		uts.sysname, uts.nodename, uts.release,
		uts.version, uts.machine);
	clock = time(NULL);
	gmt = gmtime(&clock);
	p = asctime(gmt);
	*(strchr(p, '\n')) = '\0';
	fprintf(fw, "#%% Data saved %s GMT\n", p);

	/* Preserve the order of existing comments and assignments */
	while (fr && fgets(line, sizeof(line), fr)) {
		if (!(p = strchr(line, '\n'))) {
			error("Persistent data line in %s is too long\n%s",
				persist_name, line);
			goto err3;
		}
		*p = '\0';
		p = line;
		while (isspace(*p))
			++p;
		if (strncmp(p, "#%", 2) == 0)
			continue;	/* strip generated comments */
		if (!*p || *p == '#') {
			fprintf(fw, "%s\n", line);
			continue;
		}
		name = p;
		value = strchr(p, '=');
		if (!value) {
			error("No value for %s in %s", name, persist_name);
			goto err3;
		}
		*value = '\0';
		for (i = 0; i < n_mp; ++i) {
			if (strcmp(name, mp[i].sym->name) == 0) {
				mp[i].written = print_persistent_parm(f, fw, mp+i, module);
				break;
			}
		}
		if (i == n_mp) {
			fprintf(fw, "#%% Warning: parameter %s is not supported in this kernel\n", name);
			*value = '=';
			fprintf(fw, "%s\n", line);
		}
	}

	for (i = 0; i < n_mp; ++i) {
		if (!mp[i].written) {
			mp[i].written = print_persistent_parm(f, fw, mp+i, module);
		}
	}

	rename(newname, persist_name);

err3:
	if (fr)
		fclose(fr);
	fclose(fw);

err2:
	unlink(newname);
	close(fd);

err1:
	free(newdir);
	free(newname);
}

/* Save the persistent data (if any) for a module.  If name is NULL, save data
 * from all loaded modules.
 */
static void save_persistent_data(const char *module)
{
	char *persist_name = NULL, *obj_name = NULL, *p;
	time_t mtime;
	struct stat statbuf;
	int fobj;
	struct obj_file *f;
	struct module_parm *mp = NULL;
	int n_mp = 0;
	struct obj_section *sec;
	char *ptr, *type, *n, *endptr;
	int i, len;

	if (!module) {
		/* Recursive invocation for every loaded module */
		size_t n_module_names = n_module_stat;
		char *module_names = xmalloc(l_module_name_list), *p = module_names;
		int nmod;
		memcpy(module_names, module_name_list, l_module_name_list);
		for (nmod = 0; nmod < n_module_names; ++nmod, p += strlen(p)+1)
			save_persistent_data(p);
		free(module_names);
		return;
	}

	/* Does module have persistent data? */
	verbose("Checking %s for persistent data\n", module);
	if (!(persist_name = find_insmod_symbol(module, "P")))
		return;
	verbose("%s has persistent data\n", module);

	/* Locate, lock and verify the object for this module */
	if (!(obj_name = find_insmod_symbol(module, "O"))) {
		error("Cannot find obj_name for %s", module);
		goto err1;
	}
	p = obj_name + strlen(obj_name) - 2;
	while (p > obj_name && strncmp(p, "_V", 2))
		--p;
	if (p == obj_name) {
		error("Cannot find _V in obj_name for %s", module);
		goto err1;
	}
	while (p > obj_name && strncmp(p, "_M", 2))
		--p;
	if (p == obj_name) {
		error("Cannot find _M in obj_name for %s", module);
		goto err1;
	}
	*p = '\0';	/* nul at end of filename */
	p += 2;
	p[8] = '\0';	/* nul at end of timestamp */
	mtime = strtoul(p, &p, 16);
	if (*p) {
		error("Invalid _M timestamp in obj_name for %s", module);
		goto err1;
	}
	if ((fobj = gzf_open(obj_name, O_RDONLY)) < 0) {
		error("Object %s for module %s not found", obj_name, module);
		goto err1;
	}
	error_file = obj_name;
	flock(fobj, LOCK_EX);	/* Prevent concurrent reload */
	if (stat(obj_name, &statbuf) != 0) {
		error("Object for module %s not found", module);
		goto err2;
	}
	if (statbuf.st_mtime != mtime) {
		error("Object for module %s has changed since load", module);
		goto err2;
	}

	/* Load the object and extract data for each persistent parameter */
	if (!(f = obj_load(fobj, ET_REL, obj_name))) {
		error("Failed to load object for %s", module);
		goto err2;
	}
	sec = obj_find_section(f, ".modinfo");
	if (sec == NULL) {
		error("Cannot find .modinfo for %s", module);
		goto err3;
	}
	ptr = sec->contents;
	endptr = ptr + sec->header.sh_size;
	while (ptr < endptr) {
		type = strchr(ptr, '=');
		n = strchr(ptr, '\0');
		if (type) {
			len = type - ptr;
			if (len >= 5 && strncmp(ptr, "parm_", 5) == 0
			    && !(len > 10 && strncmp(ptr, "parm_desc_", 10) == 0)) {
				int ret;
				char *pname = xmalloc(len + 1);
				++type;
				strncpy(pname, ptr + 5, len - 5);
				pname[len - 5] = '\0';
				ret = spd_parm(pname, type, obj_name, f, &mp, &n_mp);
				free(pname);
				if (ret)
					goto err3;
			}
			ptr = n + 1;
		}
	}
	if (!n_mp) {
		error("Module %s should have persistent data but could not find any", module);
		goto err3;
	}

	/* Calculate the kernel address of each persistent parameter */
	obj_allocate_commons(f);	/* Allocating commons can change the size of bss */
	if (find_insmod_sections(module, f))
		goto err3;
	for (i = 0; i < n_mp; ++i) {
		tgt_long sh_addr = f->sections[mp[i].sym->secidx]->header.sh_addr;
		if (!sh_addr) {
			error("No start address for persistent symbol %s in %s",
				mp[i].sym->name, module);
			goto err3;
		}
		mp[i].sym->value += sh_addr;
		verbose("Persistent symbol %s in %s at 0x%" tgt_long_fmt "x\n",
			mp[i].sym->name, module, mp[i].sym->value);
	}

	/* All object checks passed, get data from kernel and update the text file. */
	update_persistent_data(f, persist_name, mp, n_mp, module);

err3:
	obj_free(f);
err2:
	flock(fobj, LOCK_UN);
	close(fobj);
err1:
	free(obj_name);
	free(mp);
	free(persist_name);
	return;
}

int main(int argc, char **argv)
{
	struct module_stat *m;
	int i;
	int j;
	int ret = 0;
	int recursive = 0;
	int persistent = 0;
	size_t n_module_names;
	char *module_names = NULL;

	struct option long_opts[] = {
		{"all", 0, 0, 'a'},
		{"persist", 0, 0, 'e'},
		{"help", 0, 0, 'h'},
		{"stacks", 0, 0, 'r'},
		{"syslog", 0, 0, 's'},
		{"verbose", 0, 0, 'v'},
		{"version", 0, 0, 'V'},
		{0, 0, 0, 0}
	};

	error_file = "rmmod";

	/*
	 * Collect the loaded modules before deletion.  delete_module()
	 * gives no indication that any modules were deleted so we have to
	 * do it the hard way.
	 */
	get_kernel_info(K_SYMBOLS);
	module_names = xmalloc(l_module_name_list);
	memcpy(module_names, module_name_list, l_module_name_list);
	n_module_names = n_module_stat;

	while ((i = getopt_long(argc, argv, "aehrsvV",
				&long_opts[0], NULL)) != EOF)
		switch (i) {
		case 'a':
			/* Remove all unused modules and stacks. */
			save_persistent_data(NULL);
			if (delete_module(NULL)) {
				perror("rmmod");
				snap_shot(module_names, n_module_names);
				free(module_names);
				return 1;
			}
			snap_shot(module_names, n_module_names);
			free(module_names);
			return 0;

		case 'e':
			/* Save persistent data, do not unload module */
			persistent = 1;
			break;

		case 'h':
			rmmod_usage();
			return 0;

		case 'r':
			/* Remove stacks, starting at named top module */
			recursive = 1;
			break;

		case 's':
			/* Start syslogging.  */
			setsyslog("rmmod");
			break;

		case 'v':
			/* Be verbose.  */
			flag_verbose = 1;
			break;

		case 'V':
			fputs("rmmod version " MODUTILS_VERSION "\n", stderr);
			break;

		default:
		usage:
			rmmod_usage();
			return 1;
		}

	if (persistent) {
		if (optind < argc) {
			for (i = optind; i < argc; ++i)
				save_persistent_data(argv[i]);
		}
		else
			save_persistent_data(NULL);
		return 0;
	}

	if (optind >= argc)
		goto usage;

	if (!recursive) {
		for (i = optind; i < argc; ++i) {
			save_persistent_data(argv[i]);
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
			save_persistent_data(m->name);
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
