/* Insert a module into a running kernel.
   Copyright 1996, 1997 Linux International.

   New implementation contributed by Richard Henderson <rth@tamu.edu>
   Based on original work by Bjorn Ekwall <bj0rn@blox.se>
   Restructured (and partly rewritten) by:
	Björn Ekwall <bj0rn@blox.se> February 1999

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

  /*
     Fixes:

     Adjust module size for mod_use_count in old_init_module:
     B. James Phillippe <bryan@terran.org>

     Merged modprobe + many fixes: Björn Ekwall <bj0rn@blox.se> February 1999
     SMP "friendliness" (and -P): Bill Zumach <zumach+@transarc.com>

     Ksymoops support: Keith Owens <kaos@ocs.com.au> August 1999.

     Add -r flag: Keith Owens <kaos@ocs.com.au> October 1999.

     More flexible recognition of the way the utility was called.
     Suggested by Stepan Kasal, implemented in a different way by Keith
     Owens <kaos@ocs.com.au> December 1999.

     Rationalize common code for 32/64 bit architectures.
       Keith Owens <kaos@ocs.com.au> December 1999.
     Add arch64().
       Keith Owens <kaos@ocs.com.au> December 1999.
     kallsyms support
       Keith Owens <kaos@ocs.com.au> April 2000.
     archdata support
       Keith Owens <kaos@ocs.com.au> August 2000.
     Add insmod -O, move print map before sys_init_module.
       Keith Owens <kaos@ocs.com.au> October 2000.
     Add insmod -S.
       Keith Owens <kaos@ocs.com.au> November 2000.
     Add persistent data support.
       Keith Owens <kaos@ocs.com.au> November 2000.
     Add tainted module support.
       Keith Owens <kaos@ocs.com.au> September 2001.
   */

#include <sys/types.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <alloca.h>
#include <limits.h>
#include <ctype.h>
#include <errno.h>
#include <stddef.h>
#include <getopt.h>
#include <sys/stat.h>
#include <sys/file.h>

#include "module.h"
#include "obj.h"
#include "kallsyms.h"
#include "util.h"
#include "version.h"

#include "modstat.h"
#include "config.h"

#define STRVERSIONLEN	32

/*======================================================================*/

static int flag_force_load = 0;
static int flag_silent_probe = 0;
static int flag_export = 1;
static int flag_load_map = 0;
static int flag_ksymoops = 1;

static int n_ext_modules_used;
static int m_has_modinfo;
static int gplonly_seen;
static int warnings;

extern int insmod_main(int argc, char **argv);
extern int insmod_main_32(int argc, char **argv);
extern int insmod_main_64(int argc, char **argv);
extern int modprobe_main(int argc, char **argv);
extern int rmmod_main(int argc, char **argv);
extern int ksyms_main(int argc, char **argv);
extern int lsmod_main(int argc, char **argv);
extern int kallsyms_main(int argc, char **argv);

/*======================================================================*/

/* Get the kernel version in the canonical integer form.  */

static int get_kernel_version(char str[STRVERSIONLEN])
{
	char *p, *q;
	int a, b, c;

	strncpy(str, uts_info.release, STRVERSIONLEN);
	p = uts_info.release;

	a = strtoul(p, &p, 10);
	if (*p != '.')
		return -1;
	b = strtoul(p + 1, &p, 10);
	if (*p != '.')
		return -1;
	c = strtoul(p + 1, &q, 10);
	if (p + 1 == q)
		return -1;

	return a << 16 | b << 8 | c;
}

/* String comparison for non-co-versioned kernel and module.
 * prefix should be the same as used by genksyms for this kernel.
 */
static char *ncv_prefix = NULL;	/* Overridden by --prefix option */
static int ncv_plen = 0;

/* Only set prefix once. If set by the user, use it.  If not set by the
 * user, look for a well known kernel symbol and derive the prefix from
 * there.  Otherwise set the prefix depending on whether uts_info
 * includes SMP or not for backwards compatibility.
 */
static void set_ncv_prefix(char *prefix)
{
	static char derived_prefix[256];
	static const char *well_known_symbol[] = { "get_module_symbol_R",
						   "inter_module_get_R",
						 };
	struct module_symbol *s;
	int i, j, l, m, pl;
	const char *name;
	char *p;

	if (ncv_prefix)
		return;

	if (prefix)
		ncv_prefix = prefix;
	else {
		/* Extract the prefix (if any) from well known symbols */
		for (i = 0, s = ksyms; i < nksyms; ++i, ++s) {
			name = (char *) s->name;
			l = strlen(name);
			for (j = 0; j < sizeof(well_known_symbol)/sizeof(well_known_symbol[0]); ++j) {
				m = strlen(well_known_symbol[j]);
				if (m + 8 > l ||
				    strncmp(name, well_known_symbol[j], m))
					continue;
				pl = l - m - 8;
				if (pl > sizeof(derived_prefix)-1)
					continue;	/* Prefix is wrong length */
				/* Must end with 8 hex digits */
				(void) strtoul(name+l-8, &p, 16);
				if (*p == 0) {
					strncpy(derived_prefix, name+m, pl);
					*(derived_prefix+pl) = '\0';
					ncv_prefix = derived_prefix;
					break;
				}
			}
		}
	}
	if (!ncv_prefix) {
		p = strchr(uts_info.version, ' ');
		if (p && *(++p) && !strncmp(p, "SMP ", 4))
			ncv_prefix = "smp_";
		else
			ncv_prefix = "";
	}
	ncv_plen = strlen(ncv_prefix);
	if (flag_verbose)
		lprintf("Symbol version prefix '%s'", ncv_prefix);
}

static int ncv_strcmp(const char *a, const char *b)
{
	size_t alen = strlen(a), blen = strlen(b);

	if (blen == alen + 10 + ncv_plen &&
	    b[alen] == '_' &&
	    b[alen + 1] == 'R' &&
	    !(ncv_plen && strncmp(b + alen + 2, ncv_prefix, ncv_plen))) {
		return strncmp(a, b, alen);
	} else if (alen == blen + 10 + ncv_plen &&
		   a[blen] == '_' && a[blen + 1] == 'R' &&
		   !(ncv_plen && strncmp(a + blen + 2, ncv_prefix, ncv_plen))) {
		return strncmp(a, b, blen);
	} else
		return strcmp(a, b);
}

/*
 * String hashing for non-co-versioned kernel and module.
 * Here we are simply forced to drop the crc from the hash.
 */
static unsigned long ncv_symbol_hash(const char *str)
{
	size_t len = strlen(str);

	if (len > 10 + ncv_plen &&
	    str[len - 10 - ncv_plen] == '_' &&
	    str[len - 9 - ncv_plen] == 'R' &&
	    !(
	      ncv_plen &&
	      strncmp(str + len - (8 + ncv_plen), ncv_prefix, ncv_plen)
	     ))
		len -= 10 + ncv_plen;
	return obj_elf_hash_n(str, len);
}

/*
 * Conditionally add the symbols from the given symbol set
 * to the new module.
 */
static int add_symbols_from(struct obj_file *f, int idx,
			    struct module_symbol *syms, size_t nsyms, int gpl)
{
	struct module_symbol *s;
	size_t i;
	int used = 0;

	for (i = 0, s = syms; i < nsyms; ++i, ++s) {
		/*
		 * Only add symbols that are already marked external.
		 * If we override locals we may cause problems for
		 * argument initialization.
		 * We will also create a false dependency on the module.
		 */
		struct obj_symbol *sym;

		/* GPL licensed modules can use symbols exported with
		 * EXPORT_SYMBOL_GPL, so ignore any GPLONLY_ prefix on the
		 * exported names.  Non-GPL modules never see any GPLONLY_
		 * symbols so they cannot fudge it by adding the prefix on
		 * their references.
		 */
		if (strncmp((char *)s->name, "GPLONLY_", 8) == 0) {
			gplonly_seen = 1;
			if (gpl)
				((char *)s->name) += 8;
			else
				continue;
		}

		sym = obj_find_symbol(f, (char *) s->name);
#ifdef	ARCH_ppc64
		if (!sym)
		  {
		    static size_t buflen = 0;
		    static char *buf = 0;
		    int len;

		    /* ppc64 is one of those architectures with
		       function descriptors.  A function is exported
		       and accessed across object boundaries via its
		       function descriptor.  The function code symbol
		       happens to be the function name, prefixed with
		       '.', and a function call is a branch to the
		       code symbol.  The linker recognises when a call
		       crosses object boundaries, and inserts a stub
		       to call via the function descriptor.
		       obj_ppc64.c of course does the same thing, so
		       here we recognise that an undefined code symbol
		       can be satisfied by the corresponding function
		       descriptor symbol.  */

		    len = strlen ((char *) s->name) + 2;
		    if (buflen < len)
		      {
			buflen = len + (len >> 1);
			if (buf)
			  free (buf);
			buf = malloc (buflen);
		      }
		    buf[0] = '.';
		    strcpy (buf + 1, (char *) s->name);
		    sym = obj_find_symbol(f, buf);
		  }
#endif	/* ARCH_ppc64 */

		if (sym && ELFW(ST_BIND) (sym->info) != STB_LOCAL) {
			sym = obj_add_symbol(f, (char *) s->name, -1,
				  ELFW(ST_INFO) (STB_GLOBAL, STT_NOTYPE),
					     idx, s->value, 0);
			/*
			 * Did our symbol just get installed?
			 * If so, mark the module as "used".
			 */
			if (sym->secidx == idx)
				used = 1;
		}
	}

	return used;
}

static void add_kernel_symbols(struct obj_file *f, int gpl)
{
	struct module_stat *m;
	size_t i, nused = 0;

	/* Add module symbols first.  */
	for (i = 0, m = module_stat; i < n_module_stat; ++i, ++m)
		if (m->nsyms &&
		    add_symbols_from(f, SHN_HIRESERVE + 2 + i, m->syms, m->nsyms, gpl))
			m->status = 1 /* used */, ++nused;
	n_ext_modules_used = nused;

	/* And finally the symbols from the kernel proper.  */
	if (nksyms)
		add_symbols_from(f, SHN_HIRESERVE + 1, ksyms, nksyms, gpl);
}

static void hide_special_symbols(struct obj_file *f)
{
	struct obj_symbol *sym;
	const char *const *p;
	static const char *const specials[] =
	{
		"cleanup_module",
		"init_module",
		"kernel_version",
		NULL
	};

	for (p = specials; *p; ++p)
		if ((sym = obj_find_symbol(f, *p)) != NULL)
			sym->info = ELFW(ST_INFO) (STB_LOCAL, ELFW(ST_TYPE) (sym->info));
}

static void print_load_map(struct obj_file *f)
{
	struct obj_symbol *sym;
	struct obj_symbol **all, **p;
	struct obj_section *sec;
	int load_map_cmp(const void *a, const void *b) {
		struct obj_symbol **as = (struct obj_symbol **) a;
		struct obj_symbol **bs = (struct obj_symbol **) b;
		unsigned long aa = obj_symbol_final_value(f, *as);
		unsigned long ba = obj_symbol_final_value(f, *bs);
		 return aa < ba ? -1 : aa > ba ? 1 : 0;
	}
	int i, nsyms, *loaded;

	/* Report on the section layout.  */

	lprintf("Sections:       Size      %-*s  Align",
		(int) (2 * sizeof(void *)), "Address");

	for (sec = f->load_order; sec; sec = sec->load_next) {
		int a;
		unsigned long tmp;

		for (a = -1, tmp = sec->header.sh_addralign; tmp; ++a)
			tmp >>= 1;
		if (a == -1)
			a = 0;

		lprintf("%-15s %08lx  %0*lx  2**%d",
			sec->name,
			(long)sec->header.sh_size,
			(int) (2 * sizeof(void *)),
			(long)sec->header.sh_addr,
			a);
	}

	/* Quick reference which section indicies are loaded.  */

	loaded = alloca(sizeof(int) * (i = f->header.e_shnum));
	while (--i >= 0)
		loaded[i] = (f->sections[i]->header.sh_flags & SHF_ALLOC) != 0;

	/* Collect the symbols we'll be listing.  */

	for (nsyms = i = 0; i < HASH_BUCKETS; ++i)
		for (sym = f->symtab[i]; sym; sym = sym->next)
			if (sym->secidx <= SHN_HIRESERVE
			    && (sym->secidx >= SHN_LORESERVE || loaded[sym->secidx]))
				++nsyms;

	all = alloca(nsyms * sizeof(struct obj_symbol *));

	for (i = 0, p = all; i < HASH_BUCKETS; ++i)
		for (sym = f->symtab[i]; sym; sym = sym->next)
			if (sym->secidx <= SHN_HIRESERVE
			    && (sym->secidx >= SHN_LORESERVE || loaded[sym->secidx]))
				*p++ = sym;

	/* Sort them by final value.  */
	qsort(all, nsyms, sizeof(struct obj_file *), load_map_cmp);

	/* And list them.  */
	lprintf("\nSymbols:");
	for (p = all; p < all + nsyms; ++p) {
		char type = '?';
		unsigned long value;

		sym = *p;
		if (sym->secidx == SHN_ABS) {
			type = 'A';
			value = sym->value;
		} else if (sym->secidx == SHN_UNDEF) {
			type = 'U';
			value = 0;
		} else {
			struct obj_section *sec = f->sections[sym->secidx];

			if (sec->header.sh_type == SHT_NOBITS)
				type = 'B';
			else if (sec->header.sh_flags & SHF_ALLOC) {
				if (sec->header.sh_flags & SHF_EXECINSTR)
					type = 'T';
				else if (sec->header.sh_flags & SHF_WRITE)
					type = 'D';
				else
					type = 'R';
			}
			value = sym->value + sec->header.sh_addr;
		}

		if (ELFW(ST_BIND) (sym->info) == STB_LOCAL)
			type = tolower(type);

		lprintf("%0*lx %c %s", (int) (2 * sizeof(void *)), value,
			type, sym->name);
	}
}

/************************************************************************/
/* begin compat */

static char * get_modinfo_value(struct obj_file *f, const char *key)
{
	struct obj_section *sec;
	char *p, *v, *n, *ep;
	size_t klen = strlen(key);

	sec = obj_find_section(f, ".modinfo");
	if (sec == NULL)
		return NULL;

	p = sec->contents;
	ep = p + sec->header.sh_size;
	while (p < ep) {
		v = strchr(p, '=');
		n = strchr(p, '\0');
		if (v) {
			if (v - p == klen && strncmp(p, key, klen) == 0)
				return v + 1;
		} else {
			if (n - p == klen && strcmp(p, key) == 0)
				return n;
		}
		p = n + 1;
	}

	return NULL;
}

static int create_this_module(struct obj_file *f, const char *m_name)
{
	struct obj_section *sec;

	sec = obj_create_alloced_section_first(f, ".this", tgt_sizeof_long,
					       sizeof(struct module));
	memset(sec->contents, 0, sizeof(struct module));

	obj_add_symbol(f, "__this_module", -1, ELFW(ST_INFO) (STB_LOCAL, STT_OBJECT),
		       sec->idx, 0, sizeof(struct module));

	obj_string_patch(f, sec->idx, offsetof(struct module, name), m_name);

	return 1;
}

#ifdef COMPAT_2_0
static int old_create_mod_use_count(struct obj_file *f)
{
	struct obj_section *sec;
	struct obj_symbol  *got;

	sec = obj_create_alloced_section_first(f, ".moduse",
					       sizeof(long), sizeof(long));

	obj_add_symbol(f, "mod_use_count_",
		       -1, ELFW(ST_INFO)(STB_LOCAL, STT_OBJECT),
		       sec->idx, 0, sizeof(long));

	/*
	 * patb: if there is a _GLOBAL_OFFSET_TABLE_,
	 * add .got section for PIC type modules;
	 * we have to do this here, because obj_* calls are not made until
	 * after obj_check_undefined
	 * is there a better place for this exception?
	 */
	got = obj_find_symbol(f, "_GLOBAL_OFFSET_TABLE_");
	if (got)
	{
		sec = obj_create_alloced_section(f, ".got",
						 sizeof(long), sizeof(long),
						 SHF_WRITE);
		got->secidx = sec->idx; /* mark the symbol as defined */
	}
	return 1;
}
#endif

/* add an entry to the __ksymtab section, creating it if necessary */
static void add_ksymtab(struct obj_file *f, struct obj_symbol *sym)
{
	struct obj_section *sec;
	ElfW(Addr) ofs;

	/* ensure __ksymtab is allocated, EXPORT_NOSYMBOLS creates a non-alloc section.
	 * If __ksymtab is defined but not marked alloc, x out the first character
	 * (no obj_delete routine) and create a new __ksymtab with the correct
	 * characteristics.
	 */
	sec = obj_find_section(f, "__ksymtab");
	if (sec && !(sec->header.sh_flags & SHF_ALLOC)) {
		*((char *)(sec->name)) = 'x';	/* override const */
		sec = NULL;
	}
	if (!sec)
		sec = obj_create_alloced_section(f, "__ksymtab",
						 tgt_sizeof_void_p, 0, 0);
	if (!sec)
		return;
	sec->header.sh_flags |= SHF_ALLOC;

	ofs = sec->header.sh_size;
	obj_symbol_patch(f, sec->idx, ofs, sym);
	obj_string_patch(f, sec->idx, ofs + tgt_sizeof_void_p, sym->name);
	obj_extend_section(sec, 2 * tgt_sizeof_char_p);
}

static int create_module_ksymtab(struct obj_file *f)
{
	struct obj_section *sec;
	int i;

	/* We must always add the module references.  */

	if (n_ext_modules_used) {
		struct module_ref *dep;
		struct obj_symbol *tm;

		sec = obj_create_alloced_section(f, ".kmodtab",
			tgt_sizeof_void_p,
			sizeof(struct module_ref) * n_ext_modules_used, 0);
		if (!sec)
			return 0;

		tm = obj_find_symbol(f, "__this_module");
		dep = (struct module_ref *) sec->contents;
		for (i = 0; i < n_module_stat; ++i)
			if (module_stat[i].status /* used */) {
				dep->dep = module_stat[i].addr;
#ifdef ARCH_ppc64
				dep->dep |= ppc64_module_base (f);
#endif
				obj_symbol_patch(f, sec->idx, (char *) &dep->ref - sec->contents, tm);
				dep->next_ref = 0;
				++dep;
			}
	}
	if (flag_export && !obj_find_section(f, "__ksymtab")) {
		int *loaded;

		/* We don't want to export symbols residing in sections that
		   aren't loaded.  There are a number of these created so that
		   we make sure certain module options don't appear twice.  */

		loaded = alloca(sizeof(int) * (i = f->header.e_shnum));
		while (--i >= 0)
			loaded[i] = (f->sections[i]->header.sh_flags & SHF_ALLOC) != 0;

		for (i = 0; i < HASH_BUCKETS; ++i) {
			struct obj_symbol *sym;
			for (sym = f->symtab[i]; sym; sym = sym->next) {
				if (ELFW(ST_BIND) (sym->info) != STB_LOCAL
				    && sym->secidx <= SHN_HIRESERVE
				    && (sym->secidx >= SHN_LORESERVE
					|| loaded[sym->secidx])) {
					add_ksymtab(f, sym);
				}
			}
		}
	}
	return 1;
}

/* Get the module's kernel version in the canonical integer form.  */
static int get_module_version(struct obj_file *f, char str[STRVERSIONLEN])
{
	int a, b, c;
	char *p, *q;

	if ((p = get_modinfo_value(f, "kernel_version")) == NULL) {
		struct obj_symbol *sym;

		m_has_modinfo = 0;
		if ((sym = obj_find_symbol(f, "kernel_version")) == NULL)
			sym = obj_find_symbol(f, "__module_kernel_version");
		if (sym == NULL)
			return -1;
		p = f->sections[sym->secidx]->contents + sym->value;
	} else
		m_has_modinfo = 1;

	strncpy(str, p, STRVERSIONLEN);

	a = strtoul(p, &p, 10);
	if (*p != '.')
		return -1;
	b = strtoul(p + 1, &p, 10);
	if (*p != '.')
		return -1;
	c = strtoul(p + 1, &q, 10);
	if (p + 1 == q)
		return -1;

	return a << 16 | b << 8 | c;
}

/* Return the kernel symbol checksum version, or zero if not used. */
static int is_kernel_checksummed(void)
{
	struct module_symbol *s;
	size_t i;

	/*
	 * Using_Versions might not be the first symbol,
	 * but it should be in there.
	 */
	for (i = 0, s = ksyms; i < nksyms; ++i, ++s)
		if (strcmp((char *) s->name, "Using_Versions") == 0)
			return s->value;

	return 0;
}

static int is_module_checksummed(struct obj_file *f)
{
	if (m_has_modinfo) {
		const char *p = get_modinfo_value(f, "using_checksums");
		if (p)
			return atoi(p);
		else
			return 0;
	} else
		return obj_find_symbol(f, "Using_Versions") != NULL;
}

/* add module source, timestamp, kernel version and a symbol for the
 * start of some sections.  this info is used by ksymoops to do better
 * debugging.
 */
static void add_ksymoops_symbols(struct obj_file *f, const char *filename,
				 const char *m_name)
{
	struct obj_section *sec;
	struct obj_symbol *sym;
	char *name, *absolute_filename;
	char str[STRVERSIONLEN], real[PATH_MAX];
	int i, l, lm_name, lfilename, use_ksymtab, version;
	struct stat statbuf;

	static const char *section_names[] = {
		".text",
		".rodata",
		".data",
		".bss"
	};

	if (realpath(filename, real)) {
		absolute_filename = xstrdup(real);
	}
	else {
		int save_errno = errno;
		error("cannot get realpath for %s", filename);
		errno = save_errno;
		perror("");
		absolute_filename = xstrdup(filename);
	}

	lm_name = strlen(m_name);
	lfilename = strlen(absolute_filename);

	/* add to ksymtab if it already exists or there is no ksymtab and other symbols
	 * are not to be exported.  otherwise leave ksymtab alone for now, the
	 * "export all symbols" compatibility code will export these symbols later.
	 */

	use_ksymtab =  obj_find_section(f, "__ksymtab") || !flag_export;

	if ((sec = obj_find_section(f, ".this"))) {
		/* tag the module header with the object name, last modified
		 * timestamp and module version.  worst case for module version
		 * is 0xffffff, decimal 16777215.  putting all three fields in
		 * one symbol is less readable but saves kernel space.
		 */
		l = sizeof(symprefix)+			/* "__insmod_" */
		    lm_name+				/* module name */
		    2+					/* "_O" */
		    lfilename+				/* object filename */
		    2+					/* "_M" */
		    2*sizeof(statbuf.st_mtime)+		/* mtime in hex */
		    2+					/* "_V" */
		    8+					/* version in dec */
		    1;					/* nul */
		name = xmalloc(l);
		if (stat(absolute_filename, &statbuf) != 0)
			statbuf.st_mtime = 0;
		version = get_module_version(f, str);	/* -1 if not found */
		snprintf(name, l, "%s%s_O%s_M%0*lX_V%d",
			 symprefix, m_name, absolute_filename,
			 (int)(2*sizeof(statbuf.st_mtime)), statbuf.st_mtime,
			 version);
		sym = obj_add_symbol(f, name, -1,
				     ELFW(ST_INFO) (STB_GLOBAL, STT_NOTYPE),
				     sec->idx, sec->header.sh_addr, 0);
		if (use_ksymtab)
			add_ksymtab(f, sym);
	}
	free(absolute_filename);

	/* record where the persistent data is going, same address as previous symbol */

	if (f->persist) {
		l = sizeof(symprefix)+		/* "__insmod_" */
			lm_name+		/* module name */
			2+			/* "_P" */
			strlen(f->persist)+	/* data store */
			1;			/* nul */
		name = xmalloc(l);
		snprintf(name, l, "%s%s_P%s",
			 symprefix, m_name, f->persist);
		sym = obj_add_symbol(f, name, -1, ELFW(ST_INFO) (STB_GLOBAL, STT_NOTYPE),
				     sec->idx, sec->header.sh_addr, 0);
		if (use_ksymtab)
			add_ksymtab(f, sym);
	}

	/* tag the desired sections if size is non-zero */

	for (i = 0; i < sizeof(section_names)/sizeof(section_names[0]); ++i) {
		if ((sec = obj_find_section(f, section_names[i])) &&
		    sec->header.sh_size) {
			l = sizeof(symprefix)+		/* "__insmod_" */
				lm_name+		/* module name */
				2+			/* "_S" */
				strlen(sec->name)+	/* section name */
				2+			/* "_L" */
				8+			/* length in dec */
				1;			/* nul */
			name = xmalloc(l);
			snprintf(name, l, "%s%s_S%s_L%ld",
				 symprefix, m_name, sec->name,
				 (long)sec->header.sh_size);
			sym = obj_add_symbol(f, name, -1, ELFW(ST_INFO) (STB_GLOBAL, STT_NOTYPE),
					     sec->idx, sec->header.sh_addr, 0);
			if (use_ksymtab)
				add_ksymtab(f, sym);
		}
	}
}

static int process_module_arguments(struct obj_file *f, int argc, char **argv, int required)
{
	for (; argc > 0; ++argv, --argc) {
		struct obj_symbol *sym;
		int c;
		int min, max;
		int n;
		char *contents;
		char *input;
		char *fmt;
		char *key;
		char *loc;

		if ((input = strchr(*argv, '=')) == NULL)
			continue;

		n = input - *argv;
		input += 1; /* skip '=' */

		key = alloca(n + 6);

		if (m_has_modinfo) {
			memcpy(key, "parm_", 5);
			memcpy(key + 5, *argv, n);
			key[n + 5] = '\0';
			if ((fmt = get_modinfo_value(f, key)) == NULL) {
				if (required) {
					error("invalid parameter %s", key);
					return 0;
				}
				else {
					if (flag_verbose)
						lprintf("ignoring %s", *argv);
					continue;	/* silently ignore optional parameters */
				}
			}
			key += 5;

			if (isdigit(*fmt)) {
				min = strtoul(fmt, &fmt, 10);
				if (*fmt == '-')
					max = strtoul(fmt + 1, &fmt, 10);
				else
					max = min;
			} else
				min = max = 1;
		} else { /* not m_has_modinfo */
			memcpy(key, *argv, n);
			key[n] = '\0';

			if (isdigit(*input))
				fmt = "i";
			else
				fmt = "s";
			min = max = 0;
		}

		sym = obj_find_symbol(f, key);

		/*
		 * Also check that the parameter was not
		 * resolved from the kernel.
		 */
		if (sym == NULL || sym->secidx > SHN_HIRESERVE) {
			error("symbol for parameter %s not found", key);
			return 0;
		}

		contents = f->sections[sym->secidx]->contents;
		loc = contents + sym->value;
		n = 1;

		while (*input) {
			char *str;

			switch (*fmt) {
			case 's':
			case 'c':
				/*
				 * Do C quoting if we begin with a ",
				 * else slurp the lot.
				 */
				if (*input == '"') {
					char *r;

					str = alloca(strlen(input));
					for (r = str, input++; *input != '"'; ++input, ++r) {
						if (*input == '\0') {
							error("improperly terminated string argument for %s", key);
							return 0;
						}
						/* else */
						if (*input != '\\') {
							*r = *input;
							continue;
						}
						/* else  handle \ */
						switch (*++input) {
						case 'a': *r = '\a'; break;
						case 'b': *r = '\b'; break;
						case 'e': *r = '\033'; break;
						case 'f': *r = '\f'; break;
						case 'n': *r = '\n'; break;
						case 'r': *r = '\r'; break;
						case 't': *r = '\t'; break;

						case '0':
						case '1':
						case '2':
						case '3':
						case '4':
						case '5':
						case '6':
						case '7':
							c = *input - '0';
							if ('0' <= input[1] && input[1] <= '7') {
								c = (c * 8) + *++input - '0';
								if ('0' <= input[1] && input[1] <= '7')
									c = (c * 8) + *++input - '0';
							}
							*r = c;
							break;

						default: *r = *input; break;
						}
					}
					*r = '\0';
					++input;
				} else {
					/*
					 * The string is not quoted.
					 * We will break it using the comma
					 * (like for ints).
					 * If the user wants to include commas
					 * in a string, he just has to quote it
					 */
					char *r;

					/* Search the next comma */
					if ((r = strchr(input, ',')) != NULL) {
						/*
						 * Found a comma
						 * Recopy the current field
						 */
						str = alloca(r - input + 1);
						memcpy(str, input, r - input);
						str[r - input] = '\0';
						/* Keep next fields */
						input = r;
					} else {
						/* last string */
						str = input;
						input = "";
					}
				}

				if (*fmt == 's') {
					/* Normal string */
					obj_string_patch(f, sym->secidx, loc - contents, str);
					loc += tgt_sizeof_char_p;
				} else {
					/* Array of chars (in fact, matrix !) */
					long charssize;	/* size of each member */

					/* Get the size of each member */
					/* Probably we should do that outside the loop ? */
					if (!isdigit(*(fmt + 1))) {
						error("parameter type 'c' for %s must be followed by"
						" the maximum size", key);
						return 0;
					}
					charssize = strtoul(fmt + 1, (char **) NULL, 10);

					/* Check length */
					if (strlen(str) >= charssize-1) {
						error("string too long for %s (max %ld)",
						      key, charssize - 1);
						return 0;
					}
					/* Copy to location */
					strcpy((char *) loc, str);	/* safe, see check above */
					loc += charssize;
				}
				/*
				 * End of 's' and 'c'
				 */
				break;

			case 'b':
				*loc++ = strtoul(input, &input, 0);
				break;

			case 'h':
				*(short *) loc = strtoul(input, &input, 0);
				loc += tgt_sizeof_short;
				break;

			case 'i':
				*(int *) loc = strtoul(input, &input, 0);
				loc += tgt_sizeof_int;
				break;

			case 'l':
				*(long *) loc = strtoul(input, &input, 0);
				loc += tgt_sizeof_long;
				break;

			default:
				error("unknown parameter type '%c' for %s",
				      *fmt, key);
				return 0;
			}
			/*
			 * end of switch (*fmt)
			 */

			while (*input && isspace(*input))
				++input;
			if (*input == '\0')
				break; /* while (*input) */
			/* else */

			if (*input == ',') {
				if (max && (++n > max)) {
					error("too many values for %s (max %d)", key, max);
					return 0;
				}
				++input;
				/* continue with while (*input) */
			} else {
				error("invalid argument syntax for %s: '%c'",
				      key, *input);
				return 0;
			}
		} /* end of while (*input) */

		if (min && (n < min)) {
			error("too few values for %s (min %d)", key, min);
			return 0;
		}
	} /* end of for (;argc > 0;) */

	return 1;
}


/* Add a kallsyms section if the kernel supports all symbols. */
static int add_kallsyms(struct obj_file *f,
			struct obj_section **module_kallsyms, int force_kallsyms)
{
	struct module_symbol *s;
	struct obj_file *f_kallsyms;
	struct obj_section *sec_kallsyms;
	size_t i;
	int l;
	const char *p, *pt_R;
	unsigned long start = 0, stop = 0;

	for (i = 0, s = ksyms; i < nksyms; ++i, ++s) {
		p = (char *)s->name;
		pt_R = strstr(p, "_R");
		if (pt_R)
			l = pt_R - p;
		else
			l = strlen(p);
		if (strncmp(p, "__start_" KALLSYMS_SEC_NAME, l) == 0)
			start = s->value;
		else if (strncmp(p, "__stop_" KALLSYMS_SEC_NAME, l) == 0)
			stop = s->value;
	}

	if (start >= stop && !force_kallsyms)
		return(0);

	/* The kernel contains all symbols, do the same for this module. */

	/* Add an empty kallsyms section to the module if necessary */
	for (i = 0; i < f->header.e_shnum; ++i) {
		if (strcmp(f->sections[i]->name, KALLSYMS_SEC_NAME) == 0) {
			*module_kallsyms = f->sections[i];
			break;
		}
	    }
	if (!*module_kallsyms)
		*module_kallsyms = obj_create_alloced_section(f, KALLSYMS_SEC_NAME, 0, 0, 0);

	/* Size and populate kallsyms */
	if (obj_kallsyms(f, &f_kallsyms))
		return(1);
	sec_kallsyms = f_kallsyms->sections[KALLSYMS_IDX];
	(*module_kallsyms)->header.sh_addralign = sec_kallsyms->header.sh_addralign;
	(*module_kallsyms)->header.sh_size = sec_kallsyms->header.sh_size;
	free((*module_kallsyms)->contents);
	(*module_kallsyms)->contents = sec_kallsyms->contents;
	sec_kallsyms->contents = NULL;
	obj_free(f_kallsyms);

	return 0;
}


/* Add an arch data section if the arch wants it. */
static int add_archdata(struct obj_file *f,
			struct obj_section **sec)
{
	size_t i;

	*sec = NULL;
	/* Add an empty archdata section to the module if necessary */
	for (i = 0; i < f->header.e_shnum; ++i) {
		if (strcmp(f->sections[i]->name, ARCHDATA_SEC_NAME) == 0) {
			*sec = f->sections[i];
			break;
		}
	    }
	if (!*sec)
		*sec = obj_create_alloced_section(f, ARCHDATA_SEC_NAME, 16, 0, 0);

	/* Size and populate archdata */
	if (arch_archdata(f, *sec))
		return(1);
	return 0;
}


static int init_module(const char *m_name, struct obj_file *f,
		       unsigned long m_size, const char *blob_name,
		       unsigned int noload, unsigned int flag_load_map)
{
	struct module *module;
	struct obj_section *sec;
	void *image;
	int ret = 0;
	tgt_long m_addr;

	sec = obj_find_section(f, ".this");
	module = (struct module *) sec->contents;
	m_addr = sec->header.sh_addr;

	module->size_of_struct = sizeof(*module);
	module->size = m_size;
	module->flags = flag_autoclean ? NEW_MOD_AUTOCLEAN : 0;

	sec = obj_find_section(f, "__ksymtab");
	if (sec && sec->header.sh_size) {
		module->syms = sec->header.sh_addr;
		module->nsyms = sec->header.sh_size / (2 * tgt_sizeof_char_p);
	}
	if (n_ext_modules_used) {
		sec = obj_find_section(f, ".kmodtab");
		module->deps = sec->header.sh_addr;
		module->ndeps = n_ext_modules_used;
	}
	module->init = obj_symbol_final_value(f, obj_find_symbol(f, "init_module"));
	module->cleanup = obj_symbol_final_value(f,
		obj_find_symbol(f, "cleanup_module"));

	sec = obj_find_section(f, "__ex_table");
	if (sec) {
		module->ex_table_start = sec->header.sh_addr;
		module->ex_table_end = sec->header.sh_addr + sec->header.sh_size;
	}
	sec = obj_find_section(f, ".text.init");
	if (sec) {
		module->runsize = sec->header.sh_addr - m_addr;
	}
	sec = obj_find_section(f, ".data.init");
	if (sec) {
		if (!module->runsize ||
		    module->runsize > sec->header.sh_addr - m_addr)
			module->runsize = sec->header.sh_addr - m_addr;
	}
	sec = obj_find_section(f, ARCHDATA_SEC_NAME);
	if (sec && sec->header.sh_size) {
		module->archdata_start = sec->header.sh_addr;
		module->archdata_end = module->archdata_start + sec->header.sh_size;
	}
	sec = obj_find_section(f, KALLSYMS_SEC_NAME);
	if (sec && sec->header.sh_size) {
		module->kallsyms_start = sec->header.sh_addr;
		module->kallsyms_end = module->kallsyms_start + sec->header.sh_size;
	}
	if (!arch_init_module(f, module))
		return 0;

	/*
	 * Whew!  All of the initialization is complete.
	 * Collect the final module image and give it to the kernel.
	 */
	image = xmalloc(m_size);
	obj_create_image(f, image);

	if (flag_load_map)
		print_load_map(f);

	if (blob_name) {
		int fd, l;
		fd = open(blob_name, O_WRONLY|O_CREAT|O_TRUNC, S_IRUSR|S_IWUSR|S_IRGRP|S_IROTH);
		if (fd < 0) {
			error("open %s failed %m", blob_name);
			ret = -1;
		}
		else {
			if ((l = write(fd, image, m_size)) != m_size) {
				error("write %s failed %m", blob_name);
				ret = -1;
			}
			close(fd);
		}
	}

	if (ret == 0 && !noload) {
		fflush(stdout);		/* Flush any debugging output */
		ret = sys_init_module(m_name, (struct module *) image);
		if (ret) {
			error("init_module: %m");
			lprintf("Hint: insmod errors can be caused by incorrect module parameters, "
				"including invalid IO or IRQ parameters.\n"
			        "      You may find more information in syslog or the output from dmesg");
		}
	}

	free(image);

	return ret == 0;
}

#ifdef COMPAT_2_0
static int old_init_module(const char *m_name, struct obj_file *f,
			   unsigned long m_size)
{
	char *image;
	struct old_mod_routines routines;
	struct old_symbol_table *symtab;
	int ret;
	int nsyms = 0, strsize = 0, total;

	/* Create the symbol table */
	/* Size things first... */
	if (flag_export) {
		int i;
		for (i = 0; i < HASH_BUCKETS; ++i) {
			struct obj_symbol *sym;

			for (sym = f->symtab[i]; sym; sym = sym->next)
				if (ELFW(ST_BIND) (sym->info) != STB_LOCAL &&
				    sym->secidx <= SHN_HIRESERVE) {
					sym->ksymidx = nsyms++;
					strsize += strlen(sym->name) + 1;
				}
		}
	}
	total = (sizeof(struct old_symbol_table) +
		 nsyms * sizeof(struct old_module_symbol) +
		 n_ext_modules_used * sizeof(struct old_module_ref) +
		 strsize);
	symtab = xmalloc(total);
	symtab->size = total;
	symtab->n_symbols = nsyms;
	symtab->n_refs = n_ext_modules_used;

	if (flag_export && nsyms) {
		struct old_module_symbol *ksym;
		char *str;
		int i;

		ksym = symtab->symbol;
		str = ((char *) ksym +
		       nsyms * sizeof(struct old_module_symbol) +
		       n_ext_modules_used * sizeof(struct old_module_ref));

		for (i = 0; i < HASH_BUCKETS; ++i) {
			struct obj_symbol *sym;
			for (sym = f->symtab[i]; sym; sym = sym->next)
				if (sym->ksymidx >= 0) {
					ksym->addr = obj_symbol_final_value(f, sym);
					ksym->name = (unsigned long) str - (unsigned long) symtab;

					str = stpcpy(str, sym->name) + 1;
					ksym++;
				}
		}
	}

	if (n_ext_modules_used) {
		struct old_module_ref *ref;
		int i;

		ref = (struct old_module_ref *)
		    ((char *) symtab->symbol + nsyms * sizeof(struct old_module_symbol));

		for (i = 0; i < n_module_stat; ++i) {
			if (module_stat[i].status /* used */) {
				ref++->module = module_stat[i].modstruct;
			}
		}
	}

	/* Fill in routines.  */

	routines.init = obj_symbol_final_value(f, obj_find_symbol(f, "init_module"));
	routines.cleanup = obj_symbol_final_value(f,
		obj_find_symbol(f, "cleanup_module"));

	/*
	 * Whew!  All of the initialization is complete.
	 * Collect the final module image and give it to the kernel.
	 */
	image = xmalloc(m_size);
	obj_create_image(f, image);

	/*
	 * image holds the complete relocated module,
	 * accounting correctly for mod_use_count.
	 * However the old module kernel support assume that it
	 * is receiving something which does not contain mod_use_count.
	 */
	ret = old_sys_init_module(m_name, image + sizeof(long),
				  (m_size - sizeof(long)) |
				  (flag_autoclean ? OLD_MOD_AUTOCLEAN : 0),
				  &routines,
				  symtab);
	if (ret)
		error("init_module: %m");

	free(image);
	free(symtab);

	return ret == 0;
}
#endif
/* end compat */
/************************************************************************/

/* Check that a module parameter has a reasonable definition */
static int check_module_parameter(struct obj_file *f, char *key, char *value, int *persist_flag)
{
	struct obj_symbol *sym;
	int min, max;
	char *p = value;

	sym = obj_find_symbol(f, key);
	if (sym == NULL) {
		/* FIXME: For 2.2 kernel compatibility, only issue warnings for
		 *        most error conditions.  Make these all errors in 2.5.
		 */
		lprintf("Warning: %s symbol for parameter %s not found", error_file, key);
		++warnings;
		return(1);
	}

	if (isdigit(*p)) {
		min = strtoul(p, &p, 10);
		if (*p == '-')
			max = strtoul(p + 1, &p, 10);
		else
			max = min;
	} else
		min = max = 1;

	if (max < min) {
		lprintf("Warning: %s parameter %s has max < min!", error_file, key);
		++warnings;
		return(1);
	}

	switch (*p) {
	case 'c':
		if (!isdigit(p[1])) {
			lprintf("%s parameter %s has no size after 'c'!", error_file, key);
			++warnings;
			return(1);
		}
		while (isdigit(p[1]))
			++p;	/* swallow c array size */
		break;
	case 'b':	/* drop through */
	case 'h':	/* drop through */
	case 'i':	/* drop through */
	case 'l':	/* drop through */
	case 's':
		break;
	case '\0':
		lprintf("%s parameter %s has no format character!", error_file, key);
		++warnings;
		return(1);
	default:
		lprintf("%s parameter %s has unknown format character '%c'", error_file, key, *p);
		++warnings;
		return(1);
	}
	switch (*++p) {
	case 'p':
		if (*(p-1) == 's') {
			error("parameter %s is invalid persistent string", key);
			return(1);
		}
		*persist_flag = 1;
		break;
	case '\0':
		break;
	default:
		lprintf("%s parameter %s has unknown format modifier '%c'", error_file, key, *p);
		++warnings;
		return(1);
	}
	return(0);
}

/* Check that all module parameters have reasonable definitions */
static void check_module_parameters(struct obj_file *f, int *persist_flag)
{
	struct obj_section *sec;
	char *ptr, *value, *n, *endptr;
	int namelen, err = 0;

	sec = obj_find_section(f, ".modinfo");
	if (sec == NULL) {
		/* module does not support typed parameters */
		return;
	}

	ptr = sec->contents;
	endptr = ptr + sec->header.sh_size;
	while (ptr < endptr && !err) {
		value = strchr(ptr, '=');
		n = strchr(ptr, '\0');
		if (value) {
			namelen = value - ptr;
			if (namelen >= 5 && strncmp(ptr, "parm_", 5) == 0
			    && !(namelen > 10 && strncmp(ptr, "parm_desc_", 10) == 0)) {
				char *pname = xmalloc(namelen + 1);
				strncpy(pname, ptr + 5, namelen - 5);
				pname[namelen - 5] = '\0';
				err = check_module_parameter(f, pname, value+1, persist_flag);
				free(pname);
			}
		} else {
			if (n - ptr >= 5 && strncmp(ptr, "parm_", 5) == 0) {
				error("parameter %s found with no value", ptr);
				err = 1;
			}
		}
		ptr = n + 1;
	}

	if (err)
		*persist_flag = 0;
	return;
}

static void set_tainted(struct obj_file *f, int fd, int kernel_has_tainted,
			int noload, int taint,
			const char *text1, const char *text2)
{
	char buf[80];
	int oldval;
	static int first = 1;
	if (fd < 0 && !kernel_has_tainted)
		return;		/* New modutils on old kernel */
	lprintf("Warning: loading %s will taint the kernel: %s%s",
			f->filename, text1, text2);
	++warnings;
	if (first) {
		lprintf("  See %s for information about tainted modules", TAINT_URL);
		first = 0;
	}
	if (fd >= 0 && !noload) {
		read(fd, buf, sizeof(buf)-1);
		buf[sizeof(buf)-1] = '\0';
		oldval = strtoul(buf, NULL, 10);
		sprintf(buf, "%d\n", oldval | taint);
		write(fd, buf, strlen(buf));
	}
}

/* Check if loading this module will taint the kernel. */
static void check_tainted_module(struct obj_file *f, int noload)
{
	static const char tainted_file[] = TAINT_FILENAME;
	int fd, kernel_has_tainted;
	const char *ptr;

	if ((fd = open(tainted_file, O_RDWR)) < 0) {
		if (errno == ENOENT)
			kernel_has_tainted = 0;
		else if (errno == EACCES)
			kernel_has_tainted = 1;
		else {
			perror(tainted_file);
			kernel_has_tainted = 0;
		}
	}
	else
		kernel_has_tainted = 1;

	switch (obj_gpl_license(f, &ptr)) {
	case 0:
		break;
	case 1:
		set_tainted(f, fd, kernel_has_tainted, noload, TAINT_PROPRIETORY_MODULE, "no license", "");
		break;
	case 2:
		/* The module has a non-GPL license so we pretend that the
		 * kernel always has a taint flag to get a warning even on
		 * kernels without the proc flag.
		 */
		set_tainted(f, fd, 1, noload, TAINT_PROPRIETORY_MODULE, "non-GPL license - ", ptr);
		break;
	default:
		set_tainted(f, fd, 1, noload, TAINT_PROPRIETORY_MODULE, "Unexpected return from obj_gpl_license", "");
		break;
	}

	if (flag_force_load)
		set_tainted(f, fd, 1, noload, TAINT_FORCED_MODULE, "forced load", "");
	if (fd >= 0)
		close(fd);
}

/* For common 3264 code, only compile the usage message once, in the 64 bit version */
#if defined(COMMON_3264) && defined(ONLY_32)
extern void insmod_usage(void);		/* Use the copy in the 64 bit version */
#else	/* Common 64 bit version or any non common code - compile usage routine */
void insmod_usage(void)
{
	fputs("Usage:\n"
	      "insmod [-fhkLmnpqrsSvVxXyY] [-e persist_name] [-o module_name] [-O blob_name] [-P prefix] module [ symbol=value ... ]\n"
	      "\n"
	      "  module                Name of a loadable kernel module ('.o' can be omitted)\n"
	      "  -f, --force           Force loading under wrong kernel version\n"
	      "  -h, --help            Print this message\n"
	      "  -k, --autoclean       Make module autoclean-able\n"
	      "  -L, --lock            Prevent simultaneous loads of the same module\n"
	      "  -m, --map             Generate load map (so crashes can be traced)\n"
	      "  -n, --noload          Don't load, just show\n"
	      "  -p, --probe           Probe mode; check if the module matches the kernel\n"
	      "  -q, --quiet           Don't print unresolved symbols\n"
	      "  -r, --root            Allow root to load modules not owned by root\n"
	      "  -s, --syslog          Report errors via syslog\n"
	      "  -S, --kallsyms        Force kallsyms on module\n"
	      "  -v, --verbose         Verbose output\n"
	      "  -V, --version         Show version\n"
	      "  -x, --noexport        Do not export externs\n"
	      "  -X, --export          Do export externs (default)\n"
	      "  -y, --noksymoops      Do not add ksymoops symbols\n"
	      "  -Y, --ksymoops        Do add ksymoops symbols (default)\n"
	      "  -e persist_name\n"
	      "      --persist=persist_name Filename to hold any persistent data from the module\n"
	      "  -o NAME, --name=NAME  Set internal module name to NAME\n"
	      "  -O NAME, --blob=NAME  Save the object as a binary blob in NAME\n"
	      "  -P PREFIX\n"
	      "      --prefix=PREFIX   Prefix for kernel or module symbols\n"
	      ,stderr);
	exit(1);
}
#endif	/* defined(COMMON_3264) && defined(ONLY_32) */

#if defined(COMMON_3264) && defined(ONLY_32)
#define INSMOD_MAIN insmod_main_32	/* 32 bit version */
#elif defined(COMMON_3264) && defined(ONLY_64)
#define INSMOD_MAIN insmod_main_64	/* 64 bit version */
#else
#define INSMOD_MAIN insmod_main		/* Not common code */
#endif

int INSMOD_MAIN(int argc, char **argv)
{
	int k_version;
	int k_crcs;
	char k_strversion[STRVERSIONLEN];
	struct option long_opts[] = {
		{"force", 0, 0, 'f'},
		{"help", 0, 0, 'h'},
		{"autoclean", 0, 0, 'k'},
		{"lock", 0, 0, 'L'},
		{"map", 0, 0, 'm'},
		{"noload", 0, 0, 'n'},
		{"probe", 0, 0, 'p'},
		{"poll", 0, 0, 'p'},	/* poll is deprecated, remove in 2.5 */
		{"quiet", 0, 0, 'q'},
		{"root", 0, 0, 'r'},
		{"syslog", 0, 0, 's'},
		{"kallsyms", 0, 0, 'S'},
		{"verbose", 0, 0, 'v'},
		{"version", 0, 0, 'V'},
		{"noexport", 0, 0, 'x'},
		{"export", 0, 0, 'X'},
		{"noksymoops", 0, 0, 'y'},
		{"ksymoops", 0, 0, 'Y'},
		{"persist", 1, 0, 'e'},
		{"name", 1, 0, 'o'},
		{"blob", 1, 0, 'O'},
		{"prefix", 1, 0, 'P'},
		{0, 0, 0, 0}
	};
	char *m_name = NULL;
	char *blob_name = NULL;		/* Save object as binary blob */
	int m_version;
	ElfW(Addr) m_addr;
	unsigned long m_size;
	int m_crcs;
	char m_strversion[STRVERSIONLEN];
	char *filename;
	char *persist_name = NULL;	/* filename to hold any persistent data */
	int fp;
	struct obj_file *f;
	struct obj_section *kallsyms = NULL, *archdata = NULL;
	int o;
	int noload = 0;
	int dolock = 1; /*Note: was: 0; */
	int quiet = 0;
	int exit_status = 1;
	int force_kallsyms = 0;
	int persist_parms = 0;	/* does module have persistent parms? */
	int i;
	int gpl;

	error_file = "insmod";

	/* To handle repeated calls from combined modprobe */
	errors = optind = 0;

	/* Process the command line.  */
	while ((o = getopt_long(argc, argv, "fhkLmnpqrsSvVxXyYe:o:O:P:R:",
				&long_opts[0], NULL)) != EOF)
		switch (o) {
		case 'f':	/* force loading */
			flag_force_load = 1;
			break;
		case 'h':       /* Print the usage message. */
			insmod_usage();
			break;
		case 'k':	/* module loaded by kerneld, auto-cleanable */
			flag_autoclean = 1;
			break;
		case 'L':	/* protect against recursion.  */
			dolock = 1;
			break;
		case 'm':	/* generate load map */
			flag_load_map = 1;
			break;
		case 'n':	/* don't load, just check */
			noload = 1;
			break;
		case 'p':	/* silent probe mode */
			flag_silent_probe = 1;
			break;
		case 'q':	/* Don't print unresolved symbols */
			quiet = 1;
			break;
		case 'r':	/* allow root to load non-root modules */
			root_check_off = !root_check_off;
			break;
		case 's':	/* start syslog */
			setsyslog("insmod");
			break;
		case 'S':	/* Force kallsyms */
			force_kallsyms = 1;
			break;
		case 'v':	/* verbose output */
			flag_verbose = 1;
			break;
		case 'V':
			fputs("insmod version " MODUTILS_VERSION "\n", stderr);
			break;
		case 'x':	/* do not export externs */
			flag_export = 0;
			break;
		case 'X':	/* do export externs */
			flag_export = 1;
			break;
		case 'y':	/* do not define ksymoops symbols */
			flag_ksymoops = 0;
			break;
		case 'Y':	/* do define ksymoops symbols */
			flag_ksymoops = 1;
			break;

		case 'e':	/* persistent data filename */
			free(persist_name);
			persist_name = xstrdup(optarg);
			break;
		case 'o':	/* name the output module */
			m_name = optarg;
			break;
		case 'O':	/* save the output module object */
			blob_name = optarg;
			break;
		case 'P':	/* use prefix on crc */
			set_ncv_prefix(optarg);
			break;

		default:
			insmod_usage();
			break;
		}

	if (optind >= argc) {
		insmod_usage();
	}
	filename = argv[optind++];

	if (config_read(0, NULL, "", NULL) < 0) {
		error("Failed handle configuration");
	}

	if (persist_name && !*persist_name &&
	    (!persistdir || !*persistdir)) {
		free(persist_name);
		persist_name = NULL;
		if (flag_verbose) {
			lprintf("insmod: -e \"\" ignored, no persistdir");
			++warnings;
		}
	}

	if (m_name == NULL) {
		size_t len;
		char *p;

		if ((p = strrchr(filename, '/')) != NULL)
			p++;
		else
			p = filename;
		len = strlen(p);
		if (len > 2 && p[len - 2] == '.' && p[len - 1] == 'o')
			len -= 2;
		else if (len > 4 && p[len - 4] == '.' && p[len - 3] == 'm'
			 && p[len - 2] == 'o' && p[len - 1] == 'd')
			len -= 4;
#ifdef CONFIG_USE_ZLIB
		else if (len > 5 && !strcmp(p + len - 5, ".o.gz"))
			len -= 5;
#endif

		m_name = xmalloc(len + 1);
		memcpy(m_name, p, len);
		m_name[len] = '\0';
	}

	/* Locate the file to be loaded.  */
	if (!strchr(filename, '/') && !strchr(filename, '.')) {
		char *tmp = search_module_path(filename);
		if (tmp == NULL) {
			error("%s: no module by that name found", filename);
			return 1;
		}
		filename = tmp;
		lprintf("Using %s", filename);
	} else if (flag_verbose)
		lprintf("Using %s", filename);

	/* And open it.  */
	if ((fp = gzf_open(filename, O_RDONLY)) == -1) {
		error("%s: %m", filename);
		return 1;
	}
	/* Try to prevent multiple simultaneous loads.  */
	if (dolock)
		flock(fp, LOCK_EX);

	if (!get_kernel_info(K_SYMBOLS))
		goto out;

	/*
	 * Set the genksyms prefix if this is a versioned kernel
	 * and it's not already set.
	 */
	set_ncv_prefix(NULL);

	for (i = 0; !noload && i < n_module_stat; ++i) {
		if (strcmp(module_stat[i].name, m_name) == 0) {
			error("a module named %s already exists", m_name);
			goto out;
		}
	}

	error_file = filename;
	if ((f = obj_load(fp, ET_REL, filename)) == NULL)
		goto out;

	/* Version correspondence?  */
	k_version = get_kernel_version(k_strversion);
	m_version = get_module_version(f, m_strversion);
	if (m_version == -1) {
		error("couldn't find the kernel version the module was compiled for");
		goto out;
	}

	k_crcs = is_kernel_checksummed();
	m_crcs = is_module_checksummed(f);
	if ((m_crcs == 0 || k_crcs == 0) &&
	    strncmp(k_strversion, m_strversion, STRVERSIONLEN) != 0) {
		if (flag_force_load) {
			lprintf("Warning: kernel-module version mismatch\n"
			      "\t%s was compiled for kernel version %s\n"
				"\twhile this kernel is version %s",
				filename, m_strversion, k_strversion);
			++warnings;
		} else {
			if (!quiet)
				error("kernel-module version mismatch\n"
				      "\t%s was compiled for kernel version %s\n"
				      "\twhile this kernel is version %s.",
				      filename, m_strversion, k_strversion);
			goto out;
		}
	}
	if (m_crcs != k_crcs)
		obj_set_symbol_compare(f, ncv_strcmp, ncv_symbol_hash);

	/* Let the module know about the kernel symbols.  */
	gpl = obj_gpl_license(f, NULL) == 0;
	add_kernel_symbols(f, gpl);

#ifdef	ARCH_ppc64
	if (!ppc64_process_syms (f))
		goto out;
#endif

	/* Allocate common symbols, symbol tables, and string tables.
	 *
	 * The calls marked DEPMOD indicate the bits of code that depmod
	 * uses to do a pseudo relocation, ignoring undefined symbols.
	 * Any changes made to the relocation sequence here should be
	 * checked against depmod.
	 */
#ifdef COMPAT_2_0
	if (k_new_syscalls
	    ? !create_this_module(f, m_name)
	    : !old_create_mod_use_count(f))
		goto out;
#else
	if (!create_this_module(f, m_name))
		goto out;
#endif

	arch_create_got(f);     /* DEPMOD */
	if (!obj_check_undefineds(f, quiet)) {	/* DEPMOD, obj_clear_undefineds */
		if (!gpl && !quiet) {
			if (gplonly_seen)
				error("\n"
				      "Hint: You are trying to load a module without a GPL compatible license\n"
				      "      and it has unresolved symbols.  The module may be trying to access\n"
				      "      GPLONLY symbols but the problem is more likely to be a coding or\n"
				      "      user error.  Contact the module supplier for assistance, only they\n"
				      "      can help you.\n");
			else
				error("\n"
				      "Hint: You are trying to load a module without a GPL compatible license\n"
				      "      and it has unresolved symbols.  Contact the module supplier for\n"
				      "      assistance, only they can help you.\n");
		}
		goto out;
	}
	obj_allocate_commons(f);	/* DEPMOD */

	check_module_parameters(f, &persist_parms);
	check_tainted_module(f, noload);

	if (optind < argc) {
		if (!process_module_arguments(f, argc - optind, argv + optind, 1))
			goto out;
	}
	hide_special_symbols(f);

	if (persist_parms && persist_name && *persist_name) {
		f->persist = persist_name;
		persist_name = NULL;
	}

	if (persist_parms &&
	    persist_name && !*persist_name) {
		/* -e "".  This is ugly.  Take the filename, compare it against
		 * each of the module paths until we find a match on the start
		 * of the filename, assume the rest is the relative path.  Have
		 * to do it this way because modprobe uses absolute filenames
		 * for module names in modules.dep and the format of modules.dep
		 * does not allow for any backwards compatible changes, so there
		 * is nowhere to store the relative filename.  The only way this
		 * should fail to calculate a relative path is "insmod ./xxx", for
		 * that case the user has to specify -e filename.
		 */
		int j, l = strlen(filename);
		char *relative = NULL;
		char *p;
		for (i = 0; i < nmodpath; ++i) {
			p = modpath[i].path;
			j = strlen(p);
			while (j && p[j] == '/')
				--j;
			if (j < l && strncmp(filename, p, j) == 0 && filename[j] == '/') {
				while (filename[j] == '/')
					++j;
				relative = xstrdup(filename+j);
				break;
			}
		}
		if (relative) {
			i = strlen(relative);
			if (i > 3 && strcmp(relative+i-3, ".gz") == 0)
				relative[i -= 3] = '\0';
			if (i > 2 && strcmp(relative+i-2, ".o") == 0)
				relative[i -= 2] = '\0';
			else if (i > 4 && strcmp(relative+i-4, ".mod") == 0)
				relative[i -= 4] = '\0';
			f->persist = xmalloc(strlen(persistdir) + 1 + i + 1);
			strcpy(f->persist, persistdir);	/* safe, xmalloc */
			strcat(f->persist, "/");	/* safe, xmalloc */
			strcat(f->persist, relative);	/* safe, xmalloc */
			free(relative);
		}
		else
			error("Cannot calculate persistent filename");
	}

	if (f->persist && *(f->persist) != '/') {
		error("Persistent filenames must be absolute, ignoring '%s'",
			f->persist);
		free(f->persist);
		f->persist = NULL;
	}

	if (f->persist && !flag_ksymoops) {
		error("has persistent data but ksymoops symbols are not available");
		free(f->persist);
		f->persist = NULL;
	}

	if (f->persist && !k_new_syscalls) {
		error("has persistent data but the kernel is too old to support it");
		free(f->persist);
		f->persist = NULL;
	}

	if (persist_parms && flag_verbose) {
		if (f->persist)
			lprintf("Persist filename '%s'", f->persist);
		else
			lprintf("No persistent filename available");
	}

	if (f->persist) {
		FILE *fp = fopen(f->persist, "r");
		if (!fp) {
			if (flag_verbose)
				lprintf("Cannot open persist file '%s' %m", f->persist);
		}
		else {
			int pargc = 0;
			char *pargv[1000];	/* hard coded but big enough */
			char line[3000];	/* hard coded but big enough */
			char *p;
			while (fgets(line, sizeof(line), fp)) {
				p = strchr(line, '\n');
				if (!p) {
					error("Persistent data line is too long\n%s", line);
					break;
				}
				*p = '\0';
				p = line;
				while (isspace(*p))
					++p;
				if (!*p || *p == '#')
					continue;
				if (pargc == sizeof(pargv)/sizeof(pargv[0])) {
					error("More than %d persistent parameters", pargc);
					break;
				}
				pargv[pargc++] = xstrdup(p);
			}
			fclose(fp);
			if (!process_module_arguments(f, pargc, pargv, 0))
				goto out;
			while (pargc--)
				free(pargv[pargc]);
		}
	}

	if (flag_ksymoops)
		add_ksymoops_symbols(f, filename, m_name);

	if (k_new_syscalls)
		create_module_ksymtab(f);

	/* archdata based on relocatable addresses */
	if (add_archdata(f, &archdata))
		goto out;

	/* kallsyms based on relocatable addresses */
	if (add_kallsyms(f, &kallsyms, force_kallsyms))
		goto out;
	/**** No symbols or sections to be changed after kallsyms above ***/

	if (errors)
		goto out;

	/* If we were just checking, we made it.  */
	if (flag_silent_probe) {
		exit_status = 0;
		goto out;
	}
	/* Module has now finished growing; find its size and install it.  */
	m_size = obj_load_size(f);	/* DEPMOD */

	if (noload) {
		/* Don't bother actually touching the kernel.  */
		m_addr = 0x12340000;
	} else {
		errno = 0;
		m_addr = create_module(m_name, m_size);
#ifdef	ARCH_ppc64
		m_addr |= ppc64_module_base (f);
#endif
		switch (errno) {
		case 0:
			break;
		case EEXIST:
			if (dolock) {
				/*
				 * Assume that we were just invoked
				 * simultaneous with another insmod
				 * and return success.
				 */
				exit_status = 0;
				goto out;
			}
			error("a module named %s already exists", m_name);
			goto out;
		case ENOMEM:
			error("can't allocate kernel memory for module; needed %lu bytes",
			      m_size);
			goto out;
		default:
			error("create_module: %m");
			goto out;
		}
	}

	/* module is already built, complete with ksymoops symbols for the
	 * persistent filename.  If the kernel does not support persistent data
	 * then give an error but continue.  It is too difficult to clean up at
	 * this stage and this error will only occur on backported modules.
	 * rmmod will also get an error so warn the user now.
	 */
	if (f->persist && !noload) {
		struct {
			struct module m;
			int data;
		} test_read;
		memset(&test_read, 0, sizeof(test_read));
		test_read.m.size_of_struct = -sizeof(test_read.m);      /* -ve size => read, not write */
		test_read.m.read_start = m_addr + sizeof(struct module);
		test_read.m.read_end = test_read.m.read_start + sizeof(test_read.data);
		if (sys_init_module(m_name, (struct module *) &test_read)) {
			int old_errors = errors;
			error("has persistent data but the kernel is too old to support it."
				"  Expect errors during rmmod as well");
			errors = old_errors;
		}
	}

	if (!obj_relocate(f, m_addr)) {	/* DEPMOD */
		if (!noload)
			delete_module(m_name);
		goto out;
	}

	/* Do archdata again, this time we have the final addresses */
	if (add_archdata(f, &archdata))
		goto out;

	/* Do kallsyms again, this time we have the final addresses */
	if (add_kallsyms(f, &kallsyms, force_kallsyms))
		goto out;

#ifdef COMPAT_2_0
	if (k_new_syscalls)
		init_module(m_name, f, m_size, blob_name, noload, flag_load_map);
	else if (!noload)
		old_init_module(m_name, f, m_size);
#else
	init_module(m_name, f, m_size, blob_name, noload, flag_load_map);
#endif
	if (errors) {
		if (!noload)
			delete_module(m_name);
		goto out;
	}
	if (warnings && !noload)
		lprintf("Module %s loaded, with warnings", m_name);
	exit_status = 0;

      out:
	if (dolock)
		flock(fp, LOCK_UN);
	close(fp);
	if (!noload)
		snap_shot(NULL, 0);

	return exit_status;
}

/* For common 3264 code, add an overall insmod_main, in the 64 bit version. */
#if defined(COMMON_3264) && defined(ONLY_64)
int insmod_main(int argc, char **argv)
{
	if (arch64())
		return insmod_main_64(argc, argv);
	else
		return insmod_main_32(argc, argv);
}
#endif	/* defined(COMMON_3264) && defined(ONLY_64) */


/* For common 3264 code, only compile main in the 64 bit version. */
#if 1 || defined(COMMON_3264) && defined(ONLY_32)
/* Use the main in the 64 bit version */
#else
/* This mainline looks at the name it was invoked under, checks that the name
 * contains exactly one of the possible combined targets and invokes the
 * corresponding handler for that function.
 */
int main(int argc, char **argv)
{
	/* List of possible program names and the corresponding mainline routines */
	static struct { char *name; int (*handler)(int, char **); } mains[] =
		{
			{ "insmod", &insmod_main },
	#ifdef COMBINE_modprobe
			{ "modprobe", &modprobe_main },
	#endif
	#ifdef COMBINE_rmmod
			{ "rmmod", &rmmod_main },
	#endif
	#ifdef COMBINE_ksyms
			{ "ksyms", &ksyms_main },
	#endif
	#ifdef COMBINE_lsmod
			{ "lsmod", &lsmod_main },
	#endif
	#ifdef COMBINE_kallsyms
			{ "kallsyms", &kallsyms_main },
	#endif
		};
	#define MAINS_NO (sizeof(mains)/sizeof(mains[0]))
	static int mains_match;
	static int mains_which;

	char *p = strrchr(argv[0], '/');
	char error_id1[2048] = "The ";		/* Way oversized */
	char error_id2[2048] = "";		/* Way oversized */
	int i;

	p = p ? p + 1 : argv[0];

	for (i = 0; i < MAINS_NO; ++i) {
		if (i) {
			xstrcat(error_id1, "/", sizeof(error_id1));
			if (i == MAINS_NO-1)
				xstrcat(error_id2, " or ", sizeof(error_id2));
			else
				xstrcat(error_id2, ", ", sizeof(error_id2));
		}
		xstrcat(error_id1, mains[i].name, sizeof(error_id1));
		xstrcat(error_id2, mains[i].name, sizeof(error_id2));
		if (strstr(p, mains[i].name)) {
			++mains_match;
			mains_which = i;
		}
	}

	/* Finish the error identifiers */
	if (MAINS_NO != 1)
		xstrcat(error_id1, " combined", sizeof(error_id1));
	xstrcat(error_id1, " binary", sizeof(error_id1));

	if (mains_match == 0 && MAINS_NO == 1)
		++mains_match;		/* Not combined, any name will do */
	if (mains_match == 0) {
		error("%s does not have a recognisable name, "
		      "the name must contain one of %s.",
			error_id1, error_id2);
		return(1);
	}
	else if (mains_match > 1) {
		error("%s has an ambiguous name, it must contain %s%s.",
			error_id1, MAINS_NO == 1 ? "" : "exactly one of ", error_id2);
		return(1);
	}
	else
		return((mains[mains_which].handler)(argc, argv));
}
#endif	/* defined(COMMON_3264) && defined(ONLY_32) */
