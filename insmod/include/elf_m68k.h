/* Machine-specific elf macros for m68k.  */
#ident "$Id: elf_m68k.h,v 1.2 2000/11/22 15:45:22 snwint Exp $"

#define ELFCLASSM	ELFCLASS32
#define ELFDATAM	ELFDATA2MSB

#define MATCH_MACHINE(x)  (x == EM_68K)

#define SHT_RELM	SHT_RELA
#define Elf32_RelM	Elf32_Rela
