/* Machine-specific elf macros for HP-PA64.  */

#define ELFCLASSM	ELFCLASS64
#define ELFDATAM	ELFDATA2MSB

#define MATCH_MACHINE(x)  (x == EM_PARISC)

#define SHT_RELM	SHT_RELA
#define Elf64_RelM	Elf64_Rela
