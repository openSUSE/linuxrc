/* Powerpc64 specific support for Elf loading and relocation.
   Copyright 2001 Alan Modra <amodra@bigpond.net.au>, IBM.

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
#include <stdlib.h>

#include <module.h>
#include <obj.h>
#include <util.h>
#include <modstat.h>	/* For ksyms */

#define DEBUG_PPC 0

typedef struct _ppc64_got_t
{
  struct _ppc64_got_t *next;
  Elf64_Addr addend;
  unsigned int offset: 16;
  int reloc_done: 1;
} ppc64_got_t;

typedef struct _ppc64_symbol_t
{
  struct obj_symbol root;
  struct _ppc64_symbol_t *opd_sym;
  ppc64_got_t *got_ent;
} ppc64_symbol_t;

typedef struct _ppc64_file_t
{
  struct obj_file root;
  struct obj_section *toc;
  struct obj_section *stub;
  char *got;			/* where we start adding to .toc */
  Elf64_Addr my_tocbase;	/* 0x8000 into .toc */
  Elf64_Addr kernel_base;
  Elf64_Addr module_base;
} ppc64_file_t;

/* Stub for calling a function in the kernel or another module, from a
   module.  The first insn is patched to load the function .opd entry
   address.  */

static const unsigned char ppc64_stub[] =
{
  0xe9, 0x82, 0x00, 0x00,	/* ld	  %r12,0(%r2) */
  0xf8, 0x41, 0x00, 0x28,	/* std	  %r2,40(%r1) */
  0xe8, 0x0c, 0x00, 0x00,	/* ld	  %r0,0(%r12) */
  0xe8, 0x4c, 0x00, 0x08,	/* ld	  %r2,8(%r12) */
  0x7c, 0x09, 0x03, 0xa6,	/* mtctr  %r0	      */
  0x4e, 0x80, 0x04, 0x20,	/* bctr		      */
};

#define SIZEOF_STUB	(sizeof (ppc64_stub))

struct obj_file *
arch_new_file (void)
{
  ppc64_file_t *f;
  f = xmalloc (sizeof (*f));
  f->toc = NULL;
  f->stub = NULL;
  f->got = NULL;
  f->my_tocbase = 0;

  /* Kernel sits at 0xC000...  Modules sit at 0xD000...  */
  f->kernel_base = (Elf64_Addr) 0xC << (15*4);
  f->module_base = (Elf64_Addr) 0xD << (15*4);
  return &f->root;
}

struct obj_section *
arch_new_section (void)
{
  return xmalloc (sizeof (struct obj_section));
}

struct obj_symbol *
arch_new_symbol (void)
{
  ppc64_symbol_t *sym;
  sym = xmalloc (sizeof (*sym));
  sym->opd_sym = NULL;
  sym->got_ent = NULL;
  return &sym->root;
}

int
arch_load_proc_section (struct obj_section *sec, int fp)
{
  /* Assume it's just a debugging section that we can safely
     ignore ...  */
  sec->contents = NULL;

  return 0;
}

Elf64_Addr
ppc64_module_base (struct obj_file *f)
{
  return ((ppc64_file_t *) f)->module_base;
}

static inline Elf64_Addr
ppc64_adjust_symval (ppc64_file_t *f, ppc64_symbol_t *sym, Elf64_Addr v)
{
  /* The kernel doesn't return high 32 bits of sym values as we are
     using 32 bit userspace.  Adjust sym values as appropriate.  We
     "or" in the high bits rather than adding so this code will still
     work if we eventually have 64 bit userspace.  */

  if (sym->root.secidx == SHN_HIRESERVE + 1)
    {
      /* It's a kernel sym.  */
      v |= f->kernel_base;
    }
  else if (sym->root.secidx > SHN_HIRESERVE + 1)
    {
      /* Sym from another module.  */
      v |= f->module_base;
    }
  return v;
}

int
ppc64_process_syms (struct obj_file *f)
{
  unsigned long i;
  ppc64_symbol_t *sym;
  ppc64_file_t *ef = (ppc64_file_t *) f;

  if (DEBUG_PPC > 0)
    fprintf (stderr, "ppc64_process_syms\n");
  for (i = 0; i < HASH_BUCKETS; ++i)
    for (sym = (ppc64_symbol_t *) f->symtab[i];
	 sym != NULL;
	 sym = (ppc64_symbol_t *) sym->root.next)
      {
	/* The kernel doesn't export function code syms (which all
	   start with `.'). Instead you get the .opd section
	   function descriptor.  Tie the module reference to the
	   code sym used in a "bl" instruction, to the opd sym.  */
	if (sym->root.name[0] == '.'
	    && sym->root.secidx == SHN_UNDEF)
	  {
	    sym->opd_sym = ((ppc64_symbol_t *)
			    obj_find_symbol (f, sym->root.name + 1));
	    if (sym->opd_sym != NULL)
	      {
		if (sym->opd_sym->root.secidx < SHN_HIRESERVE + 1)
		  {
		    /* Whoa.  Don't satisfy with a sym from this
		       module.  */
		    sym->opd_sym = NULL;
		  }
		else
		  {
		    /* It's not really defined here.  This is just to
		       prevent undefined sym errors.  */
		    sym->root.secidx = sym->opd_sym->root.secidx;
		  }
	      }
	    else if (DEBUG_PPC > 0)
	      fprintf (stderr, "can't find opd symbol for %s\n",
		       sym->root.name);
	  }
      }

  sym = (ppc64_symbol_t *) obj_find_symbol (f, "__kernel_base_hi");
  if (sym != NULL && sym->root.secidx != SHN_UNDEF)
    ef->kernel_base = (Elf64_Addr) sym->root.value << 32;

  sym = (ppc64_symbol_t *) obj_find_symbol (f, "__kernel_base_lo");
  if (sym != NULL && sym->root.secidx != SHN_UNDEF)
    {
      ef->kernel_base &= ~(Elf64_Addr) 0xffffffff;
      ef->kernel_base |= (Elf64_Addr) sym->root.value & 0xffffffff;
    }

  sym = (ppc64_symbol_t *) obj_find_symbol (f, "__module_base_hi");
  if (sym != NULL && sym->root.secidx != SHN_UNDEF)
    ef->module_base = (Elf64_Addr) sym->root.value << 32;

  sym = (ppc64_symbol_t *) obj_find_symbol (f, "__module_base_lo");
  if (sym != NULL && sym->root.secidx != SHN_UNDEF)
    {
      ef->module_base &= ~(Elf64_Addr) 0xffffffff;
      ef->module_base |= (Elf64_Addr) sym->root.value & 0xffffffff;
    }

  return 1;
}

int
arch_create_got (struct obj_file *f)
{
  ppc64_file_t *hfile = (ppc64_file_t *) f;
  int i;
  int n;
  unsigned int got_offset;

  got_offset = 0;

  if (DEBUG_PPC > 0)
    fprintf (stderr, "arch_create_got\n");

  n = hfile->root.header.e_shnum;
  for (i = 0; i < n; ++i)
    {
      struct obj_section *relsec, *symsec, *strsec;
      Elf64_Rela *rel, *relend;
      Elf64_Sym *symtab;
      const char *strtab;

      relsec = hfile->root.sections[i];
      if (relsec->header.sh_type != SHT_RELA)
	continue;

      symsec = hfile->root.sections[relsec->header.sh_link];
      strsec = hfile->root.sections[symsec->header.sh_link];
      symtab = (Elf64_Sym *) symsec->contents;
      strtab = (const char *) strsec->contents;

      rel = (Elf64_Rela *) relsec->contents;
      relend = rel + relsec->header.sh_size / sizeof (Elf64_Rela);
      for (; rel < relend; ++rel)
	{
	  switch (ELF64_R_TYPE (rel->r_info))
	    {
	    default:
	      {
		unsigned r_info = ELF64_R_TYPE (rel->r_info);
		error ("r_info 0x%x not handled\n", r_info);
	      }
	      continue;

	    case R_PPC64_ADDR16_HA:
	    case R_PPC64_ADDR16_HI:
	    case R_PPC64_ADDR16_LO:
	    case R_PPC64_ADDR16_LO_DS:
	    case R_PPC64_ADDR64:
	    case R_PPC64_TOC:
	    case R_PPC64_TOC16:
	    case R_PPC64_TOC16_DS:
	      continue;

	    case R_PPC64_REL24:
	      {
		Elf64_Sym *extsym;
		ppc64_symbol_t *isym;
		const char *name;
		unsigned long symndx;
		ppc64_got_t *got;

		symndx = ELF64_R_SYM (rel->r_info);
		extsym = &symtab[symndx];
		if (ELF64_ST_BIND (extsym->st_info) == STB_LOCAL)
		  {
		    isym = (ppc64_symbol_t *) f->local_symtab[symndx];
		  }
		else
		  {
		    if (extsym->st_name)
		      name = strtab + extsym->st_name;
		    else
		      name = f->sections[extsym->st_shndx]->name;
		    isym = (ppc64_symbol_t *) obj_find_symbol (f, name);

		    if (DEBUG_PPC > 1)
		      fprintf (stderr,
			       "create_got %s + 0x%llx opd_sym=%p secidx=%x\n",
			       name, rel->r_addend, isym->opd_sym,
			       isym->opd_sym ? isym->opd_sym->root.secidx : 0);

		    if (isym->opd_sym != NULL
			&& isym->opd_sym->root.secidx != SHN_UNDEF)
		      {
			for (got = isym->got_ent; got != NULL; got = got->next)
			  if (got->addend == rel->r_addend)
			    break;
			if (got == NULL)
			  {
			    if (DEBUG_PPC > 1)
			      fprintf (stderr, "making got\n");
			    got = (ppc64_got_t *) xmalloc (sizeof (ppc64_got_t));
			    got->next = isym->got_ent;
			    got->addend = rel->r_addend;
			    got->offset = got_offset;
			    got->reloc_done = 0;
			    isym->got_ent = got;
			    got_offset += 8;
			  }
			else if (DEBUG_PPC > 1)
			  fprintf (stderr, "got a got\n");
		      }
		  }
	      }
	    }
	}
    }

  hfile->toc = obj_find_section (f, ".toc");

  if (got_offset != 0)
    {
      unsigned int stub_size;

      if (hfile->toc)
	{
	  if (hfile->toc->header.sh_size + got_offset > 0xffff)
	    {
	      error (".toc section overflow (%lu)\n",
		     (unsigned long) hfile->toc->header.sh_size + got_offset);
	      return 0;
	    }
	  hfile->got = obj_extend_section (hfile->toc, got_offset);
	}
      else
	{
	  if (got_offset > 0xffff)
	    {
	      error (".toc section overflow (%lu)\n",
		     (unsigned long) got_offset);
	      return 0;
	    }
	  hfile->toc = obj_create_alloced_section (f, ".toc", 8, got_offset,
						   SHF_ALLOC);
	  hfile->got = hfile->toc->contents;
	}

      stub_size = got_offset / 8 * SIZEOF_STUB;
      hfile->stub = obj_create_alloced_section (f, ".stub", 4, stub_size,
						SHF_ALLOC | SHF_EXECINSTR);
    }

  return 1;
}

int
arch_finalize_section_address (struct obj_file *f, Elf64_Addr base)
{
  ppc64_file_t *hfile = (ppc64_file_t *) f;
  int n = f->header.e_shnum;
  int i;

  f->baseaddr = base;
  for (i = 0; i < n; ++i)
    f->sections[i]->header.sh_addr += base;

  if (hfile->toc)
    {
      hfile->my_tocbase = hfile->toc->header.sh_addr + 0x8000;
      if (DEBUG_PPC > 0)
	fprintf (stderr, "hfile->my_tocbase = %llx\n", hfile->my_tocbase);
    }

  return 1;
}

static inline int apply_addr16_lo (char *loc, Elf64_Addr v)
{
  *((Elf64_Half *) loc) = (*((Elf64_Half *) loc) & ~0xffff) | (v & 0xffff);
  return 0;
}

static inline int apply_addr16_lo_ds (char *loc, Elf64_Addr v)
{
  *((Elf64_Half *) loc) = (*((Elf64_Half *) loc) & ~0xfffc) | (v & 0xfffc);
  return (v & 3) != 0;
}

static inline int apply_addr16_hi (char *loc, Elf64_Addr v)
{
  return apply_addr16_lo (loc, v >> 16);
}

static inline int apply_addr16_ha (char *loc, Elf64_Addr v)
{
  return apply_addr16_hi (loc, v + 0x8000);
}

static inline int apply_toc16 (char *loc, Elf64_Addr v)
{
  return apply_addr16_lo (loc, v) || v + 0x8000 > 0xffff;
}

static inline int apply_toc16_ds (char *loc, Elf64_Addr v)
{
  return apply_addr16_lo_ds (loc, v) || v + 0x8000 > 0xffff;
}

enum obj_reloc
arch_apply_relocation (struct obj_file *f,
		       struct obj_section *targsec,
		       struct obj_section *symsec,
		       struct obj_symbol *sym,
		       Elf64_Rela *rel,
		       Elf64_Addr v)
{
  ppc64_file_t *hfile = (ppc64_file_t *) f;
  ppc64_symbol_t *isym = (ppc64_symbol_t *) sym;
  ppc64_got_t *ge;
  char *loc = targsec->contents + rel->r_offset;
  Elf64_Addr dot;
  Elf64_Xword r_info = ELF64_R_TYPE (rel->r_info);
  enum obj_reloc ret = obj_reloc_ok;

  switch (r_info)
    {
    default:
      ret = obj_reloc_unhandled;
      break;

    case R_PPC64_ADDR16_HA:
      if (apply_addr16_ha (loc, v))
	ret = obj_reloc_overflow;
      break;

    case R_PPC64_ADDR16_HI:
      if (apply_addr16_hi (loc, v))
	ret = obj_reloc_overflow;
      break;

    case R_PPC64_ADDR16_LO:
      if (apply_addr16_lo (loc, v))
	ret = obj_reloc_overflow;
      break;

    case R_PPC64_ADDR16_LO_DS:
      if (apply_addr16_lo_ds (loc, v))
	ret = obj_reloc_overflow;
      break;

    case R_PPC64_ADDR64:
      v = ppc64_adjust_symval (hfile, isym, v);
      *((Elf64_Xword *) loc) = v;
      break;

    case R_PPC64_TOC:
      *((Elf64_Xword *) loc) = hfile->my_tocbase;
      break;

    case R_PPC64_TOC16_DS:
      v -= hfile->my_tocbase;
      if (apply_toc16_ds (loc, v))
	ret = obj_reloc_overflow;
      break;

    case R_PPC64_TOC16:
      v -= hfile->my_tocbase;
      if (apply_toc16 (loc, v))
	ret = obj_reloc_overflow;
      break;

    case R_PPC64_REL24:
      assert(isym != NULL);
      if (DEBUG_PPC > 1)
	fprintf (stderr, "ppc_rel24 %s got=%p addend=%llx\n",
		 isym->root.name, isym->got_ent, rel->r_addend);
      for (ge = isym->got_ent; ge != NULL; ge = ge->next)
	{
	  if (DEBUG_PPC > 1)
	    fprintf (stderr, "ge = %p addend=%llx\n", ge, ge->addend);
	  if (ge->addend == rel->r_addend)
	    break;
	}

      if (ge)
	{
	  int stub_offset = ge->offset / 8 * SIZEOF_STUB;
	  if (!ge->reloc_done)
	    {
	      char *stub_loc;
	      Elf64_Addr *opd_ptr;
	      Elf64_Addr toc_offset;

	      ge->reloc_done = 1;

	      /* Set up the toc entry that points at the function opd.  */
	      v = isym->opd_sym->root.value;
	      v = ppc64_adjust_symval (hfile, isym->opd_sym, v);
	      opd_ptr = (Elf64_Addr *) (hfile->got + ge->offset);
	      *opd_ptr = v;

	      /* Copy our call stub.  */
	      stub_loc = hfile->stub->contents + stub_offset;
	      if (DEBUG_PPC > 1)
		fprintf (stderr, "stub: %p\n", stub_loc);
	      memcpy (stub_loc, ppc64_stub, SIZEOF_STUB);

	      /* And patch in the correct offset.  */
	      toc_offset = (char *) opd_ptr - hfile->toc->contents - 0x8000;
	      if (apply_toc16_ds (stub_loc + 2, toc_offset))
		ret = obj_reloc_overflow;
	    }

	  /* Point the branch insn at the stub.  */
	  v = hfile->stub->header.sh_addr + stub_offset;
	}
      else if (DEBUG_PPC > 1)
	fprintf (stderr, "hi im here %s\n", isym->root.name);

      /* Relocate branch insn.  */
      dot = targsec->header.sh_addr + rel->r_offset;
      v -= dot;
      if (v + 0x2000000 > 0x3ffffff || (v & 3) != 0)
	{
	  if (DEBUG_PPC > 0)
	    fprintf (stderr, "rel24 overflow\n");
	  ret = obj_reloc_overflow;
	}
      *((Elf64_Word *) loc) = ((*((Elf64_Word *) loc) & ~0x3fffffc)
			       | (v & 0x3fffffc));

      if (ge)
	{
	  Elf64_Word next_insn = 0;

	  /* If the next insn is a nop (ori r0,r0,0 or cror 15,15,15
	     or cror 31,31,31), patch it to restore the module r2.  */
	  if (rel->r_offset + 7 < targsec->header.sh_size
	      && ((next_insn = *((Elf64_Word *) loc + 1)) == 0x60000000
		  || next_insn == 0x4def7b82
		  || next_insn == 0x4ffffb82))
	    {
	      *((Elf64_Word *) loc + 1) = 0xe8410028;	/* ld r2,40(r1) */
	    }
	  else if (ret == obj_reloc_ok
		   && next_insn != 0xe8410028)
	    {
	      ret = obj_reloc_dangerous;
	    }
	}
    }

  return ret;
}

int
arch_init_module (struct obj_file *f, struct module *mod)
{
  return 1;
}

int
arch_archdata (struct obj_file *f, struct obj_section *archdata_sec)
{
  return 0;
}
