/* ARM specific support for Elf loading and relocation.
   Copyright 1996, 1997, 1998 Linux International.

   Contributed by Phil Blundell <philb@gnu.org>
   and wms <woody@corelcomputer.com>
   based on the i386 code by Richard Henderson <rth@tamu.edu>

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

#include <string.h>
#include <assert.h>

#include <module.h>
#include <obj.h>
#include <util.h>


/*======================================================================*/

struct arm_plt_entry
{
  int offset;
  int allocated:1;
  int inited:1;                // has been set up
};

struct arm_got_entry
{
  int offset;
  int allocated : 1;
  unsigned reloc_done : 1;
};

struct arm_file
{
  struct obj_file root;
  struct obj_section *plt;
  struct obj_section *got;
};

struct arm_symbol
{
  struct obj_symbol root;
  struct arm_plt_entry pltent;
  struct arm_got_entry gotent;
};


/*======================================================================*/

struct obj_file *
arch_new_file (void)
{
  struct arm_file *f;
  f = xmalloc(sizeof(*f));
  f->got = NULL;
  return &f->root;
}

struct obj_section *
arch_new_section (void)
{
  return xmalloc(sizeof(struct obj_section));
}

struct obj_symbol *
arch_new_symbol (void)
{
  struct arm_symbol *sym;
  sym = xmalloc(sizeof(*sym));
  memset(&sym->gotent, 0, sizeof(sym->gotent));
  memset(&sym->pltent, 0, sizeof(sym->pltent));
  return &sym->root;
}

int
arch_load_proc_section(struct obj_section *sec, int fp)
{
    /* Assume it's just a debugging section that we can safely
       ignore ...  */
    sec->contents = NULL;

    return 0;
}

enum obj_reloc
arch_apply_relocation (struct obj_file *f,
		       struct obj_section *targsec,
		       struct obj_section *symsec,
		       struct obj_symbol *sym,
		       Elf32_Rel *rel,
		       Elf32_Addr v)
{
  struct arm_file *afile = (struct arm_file *)f;
  struct arm_symbol *asym  = (struct arm_symbol *)sym;

  Elf32_Addr *loc = (Elf32_Addr *)(targsec->contents + rel->r_offset);
  Elf32_Addr dot = targsec->header.sh_addr + rel->r_offset;
  Elf32_Addr got = afile->got ? afile->got->header.sh_addr : 0;
  Elf32_Addr plt = afile->plt ? afile->plt->header.sh_addr : 0;

  struct arm_plt_entry *pe;
  unsigned long *ip;

  enum obj_reloc ret = obj_reloc_ok;

  switch (ELF32_R_TYPE(rel->r_info))
    {
    case R_ARM_NONE:
      break;

    case R_ARM_ABS32:
      *loc += v;
      break;

    case R_ARM_GOT32:
      /* needs an entry in the .got: set it, once */
      if (! asym->gotent.reloc_done)
	{
	  asym->gotent.reloc_done = 1;
	  *(Elf32_Addr *)(afile->got->contents + asym->gotent.offset) = v;
	}
      /* make the reloc with_respect_to_.got */
      *loc += asym->gotent.offset;
      break;

      /* relative reloc, always to _GLOBAL_OFFSET_TABLE_ (which is .got)
	 similar to branch, but is full 32 bits relative */
    case R_ARM_GOTPC:
      assert(got);
      *loc += got - dot;
      break;

    case R_ARM_PC24:
    case R_ARM_PLT32:
      /* find the plt entry and initialize it if necessary */
      assert(asym != NULL);
      pe = (struct arm_plt_entry*) &asym->pltent;
      if (! pe->inited)
	{
	  ip = (unsigned long *) (afile->plt->contents + pe->offset);
	  ip[0] = 0xe51ff004;			/* ldr pc,[pc,#-4] */
	  ip[1] = v;				/* sym@ */
	  pe->inited = 1;
	}

      /* relative distance to target */
      v -= dot;
      /* if the target is too far away.... */
      if ((int)v < -0x02000000 || (int)v >= 0x02000000)
	{
	  /* go via the plt */
	  v = plt + pe->offset - dot;
	}
      if (v & 3)
	ret = obj_reloc_dangerous;

      /* Convert to words. */
      v >>= 2;

      /* merge the offset into the instruction. */
      *loc = (*loc & ~0x00ffffff) | ((v + *loc) & 0x00ffffff);
      break;

      /* address relative to the got */
    case R_ARM_GOTOFF:
      assert(got);
      *loc += v - got;
      break;

    default:
      printf("Warning: unhandled reloc %d\n",ELF32_R_TYPE(rel->r_info));
      ret = obj_reloc_unhandled;
      break;
    }

  return ret;
}

int
arch_create_got (struct obj_file *f)
{
  struct arm_file *afile = (struct arm_file *) f;
  int i;
  struct obj_section *sec, *syms, *strs;
  ElfW(Rel) *rel, *relend;
  ElfW(Sym) *symtab, *extsym;
  const char *strtab;
  struct arm_symbol *intsym;
  struct arm_plt_entry *pe;
  struct arm_got_entry *ge;
  int got_offset = 0, plt_offset = 0;

  for (i = 0; i < f->header.e_shnum; ++i)
    {
      sec = f->sections[i];
      if (sec->header.sh_type != SHT_RELM)
	continue;
      syms = f->sections[sec->header.sh_link];
      strs = f->sections[syms->header.sh_link];

      rel = (ElfW(RelM) *) sec->contents;
      relend = rel + (sec->header.sh_size / sizeof(ElfW(RelM)));
      symtab = (ElfW(Sym) *) syms->contents;
      strtab = (const char *) strs->contents;

      for (; rel < relend; ++rel)
	{
	  extsym = &symtab[ELF32_R_SYM(rel->r_info)];

	  switch(ELF32_R_TYPE(rel->r_info)) {
	  case R_ARM_PC24:
	  case R_ARM_PLT32:
	    obj_find_relsym(intsym, f, f, rel, symtab, strtab);

	    pe = &intsym->pltent;

	    if (! pe->allocated)
	      {
		pe->allocated = 1;
		pe->offset = plt_offset;
		plt_offset += 8;
		pe->inited = 0;
	      }
	    break;

	    /* these two don_t need got entries, but they need
	       the .got to exist */
	  case R_ARM_GOTOFF:
	  case R_ARM_GOTPC:
	    if (got_offset==0) got_offset = 4;
	    break;

	  case R_ARM_GOT32:
	    obj_find_relsym(intsym, f, f, rel, symtab, strtab);

	    ge = (struct arm_got_entry *) &intsym->gotent;
	    if (! ge->allocated)
	      {
		ge->allocated = 1;
		ge->offset = got_offset;
		got_offset += sizeof(void*);
	      }
	    break;

	  default:
	    continue;
	  }
	}
    }

  /* if there was a _GLOBAL_OFFSET_TABLE_, then the .got section
   exists already; find it and use it */
  if (got_offset)
  {
      struct obj_section* sec = obj_find_section(f, ".got");
      if (sec)
	obj_extend_section(sec, got_offset);
      else
	{
	  sec = obj_create_alloced_section(f, ".got", 8, got_offset,
					   SHF_WRITE);
	  assert(sec);
	}
      afile->got = sec;
  }

  if (plt_offset)
    afile->plt = obj_create_alloced_section(f, ".plt", 8, plt_offset,
					    SHF_WRITE);

  return 1;
}

int
arch_init_module (struct obj_file *f, struct module *mod)
{
  return 1;
}

int
arch_finalize_section_address(struct obj_file *f, Elf32_Addr base)
{
  int  i, n = f->header.e_shnum;

  f->baseaddr = base;
  for (i = 0; i < n; ++i)
    f->sections[i]->header.sh_addr += base;
  return 1;
}

int
arch_archdata (struct obj_file *fin, struct obj_section *sec)
{
  return 0;
}
