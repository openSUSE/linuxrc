/*
 * ia64 specific support for Elf loading and relocation.
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

#include <string.h>
#include <assert.h>
#include <stdlib.h>

#include <module.h>
#include <obj.h>
#include <util.h>

#ifndef FALSE
#define FALSE 0
#endif
#ifndef TRUE
#define TRUE  ~FALSE
#endif

/*======================================================================*/

typedef struct _ia64_opd_t
{
    int offset;
    int reloc_done;
} ia64_opd_t;

typedef struct _ia64_plt_t
{
    struct _ia64_plt_t *next;
    Elf64_Addr addend;
    int text_offset;
    int data_offset;
    int reloc_done;
} ia64_plt_t;

typedef struct _ia64_got_t
{
    struct _ia64_got_t *next;
    Elf64_Addr addend;
    int offset;
    int reloc_done;
} ia64_got_t;

typedef struct _ia64_symbol_t
{
    struct obj_symbol root;
    ia64_got_t *gotent;
    ia64_opd_t *opdent;
    ia64_plt_t *pltent;
} ia64_symbol_t;

typedef struct _ia64_file_t
{
    struct obj_file root;
    struct obj_section *got;
    struct obj_section *opd;
    struct obj_section *pltt;
    struct obj_section *pltd;
    Elf64_Addr gp;
    Elf64_Addr text;
    Elf64_Addr data;
    Elf64_Addr bss;
} ia64_file_t;

/*
 * aa=gp rel address of the function descriptor in the .IA_64.pltoff section
 */
unsigned char ia64_plt_local[] =
{
    0x0b, 0x78, 0x00, 0x02, 0x00, 0x24, /* [MMI] addl r15=aa,gp;;   */
    0x00, 0x41, 0x3c, 0x30, 0x28, 0xc0, /*       ld8 r16=[r15],8    */
    0x01, 0x08, 0x00, 0x84,             /*       mov r14=gp;;       */
    0x11, 0x08, 0x00, 0x1e, 0x18, 0x10, /* [MIB] ld8 gp=[r15]       */
    0x60, 0x80, 0x04, 0x80, 0x03, 0x00, /*       mov b6=r16         */
    0x60, 0x00, 0x80, 0x00              /*       br.few b6;;        */
};

unsigned char ia64_plt_extern[] =
{
    0x0b, 0x80, 0x00, 0x02, 0x00, 0x24, /* [MMI] addl r16=aa,gp;;   */
    0xf0, 0x00, 0x40, 0x30, 0x20, 0x00, /*       ld8 r15=[r16]      */
    0x00, 0x00, 0x04, 0x00,             /*       nop.i 0x0;;        */
    0x0b, 0x80, 0x20, 0x1e, 0x18, 0x14, /* [MMI] ld8 r16=[r15],8;;  */
    0x10, 0x00, 0x3c, 0x30, 0x20, 0xc0, /*       ld8 gp=[r15]       */
    0x00, 0x09, 0x00, 0x07,             /*       mov b6=r16;;       */
    0x11, 0x00, 0x00, 0x00, 0x01, 0x00, /* [MIB] nop.m 0x0          */
    0x00, 0x00, 0x00, 0x02, 0x00, 0x00, /*       nop.i 0x0          */
    0x60, 0x00, 0x80, 0x00              /*       br.few b6;;        */
};

/*======================================================================*/

/*
 * return the instruction at slot in bundle
 */
Elf64_Xword
obj_ia64_ins_extract_from_bundle(Elf64_Addr *bundle, Elf64_Xword slot)
{
    switch (slot)
    {
    case 0 :
	return (*bundle >> 5) & 0x1ffffffffff;

    case 1 :
	return (((*bundle >> 46) & 0x3ffff) |
	    (*(bundle + 1) << 18)) & 0x1ffffffffff;

    case 2 :
	return (*(bundle + 1) >> 23) & 0x1ffffffffff;

    default:
    }
    return (-1);
}

/*
 * insert a instruction at slot in bundle
 */
void
obj_ia64_ins_insert_in_bundle(Elf64_Addr *bundle, Elf64_Xword slot, Elf64_Xword ins)
{
    Elf64_Xword i;
    Elf64_Xword in = ins & 0x1ffffffffff;

    switch (slot)
    {
    case 0 :
	i = *bundle & 0xffffc0000000001f;
       *bundle = i | (in << 5);
	break;

    case 1 :
	i = *bundle & 0x00003fffffffffff;
	*bundle = i | (in << 46);

	++bundle;
	i = *bundle & 0xffffffffff800000;
	*bundle = i | (in >> 18);
	break;

    case 2 :
	++bundle;
	i = *bundle & 0x00000000007fffff;
	*bundle = i | (in << 23);
	break;
    }
}

/*
 * add a immediate 14 value to the instruction at slot in bundle
 */
enum obj_reloc
obj_ia64_ins_imm14(Elf64_Xword v, Elf64_Addr *bundle, Elf64_Xword slot)
{
    Elf64_Xword ins;

    ins = obj_ia64_ins_extract_from_bundle(bundle, slot);
    ins &= 0xffffffee07f01fff;
    ins |= ((v & 0x2000) << 23) | ((v & 0x1f80) << 20) | ((v & 0x007f) << 13);
    obj_ia64_ins_insert_in_bundle(bundle, slot, ins);
    if (((Elf64_Sxword) v > 8191) || ((Elf64_Sxword) v < -8192))
	return obj_reloc_overflow;
    return obj_reloc_ok;
}

/*
 * add a immediate 22 value to the instruction at slot in bundle
 */
enum obj_reloc
obj_ia64_ins_imm22(Elf64_Xword v, Elf64_Addr *bundle, Elf64_Xword slot)
{
    Elf64_Xword ins;

    ins = obj_ia64_ins_extract_from_bundle(bundle, slot);
    ins &= 0xffffffe000301fff;
    ins |= ((v & 0x200000) << 15) | ((v & 0x1f0000) << 6) |
	   ((v & 0x00ff80) << 20) | ((v & 0x00007f) << 13);
    obj_ia64_ins_insert_in_bundle(bundle, slot, ins);
    if (((Elf64_Sxword) v > 2097151) || ((Elf64_Sxword) v < -2097152))
	return obj_reloc_overflow;
    return obj_reloc_ok;
}

/*
 * add a immediate 21 value (form 1) to the instruction at slot in bundle
 */
enum obj_reloc
obj_ia64_ins_pcrel21b(Elf64_Xword v, Elf64_Addr *bundle, Elf64_Xword slot)
{
    Elf64_Xword ins;

    ins = obj_ia64_ins_extract_from_bundle(bundle, slot);
    ins &= 0xffffffee00001fff;
    ins |= ((v & 0x1000000) << 12) | ((v & 0x0fffff0) << 9);
    obj_ia64_ins_insert_in_bundle(bundle, slot, ins);
    return obj_reloc_ok;
}

/*
 * add a immediate 21 value (form 2) to the instruction at slot in bundle
 */
enum obj_reloc
obj_ia64_ins_pcrel21m(Elf64_Xword v, Elf64_Addr *bundle, Elf64_Xword slot)
{
    Elf64_Xword ins;

    ins = obj_ia64_ins_extract_from_bundle(bundle, slot);
    ins &= 0xffffffee000fe03f;
    ins |= ((v & 0x1000000) << 12) | ((v & 0x0fff800) << 9) |
	   ((v & 0x00007f0) << 2);
    obj_ia64_ins_insert_in_bundle(bundle, slot, ins);
    return obj_reloc_ok;
}

/*
 * add a immediate 21 value (form 3) to the instruction at slot in bundle
 */
enum obj_reloc
obj_ia64_ins_pcrel21f(Elf64_Xword v, Elf64_Addr *bundle, Elf64_Xword slot)
{
    Elf64_Xword ins;

    ins = obj_ia64_ins_extract_from_bundle(bundle, slot);
    ins &= 0xffffffeffc00003f;
    ins |= ((v & 0x1000000) << 12) | ((v & 0x0fffff0) << 2);
    obj_ia64_ins_insert_in_bundle(bundle, slot, ins);
    return obj_reloc_ok;
}

/*
 * add a immediate 64 value to the instruction at slot in bundle
 */
enum obj_reloc
obj_ia64_ins_imm64(Elf64_Xword v, Elf64_Addr *bundle, Elf64_Xword slot)
{
    Elf64_Xword ins;

    assert(slot == 2);

    ins = obj_ia64_ins_extract_from_bundle(bundle, slot);
    ins &= 0xffffffe000101fff;
    ins |= ((v & 0x8000000000000000) >> 27) | ((v & 0x0000000000200000)) |
	   ((v & 0x00000000001f0000) <<  6) | ((v & 0x000000000000ff80) << 20) |
	   ((v & 0x000000000000007f) << 13);
    obj_ia64_ins_insert_in_bundle(bundle, slot, ins);
    obj_ia64_ins_insert_in_bundle(bundle, --slot, ((v & 0x7fffffffffc00000) >> 22));
    return obj_reloc_ok;
}

/*
 * create a plt entry
 */
enum obj_reloc
obj_ia64_generate_plt(Elf64_Addr v,
		       Elf64_Addr gp,
		       ia64_file_t *ifile,
		       ia64_symbol_t *isym,
		       ia64_plt_t *pltent)
{
    *(Elf64_Addr *)(ifile->pltd->contents + pltent->data_offset) = v;
    if (isym->root.secidx <= SHN_HIRESERVE)
    {
	/* local entry */
	*(Elf64_Addr *)(ifile->pltd->contents + pltent->data_offset + 8) = gp;
	memcpy((Elf64_Addr *)(ifile->pltt->contents + pltent->text_offset),
	    ia64_plt_local, sizeof(ia64_plt_local));
    }
    else
    {
	/* external entry */
	memcpy((Elf64_Addr *)(ifile->pltt->contents + pltent->text_offset),
	    ia64_plt_extern, sizeof(ia64_plt_extern));
    }
    return obj_ia64_ins_imm22(
	(ifile->pltd->header.sh_addr + pltent->data_offset - gp),
	(Elf64_Addr *)(ifile->pltt->contents + pltent->text_offset), 0);
}


/*======================================================================*/

struct obj_file *
arch_new_file (void)
{
    ia64_file_t *f;
    f = xmalloc(sizeof(*f));
    f->got = NULL;
    f->opd = NULL;
    f->pltt = NULL;
    f->pltd = NULL;
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
    ia64_symbol_t *sym;
    sym = xmalloc(sizeof(*sym));
    sym->gotent = NULL;
    sym->opdent = NULL;
    sym->pltent = NULL;
    return &sym->root;
}

int
arch_load_proc_section(struct obj_section *sec, int fp)
{
    switch (sec->header.sh_type)
    {
    case SHT_IA_64_EXT :
	sec->contents = NULL;
	break;

    case SHT_IA_64_UNWIND :
	if (sec->header.sh_size > 0)
	{
	    sec->contents = xmalloc(sec->header.sh_size);
	    gzf_lseek(fp, sec->header.sh_offset, SEEK_SET);
	    if (gzf_read(fp, sec->contents, sec->header.sh_size) != sec->header.sh_size)
	    {
		error("error reading ELF section data: %m");
		return -1;
	    }
	}
	else
	    sec->contents = NULL;
	break;

    default:
      error("Unknown section header type: %08x", sec->header.sh_type);
      return -1;
    }
    return 0;
}

int
arch_create_got(struct obj_file *f)
{
    ia64_file_t *ifile = (ia64_file_t *)f;
    int i;
    int n;
    int got_offset = 0;
    int opd_offset = 32;
    int plt_text_offset = 0;
    int plt_data_offset = 0;

    n = ifile->root.header.e_shnum;
    for (i = 0; i < n; ++i)
    {
	struct obj_section *relsec, *symsec, *strsec;
	Elf64_Rela *rel, *relend;
	Elf64_Sym *symtab;
	const char *strtab;

	relsec = ifile->root.sections[i];
	if (relsec->header.sh_type != SHT_RELA)
	    continue;

	symsec = ifile->root.sections[relsec->header.sh_link];
	strsec = ifile->root.sections[symsec->header.sh_link];

	rel = (Elf64_Rela *)relsec->contents;
	relend = rel + (relsec->header.sh_size / sizeof(Elf64_Rela));
	symtab = (Elf64_Sym *)symsec->contents;
	strtab = (const char *)strsec->contents;

	for (; rel < relend; ++rel)
	{
	    int need_got = FALSE;
	    int need_opd = FALSE;
	    int need_plt = FALSE;

	    switch (ELF64_R_TYPE(rel->r_info))
	    {
	    default:
		continue;

	    case R_IA64_FPTR64I :       /* @fptr(sym + add), mov imm64 */
	    case R_IA64_FPTR32LSB :     /* @fptr(sym + add), data4 LSB */
	    case R_IA64_FPTR64LSB :     /* @fptr(sym + add), data8 LSB */
		need_opd = TRUE;
		break;

	    case R_IA64_LTOFF22 :       /* @ltoff(sym + add), add imm22 */
	    case R_IA64_LTOFF22X :
	    case R_IA64_LTOFF64I :      /* @ltoff(sym + add), mov imm64 */
		need_got = TRUE;
		break;

	    case R_IA64_LTOFF_FPTR22 :  /* @ltoff(@fptr(s+a)), imm22 */
	    case R_IA64_LTOFF_FPTR64I : /* @ltoff(@fptr(s+a)), imm64 */
	    case R_IA64_LTOFF_FPTR32LSB :
	    case R_IA64_LTOFF_FPTR64LSB :
		need_got = TRUE;
		need_opd = TRUE;
		break;

	    case R_IA64_PLTOFF22 :      /* @pltoff(sym + add), add imm22 */
	    case R_IA64_PLTOFF64I :     /* @pltoff(sym + add), mov imm64 */
	    case R_IA64_PLTOFF64LSB :   /* @pltoff(sym + add), data8 LSB */

	    case R_IA64_PCREL21B :      /* @pcrel(sym + add), ptb, call */
	    case R_IA64_PCREL21M :      /* @pcrel(sym + add), chk.s */
	    case R_IA64_PCREL21F :      /* @pcrel(sym + add), fchkf */
		need_plt = TRUE;
		break;
	    }

	    if (need_got || need_opd || need_plt)
	    {
		ia64_symbol_t *isym;
		int            local;

		obj_find_relsym(isym, f, f, rel, symtab, strtab);
		local = isym->root.secidx <= SHN_HIRESERVE;

		if (need_plt)
		{
		    ia64_plt_t *plt;

		    for (plt = isym->pltent; plt != NULL; plt = plt->next)
			if (plt->addend == rel->r_addend)
			    break;
		    if (plt == NULL)
		    {
			plt = (ia64_plt_t *) xmalloc(sizeof(ia64_plt_t));
			plt->next = isym->pltent;
			plt->addend = rel->r_addend;
			plt->text_offset = plt_text_offset;
			plt->data_offset = plt_data_offset;
			plt->reloc_done = FALSE;
			isym->pltent = plt;
			if (local)
			{
			    plt_text_offset += sizeof(ia64_plt_local);
			    plt_data_offset += 16;
			}
			else
			{
			    plt_text_offset += sizeof(ia64_plt_extern);
			    plt_data_offset += 8;
			}
			need_plt = FALSE;
		    }
		}
		if (need_got)
		{
		    ia64_got_t *got;

		    for (got = isym->gotent; got != NULL; got = got->next)
			if (got->addend == rel->r_addend)
			    break;
		    if (got == NULL)
		    {
			got = (ia64_got_t *) xmalloc(sizeof(ia64_got_t));
			got->next = isym->gotent;
			got->addend = rel->r_addend;
			got->offset = got_offset;
			got->reloc_done = FALSE;
			isym->gotent = got;
			got_offset += 8;
			need_got = FALSE;
		    }
		}
		if (need_opd && local)
		{
		    ia64_opd_t *opd;

		    if (isym->opdent == NULL)
		    {
			opd = (ia64_opd_t *) xmalloc(sizeof(ia64_opd_t));
			opd->offset = opd_offset;
			opd->reloc_done = FALSE;
			isym->opdent = opd;
			opd_offset += 16;
			need_opd = FALSE;
		    }
		}
	    }
	}
    }

    ifile->got = obj_create_alloced_section(f, ".got", 8, got_offset,
					    SHF_WRITE | SHF_IA_64_SHORT);
    assert(ifile->got != NULL);

    ifile->opd = obj_create_alloced_section(f, ".opd", 16, opd_offset,
					    SHF_WRITE | SHF_IA_64_SHORT);
    assert(ifile->opd != NULL);

    if (plt_text_offset > 0)
    {
	ifile->pltt = obj_create_alloced_section(f, ".plt", 16,
						 plt_text_offset,
						 SHF_WRITE | SHF_IA_64_SHORT);
	ifile->pltd = obj_create_alloced_section(f, ".IA_64.pltoff", 16,
						 plt_data_offset,
						 SHF_WRITE | SHF_IA_64_SHORT);
	assert(ifile->pltt != NULL);
	assert(ifile->pltd != NULL);
    }

    return 1;
}

int
arch_finalize_section_address(struct obj_file *f, Elf64_Addr base)
{
    ia64_file_t *ifile = (ia64_file_t *)f;
    Elf64_Addr min_addr = (Elf64_Addr) -1;
    Elf64_Addr max_addr = 0;
    Elf64_Addr min_short_addr = (Elf64_Addr) -1;
    Elf64_Addr max_short_addr = 0;
    Elf64_Addr gp;
    Elf64_Addr text = (Elf64_Addr) -1;
    Elf64_Addr data = (Elf64_Addr) -1;
    Elf64_Addr bss = (Elf64_Addr) -1;
    int        n = f->header.e_shnum;
    int        i;

    /*
     * Finalize the addresses of the sections, find the min and max
     * address of all sections marked short, and collect min and max
     * address of any type, for use in selecting a nice gp.
     *
     * The algorithm used for selecting set the GP value was taken from
     * the ld/bfd code contributed by David Mosberger-Tang <davidm@hpl.hp.com>
     */
    f->baseaddr = base;
    for (i = 0; i < n; ++i)
    {
	Elf64_Shdr *header = &f->sections[i]->header;
	Elf64_Addr lo;
	Elf64_Addr hi;

	header->sh_addr += base;
	if (header->sh_flags & SHF_ALLOC)
	{
	    lo = header->sh_addr;
	    hi = header->sh_addr + header->sh_size;
	    if (hi < lo)
		hi = (Elf64_Addr) -1;

	    if (min_addr > lo)
		min_addr = lo;
	    if (max_addr < hi)
		max_addr = hi;
	    if (header->sh_flags & SHF_IA_64_SHORT)
	    {
		if (min_short_addr > lo)
		    min_short_addr = lo;
		if (max_short_addr < hi)
		    max_short_addr = hi;
	    }
	    if ((header->sh_type & SHT_NOBITS) && (lo < bss))
		bss = lo;
	    else if ((header->sh_flags & SHF_EXECINSTR) && (lo < text))
		text = lo;
	    else if (lo < data)
		data = lo;
	}
    }
    /* Pick a sensible value for gp */

    /* Start with just the address of the .got */
    gp = ifile->got->header.sh_addr;

    /*
     * If it is possible to address the entire image, but we
     * don't with the choice above, adjust.
     */
    if ((max_addr - min_addr < 0x400000) && (max_addr - gp <= 0x200000) &&
	(gp - min_addr > 0x200000))
    {
	gp = min_addr + 0x200000;
    }
    else if (max_short_addr != 0)
    {
	/* If we don't cover all the short data, adjust */
	if (max_short_addr - gp >= 0x200000)
	    gp = min_short_addr + 0x200000;

	/* If we're addressing stuff past the end, adjust back */
	if (gp > max_addr)
	    gp = max_addr - 0x200000 + 8;
    }

    /*
     * Validate whether all SHF_IA_64_SHORT sections are within
     * range of the chosen GP.
     */
    if (max_short_addr != 0)
    {
	if (max_short_addr - min_short_addr >= 0x400000)
	{
	    error("short data segment overflowed (0x%lx >= 0x400000)",
		(unsigned long)(max_short_addr - min_short_addr));
	    return 0;
	}
	else if (((gp > min_short_addr) && (gp - min_short_addr > 0x200000)) ||
	    ((gp < max_short_addr) && (max_short_addr - gp >= 0x200000)))
	{
	    error("GP does not cover short data segment");
	    return 0;
	}
    }
    ifile->gp = gp;
    ifile->text = text;
    ifile->data = data;
    ifile->bss = bss;
    return 1;
}

/* Targets can be unaligned, use memcpy instead of assignment */
#define COPY_64LSB(loc, v) \
    do { \
    Elf64_Xword reloc = (v); \
    memcpy((void *)(loc), &reloc, 8); \
    } while(0)
#define COPY_32LSB(loc, v) \
    do { \
    Elf32_Xword reloc = (v); \
    memcpy((void *)(loc), &reloc, 4); \
    if ((v) != reloc) \
	ret = obj_reloc_overflow; \
    } while(0)

enum obj_reloc
arch_apply_relocation(struct obj_file *f,
		       struct obj_section *targsec,
		       struct obj_section *symsec,
		       struct obj_symbol *sym,
		       Elf64_Rela *rel,
		       Elf64_Addr v)
{
    ia64_file_t *ifile = (ia64_file_t *) f;
    ia64_symbol_t *isym  = (ia64_symbol_t *) sym;

    Elf64_Addr  loc = (Elf64_Addr)(targsec->contents + rel->r_offset);
    Elf64_Addr  dot = (targsec->header.sh_addr + rel->r_offset) & ~0x03;

    Elf64_Addr  got = ifile->got->header.sh_addr;
    Elf64_Addr  gp = ifile->gp;

    Elf64_Addr *bundle = (Elf64_Addr *)(loc & ~0x03);
    Elf64_Xword slot = loc & 0x03;

    Elf64_Xword r_info = ELF64_R_TYPE(rel->r_info);

    enum obj_reloc ret = obj_reloc_ok;

    /* We cannot load modules compiled with -mconstant-gp */
#ifndef EF_IA_64_CONS_GP
#define EF_IA_64_CONS_GP 0x00000040
#endif
#ifndef EF_IA_64_NOFUNCDESC_CONS_GP
#define EF_IA_64_NOFUNCDESC_CONS_GP 0x00000080
#endif
    if (f->header.e_flags & (EF_IA_64_CONS_GP | EF_IA_64_NOFUNCDESC_CONS_GP))
	return obj_reloc_constant_gp;

    switch (r_info)
    {
    case R_IA64_NONE :          /* none */
    case R_IA64_LDXMOV :        /* Use of LTOFF22X.  */
	break;

    case R_IA64_IMM14 :         /* symbol + addend, add imm14 */
	ret = obj_ia64_ins_imm14(v, bundle, slot);
	break;

    case R_IA64_IMM22 :         /* symbol + addend, add imm22 */
	ret = obj_ia64_ins_imm22(v, bundle, slot);
	break;

    case R_IA64_IMM64 :         /* symbol + addend, movl imm64 */
	ret = obj_ia64_ins_imm64(v, bundle, slot);
	break;

    case R_IA64_DIR32LSB :      /* symbol + addend, data4 LSB */
	COPY_32LSB(loc, v);
	break;

    case R_IA64_DIR64LSB :      /* symbol + addend, data8 LSB */
	COPY_64LSB(loc, v);
	break;

    case R_IA64_GPREL22 :       /* @gprel(sym + add), add imm22 */
	v -= gp;
	ret = obj_ia64_ins_imm22(v, bundle, slot);
	break;

    case R_IA64_GPREL64I :      /* @gprel(sym + add), mov imm64 */
	v -= gp;
	ret = obj_ia64_ins_imm64(v, bundle, slot);
	break;

    case R_IA64_GPREL32LSB :    /* @gprel(sym + add), data4 LSB */
	COPY_32LSB(loc, v-gp);
	break;

    case R_IA64_GPREL64LSB :    /* @gprel(sym + add), data8 LSB */
	COPY_64LSB(loc, v-gp);
	break;

    case R_IA64_LTOFF22 :       /* @ltoff(sym + add), add imm22 */
    case R_IA64_LTOFF22X :      /* LTOFF22, relaxable.  */
    case R_IA64_LTOFF64I :      /* @ltoff(sym + add), mov imm64 */
	{
	    ia64_got_t *ge;

	    assert(isym != NULL);
	    for (ge = isym->gotent; ge != NULL && ge->addend != rel->r_addend; )
		ge = ge->next;
	    assert(ge != NULL);
	    if (!ge->reloc_done)
	    {
		ge->reloc_done = TRUE;
		*(Elf64_Addr *)(ifile->got->contents + ge->offset) = v;
	    }
	    v = got + ge->offset - gp;
	    if (r_info == R_IA64_LTOFF64I)
		ret = obj_ia64_ins_imm64(v, bundle, slot);
	    else
		ret = obj_ia64_ins_imm22(v, bundle, slot);
	}
	break;

    case R_IA64_PLTOFF22 :      /* @pltoff(sym + add), add imm22 */
    case R_IA64_PLTOFF64I :     /* @pltoff(sym + add), mov imm64 */
    case R_IA64_PLTOFF64LSB :   /* @pltoff(sym + add), data8 LSB */
	{
	    ia64_plt_t *pe;

	    assert(isym != NULL);
	    for (pe = isym->pltent; pe != NULL && pe->addend != rel->r_addend; )
		pe = pe->next;
	    assert(pe != NULL);
	    if (!pe->reloc_done)
	    {
		pe->reloc_done = TRUE;
		ret = obj_ia64_generate_plt(v, gp, ifile, isym, pe);
	    }
	    v = ifile->pltt->header.sh_addr + pe->text_offset - gp;
	    switch (r_info)
	    {
	    case R_IA64_PLTOFF22 :
		ret = obj_ia64_ins_imm22(v, bundle, slot);
		break;

	    case R_IA64_PLTOFF64I :
		ret = obj_ia64_ins_imm64(v, bundle, slot);
		break;

	    case R_IA64_PLTOFF64LSB :
		COPY_64LSB(loc, v);
		break;
	    }
	}
	break;

    case R_IA64_FPTR64I :       /* @fptr(sym + add), mov imm64 */
    case R_IA64_FPTR32LSB :     /* @fptr(sym + add), data4 LSB */
    case R_IA64_FPTR64LSB :     /* @fptr(sym + add), data8 LSB */
	assert(isym != NULL);
	if (isym->root.secidx <= SHN_HIRESERVE)
	{
	    assert(isym->opdent != NULL);
	    if (!isym->opdent->reloc_done)
	    {
		isym->opdent->reloc_done = TRUE;
		*(Elf64_Addr *)(ifile->opd->contents + isym->opdent->offset) = v;
		*(Elf64_Addr *)(ifile->opd->contents + isym->opdent->offset + 8) = gp;
	    }
	    v = ifile->opd->header.sh_addr + isym->opdent->offset;
	}
	switch (r_info)
	{
	case R_IA64_FPTR64I :
	    ret = obj_ia64_ins_imm64(v, bundle, slot);
	    break;

	case R_IA64_FPTR32LSB :
	    COPY_32LSB(loc, v);
	    break;

	case R_IA64_FPTR64LSB :     /* @fptr(sym + add), data8 LSB */
	    /* Target can be unaligned */
	    COPY_64LSB(loc, v);
	    break;
	}
	break;

    case R_IA64_PCREL21B :      /* @pcrel(sym + add), ptb, call */
    case R_IA64_PCREL21M :      /* @pcrel(sym + add), chk.s */
    case R_IA64_PCREL21F :      /* @pcrel(sym + add), fchkf */
	assert(isym != NULL);
	if ((isym->root.secidx > SHN_HIRESERVE) ||
	   ((Elf64_Sxword) (v - dot) > 16777215) ||
	   ((Elf64_Sxword) (v - dot) < -16777216))
	{
	    ia64_plt_t *pe;

	    for (pe = isym->pltent; pe != NULL && pe->addend != rel->r_addend; )
		pe = pe->next;
	    assert(pe != NULL);
	    if (!pe->reloc_done)
	    {
		pe->reloc_done = TRUE;
		ret = obj_ia64_generate_plt(v, gp, ifile, isym, pe);
	    }
	    v = ifile->pltt->header.sh_addr + pe->text_offset;
	}
	v -= dot;
	switch (r_info)
	{
	case R_IA64_PCREL21B :
	    ret = obj_ia64_ins_pcrel21b(v, bundle, slot);
	    break;

	case R_IA64_PCREL21M :
	    ret = obj_ia64_ins_pcrel21m(v, bundle, slot);
	    break;

	case R_IA64_PCREL21F :
	    ret = obj_ia64_ins_pcrel21f(v, bundle, slot);
	    break;
	}
	break;

    case R_IA64_PCREL32LSB :    /* @pcrel(sym + add), data4 LSB */
	COPY_32LSB(loc, v-dot);
	break;

    case R_IA64_PCREL64LSB :    /* @pcrel(sym + add), data8 LSB */
	COPY_64LSB(loc, v-dot);
	break;

    case R_IA64_LTOFF_FPTR22 :  /* @ltoff(@fptr(s+a)), imm22 */
    case R_IA64_LTOFF_FPTR64I : /* @ltoff(@fptr(s+a)), imm64 */
    case R_IA64_LTOFF_FPTR32LSB : /* @ltoff(@fptr(s+a)), data4 */
    case R_IA64_LTOFF_FPTR64LSB : /* @ltoff(@fptr(s+a)), data8 */
	{
	    ia64_got_t *ge;

	    assert(isym != NULL);
	    if (isym->root.secidx <= SHN_HIRESERVE)
	    {
		assert(isym->opdent != NULL);
		if (!isym->opdent->reloc_done)
		{
		    isym->opdent->reloc_done = TRUE;
		    *(Elf64_Addr *)(ifile->opd->contents + isym->opdent->offset) = v;
		    *(Elf64_Addr *)(ifile->opd->contents + isym->opdent->offset + 8) = gp;
		}
		v = ifile->opd->header.sh_addr + isym->opdent->offset;
	    }
	    for (ge = isym->gotent; ge != NULL && ge->addend != rel->r_addend; )
		ge = ge->next;
	    assert(ge != NULL);
	    if (!ge->reloc_done)
	    {
		ge->reloc_done = TRUE;
		*(Elf64_Addr *)(ifile->got->contents + ge->offset) =  v;
	    }
	    v = got + ge->offset - gp;
	    switch (r_info)
	    {
	    case R_IA64_LTOFF_FPTR22 :
		ret = obj_ia64_ins_imm22(v, bundle, slot);
		break;

	    case R_IA64_LTOFF_FPTR64I :
		ret = obj_ia64_ins_imm64(v, bundle, slot);
		break;

	    case R_IA64_LTOFF_FPTR32LSB :
		COPY_32LSB(loc, v);
		break;

	    case R_IA64_LTOFF_FPTR64LSB :
		COPY_64LSB(loc, v);
		break;
	    }
	}
	break;

    case R_IA64_SEGREL32LSB :   /* @segrel(sym + add), data4 LSB */
    case R_IA64_SEGREL64LSB :   /* @segrel(sym + add), data8 LSB */
	/* Only one segment for modules, see segment_base in arch_archdata */
	v -= f->sections[1]->header.sh_addr;
	if (r_info == R_IA64_SEGREL32LSB)
	    COPY_32LSB(loc, v);
	else
	    COPY_64LSB(loc, v);
	break;

    case R_IA64_SECREL32LSB :   /* @secrel(sym + add), data4 LSB */
	COPY_32LSB(loc, targsec->header.sh_addr - v);
	break;

    case R_IA64_SECREL64LSB :   /* @secrel(sym + add), data8 LSB */
	COPY_64LSB(loc, targsec->header.sh_addr - v);
	break;

    /*
     * We don't handle the big-endian relocates
     *
     * R_IA64_DIR32MSB        symbol + addend, data4 MSB
     * R_IA64_DIR64MSB        symbol + addend, data8 MSB
     * R_IA64_GPREL32MSB      @gprel(sym + add), data4 MSB
     * R_IA64_GPREL64MSB      @gprel(sym + add), data8 MSB
     * R_IA64_PLTOFF64MSB     @pltoff(sym + add), data8 MSB
     * R_IA64_FPTR32MSB       @fptr(sym + add), data4 MSB
     * R_IA64_FPTR64MSB       @fptr(sym + add), data8 MSB
     * R_IA64_PCREL32MSB      @pcrel(sym + add), data4 MSB
     * R_IA64_PCREL64MSB      @pcrel(sym + add), data8 MSB
     * R_IA64_SEGREL32MSB     @segrel(sym + add), data4 MSB
     * R_IA64_SEGREL64MSB     @segrel(sym + add), data8 MSB
     * R_IA64_SECREL32MSB     @secrel(sym + add), data4 MSB
     * R_IA64_SECREL64MSB     @secrel(sym + add), data8 MSB
     * R_IA64_REL32MSB        data 4 + REL
     * R_IA64_REL64MSB        data 8 + REL
     * R_IA64_LTV32MSB        symbol + addend, data4 MSB
     * R_IA64_LTV64MSB        symbol + addend, data8 MSB
     * R_IA64_IPLTMSB         dynamic reloc, imported PLT, MSB
     */
    default:
    case R_IA64_REL32LSB :      /* data 4 + REL */
    case R_IA64_REL64LSB :      /* data 8 + REL */
    case R_IA64_LTV32LSB :      /* symbol + addend, data4 LSB */
    case R_IA64_LTV64LSB :      /* symbol + addend, data8 LSB */
    case R_IA64_IPLTLSB :       /* dynamic reloc, imported PLT, LSB */
	ret = obj_reloc_unhandled;
	break;
    }
    return ret;
}

int
arch_init_module (struct obj_file *f, struct module *mod)
{
    ia64_file_t *ifile = (ia64_file_t *)f;
    Elf64_Addr *opd = (Elf64_Addr *)(ifile->opd->contents);

    if ((opd[0] = mod->init) != 0)
    {
	opd[1] = ifile->gp;
	mod->init = ifile->opd->header.sh_addr;
    }

    if ((opd[2] = mod->cleanup) != 0)
    {
	opd[3] = ifile->gp;
	mod->cleanup = ifile->opd->header.sh_addr + 16;
    }

    return 1;
}

int
arch_archdata (struct obj_file *f, struct obj_section *archdata_sec)
{
    ia64_file_t *ifile = (ia64_file_t *)f;
    struct archdata {
	unsigned tgt_long unw_table;
	unsigned tgt_long segment_base;
	unsigned tgt_long unw_start;
	unsigned tgt_long unw_end;
	unsigned tgt_long gp;
    } *ad;
    int i;
    struct obj_section *sec;

    free(archdata_sec->contents);
    archdata_sec->contents = xmalloc(sizeof(struct archdata));
    memset(archdata_sec->contents, 0, sizeof(struct archdata));
    archdata_sec->header.sh_size = sizeof(struct archdata);

    ad = (struct archdata *)(archdata_sec->contents);
    ad->gp = ifile->gp;
    ad->unw_start = 0;
    ad->unw_end = 0;
    ad->unw_table = 0;
    ad->segment_base = f->sections[1]->header.sh_addr;
    for (i = 0; i < f->header.e_shnum; ++i)
    {
	sec = f->sections[i];
	if (sec->header.sh_type == SHT_IA_64_UNWIND)
	{
	    ad->unw_start = sec->header.sh_addr;
	    ad->unw_end = sec->header.sh_addr + sec->header.sh_size;
	    break;
	}
    }

    return 0;
}
