/* Machine-specific elf macros for i386 et al.  */

#define ELFCLASSM	ELFCLASS32
#define ELFDATAM	ELFDATA2LSB

#define MATCH_MACHINE(x)  (x == EM_386)

#define SHT_RELM	SHT_REL
#define Elf32_RelM	Elf32_Rel
