/* Machine-specific elf macros for m68k.  */
#ident "$Id: elf_m68k.h,v 1.1 2000/03/23 17:09:55 snwint Exp $"

#define ELFCLASSM	ELFCLASS32
#define ELFDATAM	ELFDATA2MSB

#define MATCH_MACHINE(x)  (x == EM_68K)

#define SHT_RELM	SHT_RELA
#define Elf32_RelM	Elf32_Rela
