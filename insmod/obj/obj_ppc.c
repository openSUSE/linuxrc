/* PowerPC specific support for Elf loading and relocation.
   Copyright 1996, 1997 Linux International.

   Adapted by Paul Mackerras <paulus@cs.anu.edu.au> from the
   obj-sparc.c and obj-alpha.c files.

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

#include <stddef.h>
#include <module.h>
#include <obj.h>
#include <util.h>
#include <assert.h>


/*======================================================================*/

/*
 * Unfortunately, the bl (branch-and-link) instruction used for
 * procedure calls on the PowerPC can only reach +/- 32MB from the
 * current instruction.  If the module is loaded far enough away from
 * the main kernel text (or other modules) that this limit is
 * exceeded, we have to redirect procedure calls via a procedure
 * linkage table (PLT).  Each entry in the PLT contains instructions
 * to put the address of the procedure in a register and jump to it.
 */

typedef unsigned int instruction;	/* a powerpc instruction (4 bytes) */

struct ppc_plt_entry
{
  struct ppc_plt_entry *next;
  ElfW(Addr) addend;
  int offset;
  int inited;
};

struct ppc_file
{
  struct obj_file file;
  struct obj_section *plt;
};

struct ppc_symbol
{
  struct obj_symbol sym;
  struct ppc_plt_entry *plt_entries;
};

struct obj_file *
arch_new_file (void)
{
  struct ppc_file *f;

  f = xmalloc(sizeof(struct ppc_file));
  f->plt = NULL;
  return &f->file;
}

struct obj_section *
arch_new_section (void)
{
  return xmalloc(sizeof(struct obj_section));
}

struct obj_symbol *
arch_new_symbol (void)
{
  struct ppc_symbol *p;

  p = xmalloc(sizeof(struct ppc_symbol));
  p->plt_entries = NULL;
  return &p->sym;
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
arch_apply_relocation (struct obj_file *ef,
		       struct obj_section *targsec,
		       struct obj_section *symsec,
		       struct obj_symbol *sym,
		       Elf32_Rela *rel,
		       Elf32_Addr v)
{
  Elf32_Addr *loc = (Elf32_Addr *)(targsec->contents + rel->r_offset);
  Elf32_Addr dot = targsec->header.sh_addr + rel->r_offset;
  struct ppc_file *pf = (struct ppc_file *) ef;
  struct ppc_symbol *psym = (struct ppc_symbol *) sym;
  struct ppc_plt_entry *pe;
  instruction *ip;

  enum obj_reloc ret = obj_reloc_ok;

  switch (ELF32_R_TYPE(rel->r_info))
    {
    case R_PPC_ADDR16_HA:
      *(unsigned short *)loc = (v + 0x8000) >> 16;
      break;

    case R_PPC_ADDR16_HI:
      *(unsigned short *)loc = v >> 16;
      break;

    case R_PPC_ADDR16_LO:
      *(unsigned short *)loc = v;
      break;

    case R_PPC_REL24:
      /* find the plt entry and initialize it if necessary */
      assert(psym != NULL);
      for (pe = psym->plt_entries; pe != NULL && pe->addend != rel->r_addend; )
	pe = pe->next;
      assert(pe != NULL);
      if (!pe->inited)
	{
	  ip = (instruction *) (pf->plt->contents + pe->offset);
	  ip[0] = 0x3d600000 + ((v + 0x8000) >> 16);  /* lis r11,sym@ha */
	  ip[1] = 0x396b0000 + (v & 0xffff);	      /* addi r11,r11,sym@l */
	  ip[2] = 0x7d6903a6;			      /* mtctr r11 */
	  ip[3] = 0x4e800420;			      /* bctr */
	  pe->inited = 1;
	}

      v -= dot;
      if ((int)v < -0x02000000 || (int)v >= 0x02000000)
	{
	  /* go via the plt */
	  v = pf->plt->header.sh_addr + pe->offset - dot;
	}
      if (v & 3)
	ret = obj_reloc_dangerous;
      *loc = (*loc & ~0x03fffffc) | (v & 0x03fffffc);
      break;

    case R_PPC_REL32:
      *loc = v - dot;
      break;

    case R_PPC_ADDR32:
      *loc = v;
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
  struct ppc_file *pf = (struct ppc_file *) f;
  int i, offset;
  struct obj_section *sec, *syms, *strs;
  ElfW(Rela) *rel, *relend;
  ElfW(Sym) *symtab;
  const char *strtab;
  struct ppc_symbol *intsym;
  struct ppc_plt_entry *pe;

  offset = 0;
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
	  if (ELF32_R_TYPE(rel->r_info) != R_PPC_REL24)
	    continue;
	  obj_find_relsym(intsym, f, f, rel, symtab, strtab);

	  for (pe = intsym->plt_entries; pe != NULL; pe = pe->next)
	    if (pe->addend == rel->r_addend)
	      break;
	  if (pe == NULL)
	    {
	      pe = xmalloc(sizeof(struct ppc_plt_entry));
	      pe->next = intsym->plt_entries;
	      pe->addend = rel->r_addend;
	      pe->offset = offset;
	      pe->inited = 0;
	      intsym->plt_entries = pe;
	      offset += 16;
	    }
	}
    }

  pf->plt = obj_create_alloced_section(f, ".plt", 16, offset, SHF_WRITE);

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
arch_archdata (struct obj_file *f, struct obj_section *archdata_sec)
{
  struct archdata {
    unsigned tgt_long __start___ftr_fixup;
    unsigned tgt_long __stop___ftr_fixup;
  } *ad;
  struct obj_section *sec;

  if (archdata_sec->contents)
    free(archdata_sec->contents);
  archdata_sec->header.sh_size = 0;
  sec = obj_find_section(f, "__ftr_fixup");
  if (sec) {
    ad = (struct archdata *) (archdata_sec->contents) = xmalloc(sizeof(*ad));
    memset(ad, 0, sizeof(*ad));
    archdata_sec->header.sh_size = sizeof(*ad);
    ad->__start___ftr_fixup = sec->header.sh_addr;
    ad->__stop___ftr_fixup = sec->header.sh_addr + sec->header.sh_size;
  }

  return 0;
}
