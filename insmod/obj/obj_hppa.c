/*
 * PA-RISC specific support for Elf loading and relocation.
 * Copyright 2000 David Huggins-Daines <dhd@linuxcare.com>, Linuxcare Inc.
 * Copyright 2000 Richard Hirst <rhirst@linuxcare.com>, Linuxcare Inc.
 *
 * Based on the IA-64 support, which is:
 * Copyright 2000 Mike Stephens <mike.stephens@intel.com>
 *
 * This file is part of the Linux modutils.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include <module.h>
#include <obj.h>
#include <util.h>
#include <modstat.h>	/* For ksyms */


typedef struct _hppa_stub_t
{
  struct _hppa_stub_t *next;
  int offset;
  int reloc_done;
} hppa_stub_t;

typedef struct _hppa_symbol_t
{
  struct obj_symbol root;
  hppa_stub_t *stub;
} hppa_symbol_t;

typedef struct _hppa_file_t
{
  struct obj_file root;
  struct obj_section *stub;
  Elf32_Addr dp;
} hppa_file_t;

/* The ABI defines various more esoteric types, but these are the only
   ones we actually need. */
enum hppa_fsel
{
  e_fsel,
  e_lsel,
  e_rsel,
  e_lrsel,
  e_rrsel
};

struct obj_file *
arch_new_file (void)
{
  hppa_file_t *f;
  f = xmalloc(sizeof(*f));
  f->stub = NULL;
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
  hppa_symbol_t *sym;
  sym = xmalloc(sizeof(*sym));
  sym->stub = NULL;
  return &sym->root;
}

/* This is called for architecture specific sections we might need to
   do special things to. */
int
arch_load_proc_section(struct obj_section *sec, int fp)
{
  /* Assume it's just a debugging section that we can safely
     ignore ...  */
  sec->contents = NULL;

  return 0;
}

/* =================================================================

   These functions are from libhppa.h in the GNU BFD library.
   (c) 1990, 91, 92, 93, 94, 95, 96, 98, 99, 2000
   Free Software Foundation, Inc.

   ================================================================= */

/* The *sign_extend functions are used to assemble various bitfields
   taken from an instruction and return the resulting immediate
   value.  */

static inline int
sign_extend (x, len)
     int x, len;
{
  int signbit = (1 << (len - 1));
  int mask = (signbit << 1) - 1;
  return ((x & mask) ^ signbit) - signbit;
}

static inline int
low_sign_extend (x, len)
     int x, len;
{
  return (x >> 1) - ((x & 1) << (len - 1));
}


/* The re_assemble_* functions prepare an immediate value for
   insertion into an opcode. pa-risc uses all sorts of weird bitfields
   in the instruction to hold the value.  */

static inline int
sign_unext (x, len)
     int x, len;
{
  int len_ones;

  len_ones = (1 << len) - 1;

  return x & len_ones;
}

static inline int
low_sign_unext (x, len)
     int x, len;
{
  int temp;
  int sign;

  sign = (x >> (len-1)) & 1;

  temp = sign_unext (x, len-1);

  return (temp << 1) | sign;
}

static inline int
re_assemble_3 (as3)
     int as3;
{
  return ((  (as3 & 4) << (13-2))
	  | ((as3 & 3) << (13+1)));
}

static inline int
re_assemble_12 (as12)
     int as12;
{
  return ((  (as12 & 0x800) >> 11)
	  | ((as12 & 0x400) >> (10 - 2))
	  | ((as12 & 0x3ff) << (1 + 2)));
}

static inline int
re_assemble_14 (as14)
     int as14;
{
  return ((  (as14 & 0x1fff) << 1)
	  | ((as14 & 0x2000) >> 13));
}

static inline int
re_assemble_16 (as16)
     int as16;
{
  int s, t;

  /* Unusual 16-bit encoding, for wide mode only.  */
  t = (as16 << 1) & 0xffff;
  s = (as16 & 0x8000);
  return (t ^ s ^ (s >> 1)) | (s >> 15);
}

static inline int
re_assemble_17 (as17)
     int as17;
{
  return ((  (as17 & 0x10000) >> 16)
	  | ((as17 & 0x0f800) << (16 - 11))
	  | ((as17 & 0x00400) >> (10 - 2))
	  | ((as17 & 0x003ff) << (1 + 2)));
}

static inline int
re_assemble_21 (as21)
     int as21;
{
  return ((  (as21 & 0x100000) >> 20)
	  | ((as21 & 0x0ffe00) >> 8)
	  | ((as21 & 0x000180) << 7)
	  | ((as21 & 0x00007c) << 14)
	  | ((as21 & 0x000003) << 12));
}

static inline int
re_assemble_22 (as22)
     int as22;
{
  return ((  (as22 & 0x200000) >> 21)
	  | ((as22 & 0x1f0000) << (21 - 16))
	  | ((as22 & 0x00f800) << (16 - 11))
	  | ((as22 & 0x000400) >> (10 - 2))
	  | ((as22 & 0x0003ff) << (1 + 2)));
}


/* Handle field selectors for PA instructions.
   The L and R (and LS, RS etc.) selectors are used in pairs to form a
   full 32 bit address.  eg.

   LDIL	L'start,%r1		; put left part into r1
   LDW	R'start(%r1),%r2	; add r1 and right part to form address

   This function returns sign extended values in all cases.
*/

static inline unsigned int
hppa_field_adjust (value, addend, r_field)
     unsigned int value;
     int addend;
     enum hppa_fsel r_field;
{
  unsigned int sym_val;

  sym_val = value - addend;
  switch (r_field)
    {
    case e_fsel:
      /* F: No change.  */
      break;

    case e_lsel:
      /* L:  Select top 21 bits.  */
      value = value >> 11;
      break;

    case e_rsel:
      /* R:  Select bottom 11 bits.  */
      value = value & 0x7ff;
      break;

    case e_lrsel:
      /* LR:  L with rounding of the addend to nearest 8k.  */
      value = sym_val + ((addend + 0x1000) & -0x2000);
      value = value >> 11;
      break;

    case e_rrsel:
      /* RR:  R with rounding of the addend to nearest 8k.
	 We need to return a value such that 2048 * LR'x + RR'x == x
	 ie. RR'x = s+a - (s + (((a + 0x1000) & -0x2000) & -0x800))
	 .	  = s+a - ((s & -0x800) + ((a + 0x1000) & -0x2000))
	 .	  = (s & 0x7ff) + a - ((a + 0x1000) & -0x2000)  */
      value = (sym_val & 0x7ff) + (((addend & 0x1fff) ^ 0x1000) - 0x1000);
      break;

    default:
      abort();
    }
  return value;
}

/* Insert VALUE into INSN using R_FORMAT to determine exactly what
   bits to change.  */

static inline int
hppa_rebuild_insn (insn, value, r_format)
     int insn;
     int value;
     int r_format;
{
  switch (r_format)
    {
    case 11:
      return (insn & ~ 0x7ff) | low_sign_unext (value, 11);

    case 12:
      return (insn & ~ 0x1ffd) | re_assemble_12 (value);


    case 10:
      return (insn & ~ 0x3ff1) | re_assemble_14 (value & -8);

    case -11:
      return (insn & ~ 0x3ff9) | re_assemble_14 (value & -4);

    case 14:
      return (insn & ~ 0x3fff) | re_assemble_14 (value);


    case -10:
      return (insn & ~ 0xfff1) | re_assemble_16 (value & -8);

    case -16:
      return (insn & ~ 0xfff9) | re_assemble_16 (value & -4);

    case 16:
      return (insn & ~ 0xffff) | re_assemble_16 (value);


    case 17:
      return (insn & ~ 0x1f1ffd) | re_assemble_17 (value);

    case 21:
      return (insn & ~ 0x1fffff) | re_assemble_21 (value);

    case 22:
      return (insn & ~ 0x3ff1ffd) | re_assemble_22 (value);

    case 32:
      return value;

    default:
      abort ();
    }
  return insn;
}

/* ====================================================================

   End of functions from GNU BFD.

   ==================================================================== */

/* This is where we get the opportunity to create any extra dynamic
   sections we might need.  In our case we do not need a GOT because
   our code is not PIC, but we do need to create a stub section.

   This is significantly less complex than what we do for shared
   libraries because, obviously, modules are not shared.  Also we have
   no issues related to symbol visibility, lazy linking, etc.
   The kernels dp is fixed (at symbol $global$), and we can fix up any
   DPREL refs in the module to use that same dp value.
   All PCREL17F refs result in a stub with the following format:

  	ldil L'func_addr,%r1
        be,n R'func_addr(%sr4,%r1)

  Note, all PCREL17F get a stub, regardless of whether they are
  local or external.  With local ones, and external ones to other
  modules, there is a good chance we could manage without the stub.
  I'll leave that for a future optimisation.
 */

#define LDIL_R1		0x20200000	/* ldil  L'XXX,%r1		*/
#define BE_N_SR4_R1	0xe0202002	/* be,n  R'XXX(%sr4,%r1)	*/

#define STUB_SIZE 8

int
arch_create_got(struct obj_file *f)
{
  hppa_file_t *hfile = (hppa_file_t *)f;
  int i, n;
  int stub_offset = 0;

  /* Create stub section.
   * XXX set flags, see obj_ia64.c
   */
  hfile->stub = obj_create_alloced_section(f, ".stub", STUB_SIZE,
					   0, SHF_WRITE);

  /* Actually this is a lot like check_relocs() in a BFD backend.  We
     walk all sections and all their relocations and look for ones
     that need special treatment. */
  n = hfile->root.header.e_shnum;
  for (i = 0; i < n; ++i)
    {
      struct obj_section *relsec, *symsec, *strsec;
      Elf32_Rela *rel, *relend;
      Elf32_Sym *symtab;
      char const *strtab;

      relsec = hfile->root.sections[i];
      if (relsec->header.sh_type != SHT_RELA)
	continue;

      symsec = hfile->root.sections[relsec->header.sh_link];
      strsec = hfile->root.sections[symsec->header.sh_link];

      rel = (Elf32_Rela *)relsec->contents;
      relend = rel + (relsec->header.sh_size / sizeof(Elf32_Rela));
      symtab = (Elf32_Sym *)symsec->contents;
      strtab = (char const *)strsec->contents;

      for (; rel < relend; rel++)
	{
	  int need_stub = 0;

	  switch (ELF32_R_TYPE(rel->r_info))
	    {
	    default:
	      continue;

	    case R_PARISC_PCREL17F:
	      need_stub = 1;
	      break;
	    }

	  if (need_stub)
	    {
	      hppa_symbol_t *hsym;
	      int local;

	      obj_find_relsym(hsym, f, f, rel, symtab, strtab);
	      local = hsym->root.secidx <= SHN_HIRESERVE;

	      if (need_stub)
		{
		  hppa_stub_t *stub;

		  if (hsym->stub == NULL)
		    {
		      stub = (hppa_stub_t *) xmalloc(sizeof(hppa_stub_t));
		      stub->offset = stub_offset;
		      stub->reloc_done = 0;
		      hsym->stub = stub;
		      stub_offset += STUB_SIZE;
		      need_stub = 0;
		    }
		}
	    }
        }
    }
  if (stub_offset)
    {
      hfile->stub->contents = xmalloc(stub_offset);
      hfile->stub->header.sh_size = stub_offset;
    }
  return 1;
}


enum obj_reloc
arch_apply_relocation(struct obj_file *f,
		      struct obj_section *targsec,
		      struct obj_section *symsec,
		      struct obj_symbol *sym,
		      Elf32_Rela *rel,
		      Elf32_Addr v)
{
  hppa_file_t *hfile = (hppa_file_t *) f;
  hppa_symbol_t *hsym  = (hppa_symbol_t *) sym;

  Elf32_Addr *loc = (Elf32_Addr *)(targsec->contents + rel->r_offset);
  Elf32_Addr dot = (targsec->header.sh_addr + rel->r_offset) & ~0x03;
  Elf32_Addr dp = hfile->dp;
  Elf32_Word r_info = ELF32_R_TYPE(rel->r_info);

  enum obj_reloc ret = obj_reloc_ok;
  enum hppa_fsel fsel = e_fsel;	/* Avoid compiler warning */
  unsigned int r_format;

  /* Fix up the value, and determine whether we can handle this
     relocation. */
  switch (r_info)
  {
  case R_PARISC_PLABEL32:
  case R_PARISC_DIR32:
  case R_PARISC_DIR21L:
  case R_PARISC_DIR14R:
    /* Easy. */
    break;

  case R_PARISC_SEGREL32:
    v -= f->baseaddr;
    break;

  case R_PARISC_DPREL21L:
  case R_PARISC_DPREL14R:
    v -= dp;
    break;

  case R_PARISC_PCREL17F:
    /* Find an import stub. */
    assert(hsym->stub != NULL);
    assert(hfile->stub != NULL);
    /* XXX Optimise.  We may not need a stub for short branches */
    if (!hsym->stub->reloc_done) {
      /* Need to create the .stub entry */
      Elf32_Addr *pstub, stubv;

      pstub = (Elf32_Addr *)(hfile->stub->contents + hsym->stub->offset);
      pstub[0] = LDIL_R1;
      pstub[1] = BE_N_SR4_R1;
      stubv = hppa_field_adjust(v, rel->r_addend, e_lrsel);
      pstub[0] = hppa_rebuild_insn(pstub[0], stubv, 21);
      stubv = hppa_field_adjust(v, rel->r_addend, e_rrsel);
      stubv >>= 2;	/* Branch; divide by 4 */
      pstub[1] = hppa_rebuild_insn(pstub[1], stubv, 17);
      hsym->stub->reloc_done = 1;
    }
    v = hsym->stub->offset + hfile->stub->header.sh_addr;
    break;

  default:
    return obj_reloc_unhandled;
  }

  /* Find the field selector. */
  switch (r_info)
    {
    case R_PARISC_DIR32:
    case R_PARISC_PLABEL32:
    case R_PARISC_PCREL17F:
    case R_PARISC_SEGREL32:
      fsel = e_fsel;
      break;

    case R_PARISC_DPREL21L:
    case R_PARISC_DIR21L:
      fsel = e_lrsel;
      break;

    case R_PARISC_DPREL14R:
    case R_PARISC_DIR14R:
      fsel = e_rrsel;
      break;
    }

  v = hppa_field_adjust(v, rel->r_addend, fsel);

  switch (r_info)
    {
    case R_PARISC_PCREL17F:
    case R_PARISC_PCREL17R:
    case R_PARISC_PCREL22F:
      v = v - dot - 8;
    case R_PARISC_DIR17F:
    case R_PARISC_DIR17R:
      /* This is a branch.  Divide the offset by four. */
      v >>= 2;
      break;
    default:
      break;
    }

  /* Find the format. */
  switch (r_info)
    {
    case R_PARISC_DIR32:
    case R_PARISC_PLABEL32:
    case R_PARISC_SEGREL32:
      r_format = 32;
      break;

    case R_PARISC_DPREL21L:
    case R_PARISC_DIR21L:
      r_format = 21;
      break;

    case R_PARISC_PCREL17F:
      r_format = 17;
      break;
	
    case R_PARISC_DPREL14R:
    case R_PARISC_DIR14R:
      r_format = 14;
      break;

    default:
      abort();
    }

  *loc = hppa_rebuild_insn(*loc, v, r_format);

  return ret;
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
arch_archdata (struct obj_file *f, struct obj_section *sec)
{
  struct module_symbol *s;
  int i;
  hppa_file_t *hfile = (hppa_file_t *)f;

  /* Initialise dp to the kernels dp (symbol $global$)
   */
  for (i = 0, s = ksyms; i < nksyms; i++, s++)
    if (!strcmp((char *)s->name, "$global$"))
      break;
  if (i >= nksyms) {
    error("Cannot initialise dp, '$global$' not found\n");
    return 1;
  }
  hfile->dp = s->value;

  return 0;
}

