/* MIPS specific support for Elf loading and relocation.
   Copyright 1997, 1998 Linux International.
   Contributed by Ralf Baechle <ralf@gnu.ai.mit.edu>

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
#include <stdlib.h>
#include <assert.h>

#include <module.h>
#include <obj.h>
#include <util.h>


/*======================================================================*/

struct mips_hi16
{
  struct mips_hi16 *next;
  Elf32_Addr *addr;
  Elf32_Addr value;
};

struct mips_file
{
  struct obj_file root;
  struct mips_hi16 *mips_hi16_list;
};

/*======================================================================*/

struct obj_file *
arch_new_file (void)
{
  struct mips_file *mf;

  mf = xmalloc(sizeof(*mf));
  mf->mips_hi16_list = NULL;

  return (struct obj_file *) mf;
}

struct obj_section *
arch_new_section (void)
{
  return xmalloc(sizeof(struct obj_section));
}

struct obj_symbol *
arch_new_symbol (void)
{
  return xmalloc(sizeof(struct obj_symbol));
}

int
arch_load_proc_section(struct obj_section *sec, int fp)
{
  switch (sec->header.sh_type)
    {
    case SHT_MIPS_DEBUG:
    case SHT_MIPS_REGINFO:
      /* Actually these two sections are as useless as something can be ...  */
      sec->contents = NULL;
      break;

    case SHT_MIPS_LIBLIST:
    case SHT_MIPS_CONFLICT:
    case SHT_MIPS_GPTAB:
    case SHT_MIPS_UCODE:
    case SHT_MIPS_OPTIONS:
    case SHT_MIPS_DWARF:
    case SHT_MIPS_EVENTS:
      /* These shouldn't ever be in a module file.  */
      error("Unhandled section header type: %08x", sec->header.sh_type);

    default:
      /* We don't even know the type.  This time it might as well be a
	 supernova.  */
      error("Unknown section header type: %08x", sec->header.sh_type);
      return -1;
    }

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
  struct mips_file *mf = (struct mips_file *)f;
  Elf32_Addr *loc = (Elf32_Addr *)(targsec->contents + rel->r_offset);
  Elf32_Addr dot = targsec->header.sh_addr + rel->r_offset;
  enum obj_reloc ret = obj_reloc_ok;

  /* _gp_disp is a magic symbol for PIC which is not supported for
     the kernel and loadable modules.  */
  if (strcmp(sym->name, "_gp_disp") == 0)
	ret = obj_reloc_unhandled;

  switch (ELF32_R_TYPE(rel->r_info))
    {
    case R_MIPS_NONE:
      break;

    case R_MIPS_32:
      *loc += v;
      break;

    case R_MIPS_26:
      if (v % 4)
	ret = obj_reloc_dangerous;
      if ((v & 0xf0000000) != ((dot + 4) & 0xf0000000))
	ret = obj_reloc_overflow;
      *loc = (*loc & ~0x03ffffff) | ((*loc + (v >> 2)) & 0x03ffffff);
      break;

    case R_MIPS_HI16:
      {
	struct mips_hi16 *n;

	/* We cannot relocate this one now because we don't know the value
	   of the carry we need to add.  Save the information, and let LO16
	   do the actual relocation.  */
	n = (struct mips_hi16 *) xmalloc (sizeof *n);
	n->addr = loc;
	n->value = v;
	n->next = mf->mips_hi16_list;
	mf->mips_hi16_list = n;
	break;
      }

    case R_MIPS_LO16:
      {
	unsigned long insnlo = *loc;
	Elf32_Addr val, vallo;

	/* Sign extend the addend we extract from the lo insn.  */
	vallo = ((insnlo & 0xffff) ^ 0x8000) - 0x8000;

	if (mf->mips_hi16_list != NULL)
	  {
	    struct mips_hi16 *l;

	    l = mf->mips_hi16_list;
	    while (l != NULL)
	      {
		struct mips_hi16 *next;
		unsigned long insn;

		/* The value for the HI16 had best be the same. */
		assert(v == l->value);

		/* Do the HI16 relocation.  Note that we actually don't
		   need to know anything about the LO16 itself, except where
		   to find the low 16 bits of the addend needed by the LO16.  */
		insn = *l->addr;
		val = ((insn & 0xffff) << 16) + vallo;
		val += v;

		/* Account for the sign extension that will happen in the
		   low bits.  */
		val = ((val >> 16) + ((val & 0x8000) != 0)) & 0xffff;

		insn = (insn &~ 0xffff) | val;
		*l->addr = insn;

		next = l->next;
		free(l);
		l = next;
	      }

	    mf->mips_hi16_list = NULL;
	  }

	/* Ok, we're done with the HI16 relocs.  Now deal with the LO16.  */
	val = v + vallo;
	insnlo = (insnlo & ~0xffff) | (val & 0xffff);
	*loc = insnlo;
	break;
      }

    default:
      ret = obj_reloc_unhandled;
      break;
    }

  return ret;
}

int
arch_create_got (struct obj_file *f)
{
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
    unsigned tgt_long __start___dbe_table;
    unsigned tgt_long __stop___dbe_table;
  } *ad;
  struct obj_section *sec;

  if (archdata_sec->contents)
    free(archdata_sec->contents);
  archdata_sec->header.sh_size = 0;
  sec = obj_find_section(f, "__dbe_table");
  if (sec) {
    ad = (struct archdata *) (archdata_sec->contents) = xmalloc(sizeof(*ad));
    memset(ad, 0, sizeof(*ad));
    archdata_sec->header.sh_size = sizeof(*ad);
    ad->__start___dbe_table = sec->header.sh_addr;
    ad->__stop___dbe_table = sec->header.sh_addr + sec->header.sh_size;
  }

  return 0;
}
