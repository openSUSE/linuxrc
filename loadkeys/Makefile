TARGET=loadkeys.a

all: $(TARGET)

SRC=loadkeys.c findfile.c analyze.c ksyms.c getfd.c setfont.c \
    mapscrn.c loadunimap.c
OBJ=loadkeys.o ksyms.o findfile.o getfd.o setfont.o mapscrn.o \
    loadunimap.o
INC=

$(TARGET): $(SRC) $(OBJ) $(INC)
	ar rcs $@ $(OBJ)

dep:	$(SRC) $(INC)
	$(CC) -M $(CFLAGS) $(SRC) > .depend

clean:
	rm -f $(TARGET) *.o analyze.c loadkeys.c

loadkeys.o:     loadkeys.c analyze.c paths.h ksyms.h
	$(CC) -pipe $(CFLAGS) $<

loadkeys.c:	loadkeys.y
	bison -y -p YYY loadkeys.y -o loadkeys.c

analyze.c:	analyze.l
	flex -8 -PYYY -t analyze.l > analyze.c

%.o:	%.c
	$(CC) $(CFLAGS) $(WARN) -o $@ $<

ifeq (.depend,$(wildcard .depend))
include .depend
endif
