TARGET=mini-libc.a

all: $(TARGET)

CFLAGS += -D_GNU_SOURCE -DMINI_LIBC -DUSE_IN_LIBIO -Iinclude -I.
WARN = -Wall -W

LIBIO = cleanup.c filedoalloc.c fileops.c genops.c iofclose.c iofdopen.c \
        iofflush.c iofgetpos.c iofgets.c iofopen.c iofprintf.c iofputs.c \
        iofread.c iofscanf.c iofsetpos.c ioftell.c iofwrite.c iogetdelim.c \
	iogetline.c iogets.c ioignore.c iopadn.c ioperror.c iopopen.c \
	ioprims.c ioprintf.c ioputs.c ioscanf.c ioseekoff.c ioseekpos.c \
	iosetbuffer.c iosetvbuf.c iosprintf.c iosscanf.c iostrerror.c \
	iotempname.c ioungetc.c iovfprintf.c iovfscanf.c iovsprintf.c \
	iovsscanf.c stdfiles.c strops.c
STDIO = asprintf.c clearerr.c fdopen.c feof.c ferror.c fgetc.c fileno.c \
	fputc.c freopen.c fseek.c getc.c getchar.c getline.c getw.c \
	pclose.c printf-prs.c putc.c putchar.c putw.c reg-printf.c rewind.c \
	setbuf.c setfileno.c setlinebuf.c snprintf.c stdio.c tempnam.c \
	tmpfile.c tmpnam.c vasprintf.c vfscanf.c vprintf.c vscanf.c \
	vsnprintf.c
STDLIB = abort.c strtol.c strtoul.c strtoll.c strtoull.c
STRING = strerror.c
MISC = _strerror.c minimal.c syslog.c
SRC = $(LIBIO) $(STDIO) $(STDLIB) $(STRING) $(MISC)
OBJ = $(SRC:.c=.o)
INC = libioP.h iolibio.h _itoa.h printf-parse.h \
      $(wildcard include/*.h)

$(TARGET): $(OBJ)
	ar rcs $@ $^

$(OBJ): $(INC)

dep:	$(SRC) $(INC)
	$(CC) -M $(CFLAGS) $(SRC) > .depend

clean:
	rm -f $(TARGET) *.o

%.o: %.c
	$(CC) $(CFLAGS) $(WARN) -o $@ -c $<

ifeq (.depend,$(wildcard .depend))
include .depend
endif
