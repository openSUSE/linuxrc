/* m68k specific support for Elf loading and relocation.
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

#include <stddef.h>
#include <module.h>
#include <obj.h>
#include <util.h>


/*======================================================================*/

struct obj_file *
arch_new_file (void)
{
  return xmalloc(sizeof(struct obj_file));
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
  char *loc = targsec->contents + rel->r_offset;
  Elf32_Addr dot = targsec->header.sh_addr + rel->r_offset;

  enum obj_reloc ret = obj_reloc_ok;

  switch (ELF32_R_TYPE(rel->r_info))
    {
    case R_68K_NONE:
      break;

    case R_68K_8:
      if (v > 0xff)
	ret = obj_reloc_overflow;
      *(char *)loc = v;
      break;
    case R_68K_16:
      if (v > 0xffff)
	ret = obj_reloc_overflow;
      *(short *)loc = v;
      break;
    case R_68K_32:
      *(int *)loc = v;
      break;

    case R_68K_PC8:
      v -= dot;
      if ((Elf32_Sword)v > 0x7f || (Elf32_Sword)v < -(Elf32_Sword)0x80)
	ret = obj_reloc_overflow;
      *(char *)loc = v;
      break;
    case R_68K_PC16:
      v -= dot;
      if ((Elf32_Sword)v > 0x7fff || (Elf32_Sword)v < -(Elf32_Sword)0x8000)
	ret = obj_reloc_overflow;
      *(short *)loc = v;
      break;
    case R_68K_PC32:
      *(int *)loc = v - dot;
      break;

    case R_68K_RELATIVE:
      *(int *)loc += ef->baseaddr;
      break;

    default:
      ret = obj_reloc_unhandled;
      break;
    }

  return ret;
}

int
arch_create_got (struct obj_file *ef)
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
arch_archdata (struct obj_file *fin, struct obj_section *sec)
{
  return 0;
}
