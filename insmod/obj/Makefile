ARCH		:= $(shell uname -m)
ifeq ($(ARCH),i686)
    ARCH	:= i386
endif
ifeq ($(ARCH),i586)
    ARCH	:= i386
endif
CC		= gcc
CFLAGS		= -O2 -Wall
AR		= ar
RANLIB		= ranlib

DEFSNOARCH	:= -I../include -D_GNU_SOURCE -DCONFIG_ROOT_CHECK_OFF=0 $(EXTRA_DEFS)
DEFS		:= -DELF_MACHINE_H='"elf_$(ARCH).h"' -DARCH_$(ARCH)

LIBOBJ_OBJS	:= obj_kallsyms.o obj_common.o obj_load.o obj_reloc.o obj_$(ARCH).o
LIBOBJ_SRCS	:= $(LIBOBJ_OBJS:.o=.c)

ifeq ($(ARCH),sparc)
LIBOBJ_OBJS	+= obj_kallsyms_64.o obj_common_64.o obj_load_64.o obj_reloc_64.o obj_sparc64_64.o
LIBOBJ_SRCS	+= obj_sparc64.c
DEFSNOARCH	+= -DCOMMON_3264
DEFS		+= -DONLY_32
DEFS64		:= -DELF_MACHINE_H='"elf_sparc64.h"' -DARCH_sparc64 -DONLY_64
endif

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
