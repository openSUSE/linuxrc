/* Machine-specific elf macros for ia64.  */

#define ELFCLASSM	ELFCLASS64
#define ELFDATAM	ELFDATA2LSB

#define MATCH_MACHINE(x)  (x == EM_IA_64)

#define SHT_RELM	SHT_RELA
#define Elf64_RelM	Elf64_Rela
