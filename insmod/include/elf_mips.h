/* Machine-specific elf macros for MIPS.  */

#define ELFCLASSM	ELFCLASS32
#ifdef __MIPSEB__
#define ELFDATAM	ELFDATA2MSB
#endif
#ifdef __MIPSEL__
#define ELFDATAM	ELFDATA2LSB
#endif

/* Account for ELF spec changes.  */
#ifndef EM_MIPS_RS3_LE
#ifdef EM_MIPS_RS4_BE
#define EM_MIPS_RS3_LE	EM_MIPS_RS4_BE
#else
#define EM_MIPS_RS3_LE	10
#endif
#endif /* !EM_MIPS_RS3_LE */

#define MATCH_MACHINE(x)  (x == EM_MIPS || x == EM_MIPS_RS3_LE)

#define SHT_RELM	SHT_REL
#define Elf32_RelM	Elf32_Rel
