/* Machine-specific elf macros for the Sparc.  */

#define ELFCLASSM	ELFCLASS64
#define ELFDATAM	ELFDATA2MSB

#ifndef EM_SPARCV9
#define EM_SPARCV9 43
#endif
#ifndef EM_SPARC64
#define EM_SPARC64 11
#endif
#define MATCH_MACHINE(x)  ((x) == EM_SPARCV9 || (x) == EM_SPARC64)

#define SHT_RELM	SHT_RELA
#define Elf64_RelM	Elf64_Rela

#ifndef ELF64_R_SYM
#define ELF64_R_SYM(x)	((x) >> 32)
#define ELF64_R_TYPE(x)	((unsigned)(x))
#endif

#ifndef ELF64_ST_BIND
#define ELF64_ST_BIND(x)	((x) >> 4)
#define ELF64_ST_TYPE(x)	((x) & 0xf)
#endif

