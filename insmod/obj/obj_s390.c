/* S/390 specific support for Elf loading and relocation.
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

#ident "$Id: obj_s390.c,v 1.1 2000/11/22 15:46:44 snwint Exp $"

#include <string.h>
#include <assert.h>

#include <module.h>
#include <obj.h>
#include <util.h>


/*======================================================================*/

struct s390_got_entry
{
  int offset;
  unsigned offset_done : 1;
  unsigned reloc_done : 1;
};

struct s390_file
{
  struct obj_file root;
  struct obj_section *got;
};

struct s390_symbol
{
  struct obj_symbol root;
  struct s390_got_entry gotent;
};


/*======================================================================*/

struct obj_file *
arch_new_file (void)
{
  struct s390_file *f;
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
  struct s390_symbol *sym;
  sym = xmalloc(sizeof(*sym));
  memset(&sym->gotent, 0, sizeof(sym->gotent));
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
		       Elf32_Rela *rel,
		       Elf32_Addr v)
{
  struct s390_file *ifile = (struct s390_file *)f;
  struct s390_symbol *isym  = (struct s390_symbol *)sym;

  Elf32_Addr *loc = (Elf32_Addr *)(targsec->contents + rel->r_offset);
  Elf32_Addr dot = targsec->header.sh_addr + rel->r_offset;
  Elf32_Addr got = ifile->got ? ifile->got->header.sh_addr : 0;

  enum obj_reloc ret = obj_reloc_ok;

  switch (ELF32_R_TYPE(rel->r_info))
    {
    case R_390_NONE:
      break;

    case R_390_32:
      *loc += v;
      break;

    case R_390_PLT32:
    case R_390_PC32:
      *loc += v - dot;
      break;

    case R_390_GLOB_DAT:
    case R_390_JMP_SLOT:
      *loc = v;
      break;

    case R_390_RELATIVE:
      *loc += f->baseaddr;
      break;

    case R_390_GOTPC:
      assert(got != 0);
      *loc += got - dot;
      break;

    case R_390_GOT32:
      assert(isym != NULL);
      if (!isym->gotent.reloc_done)
	{
	  isym->gotent.reloc_done = 1;
	  *(Elf32_Addr *)(ifile->got->contents + isym->gotent.offset) = v;
	}
      *loc += isym->gotent.offset;
      break;

    case R_390_GOTOFF:
      assert(got != 0);
      *loc += v - got;
      break;

    default:
      ret = obj_reloc_unhandled;
      break;
    }

  return ret;
}

int
arch_create_got (struct obj_file *f)
{
  struct s390_file *ifile = (struct s390_file *)f;
  int i, n, offset = 0, gotneeded = 0;

  n = ifile->root.header.e_shnum;
  for (i = 0; i < n; ++i)
    {
      struct obj_section *relsec, *symsec, *strsec;
      Elf32_Rel *rel, *relend;
      Elf32_Sym *symtab;
      const char *strtab;

      relsec = ifile->root.sections[i];
      if (relsec->header.sh_type != SHT_REL)
	continue;

      symsec = ifile->root.sections[relsec->header.sh_link];
      strsec = ifile->root.sections[symsec->header.sh_link];

      rel = (Elf32_Rel *)relsec->contents;
      relend = rel + (relsec->header.sh_size / sizeof(Elf32_Rel));
      symtab = (Elf32_Sym *)symsec->contents;
      strtab = (const char *)strsec->contents;

      for (; rel < relend; ++rel)
	{
	  Elf32_Sym *extsym;
	  struct s390_symbol *intsym;
	  const char *name;

	  switch (ELF32_R_TYPE(rel->r_info))
	    {
	    case R_390_GOTPC:
	    case R_390_GOTOFF:
	      gotneeded = 1;
	    default:
	      continue;

	    case R_390_GOT32:
	      break;
	    }

	  extsym = &symtab[ELF32_R_SYM(rel->r_info)];
	  if (extsym->st_name)
	    name = strtab + extsym->st_name;
	  else
	    name = f->sections[extsym->st_shndx]->name;
	  intsym = (struct s390_symbol *)obj_find_symbol(&ifile->root, name);

	  if (!intsym->gotent.offset_done)
	    {
	      intsym->gotent.offset_done = 1;
	      intsym->gotent.offset = offset;
	      offset += 4;
	    }
	}
    }

  if (offset > 0 || gotneeded)
    ifile->got = obj_create_alloced_section(&ifile->root, ".got", 4, offset);

  return 1;
}

int
arch_init_module (struct obj_file *f, struct module *m)
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

int
arch_finalize_section_address(struct obj_file *f, Elf32_Addr base)
{
  int  i, n = f->header.e_shnum;

  f->baseaddr = base;
  for (i = 0; i < n; ++i)
    f->sections[i]->header.sh_addr += base;
  return 1;
}

