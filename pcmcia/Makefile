TARGET=pcmcia.a

all: $(TARGET)

SRC=probe.c cardmgr.c yacc_config.c lex_config.c
OBJ=probe.o cardmgr.o yacc_config.o lex_config.o
INC=i82365.h cirrus.h tcic.h vg468.h cardmgr.h

YFLAGS = -d

$(TARGET): $(SRC) $(OBJ) $(INC)
	ar rcs $(TARGET) $(OBJ)

dep:	$(SRC) $(INC)
	$(CC) -M $(CFLAGS) $(SRC) > .depend

clean:
	rm -f $(TARGET) *.o yacc_config.c lex_config.c

%.o:	%.c
	$(CC) $(CFLAGS) $(WARN) -o $@ $<

ifeq (.depend,$(wildcard .depend))
include .depend
endif
