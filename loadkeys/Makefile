TARGET=loadkeys.a

all: $(TARGET)

DEFS=-DDATADIR=\"/kbd\"

SRC=loadkeys.c findfile.c analyze.c ksyms.c getfd.c setfont.c \
    mapscrn.c loadunimap.c \
    kdfontop.c psffontop.c
OBJ=loadkeys.o ksyms.o findfile.o getfd.o setfont.o mapscrn.o \
    loadunimap.o \
    kdfontop.o psffontop.o
INC=

$(TARGET): $(SRC) $(OBJ) $(INC)
	ar rcs $@ $(OBJ)

dep:	$(SRC) $(INC)
	$(CC) -M $(CFLAGS) $(SRC) > .depend

clean:
	rm -f $(TARGET) *.o analyze.c loadkeys.c

loadkeys.o:     loadkeys.c analyze.c paths.h ksyms.h
	$(CC) -c $(CFLAGS) $(DEFS) $<

loadkeys.c:	loadkeys.y
	bison -y -p YYY loadkeys.y -o loadkeys.c

analyze.c:	analyze.l
	flex -8 -PYYY -t analyze.l > analyze.c

%.o:	%.c
	$(CC) -c $(CFLAGS) $(DEFS) $(WARN) $<

ifeq (.depend,$(wildcard .depend))
include .depend
endif
