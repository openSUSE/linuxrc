/* Machine-specific elf macros for m68k.  */

#define ELFCLASSM	ELFCLASS32
#define ELFDATAM	ELFDATA2MSB

#define MATCH_MACHINE(x)  (x == EM_68K)

#define SHT_RELM	SHT_RELA
#define Elf32_RelM	Elf32_Rela
