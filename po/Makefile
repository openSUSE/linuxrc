POFILES	= $(wildcard *.po)
TEXTS	= $(POFILES:.po=.h)

all: text_inc.h text_array.h text_textids.h text_langids.h $(TEXTS)

%.h: %.po text_textids.h
	bin/po2h $< >$@

# en.h uses msgids from linuxrc.pot
en.h text_textids.h: linuxrc.pot
	bin/po2h $< >en.h

text_inc.h text_array.h text_langids.h: $(POFILES)
	bin/txtinc en.po $(POFILES)

clean:
	rm -f *.h *~
