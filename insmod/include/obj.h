#ifndef MODUTILS_OBJ_H
#define MODUTILS_OBJ_H 1

/* Elf object file loading and relocation routines.
   Copyright 1996, 1997 Linux International.

   Contributed by Richard Henderson <rth@tamu.edu>
   obj_free() added by Björn Ekwall <bj0rn@blox.se> March 1999

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


/* The relocatable object is manipulated using elfin types.  */

#include <stdio.h>
#include <sys/types.h>
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

#if defined(COMMON_3264) && defined(ONLY_32)
#  define ObjW(x)  obj32_ ## x
#else
#  if defined(COMMON_3264) && defined(ONLY_64)
#    define ObjW(x)  obj64_ ## x
#  else
#    define ObjW(x)    obj_ ## x
#  endif
#endif

/* For some reason this is missing from lib5.  */
#ifndef ELF32_ST_INFO
# define ELF32_ST_INFO(bind, type)       (((bind) << 4) + ((type) & 0xf))
#endif

#ifndef ELF64_ST_INFO
# define ELF64_ST_INFO(bind, type)       (((bind) << 4) + ((type) & 0xf))
#endif

struct obj_string_patch_struct;
struct obj_symbol_patch_struct;

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
  int r_type;			/* relocation type */
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
  struct obj_string_patch_struct *string_patches;
  struct obj_symbol_patch_struct *symbol_patches;
  int (*symbol_cmp)(const char *, const char *);
  unsigned long (*symbol_hash)(const char *);
  unsigned long local_symtab_size;
  struct obj_symbol **local_symtab;
  struct obj_symbol *symtab[HASH_BUCKETS];
  const char *filename;
  char *persist;
};

enum obj_reloc
{
  obj_reloc_ok,
  obj_reloc_overflow,
  obj_reloc_dangerous,
  obj_reloc_unhandled,
  obj_reloc_constant_gp
};

struct obj_string_patch_struct
{
  struct obj_string_patch_struct *next;
  int reloc_secidx;
  ElfW(Addr) reloc_offset;
  ElfW(Addr) string_offset;
};

struct obj_symbol_patch_struct
{
  struct obj_symbol_patch_struct *next;
  int reloc_secidx;
  ElfW(Addr) reloc_offset;
  struct obj_symbol *sym;
};


/* Generic object manipulation routines.  */

#define obj_elf_hash			ObjW(elf_hash)
#define obj_elf_hash_n			ObjW(elf_hash_n)
#define obj_add_symbol			ObjW(add_symbol)
#define obj_find_symbol			ObjW(find_symbol)
#define obj_symbol_final_value		ObjW(symbol_final_value)
#define obj_set_symbol_compare		ObjW(set_symbol_compare)
#define obj_find_section		ObjW(find_section)
#define obj_insert_section_load_order	ObjW(insert_section_load_order)
#define obj_create_alloced_section	ObjW(create_alloced_section)
#define obj_create_alloced_section_first \
					ObjW(create_alloced_section_first)
#define obj_extend_section		ObjW(extend_section)
#define obj_string_patch		ObjW(string_patch)
#define obj_symbol_patch		ObjW(symbol_patch)
#define obj_check_undefineds		ObjW(check_undefineds)
#define obj_clear_undefineds		ObjW(clear_undefineds)
#define obj_allocate_commons		ObjW(allocate_commons)
#define obj_load_size			ObjW(load_size)
#define obj_relocate			ObjW(relocate)
#define obj_load			ObjW(load)
#define obj_free			ObjW(free)
#define obj_create_image		ObjW(create_image)
#define obj_addr_to_native_ptr		ObjW(addr_to_native_ptr)
#define obj_native_ptr_to_addr		ObjW(native_ptr_to_addr)
#define obj_kallsyms			ObjW(kallsyms)
#define obj_gpl_license			ObjW(gpl_license)
#define arch_new_file			ObjW(arch_new_file)
#define arch_new_section		ObjW(arch_new_section)
#define arch_new_symbol			ObjW(arch_new_symbol)
#define arch_apply_relocation		ObjW(arch_apply_relocation)
#define arch_create_got			ObjW(arch_create_got)
#define arch_init_module		ObjW(arch_init_module)
#define arch_load_proc_section		ObjW(arch_load_proc_section)
#define arch_finalize_section_address	ObjW(arch_finalize_section_address)
#define arch_archdata			ObjW(arch_archdata)

unsigned long obj_elf_hash (const char *);

unsigned long obj_elf_hash_n (const char *, unsigned long len);

struct obj_symbol *obj_add_symbol (struct obj_file *f, const char *name,
				   unsigned long symidx, int info, int secidx,
				   ElfW(Addr) value, unsigned long size);

struct obj_symbol *obj_find_symbol (struct obj_file *f,
					 const char *name);

ElfW(Addr) obj_symbol_final_value (struct obj_file *f,
				  struct obj_symbol *sym);

void obj_set_symbol_compare (struct obj_file *f,
			    int (*cmp)(const char *, const char *),
			    unsigned long (*hash)(const char *));

struct obj_section *obj_find_section (struct obj_file *f,
					   const char *name);

void obj_insert_section_load_order (struct obj_file *f,
				    struct obj_section *sec);

struct obj_section *obj_create_alloced_section (struct obj_file *f,
						const char *name,
						unsigned long align,
						unsigned long size,
						unsigned long flags);

struct obj_section *obj_create_alloced_section_first (struct obj_file *f,
						      const char *name,
						      unsigned long align,
						      unsigned long size);

void *obj_extend_section (struct obj_section *sec, unsigned long more);

int obj_string_patch (struct obj_file *f, int secidx, ElfW(Addr) offset,
		     const char *string);

int obj_symbol_patch (struct obj_file *f, int secidx, ElfW(Addr) offset,
		     struct obj_symbol *sym);

int obj_check_undefineds (struct obj_file *f, int quiet);

void obj_clear_undefineds (struct obj_file *f);

void obj_allocate_commons (struct obj_file *f);

unsigned long obj_load_size (struct obj_file *f);

int obj_relocate (struct obj_file *f, ElfW(Addr) base);

struct obj_file *obj_load (int f, Elf32_Half e_type, const char *filename);

void obj_free (struct obj_file *f);

int obj_create_image (struct obj_file *f, char *image);

int obj_kallsyms (struct obj_file *fin, struct obj_file **fout);

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

struct module;
int arch_init_module (struct obj_file *f, struct module *);

int arch_load_proc_section (struct obj_section *sec, int fp);

int arch_finalize_section_address (struct obj_file *f, ElfW(Addr) base);

int arch_archdata (struct obj_file *fin, struct obj_section *sec);

#define ARCHDATA_SEC_NAME "__archdata"

/* Pointers in objects can be 32 or 64 bit */
union obj_ptr_4 {
	Elf32_Word addr;
	void *ptr;
};
union obj_ptr_8 {
	u_int64_t addr;	/* Should be Elf64_Xword but not all users have this yet */
	void *ptr;
};

void *obj_addr_to_native_ptr(ElfW(Addr));

ElfW(Addr) obj_native_ptr_to_addr(void *);

/* Standard method of finding relocation symbols, sets isym */
#define obj_find_relsym(isym, f, find, rel, symtab, strtab) \
	{ \
		unsigned long symndx = ELFW(R_SYM)((rel)->r_info); \
		ElfW(Sym) *extsym = (symtab)+symndx; \
		if (ELFW(ST_BIND)(extsym->st_info) == STB_LOCAL) { \
			isym = (typeof(isym)) (f)->local_symtab[symndx]; \
		} \
		else { \
			const char *name; \
			if (extsym->st_name) \
				name = (strtab) + extsym->st_name; \
			else \
				name = (f)->sections[extsym->st_shndx]->name; \
			isym = (typeof(isym)) obj_find_symbol((find), name); \
		} \
	}

int obj_gpl_license(struct obj_file *, const char **);

#endif /* obj.h */
