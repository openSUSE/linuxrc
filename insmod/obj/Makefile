ARCH		:= $(shell uname -m)
ifeq ($(ARCH),i686)
    ARCH	:= i386
endif
ifeq ($(ARCH),i586)
    ARCH	:= i386
endif
CFLAGS		= -O2 -Wall
AR		= ar
RANLIB		= ranlib

DEFSNOARCH	:= -I../include -D_GNU_SOURCE -DCONFIG_ROOT_CHECK_OFF=0 $(EXTRA_DEFS)
DEFS		:= -DELF_MACHINE_H='"elf_$(ARCH).h"' -DARCH_$(ARCH)

LIBOBJ_OBJS	:= obj_kallsyms.o obj_common.o obj_load.o obj_reloc.o obj_$(ARCH).o obj_gpl_license.o
LIBOBJ_SRCS	:= $(LIBOBJ_OBJS:.o=.c)


# arch specific definitions for common 32/64 code

ifeq ($(ARCH),sparc)
LIBOBJ_OBJS	+= obj_sparc64_64.o
LIBOBJ_SRCS	+= obj_sparc64.c
DEFS64		:= -DELF_MACHINE_H='"elf_sparc64.h"' -DARCH_sparc64
endif

ifeq ($(ARCH),xx_hppa)
LIBOBJ_OBJS	+= obj_hppa64_64.o
LIBOBJ_SRCS	+= obj_hppa64.c
DEFS64		:= -DELF_MACHINE_H='"elf_hppa64.h"' -DARCH_hppa64
endif

ifeq ($(ARCH),ppc)
LIBOBJ_OBJS	+= obj_ppc64_64.o
LIBOBJ_SRCS	+= obj_ppc64.c
DEFS64		:= -DELF_MACHINE_H='"elf_ppc64.h"' -DARCH_ppc64
endif

# arch independent definitions for common 32/64 code

ifdef DEFS64
LIBOBJ_OBJS	+= obj_kallsyms_64.o obj_common_64.o obj_load_64.o obj_reloc_64.o obj_gpl_license_64.o
DEFSNOARCH	+= -DCOMMON_3264
DEFS		+= -DONLY_32
DEFS64		+= -DONLY_64
endif


#ifeq ($(ARCH),sparc)
#LIBOBJ_OBJS	+= obj_kallsyms_64.o obj_common_64.o obj_load_64.o obj_reloc_64.o obj_sparc64_64.o
#LIBOBJ_SRCS	+= obj_sparc64.c
#DEFSNOARCH	+= -DCOMMON_3264
#DEFS		+= -DONLY_32
#DEFS64		:= -DELF_MACHINE_H='"elf_sparc64.h"' -DARCH_sparc64 -DONLY_64
#endif

%.o: %.c
	$(CC) $(CFLAGS) $(DEFSNOARCH) $(DEFS) -c -o $@ $<

# Rule to make 64 bit versions, only used when building common code for
# 32 and 64 bit systems.
%_64.o: %.c
	$(CC) $(CFLAGS) $(DEFSNOARCH) $(DEFS64) -c -o $@ $<

######################################################################

all: .depend libobj.a

libobj.a: $(LIBOBJ_OBJS)
	$(AR) r $@ $?
	$(RANLIB) $@

clean:
	rm -f *.o *.a .depend *~

dep depend .depend: $(LIBOBJ_SRCS)
	$(CC) -M $(CFLAGS) $(DEFSNOARCH) $(DEFS) $^ > .depend

-include .depend
