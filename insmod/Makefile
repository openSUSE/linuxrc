TARGETS = all clean
SUBDIRS = util obj insmod

$(TARGETS)::
	@for i in $(SUBDIRS); do $(MAKE) -C $$i $@ ; done

