/* Machine-specific elf macros for i386 et al.  */
#ident "$Id: elf_s390.h,v 1.1 2000/11/22 15:46:44 snwint Exp $"

#define ELFCLASSM	ELFCLASS32
#define ELFDATAM	ELFDATA2MSB

#define MATCH_MACHINE(x)  (x == EM_S390)

#define SHT_RELM	SHT_RELA
#define Elf32_RelM	Elf32_Rela
