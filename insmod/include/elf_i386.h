/* Machine-specific elf macros for i386 et al.  */
#ident "$Id: elf_i386.h,v 1.1 2000/03/23 17:09:55 snwint Exp $"

#define ELFCLASSM	ELFCLASS32
#define ELFDATAM	ELFDATA2LSB

#define MATCH_MACHINE(x)  (x == EM_386 || x == EM_486)

#define SHT_RELM	SHT_REL
#define Elf32_RelM	Elf32_Rel
