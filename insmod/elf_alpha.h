/* Machine-specific elf macros for the Alpha.  */
#ident "$Id: elf_alpha.h,v 1.1 1999/12/14 12:38:12 snwint Exp $"

#define ELFCLASSM	ELFCLASS64
#define ELFDATAM	ELFDATA2LSB

#define MATCH_MACHINE(x)  (x == EM_ALPHA)

#define SHT_RELM	SHT_RELA
#define Elf64_RelM	Elf64_Rela
