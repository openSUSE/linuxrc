/*
 * Get kernel symbol table(s) and other relevant module info.
 *
 * Add module_name_list and l_module_name_list.
 *   Keith Owens <kaos@ocs.com.au> November 1999.
 * Björn Ekwall <bj0rn@blox.se> in February 1999 (C)
 * Initial work contributed by Richard Henderson <rth@tamu.edu>
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

#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include "util.h"
#include "module.h"
#include "obj.h"
#include "modstat.h"

struct module_stat *module_stat;
size_t n_module_stat;
char *module_name_list;
size_t l_module_name_list;
struct module_symbol *ksyms;
size_t nksyms;
int k_new_syscalls;

static void *old_kernsym;

/************************************************************************/
static void drop(void)
{
	/*
	 * Clean the slate for multiple runs
	 */
	if (module_stat) {
		struct module_stat *m;
		int i;

		for (i = 0, m = module_stat; i < n_module_stat; ++i, ++m) {
			if (m->syms)
				free(m->syms);
			if (m->refs)
				free(m->refs);
		}
		free(module_stat);
		module_stat = NULL;
		n_module_stat = 0;
	}
	if (module_name_list) {
		free(module_name_list);
		module_name_list = NULL;
		l_module_name_list = 0;
	}
	if (ksyms) {
		free(ksyms);
		ksyms = NULL;
		nksyms = 0;
	}
	if (old_kernsym) {
		free(old_kernsym);
		old_kernsym = NULL;
	}
}

static int new_get_kernel_info(int type)
{
	struct module_stat *modules;
	struct module_stat *m;
	struct module_symbol *syms;
	struct module_symbol *s;
	size_t ret;
	size_t bufsize;
	size_t nmod;
	size_t nsyms;
	size_t i;
	size_t j;
	char *module_names;
	char *mn;

	drop();

	/*
	 * Collect the loaded modules
	 */
	module_names = xmalloc(bufsize = 256);
	while (query_module(NULL, QM_MODULES, module_names, bufsize, &ret)) {
		if (errno != ENOSPC) {
			error("QM_MODULES: %m\n");
			return 0;
		}
		module_names = xrealloc(module_names, bufsize = ret);
	}
	module_name_list = module_names;
	l_module_name_list = bufsize;
	n_module_stat = nmod = ret;
	module_stat = modules = xmalloc(nmod * sizeof(struct module_stat));
	memset(modules, 0, nmod * sizeof(struct module_stat));

	/* Collect the info from the modules */
	for (i = 0, mn = module_names, m = modules;
	     i < nmod;
	     ++i, ++m, mn += strlen(mn) + 1) {
		struct module_info info;

		m->name = mn;
		if (query_module(mn, QM_INFO, &info, sizeof(info), &ret)) {
			if (errno == ENOENT) {
			/* The module was removed out from underneath us. */
				m->flags = NEW_MOD_DELETED;
				continue;
			}
			/* else oops */
			error("module %s: QM_INFO: %m", mn);
			return 0;
		}

		m->addr = info.addr;

		if (type & K_INFO) {
			m->size = info.size;
			m->flags = info.flags;
			m->usecount = info.usecount;
			m->modstruct = info.addr;
		}

		if (type & K_REFS) {
			int mm;
			char *mrefs;
			char *mr;

			mrefs = xmalloc(bufsize = 64);
			while (query_module(mn, QM_REFS, mrefs, bufsize, &ret)) {
				if (errno != ENOSPC) {
					error("QM_REFS: %m");
					return 1;
				}
				mrefs = xrealloc(mrefs, bufsize = ret);
			}
			for (j = 0, mr = mrefs;
			     j < ret;
			     ++j, mr += strlen(mr) + 1) {
				for (mm = 0; mm < i; ++mm) {
					if (strcmp(mr, module_stat[mm].name) == 0) {
						m->nrefs += 1;
						m->refs = xrealloc(m->refs, m->nrefs * sizeof(struct module_stat **));
						m->refs[m->nrefs - 1] = module_stat + mm;
						break;
					}
				}
			}
			free(mrefs);
		}

		if (type & K_SYMBOLS) { /* Want info about symbols */
			syms = xmalloc(bufsize = 1024);
			while (query_module(mn, QM_SYMBOLS, syms, bufsize, &ret)) {
				if (errno == ENOSPC) {
					syms = xrealloc(syms, bufsize = ret);
					continue;
				}
				if (errno == ENOENT) {
					/*
					 * The module was removed out
					 * from underneath us.
					 */
					m->flags = NEW_MOD_DELETED;
					free(syms);
					goto next;
				} else {
					error("module %s: QM_SYMBOLS: %m", mn);
					return 0;
				}
			}
			nsyms = ret;

			m->nsyms = nsyms;
			m->syms = syms;

			/* Convert string offsets to string pointers */
			for (j = 0, s = syms; j < nsyms; ++j, ++s)
				s->name += (unsigned long) syms;
		}
		next:
	}

	if (type & K_SYMBOLS) { /* Want info about symbols */
		/* Collect the kernel's symbols.  */
		syms = xmalloc(bufsize = 16 * 1024);
		while (query_module(NULL, QM_SYMBOLS, syms, bufsize, &ret)) {
			if (errno != ENOSPC) {
				error("kernel: QM_SYMBOLS: %m");
				return 0;
			}
			syms = xrealloc(syms, bufsize = ret);
		}
		nksyms = nsyms = ret;
		ksyms = syms;

		/* Convert string offsets to string pointers */
		for (j = 0, s = syms; j < nsyms; ++j, ++s)
			s->name += (unsigned long) syms;
	}

	return 1;
}

#ifdef COMPAT_2_0
/************************************************************************/

#define mscan(offs,siz,ptr) \
	if (lseek(kmem_fd, (off_t)(offs), SEEK_SET) == -1 || \
	    read(kmem_fd, (ptr), (siz)) != (siz)) { \
		if (kmem_fd != -1) \
			close(kmem_fd); \
		error("kmem: %m"); \
		return 0; \
	}

#define OLD_MOD_RUNNING 1
#define OLD_MOD_DELETED 2
#define OLD_MOD_VISITED   0x20000000

/* Fetch all the symbols and divvy them up as appropriate for the modules.  */
static int old_get_kernel_info(int type)
{
	struct old_kernel_sym *kernsym;
	struct old_kernel_sym *k;
	struct module_stat *module;
	struct module_stat *mod;
	struct module_symbol *s = NULL;
	int kmem_fd = -1;
	int nkernsym;
	int nmod;
	int nm;
	int nms;
	int i;

	drop();
	module_name_list = xmalloc(1);
	*module_name_list = '\0';

	if ((nkernsym = get_kernel_syms(NULL)) < 0) {
		error("get_kernel_syms: %m");
		return 0;
	}
	kernsym = k = xmalloc(nkernsym * sizeof(struct old_kernel_sym));
	old_kernsym = kernsym;
	if (get_kernel_syms(kernsym) != nkernsym) {
		error("inconsistency with get_kernel_syms -- is someone else "
		      "playing with modules?");
		free(kernsym);
		return 0;
	}

	/* Number of modules */
	for (k = kernsym, nmod = 0, i = 0; i < nkernsym; ++i, ++k) {
		if (k->name[0] == '#') {
			if (k->name[1]) {
				++nmod;
				i = strlen(k->name+1) + 1;
				module_name_list =
					xrealloc(module_name_list,
					l_module_name_list + i);
				strcpy(module_name_list+l_module_name_list,	/* safe, xrealloc */
					k->name+1);
				l_module_name_list += i;	/* NUL separated strings */
			}
			else
				break;
		}
	}
	module_stat = mod = module = xmalloc(nmod * sizeof(struct module_stat));
	memset(module, 0, nmod * sizeof(struct module_stat));
	n_module_stat = nmod;

	/*
	 * Will we need kernel internal info?
	 */
	if ((type & K_INFO) || (type & K_REFS)) {
		if ((kmem_fd = open("/dev/kmem", O_RDONLY)) < 0) {
			perror("ksyms: open /dev/kmem");
			return 0;
		}
	}

	/*
	 * Collect the module information.
	 */
	for (k = kernsym, nm = 0, i = 0; i < nkernsym; ++i, ++k) {
		if (k->name[0] == '#') {
			struct old_kernel_sym *p;
			struct old_module info;

			if (k->name[1] == '\0')
				break; /* kernel resident symbols follow */
			/* else normal module */

			module = mod++;
			++nm;
			module->name = k->name + 1;
			module->modstruct = k->value;

			if ((type & K_INFO) || (type & K_REFS)) {
				long tmp;
				/*
				 * k->value is the address of the
				 * struct old_module
				 * in the kernel (for use via /dev/kmem)
				 */
				mscan(k->value, sizeof(info), &info);
				module->addr = info.addr;
				module->size = info.size * getpagesize();

				mscan(info.addr, sizeof(long), &tmp);
				module->flags = info.state &
						(OLD_MOD_RUNNING | OLD_MOD_DELETED);
				module->flags |= NEW_MOD_USED_ONCE; /* Cheat */
				if (tmp & OLD_MOD_AUTOCLEAN)
					module->flags |= NEW_MOD_AUTOCLEAN;
				if (tmp & OLD_MOD_VISITED)
					module->flags |= NEW_MOD_VISITED;

				module->usecount = tmp & ~(OLD_MOD_AUTOCLEAN | OLD_MOD_VISITED);
			}

			if ((type & K_REFS) && info.ref) {
				struct old_module_ref mr;
				int j;
				unsigned long ref = info.ref;

				do {
					mscan(ref, sizeof(struct old_module_ref), &mr);
					for (j = 0; j < nm -1; ++j) {
						if (mr.module == module_stat[j].modstruct) {
							module->nrefs += 1;
							module->refs = xrealloc(module->refs, module->nrefs * sizeof(struct module_stat **));
							module->refs[module->nrefs - 1] = module_stat + j;
							break;
						}
					}
				} while ((ref = mr.next) != 0);
			}

			if (!(type & K_SYMBOLS))
				continue;
			/*
			 * Find out how many symbols this module has.
			 */
			for (nms = 0, p = k+1; p->name[0] != '#'; ++p)
				++nms;
			s = xmalloc(nms * sizeof(struct module_symbol));
			module->syms = s;
			module->nsyms = nms;
		} else if (type & K_SYMBOLS) { /* Want info about symbols */
			s->name = (unsigned long) k->name;
			s->value = k->value;
			++s;
		}
	}
	if ((type & K_INFO) || (type & K_REFS)) {
		if (kmem_fd != -1)
			close(kmem_fd);
	}

	/*
	 * Kernel resident symbols follows
	 */
	if (type & K_SYMBOLS) { /* Want info about symbols */
		if (k->name[0] == '#')
			++k;
		nksyms = nkernsym - (k - kernsym);
		if (nksyms) {
			ksyms = s = xmalloc(nksyms * sizeof(struct module_symbol));
			for (i = 0; i < nksyms; ++i, ++k) {
				if (k->name[0] != '#') {
					s->name = (unsigned long) k->name;
					s->value = k->value;
					++s;
				}
			}
			nksyms = s - ksyms;
		} else
			ksyms = NULL;
	}

	return 1;
}
#endif /* COMPAT_2_0 */

int get_kernel_info(int type)
{
	k_new_syscalls = !query_module(NULL, 0, NULL, 0, NULL);

#ifdef COMPAT_2_0
	if (!k_new_syscalls)
		return old_get_kernel_info(type);
#endif /* COMPAT_2_0 */

	return new_get_kernel_info(type);
}
