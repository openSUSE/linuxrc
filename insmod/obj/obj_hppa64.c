/*
 * hppa parisc64 specific support for Elf loading and relocation.
 * Copyright 2000 Richard Hirst <rhirst@linuxcare.com>, Linuxcare Inc.
 *
 * Based on ia64 specific support which was
 *   Copyright 2000 Mike Stephens <mike.stephens@intel.com>
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

typedef struct _hppa64_opd_t
{
    int offset;
    int reloc_done;
} hppa64_opd_t;

typedef struct _hppa64_stub_t
{
    struct _hppa64_stub_t *next;
    Elf64_Addr addend;
    int offset;
    int reloc_done;
} hppa64_stub_t;

typedef struct _hppa64_got_t
{
    struct _hppa64_got_t *next;
    Elf64_Addr addend;
    int offset;
    int reloc_done;
} hppa64_got_t;

typedef struct _hppa64_symbol_t
{
    struct obj_symbol root;
    hppa64_got_t *gotent;
    hppa64_opd_t *opdent;
    hppa64_stub_t *stubent;
} hppa64_symbol_t;

typedef struct _hppa64_file_t
{
    struct obj_file root;
    struct obj_section *got;
    struct obj_section *opd;
    struct obj_section *stub;
    Elf64_Addr gp;
    Elf64_Addr text;
    Elf64_Addr data;
    Elf64_Addr bss;
} hppa64_file_t;

/*
 * XXX This stub assumes it can reach the .got entry with a +/- 8K offset
 * from dp.  Perhaps we should use a .plt for these entries to give a
 * greater chance of that being true.
 *
 *     53 7b 00 00     ldd 0(dp),dp
 *     			R_PARISC_LTOFF14R <.got entry offset from dp>
 *     53 61 00 20     ldd 10(dp),r1
 *     e8 20 d0 00     bve (r1)
 *     53 7b 00 30     ldd 18(dp),dp
 *
 * We need a different stub for millicode calls, which doesn't depend on
 * or modify dp:
 *
 *     20 20 00 00     ldil 0,r1
 *			R_PARISC_DIR21L <addr of kernels opd entry>
 *     34 21 00 00     ldo 0(r1),r1
 *			R_PARISC_DIR14R <addr of kernels opd entry>
 *     50 21 00 20     ldd 10(r1),r1
 *     e8 20 d0 02     bve,n (r1)
 */

/* NOTE: to keep the code cleaner we make all stubs the same size.
 */

#define SIZEOF_STUB	16

unsigned char hppa64_stub_extern[] =
{
       0x53, 0x7b, 0x00, 0x00,
       0x53, 0x61, 0x00, 0x20,
       0xe8, 0x20, 0xd0, 0x00,
       0x53, 0x7b, 0x00, 0x30,
};

unsigned char hppa64_stub_millicode[] =
{
       0x20, 0x20, 0x00, 0x00,
       0x34, 0x21, 0x00, 0x00,
       0x50, 0x21, 0x00, 0x20,
       0xe8, 0x20, 0xd0, 0x02,
};

/*======================================================================*/

enum obj_reloc
patch_14r(Elf64_Xword v64, Elf64_Word *p)
{
	Elf64_Word i = *p;
	Elf64_Word v = (Elf64_Word)v64;

	if (v & 0x80000000)
		v |= ~0x7ff;
	else
		v &= 0x7ff;
	i &= ~ 0x3fff;
	i |=    (v & 0x1fff) << 1 |
		(v & 0x2000) >> 13;
	*p = i;

    return obj_reloc_ok;
}

enum obj_reloc
patch_21l(Elf64_Xword v64, Elf64_Word *p)
{
	Elf64_Word i = *p;
	Elf64_Word v = (Elf64_Word)v64;

	v &= 0xfffff800;
	if (v & 0x80000000)
		v += 0x800;
	i &= ~ 0x1fffff;
	i |=    (v & 0x80000000) >> 31 |
		(v & 0x7ff00000) >> 19 |
		(v & 0x000c0000) >> 4 |
		(v & 0x0003e000) << 3 |
		(v & 0x00001800) << 1;
	*p = i;

    return obj_reloc_ok;
}


/* All 14 bits this time...  This is used to patch the .got offset in
 * a stub for PCREL22F.
 */

enum obj_reloc
patch_14r2(Elf64_Xword v64, Elf64_Word *p)
{
	Elf64_Word i = *p;
	Elf64_Word v = (Elf64_Word)v64;

	if ((Elf64_Sxword)v64 > 0x1fffL ||
			(Elf64_Sxword)v64 < -0x2000L)
		return obj_reloc_overflow;
	i &= ~ 0x3fff;
	i |=    (v & 0x2000) >> 13 |
		(v & 0x1fff) << 1;
	*p = i;

    return obj_reloc_ok;
}


enum obj_reloc
patch_22f(Elf64_Xword v64, Elf64_Word *p)
{
	Elf64_Word i = *p;
	Elf64_Word v = (Elf64_Word)v64;

	if ((Elf64_Sxword)v64 > 0x800000-1 ||
	    (Elf64_Sxword)v64 < -0x800000)
		return obj_reloc_overflow;

	i &= ~ 0x03ff1ffd;
	i |=    (v & 0x00800000) >> 23 |
		(v & 0x007c0000) << 3 |
		(v & 0x0003e000) << 3 |
		(v & 0x00001000) >> 10 |
		(v & 0x00000ffc) << 1;
	*p = i;

    return obj_reloc_ok;
}


struct obj_section *
obj_hppa64_create_alloced_section (struct obj_file *f, const char *name,
    unsigned long align, unsigned long size, unsigned long sh_flags)
{
    int newidx = f->header.e_shnum++;
    struct obj_section *sec;

    f->sections = xrealloc(f->sections, (newidx+1) * sizeof(sec));
    f->sections[newidx] = sec = arch_new_section();

    memset(sec, 0, sizeof(*sec));
    sec->header.sh_type = SHT_PROGBITS;
    sec->header.sh_flags = sh_flags;
    sec->header.sh_size = size;
    sec->header.sh_addralign = align;
    sec->name = name;
    sec->idx = newidx;
    if (size)
	sec->contents = xmalloc(size);

    obj_insert_section_load_order(f, sec);

    return sec;
}

/*======================================================================*/

struct obj_file *
arch_new_file (void)
{
    hppa64_file_t *f;
    f = xmalloc(sizeof(*f));
    f->got = NULL;
    f->opd = NULL;
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
    hppa64_symbol_t *sym;
    sym = xmalloc(sizeof(*sym));
    sym->gotent = NULL;
    sym->opdent = NULL;
    sym->stubent = NULL;
    return &sym->root;
}

/* This may not be needed, but does no harm (copied from ia64).
 */

int
arch_load_proc_section(struct obj_section *sec, int fp)
{
    switch (sec->header.sh_type)
    {
    case SHT_PARISC_EXT :
	sec->contents = NULL;
	break;

    case SHT_PARISC_UNWIND :
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
    hppa64_file_t *hfile = (hppa64_file_t *)f;
    int i;
    int n;
    int got_offset = 0;
    int opd_offset = 64;
    int stub_offset = 0;

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

	rel = (Elf64_Rela *)relsec->contents;
	relend = rel + (relsec->header.sh_size / sizeof(Elf64_Rela));
	symtab = (Elf64_Sym *)symsec->contents;
	strtab = (const char *)strsec->contents;

	for (; rel < relend; ++rel)
	{
	    int need_got = FALSE;
	    int need_opd = FALSE;
	    int need_stub = FALSE;

	    switch (ELF64_R_TYPE(rel->r_info))
	    {
	    default:
		{
		    unsigned r_info = ELF64_R_TYPE(rel->r_info);
		    printf("r_info 0x%x not handled\n", r_info);
		}
		continue;
	    case R_PARISC_LTOFF14R:
	    case R_PARISC_LTOFF21L:
	        /* These are simple indirect references to symbols through the
                 * DLT.  We need to create a DLT entry for any symbols which
                 * appears in a DLTIND relocation.
		 */
		need_got = TRUE;
		break;
	    case R_PARISC_PCREL22F:
		/* These are function calls.  Depending on their precise
		 * target we may need to make a stub for them.  The stub
		 * uses the dlt, so we need to create dlt entries for
		 * these symbols too.
		 */
		need_got = TRUE;
		need_stub = TRUE;
		break;
	    case R_PARISC_DIR64:
	    case R_PARISC_SEGREL32:
		break;
	    case R_PARISC_FPTR64:
		/* This is a simple OPD entry (only created for local symbols,
		 * see below).
		 */
		need_opd = TRUE;
		break;
	    }

	    if (need_got || need_opd || need_stub)
	    {
		hppa64_symbol_t *isym;
		int            local;

		obj_find_relsym(isym, f, f, rel, symtab, strtab);
		local = isym->root.secidx <= SHN_HIRESERVE;

		if (need_stub)
		{
		    hppa64_stub_t *stub;

		    for (stub = isym->stubent; stub != NULL; stub = stub->next)
			if (stub->addend == rel->r_addend)
			    break;
		    if (stub == NULL)
		    {
			stub = (hppa64_stub_t *) xmalloc(sizeof(hppa64_stub_t));
			stub->next = isym->stubent;
			stub->addend = rel->r_addend;
			stub->offset = stub_offset;
			stub->reloc_done = FALSE;
			isym->stubent = stub;
			{
			    stub_offset += SIZEOF_STUB;
			}
			need_stub = FALSE;
		    }
		}
		if (need_got)
		{
		    hppa64_got_t *got;

		    for (got = isym->gotent; got != NULL; got = got->next)
			if (got->addend == rel->r_addend)
			    break;
		    if (got == NULL)
		    {
			got = (hppa64_got_t *) xmalloc(sizeof(hppa64_got_t));
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
		    hppa64_opd_t *opd;

		    if (isym->opdent == NULL)
		    {
			opd = (hppa64_opd_t *) xmalloc(sizeof(hppa64_opd_t));
			opd->offset = opd_offset;
			opd->reloc_done = FALSE;
			isym->opdent = opd;
			opd_offset += 32;
			need_opd = FALSE;
		    }
		}
	    }
	}
    }

    hfile->got = obj_hppa64_create_alloced_section(f, ".got", 8, got_offset,
	(SHF_ALLOC | SHF_WRITE | SHF_PARISC_SHORT));
    assert(hfile->got != NULL);

    hfile->opd = obj_hppa64_create_alloced_section(f, ".opd", 16, opd_offset,
	(SHF_ALLOC | SHF_WRITE | SHF_PARISC_SHORT));
    assert(hfile->opd != NULL);

    if (stub_offset > 0)
    {
	hfile->stub = obj_hppa64_create_alloced_section(f, ".stub", 16,
	    stub_offset, (SHF_ALLOC | SHF_EXECINSTR | SHF_PARISC_SHORT));
	assert(hfile->stub != NULL);
    }

    return 1;
}


/* This is a small simple version which seems to work fine.  ia64 has
 * a much more complex algorithm.  We point dp at the end of the .got,
 * which is the start of the .opd.
 */

int
arch_finalize_section_address(struct obj_file *f, Elf64_Addr base)
{
    hppa64_file_t *hfile = (hppa64_file_t *)f;
    int        n = f->header.e_shnum;
    int        i;

    f->baseaddr = base;
    for (i = 0; i < n; ++i)
	f->sections[i]->header.sh_addr += base;

    /* Pick a sensible value for gp */
    hfile->gp = hfile->got->header.sh_addr + hfile->got->header.sh_size;

    return 1;
}


enum obj_reloc
arch_apply_relocation(struct obj_file *f,
		       struct obj_section *targsec,
		       struct obj_section *symsec,
		       struct obj_symbol *sym,
		       Elf64_Rela *rel,
		       Elf64_Addr v)
{
    hppa64_file_t *hfile = (hppa64_file_t *) f;
    hppa64_symbol_t *isym  = (hppa64_symbol_t *) sym;

    Elf64_Word  *loc = (Elf64_Word *)(targsec->contents + rel->r_offset);
    Elf64_Addr  dot = (targsec->header.sh_addr + rel->r_offset) & ~0x03;

    Elf64_Addr  got = hfile->got->header.sh_addr;
    Elf64_Addr  gp = hfile->gp;

    Elf64_Xword r_info = ELF64_R_TYPE(rel->r_info);

    enum obj_reloc ret = obj_reloc_ok;

    switch (r_info)
    {
    default:
	ret = obj_reloc_unhandled;
	break;
    case R_PARISC_LTOFF14R:
    case R_PARISC_LTOFF21L:
	{
	    hppa64_got_t *ge;

	    assert(isym != NULL);
	    for (ge = isym->gotent; ge != NULL && ge->addend != rel->r_addend; )
		ge = ge->next;
	    assert(ge != NULL);
	    if (!ge->reloc_done)
	    {
		ge->reloc_done = TRUE;
		*(Elf64_Addr *)(hfile->got->contents + ge->offset) = v;
	    }
	    v = got + ge->offset - gp;
	    if (r_info == R_PARISC_LTOFF14R)
		ret = patch_14r(v, loc);
	    else
		ret = patch_21l(v, loc);
	}
	break;
    case R_PARISC_PCREL22F:
	{
	    hppa64_got_t *ge;

	    assert(isym != NULL);
	    for (ge = isym->gotent; ge != NULL && ge->addend != rel->r_addend; )
		ge = ge->next;
	    assert(ge != NULL);
	    if (!ge->reloc_done)
	    {
		ge->reloc_done = TRUE;
		*(Elf64_Addr *)(hfile->got->contents + ge->offset) = v;
	    }
	    if ((isym->root.secidx > SHN_HIRESERVE) ||
		((Elf64_Sxword) (v - dot - 8) > 0x800000-1) ||
		((Elf64_Sxword) (v - dot - 8) < -0x800000))
	    {
		hppa64_stub_t *se;

		for (se = isym->stubent; se != NULL && se->addend != rel->r_addend; )
		    se = se->next;
		assert(se != NULL);
		if (!se->reloc_done)
		{
		    /* This requires that we can get from dp to the entry in +/- 8K,
		     * or +/- 1000 entries.  patch_14r2() will check that.
		     * Only need these dlt entries for calls to external/far
		     * functions, so should probably put them in a seperate section
		     * before dlt and point dp at the section.  Change to that
		     * scheme if we hit problems with big modules.
		     */
		    unsigned char *stub;

		    if (!strncmp(isym->root.name, "$$", 2)) {
			stub = hppa64_stub_millicode;
			memcpy((Elf64_Addr *)(hfile->stub->contents + se->offset),
					stub, SIZEOF_STUB);
			v = (Elf64_Addr)isym->root.value;
			ret = patch_21l(v, (Elf64_Word *)(hfile->stub->contents + se->offset));
			if (ret == obj_reloc_ok)
			    ret = patch_14r(v, (Elf64_Word *)(hfile->stub->contents + se->offset + 4));
		    }
		    else {
			stub = hppa64_stub_extern;
			memcpy((Elf64_Addr *)(hfile->stub->contents + se->offset),
					stub, SIZEOF_STUB);
			v = (Elf64_Addr)(hfile->got->header.sh_addr + ge->offset) - gp;
			ret = patch_14r2(v, (Elf64_Word *)(hfile->stub->contents + se->offset));
		    }
		    se->reloc_done = TRUE;
		}
		v = hfile->stub->header.sh_addr + se->offset;
	    }
	    v = v - dot - 8;
	    if (ret == obj_reloc_ok)
	        ret = patch_22f(v, loc);
	}
	break;
    case R_PARISC_DIR64:
	{
	    loc[0] = v >> 32;
	    loc[1] = v;
	}
	break;
    case R_PARISC_SEGREL32:
	{
	    loc[0] = v - f->baseaddr;
	}
	break;
    case R_PARISC_FPTR64:
	{
	    assert(isym != NULL);
	    if (isym->root.secidx <= SHN_HIRESERVE) /* local */
	    {
		assert(isym->opdent != NULL);
		if (!isym->opdent->reloc_done)
		{
		    isym->opdent->reloc_done = TRUE;
		    *(Elf64_Addr *)(hfile->opd->contents + isym->opdent->offset + 16) = v;
		    *(Elf64_Addr *)(hfile->opd->contents + isym->opdent->offset + 24) = gp;
		}
		v = hfile->opd->header.sh_addr + isym->opdent->offset;
	    }
	    loc[0] = v >> 32;
	    loc[1] = v;
	}
	break;
    }
    return ret;
}

int
arch_init_module (struct obj_file *f, struct module *mod)
{
    hppa64_file_t *hfile = (hppa64_file_t *)f;
    Elf64_Addr *opd = (Elf64_Addr *)(hfile->opd->contents);

    opd[0] = 0;
    opd[1] = 0;
    if ((opd[2] = mod->init) != 0)
    {
	opd[3] = hfile->gp;
	mod->init = hfile->opd->header.sh_addr;
    }

    opd[4] = 0;
    opd[5] = 0;
    if ((opd[6] = mod->cleanup) != 0)
    {
	opd[7] = hfile->gp;
	mod->cleanup = hfile->opd->header.sh_addr + 32;
    }

    return 1;
}

/* XXX Is this relevant to parisc? */

int
arch_archdata (struct obj_file *f, struct obj_section *archdata_sec)
{
    hppa64_file_t *hfile = (hppa64_file_t *)f;
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
    ad->gp = hfile->gp;
    ad->unw_start = 0;
    ad->unw_end = 0;
    ad->unw_table = 0;
    ad->segment_base = f->sections[1]->header.sh_addr;
    for (i = 0; i < f->header.e_shnum; ++i)
    {
	sec = f->sections[i];
	if (sec->header.sh_type == SHT_PARISC_UNWIND)
	{
	    ad->unw_start = sec->header.sh_addr;
	    ad->unw_end = sec->header.sh_addr + sec->header.sh_size;
	    break;
	}
    }

    return 0;
}
