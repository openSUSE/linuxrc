/* Alpha specific support for Elf loading and relocation.
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

#include <string.h>
#include <assert.h>

#include <module.h>
#include <obj.h>
#include <util.h>


/* This relocation got renamed, and the change hasn't propagated yet.  */
#ifndef R_ALPHA_GPREL16
#define R_ALPHA_GPREL16  R_ALPHA_IMMED_GP_16
#endif

/*======================================================================*/

struct alpha_got_entry
{
  struct alpha_got_entry *next;
  ElfW(Addr) addend;
  int offset;
  int reloc_done;
};

struct alpha_file
{
  struct obj_file root;
  struct obj_section *got;
};

struct alpha_symbol
{
  struct obj_symbol root;
  struct alpha_got_entry *got_entries;
};


/*======================================================================*/

struct obj_file *
arch_new_file (void)
{
  struct alpha_file *f;
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
  struct alpha_symbol *sym;
  sym = xmalloc(sizeof(*sym));
  sym->got_entries = NULL;
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
		       Elf64_Rela *rel,
		       Elf64_Addr v)
{
  struct alpha_file *af = (struct alpha_file *)f;
  struct alpha_symbol *asym = (struct alpha_symbol *)sym;

  unsigned long *lloc = (unsigned long *)(targsec->contents + rel->r_offset);
  unsigned int *iloc = (unsigned int *)lloc;
  unsigned short *sloc = (unsigned short *)lloc;
  Elf64_Addr dot = targsec->header.sh_addr + rel->r_offset;
  Elf64_Addr gp = af->got->header.sh_addr + 0x8000;

  enum obj_reloc ret = obj_reloc_ok;

  switch (ELF64_R_TYPE(rel->r_info))
    {
    case R_ALPHA_NONE:
    case R_ALPHA_LITUSE:
      break;

    case R_ALPHA_REFQUAD:
      *lloc += v;
      break;

    case R_ALPHA_GPREL16:
      v -= gp;
      if ((Elf64_Sxword)v > 0x7fff
	  || (Elf64_Sxword)v < -(Elf64_Sxword)0x8000)
	ret = obj_reloc_overflow;
      *sloc = v;
      break;

    case R_ALPHA_GPRELLOW:
      /* GPRELLOW does not overflow.  Errors are seen in the
	 corresponding GPRELHIGH.  */
      v -= gp;
      *sloc = v;
      break;

    case R_ALPHA_GPRELHIGH:
      v -= gp;
      v = ((Elf64_Sxword)v >> 16) + ((v >> 15) & 1);
      if ((Elf64_Sxword)v > 0x7fff
	  || (Elf64_Sxword)v < -(Elf64_Sxword)0x8000)
	ret = obj_reloc_overflow;
      *sloc = v;
      break;

    case R_ALPHA_GPREL32:
      v -= gp;
      if ((Elf64_Sxword)v > 0x7fffffff
	  || (Elf64_Sxword)v < -(Elf64_Sxword)0x80000000)
	ret = obj_reloc_overflow;
      *iloc = v;
      break;

    case R_ALPHA_LITERAL:
      {
	struct alpha_got_entry *gotent;

	assert(asym != NULL);
	gotent = asym->got_entries;
	while (gotent->addend != rel->r_addend)
	  gotent = gotent->next;

	if (!gotent->reloc_done)
	  {
	    *(unsigned long *)(af->got->contents + gotent->offset) = v;
	    gotent->reloc_done = 1;
	  }

	*sloc = gotent->offset - 0x8000;
      }
    break;

    case R_ALPHA_GPDISP:
      {
	unsigned int *p_ldah, *p_lda;
	unsigned int i_ldah, i_lda, hi, lo;

	p_ldah = iloc;
	p_lda = (unsigned int *)((char *)iloc + rel->r_addend);
	i_ldah = *p_ldah;
	i_lda = *p_lda;

	/* Make sure the instructions are righteous.  */
	if ((i_ldah >> 26) != 9 || (i_lda >> 26) != 8)
	  ret = obj_reloc_dangerous;

	/* Extract the existing addend.  */
	v = (i_ldah & 0xffff) << 16 | (i_lda & 0xffff);
	v = (v ^ 0x80008000) - 0x80008000;

	v += gp - dot;

	if ((Elf64_Sxword)v >= 0x7fff8000
	    || (Elf64_Sxword)v < -(Elf64_Sxword)0x80000000)
	  ret = obj_reloc_overflow;

	/* Modify the instructions and finish up.  */
	lo = v & 0xffff;
	hi = ((v >> 16) + ((v >> 15) & 1)) & 0xffff;

	*p_ldah = (i_ldah & 0xffff0000) | hi;
	*p_lda = (i_lda & 0xffff0000) | lo;
      }
    break;

    case R_ALPHA_BRADDR:
      v -= dot + 4;
      if (v % 4)
	ret = obj_reloc_dangerous;
      else if ((Elf64_Sxword)v > 0x3fffff
	       || (Elf64_Sxword)v < -(Elf64_Sxword)0x400000)
	ret = obj_reloc_overflow;
      v /= 4;

      *iloc = (*iloc & ~0x1fffff) | (v & 0x1fffff);
      break;

    case R_ALPHA_HINT:
      v -= dot + 4;
      if (v % 4)
	ret = obj_reloc_dangerous;
      v /= 4;

      *iloc = (*iloc & ~0x3fff) | (v & 0x3fff);
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
  struct alpha_file *af = (struct alpha_file *)f;
  int i, n, offset = 0;

  n = af->root.header.e_shnum;
  for (i = 0; i < n; ++i)
    {
      struct obj_section *relsec, *symsec, *strsec;
      Elf64_Rela *rel, *relend;
      Elf64_Sym *symtab;
      const char *strtab;

      relsec = af->root.sections[i];
      if (relsec->header.sh_type != SHT_RELA)
	continue;

      symsec = af->root.sections[relsec->header.sh_link];
      strsec = af->root.sections[symsec->header.sh_link];

      rel = (Elf64_Rela *)relsec->contents;
      relend = rel + (relsec->header.sh_size / sizeof(Elf64_Rela));
      symtab = (Elf64_Sym *)symsec->contents;
      strtab = (const char *)strsec->contents;

      for (; rel < relend; ++rel)
	{
	  struct alpha_got_entry *ent;
	  struct alpha_symbol *intsym;

	  if (ELF64_R_TYPE(rel->r_info) != R_ALPHA_LITERAL)
	    continue;

	  obj_find_relsym(intsym, f, &af->root, rel, symtab, strtab);

	  for (ent = intsym->got_entries; ent ; ent = ent->next)
	    if (ent->addend == rel->r_addend)
	      goto found;

	  ent = xmalloc(sizeof(*ent));
	  ent->addend = rel->r_addend;
	  ent->offset = offset;
	  ent->reloc_done = 0;
	  ent->next = intsym->got_entries;
	  intsym->got_entries = ent;
	  offset += 8;

	found:;
	}
    }

  if (offset > 0x10000)
    {
      error(".got section overflow: %#x > 0x10000", offset);
      return 0;
    }

  /* We always want a .got section so that we always have a GP for
     use with GPDISP and GPREL relocs.  Besides, if the section
     is empty we don't use up space anyway.  */
  af->got = obj_create_alloced_section(&af->root, ".got", 8, offset,
				       SHF_WRITE | SHF_ALPHA_GPREL);

  return 1;
}

int
arch_init_module (struct obj_file *f, struct module *mod)
{
  struct alpha_file *af = (struct alpha_file *)f;

  mod->gp = af->got->header.sh_addr + 0x8000;

  return 1;
}

int
arch_finalize_section_address(struct obj_file *f, Elf64_Addr base)
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
