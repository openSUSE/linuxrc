/* Elf file reader.
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

#ident "$Id: obj_load.c,v 1.1 1999/12/14 12:38:12 snwint Exp $"

#include <alloca.h>
#include <string.h>

#include "obj.h"
#include "util.h"

/*======================================================================*/

struct obj_file *
obj_load (FILE *fp)
{
  struct obj_file *f;
  ElfW(Shdr) *section_headers;
  int shnum, i;
  char *shstrtab;

  /* Read the file header.  */

  f = arch_new_file();
  memset(f, 0, sizeof(*f));
  f->symbol_cmp = strcmp;
  f->symbol_hash = obj_elf_hash;
  f->load_order_search_start = &f->load_order;

  fseek(fp, 0, SEEK_SET);
  if (fread(&f->header, sizeof(f->header), 1, fp) != 1)
    {
      error("error reading ELF header: %m");
      return NULL;
    }

  if (f->header.e_ident[EI_MAG0] != ELFMAG0
      || f->header.e_ident[EI_MAG1] != ELFMAG1
      || f->header.e_ident[EI_MAG2] != ELFMAG2
      || f->header.e_ident[EI_MAG3] != ELFMAG3)
    {
      error("not an ELF file");
      return NULL;
    }
  if (f->header.e_ident[EI_CLASS] != ELFCLASSM
      || f->header.e_ident[EI_DATA] != ELFDATAM
      || f->header.e_ident[EI_VERSION] != EV_CURRENT
      || !MATCH_MACHINE(f->header.e_machine))
    {
      error("ELF file not for this architecture");
      return NULL;
    }
  if (f->header.e_type != ET_REL)
    {
      error("ELF file not a relocatable object");
      return NULL;
    }

  /* Read the section headers.  */

  if (f->header.e_shentsize != sizeof(ElfW(Shdr)))
    {
      error("section header size mismatch: %lu != %lu",
	    (unsigned long)f->header.e_shentsize,
	    (unsigned long)sizeof(ElfW(Shdr)));
      return NULL;
    }

  shnum = f->header.e_shnum;
  f->sections = xmalloc(sizeof(struct obj_section *) * shnum);
  memset(f->sections, 0, sizeof(struct obj_section *) * shnum);

  section_headers = alloca(sizeof(ElfW(Shdr)) * shnum);
  fseek(fp, f->header.e_shoff, SEEK_SET);
  if (fread(section_headers, sizeof(ElfW(Shdr)), shnum, fp) != shnum)
    {
      error("error reading ELF section headers: %m");
      return NULL;
    }

  /* Read the section data.  */

  for (i = 0; i < shnum; ++i)
    {
      struct obj_section *sec;

      f->sections[i] = sec = arch_new_section();
      memset(sec, 0, sizeof(*sec));

      sec->header = section_headers[i];
      sec->idx = i;

      switch (sec->header.sh_type)
 	{
	case SHT_NULL:
	case SHT_NOTE:
	case SHT_NOBITS:
	  /* ignore */
	  break;

	case SHT_PROGBITS:
	case SHT_SYMTAB:
	case SHT_STRTAB:
	case SHT_RELM:
	  if (sec->header.sh_size > 0)
	    {
	      sec->contents = xmalloc(sec->header.sh_size);
	      fseek(fp, sec->header.sh_offset, SEEK_SET);
	      if (fread(sec->contents, sec->header.sh_size, 1, fp) != 1)
	        {
	          error("error reading ELF section data: %m");
	          return NULL;
	        }
	    }
	  else
	    sec->contents = NULL;
	  break;

#if SHT_RELM == SHT_REL
	case SHT_RELA:
	  error("RELA relocations not supported on this architecture");
	  return NULL;
#else
	case SHT_REL:
	  error("REL relocations not supported on this architecture");
	  return NULL;
#endif

	default:
	  if (sec->header.sh_type >= SHT_LOPROC)
	    {
	      /* Assume processor specific section types are debug
		 info and can safely be ignored.  If this is ever not
		 the case (Hello MIPS?), don't put ifdefs here but
		 create an arch_load_proc_section().  */
	      break;
	    }

	  error("can't handle sections of type %ld",
		(long)sec->header.sh_type);
	  return NULL;
	}
    }

  /* Do what sort of interpretation as needed by each section.  */

  shstrtab = f->sections[f->header.e_shstrndx]->contents;

  for (i = 0; i < shnum; ++i)
    {
      struct obj_section *sec = f->sections[i];
      sec->name = shstrtab + sec->header.sh_name;
    }

  for (i = 0; i < shnum; ++i)
    {
      struct obj_section *sec = f->sections[i];

      if (sec->header.sh_flags & SHF_ALLOC)
	obj_insert_section_load_order(f, sec);

      switch (sec->header.sh_type)
	{
	case SHT_SYMTAB:
	  {
	    unsigned long nsym, j;
	    char *strtab;
	    ElfW(Sym) *sym;

	    if (sec->header.sh_entsize != sizeof(ElfW(Sym)))
	      {
		error("symbol size mismatch: %lu != %lu",
		      (unsigned long)sec->header.sh_entsize,
		      (unsigned long)sizeof(ElfW(Sym)));
		return NULL;
	      }

	    nsym = sec->header.sh_size / sizeof(ElfW(Sym));
	    strtab = f->sections[sec->header.sh_link]->contents;
	    sym = (ElfW(Sym) *) sec->contents;

	    /* Allocate space for a table of local symbols.  */
	    j = f->local_symtab_size = sec->header.sh_info;
	    f->local_symtab = xmalloc(j *= sizeof(struct obj_symbol *));
	    memset(f->local_symtab, 0, j);

	    /* Insert all symbols into the hash table.  */
	    for (j = 1, ++sym; j < nsym; ++j, ++sym)
	      {
		const char *name;
		if (sym->st_name)
		  name = strtab+sym->st_name;
		else
		  name = f->sections[sym->st_shndx]->name;

		obj_add_symbol(f, name, j, sym->st_info, sym->st_shndx,
			       sym->st_value, sym->st_size);

	      }
	  }
	break;

	case SHT_RELM:
	  if (sec->header.sh_entsize != sizeof(ElfW(RelM)))
	    {
	      error("relocation entry size mismatch: %lu != %lu",
		    (unsigned long)sec->header.sh_entsize,
		    (unsigned long)sizeof(ElfW(RelM)));
	      return NULL;
	    }
	  break;
	}
    }

  return f;
}
