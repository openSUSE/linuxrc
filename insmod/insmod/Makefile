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

DEFSNOARCH	:= -I../include -D_GNU_SOURCE  -DCOMPAT_2_0=1  $(EXTRA_DEFS)
DEFS		:= -DELF_MACHINE_H='"elf_$(ARCH).h"' -DARCH_$(ARCH)
COMBDEFS	:= -DCOMBINE_rmmod

INSMODOBJS	:= insmod.o rmmod.o

ifeq (no,yes)
INSMODOBJS	+= insmod_64.o
MODINFOOBJS	+= modinfo_64.o
DEFSNOARCH	+= -DCOMMON_3264
DEFS		+= -DONLY_32
DEFS64		:= -DELF_MACHINE_H='"elf_sparc64.h"' -DARCH_sparc64 -DONLY_64
endif

INSMODOBJS	+= $(addsuffix .o, $(COMB))

#=====================================================================

# Rule for building "to-be-combined" modutils object files
%.o: %.c
	$(CC) $(CFLAGS) $(DEFSNOARCH) $(DEFS) $(COMBDEFS) -c -o $@ $<

# Rule for building 64 bit versions, only used when building common code
# for 32 and 64 bit systems.
%_64.o: %.c
	$(CC) $(CFLAGS) $(DEFSNOARCH) $(DEFS64) $(COMBDEFS) -c -o $@ $<

# Rule for building "normal" modutils executables (non-combined)
%: %.c ../util/libutil.a
	$(CC) $(CFLAGS) $(DEFSNOARCH) $(DEFS) -o $@ $^ $(LDFLAGS)

#=====================================================================

all: .depend ../insmod.a

../insmod.a: ../obj/libobj.a ../util/libutil.a insmod.o rmmod.o
	@mkdir x
	@cd x; $(AR) x ../../obj/libobj.a
	@cd x; $(AR) x ../../util/libutil.a
	@cp insmod.o rmmod.o x
	@cd x; $(AR) r ../../insmod.a *.o
	@rm -rf x

insmod: ../insmod.a
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

clean:
	rm -f *.o insmod ../insmod.a *.static .depend *~

dep depend .depend: insmod.c rmmod.c
	$(CC) -M $(CFLAGS) $(DEFSNOARCH) $(DEFS) $^ > .depend

-include .depend
