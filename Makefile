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
ifeq "$(ARCH)" "armv5tel"
ARCH	:= armv4l
endif

CC_DIET	= diet gcc
CC_UC	= /opt/$(ARCH)-linux-uclibc/usr/bin/gcc

CC	= gcc
YACC	= bison -y
LEX	= flex -8
# _No_ -fomit-frame-pointer! It makes linuxrc larger (after compression).
CFLAGS	= -g -O1 -c -I$(TOPDIR) $(EXTRA_FLAGS)

#LDFLAGS	= -static -Wl,-Map=linuxrc.map
ifeq ($(CC),$(CC_DIET))
LDFLAGS	+= -lrpc -lcompat -lhd_tiny_diet -lsysfs
else
ifeq ($(CC),$(CC_UC))
LDFLAGS	+= -lhd_tiny_uc -lsysfs
else
# LDFLAGS	+= -lhd_tiny -lsysfs -lresolv
LDFLAGS	+= -lhd_tiny
endif
endif

WARN	= -Wall -Wno-pointer-sign
LIBHDFL	= -DUSE_LIBHD

SRC	= $(filter-out inflate.c,$(wildcard *.c))
INC	= $(wildcard *.h)
OBJ	= $(SRC:.c=.o)

SUBDIRS	= po dhcpcd mkpsfu
LIBS	= dhcpcd/dhcpcd.a

ifeq ($(ARCH),i386)
    CFLAGS		+= -DLX_ARCH=\"i386\"
endif

ifeq ($(ARCH),x86_64)
    CFLAGS		+= -DLX_ARCH=\"x86_64\"
endif

ifeq ($(ARCH),alpha)
    CFLAGS		+= -DLINUXRC_AXP -DLX_ARCH=\"axp\"
endif

ifneq ($(findstring $(ARCH),ppc ppc64),)
    CFLAGS		+= -DLX_ARCH=\"ppc\"
endif

ifeq ($(ARCH),sparc)
    CFLAGS		+= -DLX_ARCH=\"sparc\"
endif

ifeq ($(ARCH),sparc64)
    CFLAGS		+= -DLX_ARCH=\"sparc64\"
endif

ifeq ($(ARCH),ia64)
    CFLAGS		+= -DLX_ARCH=\"ia64\"
endif

ifeq ($(ARCH),mips)
    CFLAGS		+= -DLX_ARCH=\"mips\"
endif

ifeq ($(ARCH),armv4l)
    CFLAGS		+= -DLX_ARCH=\"armv4l\"
endif

ifneq (,$(findstring -$(ARCH)-,-s390-s390x-))
    CFLAGS		+= -DLX_ARCH=\"s390\"
endif

.EXPORT_ALL_VARIABLES:
.PHONY:	all clean install libs tiny uc tinyuc diet tinydiet

%.o:	%.c
	$(CC) $(CFLAGS) $(LIBHDFL) $(WARN) -o $@ $<

all: libs linuxrc

tiny:
	$(MAKE) EXTRA_FLAGS+="-DLXRC_TINY=1"

uc:
	$(MAKE) CC="$(CC_UC)" EXTRA_FLAGS+="-DUCLIBC"

tinyuc:
	$(MAKE) CC="$(CC_UC)" EXTRA_FLAGS+="-DUCLIBC -DLXRC_TINY=1"

diet:
	$(MAKE) CC="$(CC_DIET)" EXTRA_FLAGS+="-DDIET"

tinydiet:
	$(MAKE) CC="$(CC_DIET)" EXTRA_FLAGS+="-DDIET -DLXRC_TINY=1"

version.h: VERSION
	@echo "#define LXRC_VERSION \"`cut -d. -f1-2 VERSION`\"" >$@
	@echo "#define LXRC_FULL_VERSION \"`cat VERSION`\"" >>$@

linuxrc: $(OBJ) $(LIBS)
	$(CC) $(OBJ) $(LIBS) $(LDFLAGS) -o $@
	@cp $@ $(@)-debug
	@strip -R .note -R .comment $@
	@ls -l linuxrc
	@mv $(@)-debug $@

install: linuxrc
	install -m 755 linuxrc $(DESTDIR)/usr/sbin
	install -m 755 mkpsfu/mkpsfu $(DESTDIR)/usr/bin
	install -d -m 755 $(DESTDIR)/usr/share/linuxrc
	gzip -c9 mkpsfu/linuxrc-16.psfu >$(DESTDIR)/usr/share/linuxrc/linuxrc-16.psfu.gz
	gzip -c9 mkpsfu/linuxrc2-16.psfu >$(DESTDIR)/usr/share/linuxrc/linuxrc2-16.psfu.gz

libs:
	@for d in $(SUBDIRS); do $(MAKE) -C $$d $(MAKECMDGOALS); done

clean: libs
	rm -f $(OBJ) *~ linuxrc linuxrc.map linuxrc-debug .depend version.h

TAGS: *.c *.h */*.c */*.h
	etags *.c *.h */*.c */*.h

ifneq ($(MAKECMDGOALS),clean)
.depend: version.h $(SRC) $(INC)
	@$(MAKE) -C po
	@$(CC) -MM $(CFLAGS) $(SRC) >$@
-include .depend
endif
