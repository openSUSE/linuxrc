TARGET=pcmcia.a

all: $(TARGET)

SRC=pcic_probe.c cardmgr.c yacc_config.c lex_config.c
OBJ=pcic_probe.o cardmgr.o yacc_config.o lex_config.o
INC=i82365.h cirrus.h tcic.h vg468.h cardmgr.h

YACC = bison -y
YFLAGS = -d

$(TARGET): $(SRC) $(OBJ) $(INC)
	ar rcs $(TARGET) $(OBJ)

dep:	$(SRC) $(INC)
	$(CC) -M $(CFLAGS) $(SRC) > .depend

clean:
	rm -f $(TARGET) *.o *~ y.tab.h yacc_config.c lex_config.c yacc_config.h

%.o:	%.c
	$(CC) $(CFLAGS) $(WARN) -o $@ $<

yacc_config.c:    yacc_config.y
	bison -y -d $<
	mv y.tab.c yacc_config.c
	mv y.tab.h yacc_config.h

ifeq (.depend,$(wildcard .depend))
include .depend
endif
