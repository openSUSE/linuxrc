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

DEFS = -I../include -D_GNU_SOURCE  -DCOMPAT_2_0=1 
DEFS += -DELF_MACHINE_H='"elf_$(ARCH).h"' -DARCH_$(ARCH) $(EXTRA_DEFS)

%.o: %.c
	$(CC) $(CFLAGS) $(DEFS) -c -o $@ $<

######################################################################

all: .depend libutil.a

OBJS = xmalloc.o xrealloc.o xstrdup.o logger.o modstat.o \
	meta_expand.o config.o snap_shot.o arch64.o sys_nim.o
ifneq ($(ARCH),ia64)
OBJS += sys_cm.o sys_dm.o sys_gks.o sys_nim.o sys_oim.o sys_qm.o
endif

meta_expand.o: meta_expand.c
	$(CC) $(CFLAGS) $(DEFS) -DHAVE_WORDEXP=1 -DHAVE_GLOB=1 -c $<

libutil.a: $(OBJS)
	$(AR) r $@ $?
	$(RANLIB) $@

clean:
	rm -f *.o *.a .depend *~

dep depend .depend: $(OBJS:.o=.c)
	$(CC) -M $(CFLAGS) $(DEFS) $^ > .depend

-include .depend
