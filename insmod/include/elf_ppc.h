/* Machine-specific elf macros for the PowerPC.  */
#ident "$Id: elf_ppc.h,v 1.1 2000/03/23 17:09:55 snwint Exp $"

#define ELFCLASSM	ELFCLASS32
#define ELFDATAM	ELFDATA2MSB

#define MATCH_MACHINE(x)  (x == EM_PPC)

#define SHT_RELM	SHT_RELA
#define Elf32_RelM	Elf32_Rela
