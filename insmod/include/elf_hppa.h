/* Machine-specific elf macros for HP-PA.  */

#define ELFCLASSM	ELFCLASS32
#define ELFDATAM	ELFDATA2MSB

#define MATCH_MACHINE(x)  (x == EM_PARISC)

#define SHT_RELM	SHT_RELA
#define Elf32_RelM	Elf32_Rela
