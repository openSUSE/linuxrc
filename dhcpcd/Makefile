SRC	= $(wildcard *.c)
INC	= $(wildcard *.h)
OBJ	= $(SRC:.c=.o)

%.o:	%.c
	$(CC) -c $(CFLAGS) $(WARN) -o $@ $<

all: dhcpcd.a

dhcpcd.a: $(SRC) $(OBJ) $(INC)
	ar rcs $@ $(OBJ)

clean:
	rm -f $(OBJ) *~ dhcpcd.a .depend

ifneq ($(MAKECMDGOALS),clean)
.depend: $(SRC) $(INC)
	@$(CC) -MM $(CFLAGS) $(SRC) >$@
-include .depend
endif
