/* Machine-specific elf macros for the super-h.  */

#define ELFCLASSM	ELFCLASS32
#ifdef __LITTLE_ENDIAN__
#define ELFDATAM	ELFDATA2LSB
#else
#define ELFDATAM	ELFDATA2MSB
#endif

#define MATCH_MACHINE(x)  (x == EM_SH)

#define SHT_RELM	SHT_RELA
#define Elf32_RelM	Elf32_Rela
