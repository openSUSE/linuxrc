CC	= gcc
CFLAGS	= -c -g -O2 -Wall -Wno-pointer-sign $(RPM_OPT_FLAGS)
LDFLAGS	= -rdynamic -lhd -lblkid -lcurl -lreadline -lmediacheck
ARCH	= $(shell /usr/bin/uname -m)
ifeq ($(ARCH),s390x)
LDFLAGS	+= -lqc
endif

GIT2LOG := $(shell if [ -x ./git2log ] ; then echo ./git2log --update ; else echo true ; fi)
GITDEPS := $(shell [ -d .git ] && echo .git/HEAD .git/refs/heads .git/refs/tags)
VERSION := $(shell $(GIT2LOG) --version VERSION ; cat VERSION)
BRANCH  := $(shell [ -d .git ] && git branch | perl -ne 'print $$_ if s/^\*\s*//')
PREFIX  := linuxrc-$(VERSION)

SRC	= $(filter-out inflate.c,$(sort $(wildcard *.c)))
INC	= $(wildcard *.h)
OBJ	= $(SRC:.c=.o)

SUBDIRS	= mkpsfu

.EXPORT_ALL_VARIABLES:
.PHONY:	all clean install libs archive

%.o:	%.c
	$(CC) $(CFLAGS) -o $@ $<

all: changelog libs linuxrc

changelog: $(GITDEPS)
	$(GIT2LOG) --changelog changelog

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

archive: changelog
	@if [ ! -d .git ] ; then echo no git repo ; false ; fi
	mkdir -p package
	git archive --prefix=$(PREFIX)/ $(BRANCH) > package/$(PREFIX).tar
	tar -r -f package/$(PREFIX).tar --mode=0664 --owner=root --group=root --mtime="`git show -s --format=%ci`" --transform='s:^:$(PREFIX)/:' VERSION changelog
	xz -f package/$(PREFIX).tar

clean: libs
	rm -f $(OBJ) *~ linuxrc linuxrc.map linuxrc-debug .depend version.h
	rm -rf package

TAGS: *.c *.h */*.c
	etags $^

ifeq ($(filter clean changelog VERSION, $(MAKECMDGOALS)),)
.depend: version.h $(SRC) $(INC)
	@$(CC) -MM $(CFLAGS) $(SRC) >$@
-include .depend
endif

