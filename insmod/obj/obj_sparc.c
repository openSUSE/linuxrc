/* Sparc specific support for Elf loading and relocation.
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
  Elf32_Addr *loc = (Elf32_Addr *)(targsec->contents + rel->r_offset);
  Elf32_Addr dot = targsec->header.sh_addr + rel->r_offset;

  enum obj_reloc ret = obj_reloc_ok;

  switch (ELF32_R_TYPE(rel->r_info))
    {
    case R_SPARC_NONE:
      break;
    case R_SPARC_8:
      if (v > 0xff)
	ret = obj_reloc_overflow;
      *loc = (*loc & ~0xff) | (v & 0xff);
      break;
    case R_SPARC_16:
      if (v > 0xffff)
	ret = obj_reloc_overflow;
      *loc = (*loc & ~0xffff) | (v & 0xffff);
      break;
    case R_SPARC_32:
      *loc = v;
      break;
    case R_SPARC_DISP8:
      v -= dot;
      if (v > 0xff)
        ret = obj_reloc_overflow;
      *loc = (*loc & ~0xff) | (v & 0xff);
      break;
    case R_SPARC_DISP16:
      v -= dot;
      if (v > 0xffff)
	ret = obj_reloc_overflow;
      *loc = (*loc & ~0xffff) | (v & 0xffff);
      break;
    case R_SPARC_DISP32:
      v -= dot;
      *loc = v;
      break;
    case R_SPARC_WDISP30:
      v -= dot;
      if (v % 4)
	ret = obj_reloc_dangerous;
      *loc = (*loc & ~0x3fffffff) | (v >> 2);
      break;
    case R_SPARC_WDISP22:
      v -= dot;
      if (v % 4)
	ret = obj_reloc_dangerous;
      *loc = (*loc & ~0x3fffff) | ((v >> 2) & 0x3fffff);
      break;
    case R_SPARC_HI22:
      *loc = (*loc & ~0x3fffff) | (v >> 10);
      break;
    case R_SPARC_22:
      if (v > 0x3fffff)
	ret = obj_reloc_overflow;
      *loc = (*loc & ~0x3fffff) | (v & 0x3fffff);
      break;
    case R_SPARC_13:
      if (v > 0x1fff)
	ret = obj_reloc_overflow;
      *loc = (*loc & ~0x1fff) | (v & 0x1fff);
      break;
    case R_SPARC_LO10:
      *loc = (*loc & ~0x3ff) | (v & 0x3ff);
      break;

    case R_SPARC_PC10:
      v -= dot;
      *loc = (*loc & ~0x3ff) | (v & 0x3ff);
      break;
    case R_SPARC_PC22:
      v -= dot;
      *loc = (*loc & ~0x3fffff) | (v >> 10);
      break;

    case R_SPARC_UA32:
      *(((char *)loc) + 0) = (char)(v >> 24);
      *(((char *)loc) + 1) = (char)(v >> 16);
      *(((char *)loc) + 2) = (char)(v >> 8);
      *(((char *)loc) + 3) = (char)v;
      break;

#ifdef R_SPARC_10
    case R_SPARC_10:
      if (v > 0x3ff)
	ret = obj_reloc_overflow;
      *loc = (*loc & ~0x3ff) | (v & 0x3ff);
      break;
    case R_SPARC_11:
      if (v > 0x7ff)
	ret = obj_reloc_overflow;
      *loc = (*loc & ~0x7ff) | (v & 0x7ff);
      break;
    case R_SPARC_WDISP16:
      v -= dot;
      if (v % 4)
	ret = obj_reloc_dangerous;
      *loc = (*loc & ~0x303fff) | ((v << 4) & 0x300000) | ((v >> 2) & 0x3fff);
      break;
    case R_SPARC_WDISP19:
      v -= dot;
      if (v % 4)
	ret = obj_reloc_dangerous;
      *loc = (*loc & ~0x7ffff) | ((v >> 2) & 0x7ffff);
      break;
    case R_SPARC_7:
      if (v > 0x7f)
	ret = obj_reloc_overflow;
      *loc = (*loc & ~0x7f) | (v & 0x7f);
      break;
    case R_SPARC_5:
      if (v > 0x1f)
	ret = obj_reloc_overflow;
      *loc = (*loc & ~0x1f) | (v & 0x1f);
      break;
    case R_SPARC_6:
      if (v > 0x3f)
	ret = obj_reloc_overflow;
      *loc = (*loc & ~0x3f) | (v & 0x3f);
      break;
#endif /* R_SPARC_10 */

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
