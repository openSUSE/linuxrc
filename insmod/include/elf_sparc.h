/* Machine-specific elf macros for the Sparc.  */
#ident "$Id: elf_sparc.h,v 1.2 2000/11/22 15:45:22 snwint Exp $"

#define ELFCLASSM	ELFCLASS32
#define ELFDATAM	ELFDATA2MSB

#define MATCH_MACHINE(x)  (x == EM_SPARC)

#define SHT_RELM	SHT_RELA
#define Elf32_RelM	Elf32_Rela
