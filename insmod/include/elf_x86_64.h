/* Machine-specific elf macros for x86_64.  */

#define ELFCLASSM	ELFCLASS64
#define ELFDATAM	ELFDATA2LSB

#define MATCH_MACHINE(x)  (x == EM_X86_64)

#define SHT_RELM	SHT_RELA
#define Elf64_RelM	Elf64_Rela
