include pcmcia/config.mk

TOPDIR	:= $(CURDIR)
ARCH	:= $(shell uname -m)

CC	= gcc
YACC	= bison -y
LEX	= flex -8
CFLAGS	= -O2 -fomit-frame-pointer -c -I$(TOPDIR) $(EXTRA_FLAGS)
LDFLAGS	= -static -Wl,-Map=linuxrc.map
WARN	= -Wstrict-prototypes -Wall

SRC	= $(filter-out inflate.c,$(wildcard *.c))
INC	= $(wildcard *.h)
OBJ	= $(SRC:.c=.o)

SUBDIRS	= insmod loadkeys pcmcia portmap
LIBS	= insmod/insmod.a loadkeys/loadkeys.a pcmcia/pcmcia.a portmap/portmap.a

ifeq ($(ARCH),alpha)
    USE_MINI_GLIBC	= no
    CFLAGS		+= -DLINUXRC_AXP
endif

ifeq ($(ARCH),ppc)
    USE_MINI_GLIBC	= no
endif

ifeq ($(ARCH),sparc)
    USE_MINI_GLIBC	= no
endif

ifneq ($(USE_MINI_GLIBC),no)
    CC		+= -V2.7.2.3			# doesn't work with newer gcc/egcs
    CFLAGS	+= -I$(TOPDIR)/libc/include
    LDFLAGS	+= -u__force_mini_libc_symbols
    SUBDIRS	+= libc
    LIBS	+= libc/mini-libc.a
endif

.EXPORT_ALL_VARIABLES:
.PHONY:	all clean default install libs

%.o:	%.c
	$(CC) $(CFLAGS) $(WARN) -o $@ $<

default:
	libs linuxrc

linuxrc: $(OBJ) $(LIBS)
	$(CC) $(OBJ) $(LIBS) $(LDFLAGS) -lhd -o $@
	@strip -R .note -R .comment $@
ifdef EXTRA_FLAGS
	$(CC) $(OBJ) $(LIBS) $(LDFLAGS) -lhd_tiny -o $@_mini
	@strip -R .note -R .comment $@_mini
	@ls -l linuxrc{,_mini}
else
	@ls -l linuxrc
endif

all: libs linuxrc
	@rm $(OBJ)
	@mv linuxrc linuxrc_tiny
	@make EXTRA_FLAGS=-DUSE_LIBHD linuxrc
	@ls -l linuxrc{,_mini,_tiny}

install: linuxrc
	@install linuxrc /usr/sbin

libs:
	@for d in $(SUBDIRS); do $(MAKE) -C $$d $(MAKECMDGOALS); done

clean: libs
	rm -f $(OBJ) *~ linuxrc linuxrc.map .depend

ifneq ($(MAKECMDGOALS),clean)
.depend: $(SRC) $(INC)
	@$(CC) -MM $(CFLAGS) $(SRC) >$@
-include .depend
endif

