/* Build a section containing all non-stack symbols.
   Copyright 2000 Keith Owens <kaos@ocs.com.au>

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

#include <stdlib.h>
#include <string.h>
#include <malloc.h>

#include "obj.h"
#include "kallsyms.h"
#include "util.h"

/*======================================================================*/

#define EXPAND_BY 4096  /* Arbitrary */

/* Append a string to the big list of strings */

static void
append_string (const char *s, char **strings,
	       ElfW(Word) *strings_size, ElfW(Word) *strings_left)
{
    int l = strlen(s) + 1;
    while (l > *strings_left) {
	*strings = xrealloc(*strings, *strings_size += EXPAND_BY);
	*strings_left += EXPAND_BY;
    }
    memcpy((char *)*strings+*strings_size-*strings_left, s, l);
    *strings_left -= l;
}


/* Append a symbol to the big list of symbols */

static void
append_symbol (const struct kallsyms_symbol *s,
	       struct kallsyms_symbol **symbols,
	       ElfW(Word) *symbols_size, ElfW(Word) *symbols_left)
{
    int l = sizeof(*s);
    while (l > *symbols_left) {
	*symbols = xrealloc(*symbols, *symbols_size += EXPAND_BY);
	*symbols_left += EXPAND_BY;
    }
    memcpy((char *)*symbols+*symbols_size-*symbols_left, s, l);
    *symbols_left -= l;
}

/* qsort compare routine to sort symbols */

static const char *sym_strings;

static int
symbol_compare (const void *a, const void *b)
{
    struct kallsyms_symbol *c = (struct kallsyms_symbol *) a;
    struct kallsyms_symbol *d = (struct kallsyms_symbol *) b;

    if (c->symbol_addr > d->symbol_addr)
	return(1);
    if (c->symbol_addr < d->symbol_addr)
	return(-1);
    return(strcmp(sym_strings+c->name_off, sym_strings+d->name_off));
}


/* Extract all symbols from the input obj_file, ignore ones that are
 * no use for debugging, build an output obj_file containing only the
 * kallsyms section.
 *
 * The kallsyms section is a bit unusual.  It deliberately has no
 * relocatable data, all "pointers" are represented as byte offsets
 * into the the section.  This means it can be stored anywhere without
 * relocation problems.  In particular it can be stored within a kernel
 * image, it can be stored separately from the kernel image, it can be
 * appended to a module just before loading, it can be stored in a
 * separate area etc.
 *
 * Format of the kallsyms section.
 *
 * Header:
 *   Size of header.
 *   Total size of kallsyms data, including strings.
 *   Number of loaded sections.
 *   Offset to first section entry from start of header.
 *   Size of each section entry, excluding the name string.
 *   Number of symbols.
 *   Offset to first symbol entry from start of header.
 *   Size of each symbol entry, excluding the name string.
 *
 * Section entry - one per loaded section.
 *   Start of section[1].
 *   Size of section.
 *   Offset to name of section, from start of strings.
 *   Section flags.
 *
 * Symbol entry - one per symbol in the input file[2].
 *   Offset of section that owns this symbol, from start of section data.
 *   Address of symbol within the real section[1].
 *   Offset to name of symbol, from start of strings.
 *
 * Notes: [1] This is an exception to the "represent pointers as
 *            offsets" rule, it is a value, not an offset.  The start
 *            address of a section or a symbol is extracted from the
 *            obj_file data which may contain absolute or relocatable
 *            addresses.  If the addresses are relocatable then the
 *            caller must adjust the section and/or symbol entries in
 *            kallsyms after relocation.
 *        [2] Only symbols that fall within loaded sections are stored.
 */

int
obj_kallsyms (struct obj_file *fin, struct obj_file **fout_result)
{
    struct obj_file *fout;
    int i, loaded = 0, *fin_to_allsym_map;
    struct obj_section *isec, *osec;
    struct kallsyms_header *a_hdr;
    struct kallsyms_section *a_sec;
    ElfW(Off) sec_off;
    struct kallsyms_symbol *symbols = NULL, a_sym;
    ElfW(Word) symbols_size = 0, symbols_left = 0;
    char *strings = NULL, *p;
    ElfW(Word) strings_size = 0, strings_left = 0;
    ElfW(Off) file_offset;
    static char strtab[] = "\000" KALLSYMS_SEC_NAME;

    /* Create the kallsyms section.  */
    fout = arch_new_file();
    memset(fout, 0, sizeof(*fout));
    fout->symbol_cmp = strcmp;
    fout->symbol_hash = obj_elf_hash;
    fout->load_order_search_start = &fout->load_order;

    /* Copy file characteristics from input file and modify to suit */
    memcpy(&fout->header, &fin->header, sizeof(fout->header));
    fout->header.e_type = ET_REL;		/* Output is relocatable */
    fout->header.e_entry = 0;			/* No entry point */
    fout->header.e_phoff = 0;			/* No program header */
    file_offset = sizeof(fout->header);		/* Step over Elf header */
    fout->header.e_shoff = file_offset;		/* Section headers next */
    fout->header.e_phentsize = 0;		/* No program header */
    fout->header.e_phnum = 0;			/* No program header */
    fout->header.e_shnum = KALLSYMS_IDX+1;	/* Initial, strtab, kallsyms */
    fout->header.e_shstrndx = KALLSYMS_IDX-1;	/* strtab */
    file_offset += fout->header.e_shentsize * fout->header.e_shnum;

    /* Populate the section data for kallsyms itself */
    fout->sections = xmalloc(sizeof(*(fout->sections))*fout->header.e_shnum);
    memset(fout->sections, 0, sizeof(*(fout->sections))*fout->header.e_shnum);

    fout->sections[0] = osec = arch_new_section();
    memset(osec, 0, sizeof(*osec));
    osec->header.sh_type = SHT_NULL;
    osec->header.sh_link = SHN_UNDEF;

    fout->sections[KALLSYMS_IDX-1] = osec = arch_new_section();
    memset(osec, 0, sizeof(*osec));
    osec->name = ".strtab";
    osec->header.sh_type = SHT_STRTAB;
    osec->header.sh_link = SHN_UNDEF;
    osec->header.sh_offset = file_offset;
    osec->header.sh_size = sizeof(strtab);
    osec->contents = xmalloc(sizeof(strtab));
    memcpy(osec->contents, strtab, sizeof(strtab));
    file_offset += osec->header.sh_size;

    fout->sections[KALLSYMS_IDX] = osec = arch_new_section();
    memset(osec, 0, sizeof(*osec));
    osec->name = KALLSYMS_SEC_NAME;
    osec->header.sh_name = 1;			/* Offset in strtab */
    osec->header.sh_type = SHT_PROGBITS;	/* Load it */
    osec->header.sh_flags = SHF_ALLOC;		/* Read only data */
    osec->header.sh_link = SHN_UNDEF;
    osec->header.sh_addralign = sizeof(ElfW(Word));
    file_offset = (file_offset + osec->header.sh_addralign - 1)
	& -(osec->header.sh_addralign);
    osec->header.sh_offset = file_offset;

    /* How many loaded sections are there? */
    for (i = 0; i < fin->header.e_shnum; ++i) {
	if (fin->sections[i]->header.sh_flags & SHF_ALLOC)
	    ++loaded;
    }

    /* Initial contents, header + one entry per input section.  No strings. */
    osec->header.sh_size = sizeof(*a_hdr) + loaded*sizeof(*a_sec);
    a_hdr = (struct kallsyms_header *) osec->contents =
    	xmalloc(osec->header.sh_size);
    memset(osec->contents, 0, osec->header.sh_size);
    a_hdr->size = sizeof(*a_hdr);
    a_hdr->sections = loaded;
    a_hdr->section_off = a_hdr->size;
    a_hdr->section_size = sizeof(*a_sec);
    a_hdr->symbol_off = osec->header.sh_size;
    a_hdr->symbol_size = sizeof(a_sym);
    a_hdr->start = (ElfW(Addr))(~0);

    /* Map input section numbers to kallsyms section offsets. */
    sec_off = 0;	/* Offset to first kallsyms section entry */
    fin_to_allsym_map = xmalloc(sizeof(*fin_to_allsym_map)*fin->header.e_shnum);
    for (i = 0; i < fin->header.e_shnum; ++i) {
	isec = fin->sections[i];
	if (isec->header.sh_flags & SHF_ALLOC) {
	    fin_to_allsym_map[isec->idx] = sec_off;
	    sec_off += a_hdr->section_size;
	}
	else
	    fin_to_allsym_map[isec->idx] = -1;	/* Ignore this section */
    }

    /* Copy the loaded section data. */
    a_sec = (struct kallsyms_section *) ((char *) a_hdr + a_hdr->section_off);
    for (i = 0; i < fin->header.e_shnum; ++i) {
	isec = fin->sections[i];
	if (!(isec->header.sh_flags & SHF_ALLOC))
	    continue;
	a_sec->start = isec->header.sh_addr;
	a_sec->size = isec->header.sh_size;
	a_sec->flags = isec->header.sh_flags;
	a_sec->name_off = strings_size - strings_left;
	append_string(isec->name, &strings, &strings_size, &strings_left);
	if (a_sec->start < a_hdr->start)
	    a_hdr->start = a_sec->start;
	if (a_sec->start+a_sec->size > a_hdr->end)
	    a_hdr->end = a_sec->start+a_sec->size;
	++a_sec;
    }

    /* Build the kallsyms symbol table from the symbol hashes. */
    for (i = 0; i < HASH_BUCKETS; ++i) {
	struct obj_symbol *sym = fin->symtab[i];
	for (sym = fin->symtab[i]; sym ; sym = sym->next) {
	    if (!sym || sym->secidx >= fin->header.e_shnum)
		continue;
	    if ((a_sym.section_off = fin_to_allsym_map[sym->secidx]) == -1)
		continue;
	    if (strcmp(sym->name, "gcc2_compiled.") == 0 ||
		strncmp(sym->name, "__insmod_", 9) == 0)
		continue;
	    a_sym.symbol_addr = sym->value;
	    if (fin->header.e_type == ET_REL)
		a_sym.symbol_addr += fin->sections[sym->secidx]->header.sh_addr;
	    a_sym.name_off = strings_size - strings_left;
	    append_symbol(&a_sym, &symbols, &symbols_size, &symbols_left);
	    append_string(sym->name, &strings, &strings_size, &strings_left);
	    ++a_hdr->symbols;
	}
    }
    free(fin_to_allsym_map);

    /* Sort the symbols into ascending order by address and name */
    sym_strings = strings;	/* For symbol_compare */
    qsort((char *) symbols, (unsigned) a_hdr->symbols,
	sizeof(* symbols), symbol_compare);
    sym_strings = NULL;

    /* Put the lot together */
    osec->header.sh_size = a_hdr->total_size =
	a_hdr->symbol_off +
	a_hdr->symbols*a_hdr->symbol_size +
	strings_size - strings_left;
    a_hdr = (struct kallsyms_header *) osec->contents =
	xrealloc(a_hdr, a_hdr->total_size);
    p = (char *)a_hdr + a_hdr->symbol_off;
    memcpy(p, symbols, a_hdr->symbols*a_hdr->symbol_size);
    free(symbols);
    p += a_hdr->symbols*a_hdr->symbol_size;
    a_hdr->string_off = p - (char *)a_hdr;
    memcpy(p, strings, strings_size - strings_left);
    free(strings);

    *fout_result = fout;
    return 0;
}
