CC	 = gcc
CFLAGS	 = -Wall -O2 -fomit-frame-pointer $(RPM_OPT_FLAGS)

.PHONY: all clean

all: edid-write

edid-write: edid-write.c
	$(CC) $(CFLAGS) $< -o $@
clean:
	@rm -f edid-write *~
