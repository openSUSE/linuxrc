/* Elf file reader.
   Copyright 1996, 1997 Linux International.

   Contributed by Richard Henderson <rth@tamu.edu>
   obj_free() added by Björn Ekwall <bj0rn@blox.se> March 1999
   Support for kallsyms Keith Owens <kaos@ocs.com.au> April 2000

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

#include <alloca.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#include "obj.h"
#include "util.h"

/*======================================================================*/

struct obj_file *
obj_load (int fp, Elf32_Half e_type, const char *filename)
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

  gzf_lseek(fp, 0, SEEK_SET);
  if (gzf_read(fp, &f->header, sizeof(f->header)) != sizeof(f->header))
    {
      error("cannot read ELF header from %s", filename);
      return NULL;
    }

  if (f->header.e_ident[EI_MAG0] != ELFMAG0
      || f->header.e_ident[EI_MAG1] != ELFMAG1
      || f->header.e_ident[EI_MAG2] != ELFMAG2
      || f->header.e_ident[EI_MAG3] != ELFMAG3)
    {
      error("%s is not an ELF file", filename);
      return NULL;
    }
  if (f->header.e_ident[EI_CLASS] != ELFCLASSM
      || f->header.e_ident[EI_DATA] != ELFDATAM
      || f->header.e_ident[EI_VERSION] != EV_CURRENT
      || !MATCH_MACHINE(f->header.e_machine))
    {
      error("ELF file %s not for this architecture", filename);
      return NULL;
    }
  if (f->header.e_type != e_type && e_type != ET_NONE)
    {
      switch (e_type) {
      case ET_REL:
	error("ELF file %s not a relocatable object", filename);
	break;
      case ET_EXEC:
	error("ELF file %s not an executable object", filename);
	break;
      default:
	error("ELF file %s has wrong type, expecting %d got %d",
		filename, e_type, f->header.e_type);
	break;
      }
      return NULL;
    }

  /* Read the section headers.  */

  if (f->header.e_shentsize != sizeof(ElfW(Shdr)))
    {
      error("section header size mismatch %s: %lu != %lu",
	    filename,
	    (unsigned long)f->header.e_shentsize,
	    (unsigned long)sizeof(ElfW(Shdr)));
      return NULL;
    }

  shnum = f->header.e_shnum;
  f->sections = xmalloc(sizeof(struct obj_section *) * shnum);
  memset(f->sections, 0, sizeof(struct obj_section *) * shnum);

  section_headers = alloca(sizeof(ElfW(Shdr)) * shnum);
  gzf_lseek(fp, f->header.e_shoff, SEEK_SET);
  if (gzf_read(fp, section_headers, sizeof(ElfW(Shdr))*shnum) != sizeof(ElfW(Shdr))*shnum)
    {
      error("error reading ELF section headers %s: %m", filename);
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
	      gzf_lseek(fp, sec->header.sh_offset, SEEK_SET);
	      if (gzf_read(fp, sec->contents, sec->header.sh_size) != sec->header.sh_size)
		{
		  error("error reading ELF section data %s: %m", filename);
		  return NULL;
		}
	    }
	  else
	    sec->contents = NULL;
	  break;

#if SHT_RELM == SHT_REL
	case SHT_RELA:
	  if (sec->header.sh_size) {
	    error("RELA relocations not supported on this architecture %s", filename);
	    return NULL;
	  }
	  break;
#else
	case SHT_REL:
	  if (sec->header.sh_size) {
	    error("REL relocations not supported on this architecture %s", filename);
	    return NULL;
	  }
	  break;
#endif

	default:
	  if (sec->header.sh_type >= SHT_LOPROC)
	    {
	      if (arch_load_proc_section(sec, fp) < 0)
		return NULL;
	      break;
	    }

	  error("can't handle sections of type %ld %s",
		(long)sec->header.sh_type, filename);
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

      /* .modinfo and .modstring should be contents only but gcc has no
       *  attribute for that.  The kernel may have marked these sections as
       *  ALLOC, ignore the allocate bit.
       */
      if (strcmp(sec->name, ".modinfo") == 0 ||
	  strcmp(sec->name, ".modstring") == 0)
	sec->header.sh_flags &= ~SHF_ALLOC;

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
		error("symbol size mismatch %s: %lu != %lu",
		      filename,
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
	}
    }

  /* second pass to add relocation data to symbols */
  for (i = 0; i < shnum; ++i)
    {
      struct obj_section *sec = f->sections[i];
      switch (sec->header.sh_type)
	{
	case SHT_RELM:
	  {
	    unsigned long nrel, j, nsyms;
	    ElfW(RelM) *rel;
	    struct obj_section *symtab;
	    char *strtab;
	    if (sec->header.sh_entsize != sizeof(ElfW(RelM)))
	      {
		error("relocation entry size mismatch %s: %lu != %lu",
		      filename,
		      (unsigned long)sec->header.sh_entsize,
		      (unsigned long)sizeof(ElfW(RelM)));
		return NULL;
	      }

	    nrel = sec->header.sh_size / sizeof(ElfW(RelM));
	    rel = (ElfW(RelM) *) sec->contents;
	    symtab = f->sections[sec->header.sh_link];
	    nsyms = symtab->header.sh_size / symtab->header.sh_entsize;
	    strtab = f->sections[symtab->header.sh_link]->contents;

	    /* Save the relocate type in each symbol entry.  */
	    for (j = 0; j < nrel; ++j, ++rel)
	      {
		struct obj_symbol *intsym;
		unsigned long symndx;
		symndx = ELFW(R_SYM)(rel->r_info);
		if (symndx)
		  {
		    if (symndx >= nsyms)
		      {
			error("%s: Bad symbol index: %08lx >= %08lx",
			      filename, symndx, nsyms);
			continue;
		      }

		    obj_find_relsym(intsym, f, f, rel, (ElfW(Sym) *)(symtab->contents), strtab);
		    intsym->r_type = ELFW(R_TYPE)(rel->r_info);
		  }
	      }
	  }
	  break;
	}
    }

  f->filename = xstrdup(filename);

  return f;
}

void obj_free(struct obj_file *f)
{
	struct obj_section *sec;
	struct obj_symbol *sym;
	struct obj_symbol *next;
	int i;
	int n;

	if (f->sections) {
		n = f->header.e_shnum;
		for (i = 0; i < n; ++i) {
			if ((sec = f->sections[i]) != NULL) {
				if (sec->contents)
					free(sec->contents);
				free(sec);
			}
		}
		free(f->sections);
	}

	for (i = 0; i < HASH_BUCKETS; ++i) {
		for (sym = f->symtab[i]; sym; sym = next) {
			next = sym->next;
			free(sym);
		}
	}

	if (f->local_symtab)
		free(f->local_symtab);

	if (f->filename)
		free((char *)(f->filename));

	if (f->persist)
		free((char *)(f->persist));

	free(f);
}
