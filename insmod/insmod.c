/* Insert a module into a running kernel.
   Copyright 1996, 1997 Linux International.

   New implementation contributed by Richard Henderson <rth@tamu.edu>
   Based on original work by Bjorn Eckwall <bj0rn@blox.se>

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

#ident "$Id: insmod.c,v 1.1 1999/12/14 12:38:12 snwint Exp $"

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
#include <sys/utsname.h>
#include <sys/stat.h>
#include <sys/file.h>

#include "module.h"
#include "obj.h"
#include "util.h"
#include "version.h"

#include "logger.h"

#define STRVERSIONLEN	32


/*======================================================================*/

int flag_force_load = 0;
int flag_autoclean = 0;
int flag_silent_poll = 0;
int flag_verbose = 0;
int flag_export = 1;
int flag_load_map = 0;

struct external_module
{
  const char *name;
  ElfW(Addr) addr;
  int used;
  size_t nsyms;
  struct new_module_symbol *syms;
};

struct new_module_symbol *ksyms;
size_t nksyms;

struct external_module *ext_modules;
int n_ext_modules;
int n_ext_modules_used;


/*======================================================================*/

/* Given a bare module name, poke through the module path to find the file.  */

static char *
search_module_path(char *base)
{
  static const char default_path[] =
    ".:"
    "/linux/modules:"
    "/lib/modules/%s/fs:"
    "/lib/modules/%s/net:"
    "/lib/modules/%s/scsi:"
    "/lib/modules/%s/block:"
    "/lib/modules/%s/cdrom:"
    "/lib/modules/%s/ipv4:"
    "/lib/modules/%s/ipv6:"
    "/lib/modules/%s/sound:"
    "/lib/modules/%s/fc4:"
    "/lib/modules/%s/video:"
    "/lib/modules/%s/misc:"
    "/lib/modules/default/fs:"
    "/lib/modules/default/net:"
    "/lib/modules/default/scsi:"
    "/lib/modules/default/block:"
    "/lib/modules/default/cdrom:"
    "/lib/modules/default/ipv4:"
    "/lib/modules/default/ipv6:"
    "/lib/modules/default/sound:"
    "/lib/modules/default/fc4:"
    "/lib/modules/default/video:"
    "/lib/modules/default/misc:"
    "/lib/modules/fs:"
    "/lib/modules/net:"
    "/lib/modules/scsi:"
    "/lib/modules/block:"
    "/lib/modules/cdrom:"
    "/lib/modules/ipv4:"
    "/lib/modules/ipv6:"
    "/lib/modules/sound:"
    "/lib/modules/fc4:"
    "/lib/modules/video:"
    "/lib/modules/misc";

  char *path, *p, *filename;
  struct utsname uts_info;
  size_t len;

  if ((path = getenv("MODPATH")) == NULL)
    path = (char *)default_path;

  /* Make a copy so's we can mung it with strtok.  */
  len = strlen(path)+1;
  p = alloca(len);
  path = memcpy(p, path, len);

  uname(&uts_info);
  filename = xmalloc(PATH_MAX);

  for (p = strtok(path, ":"); p != NULL ; p = strtok(NULL, ":"))
    {
      struct stat sb;

      len = snprintf(filename, PATH_MAX, p, uts_info.release);
      len += snprintf(filename+len, PATH_MAX-len, "/%s", base);

      if (stat(filename, &sb) == 0 && S_ISREG(sb.st_mode))
	return filename;

      snprintf(filename+len, PATH_MAX-len, ".o");

      if (stat(filename, &sb) == 0 && S_ISREG(sb.st_mode))
	return filename;
    }

  free(filename);
  return NULL;
}

/* Get the kernel version in the canonical integer form.  */

static int
get_kernel_version(char str[STRVERSIONLEN])
{
  struct utsname uts_info;
  char *p, *q;
  int a, b, c;

  if (uname(&uts_info) < 0)
    return -1;
  strncpy(str, uts_info.release, STRVERSIONLEN);
  p = uts_info.release;

  a = strtoul(p, &p, 10);
  if (*p != '.')
    return -1;
  b = strtoul(p+1, &p, 10);
  if (*p != '.')
    return -1;
  c = strtoul(p+1, &q, 10);
  if (p+1 == q)
    return -1;

  return a << 16 | b << 8 | c;
}

/* String comparison for non-co-versioned kernel and module.  */

static int
ncv_strcmp(const char *a, const char *b)
{
  size_t alen = strlen(a), blen = strlen(b);

  if (blen == alen + 10 && b[alen] == '_' && b[alen+1] == 'R')
    return strncmp(a, b, alen);
  else if (alen == blen + 10 && a[blen] == '_' && a[blen+1] == 'R')
    return strncmp(a, b, blen);
  else
    return strcmp(a, b);
}

/* String hashing for non-co-versioned kernel and module.  Here
   we are simply forced to drop the crc from the hash.  */

static unsigned long
ncv_symbol_hash(const char *str)
{
  size_t len = strlen(str);
  if (len > 10 && str[len-10] == '_' && str[len-9] == 'R')
    len -= 10;
  return obj_elf_hash_n(str, len);
}

/* Conditionally add the symbols from the given symbol set to the
   new module.  */

static int
add_symbols_from(struct obj_file *f, int idx,
		 struct new_module_symbol *syms, size_t nsyms)
{
  struct new_module_symbol *s;
  size_t i;
  int used = 0;

  for (i = 0, s = syms; i < nsyms; ++i, ++s)
    {
      /* Only add symbols that are already marked external.  If we
	 override locals we may cause problems for argument initialization.
	 We will also create a false dependency on the module.  */

      struct obj_symbol *sym;
      sym = obj_find_symbol(f, (char *)s->name);
      if (sym && ! ELFW(ST_BIND)(sym->info) == STB_LOCAL)
	{
	  sym = obj_add_symbol(f, (char *)s->name, -1,
			       ELFW(ST_INFO)(STB_GLOBAL, STT_NOTYPE),
			       idx, s->value, 0);

	  /* Did our symbol just get installed?  If so, mark the
	     module as "used".  */

	  if (sym->secidx == idx)
	    used = 1;
	}
    }

  return used;
}

static void
add_kernel_symbols(struct obj_file *f)
{
  struct external_module *m;
  size_t i, nused = 0;

  /* Add module symbols first.  */

  for (i = 0, m = ext_modules; i < n_ext_modules; ++i, ++m)
    if (m->nsyms && add_symbols_from(f, SHN_HIRESERVE+2+i, m->syms, m->nsyms))
      m->used = 1, ++nused;

  n_ext_modules_used = nused;

  /* And finally the symbols from the kernel proper.  */

  if (nksyms)
    add_symbols_from(f, SHN_HIRESERVE+1, ksyms, nksyms);
}

static void
hide_special_symbols(struct obj_file *f)
{
  static const char * const specials[] =
  {
    "cleanup_module",
    "init_module",
    "kernel_version",
    NULL
  };

  struct obj_symbol *sym;
  const char * const *p;

  for (p = specials; *p ; ++p)
    if ((sym = obj_find_symbol(f, *p)) != NULL)
      sym->info = ELFW(ST_INFO)(STB_LOCAL, ELFW(ST_TYPE)(sym->info));
}

static void
print_load_map(struct obj_file *f)
{
  int
  load_map_cmp(const void *a, const void *b)
  {
    struct obj_symbol **as = (struct obj_symbol **)a;
    struct obj_symbol **bs = (struct obj_symbol **)b;
    unsigned long aa = obj_symbol_final_value(f, *as);
    unsigned long ba = obj_symbol_final_value(f, *bs);
    return aa < ba ? -1 : aa > ba ? 1 : 0;
  }

  int i, nsyms, *loaded;
  struct obj_symbol *sym;
  struct obj_symbol **all, **p;
  struct obj_section *sec;

  /* Report on the section layout.  */

  lprintf("Sections:       Size      %-*s  Align",
	  (int)(2*sizeof(void*)), "Address");
  for (sec = f->load_order; sec ; sec = sec->load_next)
    {
      int a;
      unsigned long tmp;
      for (a = -1, tmp = sec->header.sh_addralign; tmp ; ++a)
	tmp >>= 1;
      if (a == -1)
	a = 0;

      lprintf("%-16s%08lx  %0*lx  2**%d", sec->name, sec->header.sh_size,
	      (int)(2*sizeof(void*)), sec->header.sh_addr, a);
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
  for (p = all; p < all+nsyms; ++p)
    {
      char type = '?';
      unsigned long value;

      sym = *p;
      if (sym->secidx == SHN_ABS)
	{
	  type = 'A';
	  value = sym->value;
	}
      else if (sym->secidx == SHN_UNDEF)
	{
	  type = 'U';
	  value = 0;
	}
      else
	{
	  struct obj_section *sec = f->sections[sym->secidx];

	  if (sec->header.sh_type == SHT_NOBITS)
	    type = 'B';
	  else if (sec->header.sh_flags & SHF_ALLOC)
	    {
	      if (sec->header.sh_flags & SHF_EXECINSTR)
		type = 'T';
	      else if (sec->header.sh_flags & SHF_WRITE)
		type = 'D';
	      else
		type = 'R';
	    }

	  value = sym->value + sec->header.sh_addr;
	}

      if (ELFW(ST_BIND)(sym->info) == STB_LOCAL)
	type = tolower(type);

      lprintf("%0*lx %c %s", (int)(2*sizeof(void*)), value,
	      type, sym->name);
    }
}

/*======================================================================*/
/* Functions relating to module loading in pre 2.1 kernels.  */

/* Fetch all the symbols and divvy them up as appropriate for the modules.  */

static int
old_get_kernel_symbols(void)
{
  struct old_kernel_sym *ks, *k;
  struct new_module_symbol *s;
  struct external_module *mod;
  int nks, nms, nmod, i;

  nks = get_kernel_syms(NULL);
  if (nks < 0)
    {
      error("get_kernel_syms: %m");
      return 0;
    }

  ks = k = xmalloc(nks * sizeof(*ks));

  if (get_kernel_syms(ks) != nks)
    {
      error("inconsistency with get_kernel_syms -- is someone else "
	    "playing with modules?");
      free(ks);
      return 0;
    }

  /* Collect the module information.  */

  mod = NULL;
  nmod = -1;

  while (k->name[0] == '#' && k->name[1])
    {
      struct old_kernel_sym *k2;
      struct new_module_symbol *s;

      /* Find out how many symbols this module has.  */
      for (k2 = k+1; k2->name[0] != '#'; ++k2)
	continue;
      nms = k2 - k - 1;

      mod = xrealloc(mod, (++nmod+1) * sizeof(*mod));
      mod[nmod].name = k->name+1;
      mod[nmod].addr = k->value;
      mod[nmod].used = 0;
      mod[nmod].nsyms = nms;
      mod[nmod].syms = s = (nms ? xmalloc(nms * sizeof(*s)) : NULL);

      for (i = 0, ++k; i < nms; ++i, ++s, ++k)
	{
	  s->name = (unsigned long)k->name;
	  s->value = k->value;
	}

      k = k2;
    }

  ext_modules = mod;
  n_ext_modules = nmod+1;

  /* Now collect the symbols for the kernel proper.  */

  if (k->name[0] == '#')
    ++k;

  nksyms = nms = nks - (k - ks);
  ksyms = s = (nms ? xmalloc(nms * sizeof(*s)) : NULL);

  for (i = 0; i < nms; ++i, ++s, ++k)
    {
      s->name = (unsigned long)k->name;
      s->value = k->value;
    }

  return 1;
}

/* Return the kernel symbol checksum version, or zero if not used.  */

static int
old_is_kernel_checksummed(void)
{
  /* Using_Versions is the first symbol.  */
  if (nksyms > 0 && strcmp((char *)ksyms[0].name, "Using_Versions") == 0)
    return ksyms[0].value;
  else
    return 0;
}

/* Get the module's kernel version in the canonical integer form.  */

static int
old_get_module_version(struct obj_file *f, char str[STRVERSIONLEN])
{
  struct obj_symbol *sym;
  char *p, *q;
  int a, b, c;

  sym = obj_find_symbol(f, "kernel_version");
  if (sym == NULL)
    return -1;
  p = f->sections[sym->secidx]->contents + sym->value;
  strncpy(str, p, STRVERSIONLEN);

  a = strtoul(p, &p, 10);
  if (*p != '.')
    return -1;
  b = strtoul(p+1, &p, 10);
  if (*p != '.')
    return -1;
  c = strtoul(p+1, &q, 10);
  if (p+1 == q)
    return -1;

  return a << 16 | b << 8 | c;
}

static int
old_is_module_checksummed(struct obj_file *f)
{
  return obj_find_symbol(f, "Using_Versions") != NULL;
}

static int
old_create_mod_use_count(struct obj_file *f)
{
  struct obj_section *sec;

  sec = obj_create_alloced_section_first(f, ".moduse", sizeof(long),
					 sizeof(long));

  obj_add_symbol(f, "mod_use_count_", -1, ELFW(ST_INFO)(STB_LOCAL, STT_OBJECT),
		 sec->idx, 0, sizeof(long));

  return 1;
}

static int
old_process_module_arguments(struct obj_file *f, int argc, char **argv)
{
  while (argc > 0)
    {
      char *p, *q;
      struct obj_symbol *sym;
      int *loc;

      p = *argv;
      if ((q = strchr(p, '=')) == NULL)
	continue;
      *q++ = '\0';

      sym = obj_find_symbol(f, p);

      /* Also check that the parameter was not resolved from the kernel.  */
      if (sym == NULL || sym->secidx > SHN_HIRESERVE)
	{
	  error("symbol for parameter %s not found", p);
	  return 0;
	}

      loc = (int *)(f->sections[sym->secidx]->contents + sym->value);

      /* Do C quoting if we begin with a ".  */
      if (*q == '"')
	{
	  char *r, *str;

	  str = alloca(strlen(q));
	  for (r = str, q++; *q != '"'; ++q, ++r)
	    {
	      if (*q == '\0')
		{
		  error("improperly terminated string argument for %s", p);
		  return 0;
		}
	      else if (*q == '\\')
		switch (*++q)
		  {
		  case 'a': *r = '\a'; break;
		  case 'b': *r = '\b'; break;
		  case 'e': *r = '\033'; break;
		  case 'f': *r = '\f'; break;
		  case 'n': *r = '\n'; break;
		  case 'r': *r = '\r'; break;
		  case 't': *r = '\t'; break;

		  case '0': case '1': case '2': case '3':
		  case '4': case '5': case '6': case '7':
		    {
		      int c = *q - '0';
		      if (q[1] >= '0' && q[1] <= '7')
			{
			  c = (c * 8) + *++q - '0';
			  if (q[1] >= '0' && q[1] <= '7')
			    c = (c * 8) + *++q - '0';
			}
		      *r = c;
		    }
		  break;

		  default:
		    *r = *q;
		    break;
		  }
	      else
		*r = *q;
	    }
	  *r = '\0';
	  obj_string_patch(f, sym->secidx, sym->value, str);
	}
      else if (*q >= '0' && *q <= '9')
	{
	  do
	    *loc++ = strtoul(q, &q, 0);
	  while (*q++ == ',');
	}
      else
	{
	  char *contents = f->sections[sym->secidx]->contents;
	  char *loc = contents + sym->value;
	  char *r;	/* To search for commas */

	  /* Break the string with comas */
	  while((r = strchr(q, ',')) != (char *) NULL)
	    {
	      *r++ = '\0';
	      obj_string_patch(f, sym->secidx, loc-contents, q);
	      loc += sizeof(char*);
	      q = r;
	    }

	  /* last part */
	  obj_string_patch(f, sym->secidx, loc-contents, q);
	}

      argc--, argv++;
    }

  return 1;
}

static int
old_init_module(const char *m_name, struct obj_file *f, unsigned long m_size)
{
  char *image;
  struct old_mod_routines routines;
  struct old_symbol_table *symtab;
  int ret;

  /* Create the symbol table */
  {
    int nsyms = 0, strsize = 0, total;

    /* Size things first... */
    if (flag_export)
      {
	int i;
	for (i = 0; i < HASH_BUCKETS; ++i)
	  {
	    struct obj_symbol *sym;
	    for (sym = f->symtab[i]; sym; sym = sym->next)
	      if (ELFW(ST_BIND)(sym->info) != STB_LOCAL
		  && sym->secidx <= SHN_HIRESERVE)
		{
		  sym->ksymidx = nsyms++;
		  strsize += strlen(sym->name)+1;
		}
	  }
      }

    total = (sizeof(struct old_symbol_table)
	     + nsyms * sizeof(struct old_module_symbol)
	     + n_ext_modules_used * sizeof(struct old_module_ref)
	     + strsize);
    symtab = xmalloc(total);
    symtab->size = total;
    symtab->n_symbols = nsyms;
    symtab->n_refs = n_ext_modules_used;

    if (flag_export && nsyms)
      {
	struct old_module_symbol *ksym;
	char *str;
	int i;

	ksym = symtab->symbol;
	str = ((char *)ksym
	       + nsyms * sizeof(struct old_module_symbol)
	       + n_ext_modules_used * sizeof(struct old_module_ref));

	for (i = 0; i < HASH_BUCKETS; ++i)
	  {
	    struct obj_symbol *sym;
	    for (sym = f->symtab[i]; sym; sym = sym->next)
	      if (sym->ksymidx >= 0)
		{
		  ksym->addr = obj_symbol_final_value(f, sym);
		  ksym->name = (unsigned long)str - (unsigned long)symtab;

		  str = stpcpy(str, sym->name)+1;
		  ksym++;
		}
	  }
      }

    if (n_ext_modules_used)
      {
	struct old_module_ref *ref;
	int i;

	ref = (struct old_module_ref *)
	  ((char *)symtab->symbol + nsyms * sizeof(struct old_module_symbol));

	for (i = 0; i < n_ext_modules; ++i)
	  if (ext_modules[i].used)
	    ref++->module = ext_modules[i].addr;
      }
  }

  /* Fill in routines.  */

  routines.init = obj_symbol_final_value(f, obj_find_symbol(f, "init_module"));
  routines.cleanup
    = obj_symbol_final_value(f, obj_find_symbol(f, "cleanup_module"));

  /* Whew!  All of the initialization is complete.  Collect the final
     module image and give it to the kernel.  */

  image = xmalloc(m_size);
  obj_create_image(f, image);

  /* image holds the complete relocated module, accounting correctly for
     mod_use_count.  However the old module kernel support assume that
     it is receiving something which does not contain mod_use_count.  */
  ret = old_sys_init_module(m_name, image+sizeof(long),
			    m_size | (flag_autoclean ? OLD_MOD_AUTOCLEAN : 0),
			    &routines, symtab);
  if (ret)
    error("init_module: %m");

  free(image);
  free(symtab);

  return ret == 0;
}

/*======================================================================*/
/* Functions relating to module loading after 2.1.18.  */

/* Fetch the loaded modules, and all currently exported symbols.  */

static int
new_get_kernel_symbols(void)
{
  char *module_names, *mn;
  struct external_module *modules, *m;
  struct new_module_symbol *syms, *s;
  size_t ret, bufsize, nmod, nsyms, i, j;

  /* Collect the loaded modules.  */

  module_names = xmalloc(bufsize = 256);
retry_modules_load:
  if (query_module(NULL, QM_MODULES, module_names, bufsize, &ret))
    {
      if (errno == ENOSPC)
	{
	  module_names = xrealloc(module_names, bufsize = ret);
	  goto retry_modules_load;
	}
      error("QM_MODULES: %m\n");
      return 0;
    }

  n_ext_modules = nmod = ret;
  ext_modules = modules = xmalloc(nmod * sizeof(*modules));
  memset(modules, 0, nmod * sizeof(*modules));

  /* Collect the modules' symbols.  */

  for (i = 0, mn = module_names, m = modules;
       i < nmod;
       ++i, ++m, mn += strlen(mn)+1)
    {
      struct new_module_info info;

      if (query_module(mn, QM_INFO, &info, sizeof(info), &ret))
	{
	  if (errno == ENOENT)
	    /* The module was removed out from underneath us.  */
	    continue;
	  error("module %s: QM_INFO: %m", mn);
	  return 0;
	}

      syms = xmalloc(bufsize = 1024);
    retry_mod_sym_load:
      if (query_module(mn, QM_SYMBOLS, syms, bufsize, &ret))
	{
	  switch (errno)
	    {
	    case ENOSPC:
	      syms = xrealloc(syms, bufsize = ret);
	      goto retry_mod_sym_load;
	    case ENOENT:
	      /* The module was removed out from underneath us.  */
	      continue;
	    default:
	      error("module %s: QM_SYMBOLS: %m", mn);
	      return 0;
	    }
	}
      nsyms = ret;

      m->name = mn;
      m->addr = info.addr;
      m->nsyms = nsyms;
      m->syms = syms;

      for (j = 0, s = syms; j < nsyms; ++j, ++s)
	s->name += (unsigned long)syms;
    }

  /* Collect the kernel's symbols.  */

  syms = xmalloc(bufsize = 16*1024);
retry_kern_sym_load:
  if (query_module(NULL, QM_SYMBOLS, syms, bufsize, &ret))
    {
      if (errno == ENOSPC)
	{
	  syms = xrealloc(syms, bufsize = ret);
	  goto retry_kern_sym_load;
	}
      error("kernel: QM_SYMBOLS: %m");
      return 0;
    }
  nksyms = nsyms = ret;
  ksyms = syms;

  for (j = 0, s = syms; j < nsyms; ++j, ++s)
    s->name += (unsigned long)syms;

  return 1;
}

/* Return the kernel symbol checksum version, or zero if not used.  */

static int
new_is_kernel_checksummed(void)
{
  struct new_module_symbol *s;
  size_t i;

  /* Using_Versions is not the first symbol, but it should be in there.  */

  for (i = 0, s = ksyms; i < nksyms; ++i, ++s)
    if (strcmp((char *)s->name, "Using_Versions") == 0)
      return s->value;

  return 0;
}

static char *
get_modinfo_value(struct obj_file *f, const char *key)
{
  struct obj_section *sec;
  char *p, *v, *n, *ep;
  size_t klen = strlen(key);

  sec = obj_find_section(f, ".modinfo");
  if (sec == NULL)
    return NULL;

  p = sec->contents;
  ep = p + sec->header.sh_size;
  while (p < ep)
    {
      v = strchr(p, '=');
      n = strchr(p, '\0');
      if (v)
	{
	  if (v-p == klen && strncmp(p, key, klen) == 0)
	    return v+1;
	}
      else
	{
	  if (n-p == klen && strcmp(p, key) == 0)
	    return n;
	}
      p = n+1;
    }

  return NULL;
}

/* Get the module's kernel version in the canonical integer form.  */

static int
new_get_module_version(struct obj_file *f, char str[STRVERSIONLEN])
{
  char *p, *q;
  int a, b, c;

  p = get_modinfo_value(f, "kernel_version");
  if (p == NULL)
    return -1;
  strncpy(str, p, STRVERSIONLEN);

  a = strtoul(p, &p, 10);
  if (*p != '.')
    return -1;
  b = strtoul(p+1, &p, 10);
  if (*p != '.')
    return -1;
  c = strtoul(p+1, &q, 10);
  if (p+1 == q)
    return -1;

  return a << 16 | b << 8 | c;
}

static int
new_is_module_checksummed(struct obj_file *f)
{
  const char *p = get_modinfo_value(f, "using_checksums");
  if (p)
    return atoi(p);
  else
    return 0;
}

static int
new_process_module_arguments(struct obj_file *f, int argc, char **argv)
{
  while (argc > 0)
    {
      char *p, *q, *key;
      struct obj_symbol *sym;
      char *contents, *loc;
      int min, max, n;

      p = *argv;
      if ((q = strchr(p, '=')) == NULL)
	continue;

      key = alloca(q-p + 6);
      memcpy(key, "parm_", 5);
      memcpy(key+5, p, q-p);
      key[q-p+5] = 0;

      p = get_modinfo_value(f, key);
      key += 5;
      if (p == NULL)
	{
	  error("invalid parameter %s", key);
	  return 0;
	}

      sym = obj_find_symbol(f, key);

      /* Also check that the parameter was not resolved from the kernel.  */
      if (sym == NULL || sym->secidx > SHN_HIRESERVE)
	{
	  error("symbol for parameter %s not found", key);
	  return 0;
	}

      if (isdigit(*p))
	{
	  min = strtoul(p, &p, 10);
	  if (*p == '-')
	    max = strtoul(p+1, &p, 10);
	  else
	    max = min;
	}
      else
	min = max = 1;

      contents = f->sections[sym->secidx]->contents;
      loc = contents + sym->value;
      n = (*++q != '\0');

      while (1)
	{
	  if((*p == 's') || (*p == 'c'))
	    {
	      char *str;

	      /* Do C quoting if we begin with a ", else slurp the lot.  */
	      if (*q == '"')
		{
		  char *r;

		  str = alloca(strlen(q));
		  for (r = str, q++; *q != '"'; ++q, ++r)
		    {
		      if (*q == '\0')
			{
			  error("improperly terminated string argument for %s",
				key);
			  return 0;
			}
		      else if (*q == '\\')
			switch (*++q)
			  {
			  case 'a': *r = '\a'; break;
			  case 'b': *r = '\b'; break;
			  case 'e': *r = '\033'; break;
			  case 'f': *r = '\f'; break;
			  case 'n': *r = '\n'; break;
			  case 'r': *r = '\r'; break;
			  case 't': *r = '\t'; break;

			  case '0': case '1': case '2': case '3':
			  case '4': case '5': case '6': case '7':
			    {
			      int c = *q - '0';
			      if (q[1] >= '0' && q[1] <= '7')
				{
				  c = (c * 8) + *++q - '0';
				  if (q[1] >= '0' && q[1] <= '7')
				    c = (c * 8) + *++q - '0';
				}
			      *r = c;
			    }
			  break;

			  default:
			    *r = *q;
			    break;
			  }
		      else
			*r = *q;
		    }
		  *r = '\0';
		  ++q;
		}
	      else
		{
		  char *r;

		  /* In this case, the string is not quoted. We will break
		     it using the coma (like for ints). If the user wants to
		     include comas in a string, he just has to quote it */

		  /* Search the next coma */
		  r = strchr(q, ',');

		  /* Found ? */
		  if(r != (char *) NULL)
		    {
		      /* Recopy the current field */
		      str = alloca(r - q + 1);
		      memcpy(str, q, r - q);

		      /* I don't know if it is usefull, as the previous case
		         doesn't null terminate the string ??? */
		      str[r - q] = '\0';

		      /* Keep next fields */
		      q = r;
		    }
		  else
		    {
		      /* last string */
		      str = q;
		      q = "";
		    }
		}

	      if (*p == 's')
		{
		  /* Normal string */
		  obj_string_patch(f, sym->secidx, loc-contents, str);
		  loc += tgt_sizeof_char_p;
		}
	      else
		{
		  /* Array of chars (in fact, matrix !) */
		  long	charssize;	/* size of each member */

		  /* Get the size of each member */
		  /* Probably we should do that outside the loop ? */
		  if(!isdigit(*(p + 1)))
		    {
		      error("parameter type 'c' for %s must be followed by"
			    " the maximum size", key);
		      return 0;
		    }
		  charssize = strtoul(p + 1, (char **) NULL, 10);

		  /* Check length */
		  if(strlen(str) >= charssize)
		    {
		      error("string too long for %s (max %ld)",
			    key, charssize - 1);
		      return 0;
		    }

		  /* Copy to location */
		  strcpy((char *) loc, str);
		  loc += charssize;
		}
	    }
	  else
	    {
	      long v = strtoul(q, &q, 0);
	      switch (*p)
		{
		case 'b':
		  *loc++ = v;
		  break;
		case 'h':
		  *(short *)loc = v;
		  loc += tgt_sizeof_short;
		  break;
		case 'i':
		  *(int *)loc = v;
		  loc += tgt_sizeof_int;
		  break;
		case 'l':
		  *(long *)loc = v;
		  loc += tgt_sizeof_long;
		  break;

		default:
		  error("unknown parameter type '%c' for %s",
			*p, key);
		  return 0;
		}
	    }

	retry_end_of_value:
	  switch (*q)
	    {
	    case '\0':
	      goto end_of_arg;

	    case ' ': case '\t': case '\n': case '\r':
	      ++q;
	      goto retry_end_of_value;

	    case ',':
	      if (++n > max)
		{
		  error("too many values for %s (max %d)", key, max);
		  return 0;
		}
	      ++q;
	      break;

	    default:
	      error("invalid argument syntax for %s", key);
	      return 0;
	    }
	}

    end_of_arg:
      if (n < min)
	{
	  error("too few values for %s (min %d)", key, min);
	  return 0;
	}

      argc--, argv++;
    }

  return 1;
}

static int
new_create_this_module(struct obj_file *f, const char *m_name)
{
  struct obj_section *sec;

  sec = obj_create_alloced_section_first(f, ".this", tgt_sizeof_long,
					 sizeof(struct new_module));
  memset(sec->contents, 0, sizeof(struct new_module));

  obj_add_symbol(f, "__this_module", -1, ELFW(ST_INFO)(STB_LOCAL, STT_OBJECT),
		 sec->idx, 0, sizeof(struct new_module));

  obj_string_patch(f, sec->idx, offsetof(struct new_module, name), m_name);

  return 1;
}

static int
new_create_module_ksymtab(struct obj_file *f)
{
  struct obj_section *sec;
  int i;

  /* We must always add the module references.  */

  if (n_ext_modules_used)
    {
      struct new_module_ref *dep;
      struct obj_symbol *tm;

      sec = obj_create_alloced_section(f, ".kmodtab", tgt_sizeof_void_p,
				       (sizeof(struct new_module_ref)
					* n_ext_modules_used));
      if (!sec)
	return 0;

      tm = obj_find_symbol(f, "__this_module");
      dep = (struct new_module_ref *)sec->contents;
      for (i = 0; i < n_ext_modules; ++i)
	if (ext_modules[i].used)
	  {
	    dep->dep = ext_modules[i].addr;
	    obj_symbol_patch(f, sec->idx, (char*)&dep->ref - sec->contents, tm);
	    dep->next_ref = 0;
	    ++dep;
	  }
    }

  if (flag_export && !obj_find_section(f, "__ksymtab"))
    {
      size_t nsyms;
      int *loaded;

      sec = obj_create_alloced_section(f, "__ksymtab", tgt_sizeof_void_p, 0);

      /* We don't want to export symbols residing in sections that
	 aren't loaded.  There are a number of these created so that
	 we make sure certain module options don't appear twice.  */

      loaded = alloca(sizeof(int) * (i = f->header.e_shnum));
      while (--i >= 0)
	loaded[i] = (f->sections[i]->header.sh_flags & SHF_ALLOC) != 0;

      for (nsyms = i = 0; i < HASH_BUCKETS; ++i)
	{
	  struct obj_symbol *sym;
	  for (sym = f->symtab[i]; sym; sym = sym->next)
	    if (ELFW(ST_BIND)(sym->info) != STB_LOCAL
		&& sym->secidx <= SHN_HIRESERVE
		&& (sym->secidx >= SHN_LORESERVE
		    || loaded[sym->secidx]))
	      {
		ElfW(Addr) ofs = nsyms * 2*tgt_sizeof_void_p;

		obj_symbol_patch(f, sec->idx, ofs, sym);
		obj_string_patch(f, sec->idx, ofs+tgt_sizeof_void_p, sym->name);

		nsyms++;
	      }
	}

      obj_extend_section(sec, nsyms * 2 * tgt_sizeof_char_p);
    }

  return 1;
}

static int
new_init_module(const char *m_name, struct obj_file *f, unsigned long m_size)
{
  struct new_module *module;
  struct obj_section *sec;
  void *image;
  int ret;
  tgt_long m_addr;

  sec = obj_find_section(f, ".this");
  module = (struct new_module *)sec->contents;
  m_addr = sec->header.sh_addr;

  module->size_of_struct = sizeof(*module);
  module->size = m_size;
  module->flags = flag_autoclean ? NEW_MOD_AUTOCLEAN : 0;

  sec = obj_find_section(f, "__ksymtab");
  if (sec && sec->header.sh_size)
    {
      module->syms = sec->header.sh_addr;
      module->nsyms = sec->header.sh_size / (2 * tgt_sizeof_char_p);
    }

  if (n_ext_modules_used)
    {
      sec = obj_find_section(f, ".kmodtab");
      module->deps = sec->header.sh_addr;
      module->ndeps = n_ext_modules_used;
    }

  module->init = obj_symbol_final_value(f, obj_find_symbol(f, "init_module"));
  module->cleanup
    = obj_symbol_final_value(f, obj_find_symbol(f, "cleanup_module"));

  sec = obj_find_section(f, "__ex_table");
  if (sec)
    {
      module->ex_table_start = sec->header.sh_addr;
      module->ex_table_end = sec->header.sh_addr + sec->header.sh_size;
    }

  sec = obj_find_section(f, ".text.init");
  if (sec)
    {
      module->runsize = sec->header.sh_addr - m_addr;
    }
  sec = obj_find_section(f, ".data.init");
  if (sec)
    {
      if (!module->runsize || 
          module->runsize > sec->header.sh_addr - m_addr)
	module->runsize = sec->header.sh_addr - m_addr;
    }

  if (!arch_init_module(f, module))
    return 0;

  /* Whew!  All of the initialization is complete.  Collect the final
     module image and give it to the kernel.  */

  image = xmalloc(m_size);
  obj_create_image(f, image);

  ret = new_sys_init_module(m_name, (struct new_module *)image);
  if (ret)
    error("init_module: %m");

  free(image);

  return ret == 0;
}


/*======================================================================*/

/* Print the usage message. */
static void
usage(void)
{
  fputs("Usage:\n"
	"insmod [-fkmopsvVxX] [-o name] module [[sym=value]...]\n"
	"\n"
	"  module              Filename of a loadable kernel module (*.o)\n"
	"  -f, --force         Force loading under wrong kernel version\n"
	"  -k, --autoclean     Make module autoclean-able\n"
	"  -m                  Generate load map (so crashes can be traced)\n"
	"  -o NAME\n"
	"      --name=NAME     Set internal module name to NAME\n"
	"  -p, --poll          Poll mode; check if the module matches the kernel\n"
	"  -s, --syslog        Report errors via syslog\n"
	"  -v, --verbose       Verbose output\n"
	"  -V, --version       Show version\n"
	"  -x                  Do not export externs\n"
	"  -X                  Do export externs (default)\n"
	, stderr);
  exit(1);
}

int
insmod_main(int argc, char **argv)
{
  int k_version;
  int k_crcs;
  int k_new_syscalls;
  char k_strversion[STRVERSIONLEN];
  static struct option long_opts[] = {
    { "force", 0, 0, 'f' },
    { "autoclean", 0, 0, 'k' },
    { "name", 1, 0, 'o' },
    { "poll", 0, 0, 'p' },
    { "syslog", 0, 0, 's' },
    { "verbose", 0, 0, 'v' },
    { "version", 0, 0, 'V' },
    { "lock", 0, 0, 'L' },
    { 0, 0, 0, 0 }
  };

  char *m_name = NULL;
  int m_version;
  ElfW(Addr) m_addr;
  unsigned long m_size;
  int m_crcs;
  int m_has_modinfo;
  char m_strversion[STRVERSIONLEN];

  char *filename;
  FILE *fp;
  struct obj_file *f;
  int o, noload = 0, dolock = 0;
  int exit_status = 1;

  error_file = "insmod";

  /* Process the command line.  */

  while ((o = getopt_long(argc, argv, "fkmno:psvVxXL",
			  &long_opts[0], NULL)) != EOF)
    switch (o)
      {
      case 'f': /* force loading */
	flag_force_load = 1;
	break;
      case 'k': /* module loaded by kerneld, auto-cleanable */
	flag_autoclean = 1;
	break;
      case 'L': /* protect against recursion.  */
	dolock = 1;
	break;
      case 'm': /* generate load map */
	flag_load_map = 1;
	break;
      case 'n':
        noload = 1;
	break;
      case 'o': /* name the output module */
	m_name = optarg;
	break;
      case 'p': /* silent poll mode */
	flag_silent_poll = 1;
	break;
      case 's': /* start syslog */
	setsyslog("insmod");
	break;
      case 'v': /* verbose output */
	flag_verbose = 1;
	break;
      case 'V':
	fputs("insmod version " MODUTILS_VERSION "\n", stderr);
	break;
      case 'x': /* do not export externs */
	flag_export = 0;
	break;
      case 'X': /* do export externs */
	flag_export = 1;
	break;
      default:
	usage();
	break;
      }

  if (optind >= argc)
    {
      usage();
    }

  filename = argv[optind++];

  if (m_name == NULL)
    {
      size_t len;
      char *p;

      if ((p = strrchr(filename, '/')) != NULL)
	p++;
      else
	p = filename;
      len = strlen(p);
      if (len > 2 && p[len-2] == '.' && p[len-1] == 'o')
	len -= 2;
      else if (len > 4 && p[len-4] == '.' && p[len-3] == 'm'
	       && p[len-2] == 'o' && p[len-1] == 'd')
	len -= 4;

      m_name = xmalloc(len+1);
      memcpy(m_name, p, len);
      m_name[len] = '\0';
    }

  /* Locate the file to be loaded.  */

  if (!strchr(filename, '/') && !strchr(filename, '.'))
    {
      char *tmp = search_module_path(filename);
      if (tmp == NULL)
        {
	  error("%s: no module by that name found", filename);
	  return 1;
	}
      filename = tmp;
    }

  error_file = filename;

  /* And open it.  */

  if ((fp = fopen(filename, "r")) == NULL)
    {
      error("%s: %m", filename);
      return 1;
    }

  /* Try to prevent multiple simultaneous loads.  */

  if (dolock)
    flock(fileno(fp), LOCK_EX);

  if ((f = obj_load(fp)) == NULL)
    goto out;

  /* Version correspondence?  */

  k_version = get_kernel_version(k_strversion);
  m_version = new_get_module_version(f, m_strversion);
  if (m_version != -1)
    m_has_modinfo = 1;
  else
    {
      m_has_modinfo = 0;
      m_version = old_get_module_version(f, m_strversion);
      if (m_version == -1)
	{
	  error("couldn't find the kernel version the module was compiled for");
	  goto out;
	}
    }

  if (strncmp(k_strversion, m_strversion, STRVERSIONLEN) != 0)
    {
      if (flag_force_load)
	{
	  lprintf("Warning: kernel-module version mismatch\n"
		  "\t%s was compiled for kernel version %s\n"
		  "\twhile this kernel is version %s\n",
		  filename, m_strversion, k_strversion);
	}
      else
	{
	  error("kernel-module version mismatch\n"
		"\t%s was compiled for kernel version %s\n"
		"\twhile this kernel is version %s.",
		filename, m_strversion, k_strversion);
	  goto out;
	}
    }

  k_new_syscalls = !query_module(NULL, 0, NULL, 0, NULL);

  if (k_new_syscalls)
    {
      if (!new_get_kernel_symbols())
	goto out;
      k_crcs = new_is_kernel_checksummed();
    }
  else
    {
      if (!old_get_kernel_symbols())
	goto out;
      k_crcs = old_is_kernel_checksummed();
    }

  if (m_has_modinfo)
    m_crcs = new_is_module_checksummed(f);
  else
    m_crcs = old_is_module_checksummed(f);

  if (m_crcs != k_crcs)
    obj_set_symbol_compare(f, ncv_strcmp, ncv_symbol_hash);

  /* Let the module know about the kernel symbols.  */

  add_kernel_symbols(f);

  /* Allocate common symbols, symbol tables, and string tables.  */

  if (k_new_syscalls
      ? !new_create_this_module(f, m_name)
      : !old_create_mod_use_count(f))
    goto out;

  if (!obj_check_undefineds(f))
    goto out;
  obj_allocate_commons(f);

  if (optind < argc)
    {
      if (m_has_modinfo
	  ? !new_process_module_arguments(f, argc-optind, argv+optind)
	  : !old_process_module_arguments(f, argc-optind, argv+optind))
	goto out;
    }

  arch_create_got(f);
  hide_special_symbols(f);

  if (k_new_syscalls)
    new_create_module_ksymtab(f);

  if (errors)
    goto out;

  /* If we were just checking, we made it.  */

  if (flag_silent_poll)
    {
      exit_status = 0;
      goto out;
    }

  /* Module has now finished growing; find its size and install it.  */

  m_size = obj_load_size(f);

  if (noload)
    {
      /* Don't bother actually touching the kernel.  */
      m_addr = 0x12340000;
    }
  else
    {
      errno = 0;
      m_addr = create_module(m_name, m_size);
      switch (errno)
        {
        case 0:
          break;
        case EEXIST:
	  if (dolock)
	    {
	      /* Assume that we were just invoked simultaneous with 
		 another insmod and return success.  */
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

  if (!obj_relocate(f, m_addr))
    {
      if (!noload)
        delete_module(m_name);
      goto out;
    }

  if (noload)
    ;
  else if (k_new_syscalls)
    new_init_module(m_name, f, m_size);
  else
    old_init_module(m_name, f, m_size);
  if (errors)
    {
      if (!noload)
        delete_module(m_name);
      goto out;
    }

  if (flag_load_map)
    print_load_map(f);
  exit_status = 0;

out:
  if (dolock)
    flock(fileno(fp), LOCK_UN);
  fclose(fp);
  return exit_status;
}
