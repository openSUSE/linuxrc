/* Elf relocation routines.
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

#ident "$Id: obj_reloc.c,v 1.1 2000/03/23 17:09:56 snwint Exp $"

#include <string.h>
#include <assert.h>
#include <alloca.h>

#include <obj.h>
#include <util.h>

/*======================================================================*/

int
obj_string_patch(struct obj_file *f, int secidx, ElfW(Addr) offset,
		 const char *string)
{
  struct obj_string_patch_struct *p;
  struct obj_section *strsec;
  size_t len = strlen(string)+1;
  char *loc;

  p = xmalloc(sizeof(*p));
  p->next = f->string_patches;
  p->reloc_secidx = secidx;
  p->reloc_offset = offset;
  f->string_patches = p;

  strsec = obj_find_section(f, ".kstrtab");
  if (strsec == NULL)
    {
      strsec = obj_create_alloced_section(f, ".kstrtab", 1, len);
      p->string_offset = 0;
      loc = strsec->contents;
    }
  else
    {
      p->string_offset = strsec->header.sh_size;
      loc = obj_extend_section(strsec, len);
    }
  memcpy(loc, string, len);

  return 1;
}

int
obj_symbol_patch(struct obj_file *f, int secidx, ElfW(Addr) offset,
		 struct obj_symbol *sym)
{
  struct obj_symbol_patch_struct *p;

  p = xmalloc(sizeof(*p));
  p->next = f->symbol_patches;
  p->reloc_secidx = secidx;
  p->reloc_offset = offset;
  p->sym = sym;
  f->symbol_patches = p;

  return 1;
}

int
obj_check_undefineds(struct obj_file *f, int quiet)
{
  unsigned long i;
  int ret = 1;

  for (i = 0; i < HASH_BUCKETS; ++i)
    {
      struct obj_symbol *sym;
      for (sym = f->symtab[i]; sym ; sym = sym->next)
	if (sym->secidx == SHN_UNDEF)
	  {
	    if (ELFW(ST_BIND)(sym->info) == STB_WEAK)
	      {
		sym->secidx = SHN_ABS;
		sym->value = 0;
	      }
	    else
	      {
		if (!quiet)
			error("unresolved symbol %s", sym->name);
		ret = 0;
	      }
	  }
    }

  return ret;
}

void
obj_allocate_commons(struct obj_file *f)
{
  struct common_entry
  {
    struct common_entry *next;
    struct obj_symbol *sym;
  } *common_head = NULL;

  unsigned long i;

  for (i = 0; i < HASH_BUCKETS; ++i)
    {
      struct obj_symbol *sym;
      for (sym = f->symtab[i]; sym ; sym = sym->next)
	if (sym->secidx == SHN_COMMON)
	  {
	    /* Collect all COMMON symbols and sort them by size so as to
	       minimize space wasted by alignment requirements.  */
	    {
	      struct common_entry **p, *n;
	      for (p = &common_head; *p ; p = &(*p)->next)
		if (sym->size <= (*p)->sym->size)
		  break;

	      n = alloca(sizeof(*n));
	      n->next = *p;
	      n->sym = sym;
	      *p = n;
	    }
	  }
    }

  for (i = 1; i < f->local_symtab_size; ++i)
    {
      struct obj_symbol *sym = f->local_symtab[i];
      if (sym && sym->secidx == SHN_COMMON)
	{
	  struct common_entry **p, *n;
	  for (p = &common_head; *p ; p = &(*p)->next)
	    if (sym == (*p)->sym)
	      break;
	    else if (sym->size < (*p)->sym->size)
	      {
		n = alloca(sizeof(*n));
		n->next = *p;
		n->sym = sym;
		*p = n;
		break;
	      }
	}
    }

  if (common_head)
    {
      /* Find the bss section.  */
      for (i = 0; i < f->header.e_shnum; ++i)
	if (f->sections[i]->header.sh_type == SHT_NOBITS)
	  break;

      /* If for some reason there hadn't been one, create one.  */
      if (i == f->header.e_shnum)
	{
	  struct obj_section *sec;

	  f->sections = xrealloc(f->sections, (i+1) * sizeof(sec));
	  f->sections[i] = sec = arch_new_section();
	  f->header.e_shnum = i+1;

	  memset(sec, 0, sizeof(*sec));
	  sec->header.sh_type = SHT_PROGBITS;
	  sec->header.sh_flags = SHF_WRITE|SHF_ALLOC;
	  sec->name = ".bss";
	  sec->idx = i;
	}

      /* Allocate the COMMONS.  */
      {
	ElfW(Addr) bss_size = f->sections[i]->header.sh_size;
	ElfW(Addr) max_align = f->sections[i]->header.sh_addralign;
	struct common_entry *c;

	for (c = common_head; c ; c = c->next)
	  {
	    ElfW(Addr) align = c->sym->value;

	    if (align > max_align)
	      max_align = align;
	    if (bss_size & (align - 1))
	      bss_size = (bss_size | (align - 1)) + 1;

	    c->sym->secidx = i;
	    c->sym->value = bss_size;

	    bss_size += c->sym->size;
	  }

	f->sections[i]->header.sh_size = bss_size;
	f->sections[i]->header.sh_addralign = max_align;
      }
    }

  /* For the sake of patch relocation and parameter initialization,
     allocate zeroed data for NOBITS sections now.  Note that after
     this we cannot assume NOBITS are really empty.  */
  for (i = 0; i < f->header.e_shnum; ++i)
    {
      struct obj_section *s = f->sections[i];
      if (s->header.sh_type == SHT_NOBITS)
	{
	  if (s->header.sh_size)
	    s->contents = memset(xmalloc(s->header.sh_size),
				 0, s->header.sh_size);
	  else
	    s->contents = NULL;
	  s->header.sh_type = SHT_PROGBITS;
	}
    }
}

unsigned long
obj_load_size (struct obj_file *f)
{
  unsigned long dot = 0;
  struct obj_section *sec;

  /* Finalize the positions of the sections relative to one another.  */

  for (sec = f->load_order; sec ; sec = sec->load_next)
    {
      ElfW(Addr) align;

      align = sec->header.sh_addralign;
      if (align && (dot & (align - 1)))
	dot = (dot | (align - 1)) + 1;

      sec->header.sh_addr = dot;
      dot += sec->header.sh_size;
    }

  return dot;
}

int
obj_relocate (struct obj_file *f, ElfW(Addr) base)
{
  int i, n = f->header.e_shnum;
  int ret = 1;

  /* Finalize the addresses of the sections.  */

  f->baseaddr = base;
  for (i = 0; i < n; ++i)
    f->sections[i]->header.sh_addr += base;

  /* And iterate over all of the relocations.  */

  for (i = 0; i < n; ++i)
    {
      struct obj_section *relsec, *symsec, *targsec, *strsec;
      ElfW(RelM) *rel, *relend;
      ElfW(Sym) *symtab;
      const char *strtab;

      relsec = f->sections[i];
      if (relsec->header.sh_type != SHT_RELM)
	continue;

      symsec = f->sections[relsec->header.sh_link];
      targsec = f->sections[relsec->header.sh_info];
      strsec = f->sections[symsec->header.sh_link];

      rel = (ElfW(RelM) *)relsec->contents;
      relend = rel + (relsec->header.sh_size / sizeof(ElfW(RelM)));
      symtab = (ElfW(Sym) *)symsec->contents;
      strtab = (const char *)strsec->contents;

      for (; rel < relend; ++rel)
	{
	  ElfW(Addr) value = 0;
	  struct obj_symbol *intsym = NULL;
	  unsigned long symndx;
	  ElfW(Sym) *extsym = 0;
	  const char *errmsg;

	  /* Attempt to find a value to use for this relocation.  */

	  symndx = ELFW(R_SYM)(rel->r_info);
	  if (symndx)
	    {
	      /* Note we've already checked for undefined symbols.  */

	      extsym = &symtab[symndx];
	      if (ELFW(ST_BIND)(extsym->st_info) == STB_LOCAL)
		{
		  /* Local symbols we look up in the local table to be sure
		     we get the one that is really intended.  */
		  intsym = f->local_symtab[symndx];
		}
	      else
		{
		  /* Others we look up in the hash table.  */
	          const char *name;
		  if (extsym->st_name)
		    name = strtab + extsym->st_name;
	          else
		    name = f->sections[extsym->st_shndx]->name;
	          intsym = obj_find_symbol(f, name);
		}

	      value = obj_symbol_final_value(f, intsym);
	      intsym->referenced = 1;
	    }

#if SHT_RELM == SHT_RELA
#if defined(__alpha__) && defined(AXP_BROKEN_GAS)
          /* Work around a nasty GAS bug, that is fixed as of 2.7.0.9.  */
          if (!extsym || !extsym->st_name ||
              ELFW(ST_BIND)(extsym->st_info) != STB_LOCAL)
#endif
	  value += rel->r_addend;
#endif

	  /* Do it! */
	  switch (arch_apply_relocation(f,targsec,symsec,intsym,rel,value))
	    {
	    case obj_reloc_ok:
	      break;

	    case obj_reloc_overflow:
	      errmsg = "Relocation overflow";
	      goto bad_reloc;
	    case obj_reloc_dangerous:
	      errmsg = "Dangerous relocation";
	      goto bad_reloc;
	    case obj_reloc_unhandled:
	      errmsg = "Unhandled relocation";
	    bad_reloc:
	      if (extsym)
		{
		  error("%s of type %ld for %s", errmsg,
			(long)ELFW(R_TYPE)(rel->r_info),
			strtab + extsym->st_name);
		}
	      else
		{
		  error("%s of type %ld", errmsg,
			(long)ELFW(R_TYPE)(rel->r_info));
		}
	      ret = 0;
	      break;
	    }
	}
    }

  /* Finally, take care of the patches.  */

  if (f->string_patches)
    {
      struct obj_string_patch_struct *p;
      struct obj_section *strsec;
      ElfW(Addr) strsec_base;
      strsec = obj_find_section(f, ".kstrtab");
      strsec_base = strsec->header.sh_addr;

      for (p = f->string_patches; p ; p = p->next)
	{
	  struct obj_section *targsec = f->sections[p->reloc_secidx];
	  *(ElfW(Addr) *)(targsec->contents + p->reloc_offset)
	    = strsec_base + p->string_offset;
	}
    }

  if (f->symbol_patches)
    {
      struct obj_symbol_patch_struct *p;

      for (p = f->symbol_patches; p; p = p->next)
	{
	  struct obj_section *targsec = f->sections[p->reloc_secidx];
	  *(ElfW(Addr) *)(targsec->contents + p->reloc_offset)
	    = obj_symbol_final_value(f, p->sym);
	}
    }

  return ret;
}

int
obj_create_image (struct obj_file *f, char *image)
{
  struct obj_section *sec;
  ElfW(Addr) base = f->baseaddr;

  for (sec = f->load_order; sec ; sec = sec->load_next)
    {
      char *secimg;

      if (sec->contents == 0)
	continue;

      secimg = image + (sec->header.sh_addr - base);

      /* Note that we allocated data for NOBITS sections earlier.  */
      memcpy(secimg, sec->contents, sec->header.sh_size);
    }

  return 1;
}
