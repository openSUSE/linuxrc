/* Machine-specific elf macros for the PowerPC.  */

#define ELFCLASSM	ELFCLASS32
#define ELFDATAM	ELFDATA2MSB

#define MATCH_MACHINE(x)  (x == EM_PPC)

#define SHT_RELM	SHT_RELA
#define Elf32_RelM	Elf32_Rela
