POFILES	= $(wildcard *.po)
TEXTS	= $(POFILES:.po=.h)

all: text_inc.h text_array.h text_textids.h text_langids.h $(TEXTS)

%.h: %.po
	bin/po2h $< >$@

# en.h uses msgids from de.po
en.h text_textids.h: de.po
	bin/po2h en.po >en.h

text_inc.h text_array.h text_langids.h: $(POFILES)
	bin/txtinc en.po $(POFILES)

clean:
	rm -f *.h *~
