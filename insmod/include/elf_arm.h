/* Machine-specific elf macros for ARM.  */
#ident "$Id: elf_arm.h,v 1.2 2000/11/22 15:45:22 snwint Exp $"

#define ELFCLASSM	ELFCLASS32
#define ELFDATAM	ELFDATA2LSB

#define MATCH_MACHINE(x)  (x == EM_ARM)

#define SHT_RELM	SHT_REL
#define Elf32_RelM	Elf32_Rel
