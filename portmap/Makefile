TARGET=portmap.a

all: $(TARGET)

SRC=portmap.c pmap_check.c from_local.c
OBJ=$(SRC:.c=.o)
INC=pmap_check.h

$(TARGET): $(SRC) $(OBJ) $(INC)
	ar rcs $(TARGET) $(OBJ)

dep:    $(SRC) $(INC)
	$(CC) -M $(CFLAGS) $(SRC) > .depend

clean:
	rm -f $(TARGET) *.o

%.o:    %.c
	$(CC) $(CFLAGS) -o $@ $<

ifeq (.depend,$(wildcard .depend))
include .depend
endif

