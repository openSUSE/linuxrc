/* Elf object file loading and relocation routines.
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


#ifndef MODUTILS_OBJ_H
#define MODUTILS_OBJ_H 1

#ident "$Id: obj.h,v 1.1 1999/12/14 12:38:12 snwint Exp $"

/* The relocatable object is manipulated using elfin types.  */

#include <stdio.h>
#include <elf.h>
#include ELF_MACHINE_H

#ifndef ElfW
# if ELFCLASSM == ELFCLASS32
#  define ElfW(x)  Elf32_ ## x
#  define ELFW(x)  ELF32_ ## x
# else
#  define ElfW(x)  Elf64_ ## x
#  define ELFW(x)  ELF64_ ## x
# endif
#endif

/* For some reason this is missing from libc5.  */
#ifndef ELF32_ST_INFO
# define ELF32_ST_INFO(bind, type)       (((bind) << 4) + ((type) & 0xf))
#endif

#ifndef ELF64_ST_INFO
# define ELF64_ST_INFO(bind, type)       (((bind) << 4) + ((type) & 0xf))
#endif

struct obj_string_patch;
struct obj_symbol_patch;

struct obj_section
{
  ElfW(Shdr) header;
  const char *name;
  char *contents;
  struct obj_section *load_next;
  int idx;
};

struct obj_symbol
{
  struct obj_symbol *next;	/* hash table link */
  const char *name;
  unsigned long value;
  unsigned long size;
  int secidx;			/* the defining section index/module */
  int info;
  int ksymidx;			/* for export to the kernel symtab */
  int referenced;		/* actually used in the link */
};

/* Hardcode the hash table size.  We shouldn't be needing so many
   symbols that we begin to degrade performance, and we get a big win
   by giving the compiler a constant divisor.  */

#define HASH_BUCKETS  521

struct obj_file
{
  ElfW(Ehdr) header;
  ElfW(Addr) baseaddr;
  struct obj_section **sections;
  struct obj_section *load_order;
  struct obj_section **load_order_search_start;
  struct obj_string_patch *string_patches;
  struct obj_symbol_patch *symbol_patches;
  int (*symbol_cmp)(const char *, const char *);
  unsigned long (*symbol_hash)(const char *);
  unsigned long local_symtab_size;
  struct obj_symbol **local_symtab;
  struct obj_symbol *symtab[HASH_BUCKETS];
};

enum obj_reloc
{
  obj_reloc_ok,
  obj_reloc_overflow,
  obj_reloc_dangerous,
  obj_reloc_unhandled
};

struct obj_string_patch
{
  struct obj_string_patch *next;
  int reloc_secidx;
  ElfW(Addr) reloc_offset;
  ElfW(Addr) string_offset;
};

struct obj_symbol_patch
{
  struct obj_symbol_patch *next;
  int reloc_secidx;
  ElfW(Addr) reloc_offset;
  struct obj_symbol *sym;
};


/* Generic object manipulation routines.  */

unsigned long obj_elf_hash(const char *);

unsigned long obj_elf_hash_n(const char *, unsigned long len);

struct obj_symbol *obj_add_symbol (struct obj_file *f, const char *name,
				   unsigned long symidx, int info, int secidx,
				   ElfW(Addr) value, unsigned long size);

struct obj_symbol *obj_find_symbol (struct obj_file *f,
					 const char *name);

ElfW(Addr) obj_symbol_final_value(struct obj_file *f,
				  struct obj_symbol *sym);

void obj_set_symbol_compare(struct obj_file *f,
			    int (*cmp)(const char *, const char *),
			    unsigned long (*hash)(const char *));

struct obj_section *obj_find_section (struct obj_file *f,
					   const char *name);

void obj_insert_section_load_order (struct obj_file *f,
				    struct obj_section *sec);

struct obj_section *obj_create_alloced_section (struct obj_file *f,
						const char *name,
						unsigned long align,
						unsigned long size);

struct obj_section *obj_create_alloced_section_first (struct obj_file *f,
						      const char *name,
						      unsigned long align,
						      unsigned long size);

void *obj_extend_section (struct obj_section *sec, unsigned long more);

int obj_string_patch(struct obj_file *f, int secidx, ElfW(Addr) offset,
		     const char *string);

int obj_symbol_patch(struct obj_file *f, int secidx, ElfW(Addr) offset,
		     struct obj_symbol *sym);

int obj_check_undefineds(struct obj_file *f);

void obj_allocate_commons(struct obj_file *f);

unsigned long obj_load_size (struct obj_file *f);

int obj_relocate (struct obj_file *f, ElfW(Addr) base);

struct obj_file *obj_load(FILE *f);

int obj_create_image (struct obj_file *f, char *image);

/* Architecture specific manipulation routines.  */

struct obj_file *arch_new_file (void);

struct obj_section *arch_new_section (void);

struct obj_symbol *arch_new_symbol (void);

enum obj_reloc arch_apply_relocation (struct obj_file *f,
				      struct obj_section *targsec,
				      struct obj_section *symsec,
				      struct obj_symbol *sym,
				      ElfW(RelM) *rel, ElfW(Addr) value);

int arch_create_got (struct obj_file *f);

struct new_module;
int arch_init_module (struct obj_file *f, struct new_module *);

#endif /* obj.h */
