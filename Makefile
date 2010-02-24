CC	= gcc
CFLAGS	= -c -g -O2 -Wall -Wno-pointer-sign
LDFLAGS	= -rdynamic -lhd -lblkid -lcurl

SRC	= $(filter-out inflate.c,$(wildcard *.c))
INC	= $(wildcard *.h)
OBJ	= $(SRC:.c=.o)

SUBDIRS	= po mkpsfu

.EXPORT_ALL_VARIABLES:
.PHONY:	all clean install libs

%.o:	%.c
	$(CC) $(CFLAGS) -o $@ $<

all: changelog libs linuxrc

changelog: .git/HEAD
	git2log --log >changelog

VERSION: .git/HEAD
	git2log --version >VERSION

version.h: VERSION
	@echo "#define LXRC_VERSION \"`cut -d. -f1-2 VERSION`\"" >$@
	@echo "#define LXRC_FULL_VERSION \"`cat VERSION`\"" >>$@

linuxrc: $(OBJ)
	$(CC) $(OBJ) $(LDFLAGS) -o $@
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

ifeq ($(filter clean changelog VERSION, $(MAKECMDGOALS)),)
.depend: version.h $(SRC) $(INC)
	@$(MAKE) -C po
	@$(CC) -MM $(CFLAGS) $(SRC) >$@
-include .depend
endif

