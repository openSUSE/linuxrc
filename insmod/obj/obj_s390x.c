/* IBM zSeries 64-bit specific support for Elf loading and relocation.
   Copyright 2000, 2001 IBM Deutschland Entwicklung GmbH, IBM Corporation.

   Contributed by Martin Schwidefsky <schwidefsky@de.ibm.com>

   Derived from obj/obj_i386.c:
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


/*======================================================================*/

struct s390_plt_entry
{
  long offset;
  int  allocated:1;
  int  initialized:1;
};

struct s390_got_entry
{
  long offset;
  unsigned allocated:1;
  unsigned reloc_done : 1;
};

struct s390_file
{
  struct obj_file root;
  struct obj_section *plt;
  struct obj_section *got;
};

struct s390_symbol
{
  struct obj_symbol root;
  struct s390_plt_entry pltent;
  struct s390_got_entry gotent;
};


/*======================================================================*/

struct obj_file *
arch_new_file (void)
{
  struct s390_file *f;
  f = xmalloc(sizeof(*f));
  f->got = NULL;
  f->plt = NULL;
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
                      Elf64_Rela *rel,
                      Elf64_Addr v)
{
  struct s390_file *ifile = (struct s390_file *) f;
  struct s390_symbol *isym  = (struct s390_symbol *) sym;
  struct s390_plt_entry *pe;

  Elf64_Addr *loc = (Elf64_Addr *)(targsec->contents + rel->r_offset);
  Elf64_Addr dot = targsec->header.sh_addr + rel->r_offset;
  Elf64_Addr got = ifile->got ? ifile->got->header.sh_addr : 0;
  Elf64_Addr plt = ifile->plt ? ifile->plt->header.sh_addr : 0;

  enum obj_reloc ret = obj_reloc_ok;

  switch (ELF64_R_TYPE(rel->r_info))
    {
    case R_390_NONE:
      break;

    case R_390_64:
      *loc += v;
      break;
    case R_390_32:
      *(unsigned int *) loc += v;
      break;
    case R_390_16:
      *(unsigned short *) loc += v;
      break;
    case R_390_8:
      *(unsigned char *) loc += v;
      break;

    case R_390_PC64:
      *loc += v - dot;
      break;
    case R_390_PC32:
      *(unsigned int *) loc += v - dot;
      break;
    case R_390_PC32DBL:
      *(unsigned int *) loc += (v - dot) >> 1;
      break;
    case R_390_PC16DBL:
      *(unsigned short *) loc += (v - dot) >> 1;
      break;
    case R_390_PC16: 
      *(unsigned short *) loc += v - dot;
      break;

    case R_390_PLT64:
    case R_390_PLT32DBL:
    case R_390_PLT32:
    case R_390_PLT16DBL:
      /* find the plt entry and initialize it.  */
      assert(isym != NULL);
      pe = (struct s390_plt_entry *) &isym->pltent;
      assert(pe->allocated);
      if (pe->initialized == 0) {
        unsigned int *ip = (unsigned int *)(ifile->plt->contents + pe->offset); 
        ip[0] = 0x0d10e310; /* basr 1,0; lg 1,10(1); br 1 */
        ip[1] = 0x100a0004;
        ip[2] = 0x07f10000;
       if (ELF64_R_TYPE(rel->r_info) == R_390_PLT32DBL ||
           ELF64_R_TYPE(rel->r_info) == R_390_PLT16DBL) {
         ip[3] = (unsigned int) ((v - 2) >> 32);
         ip[4] = (unsigned int) (v - 2);
       } else {
         ip[3] = (unsigned int) (v >> 32);
         ip[4] = (unsigned int) v;
       }
        pe->initialized = 1;
      }

      /* Insert relative distance to target.  */
      v = plt + pe->offset - dot;
      if (ELF64_R_TYPE(rel->r_info) == R_390_PLT64)
        *(unsigned long *) loc = v;
      else if (ELF64_R_TYPE(rel->r_info) == R_390_PLT32DBL)
        *(unsigned int *) loc = (unsigned int) ((v + 2) >> 1);
      else if (ELF64_R_TYPE(rel->r_info) == R_390_PLT32)
        *(unsigned int *) loc = (unsigned int) v;
      else if (ELF64_R_TYPE(rel->r_info) == R_390_PLT16DBL)
        *(unsigned short *) loc = (unsigned short) ((v + 2) >> 1);
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
      *(unsigned long *) loc += got - dot;
      break;
    case R_390_GOTPCDBL:
      assert(got != 0);
      *(unsigned int *) loc += (got + 2 - dot) >> 1;
      break;

    case R_390_GOT12:
    case R_390_GOT16:
    case R_390_GOT32:
    case R_390_GOT64:
    case R_390_GOTENT:
      assert(isym != NULL);
      assert(got != 0);
      if (!isym->gotent.reloc_done)
       {
         isym->gotent.reloc_done = 1;
         *(Elf64_Addr *)(ifile->got->contents + isym->gotent.offset) = v;
       }
      if (ELF64_R_TYPE(rel->r_info) == R_390_GOT12)
        *(unsigned short *) loc |= (*(unsigned short *) loc + isym->gotent.offset) & 0xfff;
      else if (ELF64_R_TYPE(rel->r_info) == R_390_GOT16)
        *(unsigned short *) loc += isym->gotent.offset;
      else if (ELF64_R_TYPE(rel->r_info) == R_390_GOT32)
        *(unsigned int *) loc += isym->gotent.offset;
      else if (ELF64_R_TYPE(rel->r_info) == R_390_GOT64)
        *loc += isym->gotent.offset;
      else if (ELF64_R_TYPE(rel->r_info) == R_390_GOTENT)
        *(unsigned int *) loc += isym->gotent.offset >> 1;
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
  struct s390_file *ifile = (struct s390_file *) f;
  int i, got_offset = 0, plt_offset = 0, gotneeded = 0;

  for (i = 0; i < f->header.e_shnum; ++i)
    {
      struct obj_section *relsec, *symsec, *strsec;
      Elf64_Rela *rel, *relend;
      Elf64_Sym *symtab;
      const char *strtab;

      relsec = f->sections[i];
      if (relsec->header.sh_type != SHT_RELA)
       continue;

      symsec = f->sections[relsec->header.sh_link];
      strsec = f->sections[symsec->header.sh_link];

      rel = (Elf64_Rela *)relsec->contents;
      relend = rel + (relsec->header.sh_size / sizeof(Elf64_Rela));
      symtab = (Elf64_Sym *)symsec->contents;
      strtab = (const char *)strsec->contents;

      for (; rel < relend; ++rel)
       {
         struct s390_symbol *intsym;
          struct s390_plt_entry *pe;
          struct s390_got_entry *ge;

         switch (ELF64_R_TYPE(rel->r_info)) {
            /* These four relocations refer to a plt entry.  */
            case R_390_PLT16DBL:
            case R_390_PLT32:
            case R_390_PLT32DBL:
            case R_390_PLT64:
	      obj_find_relsym(intsym, f, f, rel, symtab, strtab);
              assert(intsym);
              pe = &intsym->pltent;
              if (!pe->allocated) {
                pe->allocated = 1;
                pe->offset = plt_offset;
                plt_offset += 20;
              }
              break;
            /* The next three don't need got entries but the address
               of the got itself.  */
           case R_390_GOTPC:
            case R_390_GOTPCDBL:
           case R_390_GOTOFF:
             gotneeded = 1;
              break;

            case R_390_GOT12:
            case R_390_GOT16:
           case R_390_GOT32:
            case R_390_GOT64:
            case R_390_GOTENT:
	      obj_find_relsym(intsym, f, f, rel, symtab, strtab);
             assert(intsym);
              ge = (struct s390_got_entry *) &intsym->gotent;
              if (!ge->allocated) {
                ge->allocated = 1;
                ge->offset = got_offset;
                got_offset += sizeof(void*);
              }
             break;

            default:
              break;
           }
       }
    }

  if (got_offset > 0 || gotneeded) {
    struct obj_section *gotsec;
    struct obj_symbol *gotsym;

    gotsec = obj_find_section(f, ".got");
    if (gotsec == NULL)
      gotsec = obj_create_alloced_section(f, ".got", 8, got_offset, SHF_WRITE);
    else
      obj_extend_section(gotsec, got_offset);
    gotsym = obj_add_symbol(f, "_GLOBAL_OFFSET_TABLE_", -1,
                            ELFW(ST_INFO) (STB_LOCAL, STT_OBJECT),
                            gotsec->idx, 0, 0);
    gotsym->secidx = gotsec->idx;  /* mark the symbol as defined */
    ifile->got = gotsec;
  }

  if (plt_offset > 0)
    ifile->plt = obj_create_alloced_section(f, ".plt", 8, plt_offset,
					    SHF_WRITE);

  return 1;
}

int
arch_init_module (struct obj_file *f, struct module *mod)
{
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

