/* Machine-specific elf macros for the PowerPC64.  */

#define ELFCLASSM	ELFCLASS64
#define ELFDATAM	ELFDATA2MSB

#define MATCH_MACHINE(x)  (x == EM_PPC64)

#define SHT_RELM	SHT_RELA
#define Elf64_RelM	Elf64_Rela

struct obj_file;
extern int ppc64_process_syms (struct obj_file *);
extern Elf64_Addr ppc64_module_base (struct obj_file *);

/* these ought to be eventually available in /usr/include/elf.h */
#ifndef EM_PPC64
#define EM_PPC64 21
#endif

#ifndef R_PPC64_ADDR64
#define R_PPC64_ADDR64          38
#define R_PPC64_ADDR16_HIGHER   39
#define R_PPC64_ADDR16_HIGHERA  40
#define R_PPC64_ADDR16_HIGHEST  41
#define R_PPC64_ADDR16_HIGHESTA 42
#define R_PPC64_UADDR64         43
#define R_PPC64_REL64           44
#define R_PPC64_PLT64           45
#define R_PPC64_PLTREL64        46
#define R_PPC64_TOC16           47
#define R_PPC64_TOC16_LO        48
#define R_PPC64_TOC16_HI        49
#define R_PPC64_TOC16_HA        50
#define R_PPC64_TOC             51
#define R_PPC64_PLTGOT16        52
#define R_PPC64_PLTGOT16_LO     53
#define R_PPC64_PLTGOT16_HI     54
#define R_PPC64_PLTGOT16_HA     55
#define R_PPC64_ADDR16_DS       56
#define R_PPC64_ADDR16_LO_DS    57
#define R_PPC64_GOT16_DS        58
#define R_PPC64_GOT16_LO_DS     59
#define R_PPC64_PLT16_LO_DS     60
#define R_PPC64_SECTOFF_DS      61
#define R_PPC64_SECTOFF_LO_DS   62
#define R_PPC64_TOC16_DS        63
#define R_PPC64_TOC16_LO_DS     64
#define R_PPC64_PLTGOT16_DS     65
#define R_PPC64_PLTGOT16_LO_DS  66
#define R_PPC64_NONE            R_PPC_NONE
#define R_PPC64_ADDR32          R_PPC_ADDR32
#define R_PPC64_ADDR24          R_PPC_ADDR24
#define R_PPC64_ADDR16          R_PPC_ADDR16
#define R_PPC64_ADDR16_LO       R_PPC_ADDR16_LO
#define R_PPC64_ADDR16_HI       R_PPC_ADDR16_HI
#define R_PPC64_ADDR16_HA       R_PPC_ADDR16_HA
#define R_PPC64_ADDR14          R_PPC_ADDR14
#define R_PPC64_ADDR14_BRTAKEN  R_PPC_ADDR14_BRTAKEN
#define R_PPC64_ADDR14_BRNTAKEN R_PPC_ADDR14_BRNTAKEN
#define R_PPC64_REL24           R_PPC_REL24
#define R_PPC64_REL14           R_PPC_REL14
#define R_PPC64_REL14_BRTAKEN   R_PPC_REL14_BRTAKEN
#define R_PPC64_REL14_BRNTAKEN  R_PPC_REL14_BRNTAKEN
#define R_PPC64_GOT16           R_PPC_GOT16
#define R_PPC64_GOT16_LO        R_PPC_GOT16_LO
#define R_PPC64_GOT16_HI        R_PPC_GOT16_HI
#define R_PPC64_GOT16_HA        R_PPC_GOT16_HA
#define R_PPC64_COPY            R_PPC_COPY
#define R_PPC64_GLOB_DAT        R_PPC_GLOB_DAT
#define R_PPC64_JMP_SLOT        R_PPC_JMP_SLOT
#define R_PPC64_RELATIVE        R_PPC_RELATIVE
#define R_PPC64_UADDR32         R_PPC_UADDR32
#define R_PPC64_UADDR16         R_PPC_UADDR16
#define R_PPC64_REL32           R_PPC_REL32
#define R_PPC64_PLT32           R_PPC_PLT32
#define R_PPC64_PLTREL32        R_PPC_PLTREL32
#define R_PPC64_PLT16_LO        R_PPC_PLT16_LO
#define R_PPC64_PLT16_HI        R_PPC_PLT16_HI
#define R_PPC64_PLT16_HA        R_PPC_PLT16_HA
#define R_PPC64_SECTOFF         R_PPC_SECTOFF
#define R_PPC64_SECTOFF_LO      R_PPC_SECTOFF_LO
#define R_PPC64_SECTOFF_HI      R_PPC_SECTOFF_HI
#define R_PPC64_SECTOFF_HA      R_PPC_SECTOFF_HA
#define R_PPC64_ADDR30          R_PPC_ADDR30
#define R_PPC64_GNU_VTINHERIT   R_PPC_GNU_VTINHERIT
#define R_PPC64_GNU_VTENTRY     R_PPC_GNU_VTENTRY
#endif	/* R_PPC64_ADDR64 */
