# SuSE release number, needed for driver update feature
LX_REL	?= -DLX_REL=\"7.2\"

include pcmcia/config.mk

TOPDIR	:= $(CURDIR)
ARCH	:= $(shell uname -m)
ifeq "$(ARCH)" "i486"
ARCH	:= i386
endif
ifeq "$(ARCH)" "i586"
ARCH	:= i386
endif
ifeq "$(ARCH)" "i686"
ARCH	:= i386
endif

CC	= gcc
YACC	= bison -y
LEX	= flex -8
CFLAGS	= -O1 -fomit-frame-pointer -c -I$(TOPDIR) $(EXTRA_FLAGS) $(LX_REL)
LDFLAGS	= -static -Wl,-Map=linuxrc.map
WARN	= -Wstrict-prototypes -Wall
LIBHDFL	= -DUSE_LIBHD

SRC	= $(filter-out inflate.c,$(wildcard *.c))
INC	= $(wildcard *.h)
OBJ	= $(SRC:.c=.o)

SUBDIRS	= insmod loadkeys pcmcia portmap
LIBS	= insmod/insmod.a loadkeys/loadkeys.a pcmcia/pcmcia.a portmap/portmap.a

ifeq ($(ARCH),i386)
    CFLAGS		+= -DLX_ARCH=\"i386\"
endif

ifeq ($(ARCH),alpha)
    USE_MINI_GLIBC	= no
    CFLAGS		+= -DLINUXRC_AXP -DLX_ARCH=\"axp\"
endif

ifeq ($(ARCH),ppc)
    USE_MINI_GLIBC	= no
    SUBDIRS		:= $(filter-out pcmcia, $(SUBDIRS))
    LIBS		:= $(filter-out pcmcia/pcmcia.a, $(LIBS))
    OBJ			:= $(filter-out pcmcia.o, $(OBJ))
    CFLAGS		+= -DLX_ARCH=\"ppc\"
endif

ifeq ($(ARCH),sparc)
    USE_MINI_GLIBC	= yes
    SUBDIRS		:= $(filter-out pcmcia, $(SUBDIRS))
    LIBS		:= $(filter-out pcmcia/pcmcia.a, $(LIBS))
    OBJ			:= $(filter-out pcmcia.o, $(OBJ))
    CFLAGS		+= -DLX_ARCH=\"sparc\"
endif

ifeq ($(ARCH),sparc64)
    USE_MINI_GLIBC	= no
    SUBDIRS		:= $(filter-out pcmcia, $(SUBDIRS))
    LIBS		:= $(filter-out pcmcia/pcmcia.a, $(LIBS))
    OBJ			:= $(filter-out pcmcia.o, $(OBJ))
    CFLAGS		+= -DLX_ARCH=\"sparc64\"
endif

ifeq ($(ARCH),ia64)
    USE_MINI_GLIBC	= no
    CFLAGS		+= -DLX_ARCH=\"ia64\"
endif

ifeq ($(ARCH),s390)
    USE_MINI_GLIBC	= no
    SUBDIRS		:= $(filter-out pcmcia, $(SUBDIRS))
    LIBS		:= $(filter-out pcmcia/pcmcia.a, $(LIBS))
    OBJ			:= $(filter-out pcmcia.o, $(OBJ))
    CFLAGS		+= -DLX_ARCH=\"s390\"
endif

ifneq ($(USE_MINI_GLIBC),no)
    CFLAGS	+= -I$(TOPDIR)/libc/include
    LDFLAGS	+= -u__force_mini_libc_symbols
    SUBDIRS	+= libc
    LIBS	+= libc/mini-libc.a
endif

.EXPORT_ALL_VARIABLES:
.PHONY:	all clean install libs

%.o:	%.c
	$(CC) $(CFLAGS) $(LIBHDFL) $(WARN) -o $@ $<

all: libs linuxrc

version.h: VERSION
	@echo "#define LXRC_VERSION \"`cut -d. -f1-2 VERSION`\"" >$@
	@echo "#define LXRC_FULL_VERSION \"`cat VERSION`\"" >>$@

linuxrc: $(OBJ) $(LIBS)
	$(CC) $(OBJ) $(LIBS) $(LDFLAGS) -lhd_tiny -o $@
	@strip -R .note -R .comment $@
	@ls -l linuxrc

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
