/* Machine-specific elf macros for MIPS.  */
#ident "$Id: elf_mips.h,v 1.1 2000/03/23 17:09:55 snwint Exp $"

#define ELFCLASSM	ELFCLASS32
#ifdef __MIPSEB__
#define ELFDATAM	ELFDATA2MSB
#endif
#ifdef __MIPSEL__
#define ELFDATAM	ELFDATA2LSB
#endif

#define MATCH_MACHINE(x)  (x == EM_MIPS || x == EM_MIPS_RS4_BE)

#define SHT_RELM	SHT_REL
#define Elf32_RelM	Elf32_Rel
