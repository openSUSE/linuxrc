TARGET=insmod.a

ARCH = $(shell uname -m)
ifeq "$(ARCH)" "alpha"
    CFLAGS += -D_GNU_SOURCE -DELF_MACHINE_H='"elf_alpha.h"'
else
    CFLAGS += -D_GNU_SOURCE -DELF_MACHINE_H='"elf_i386.h"'
endif

all: $(TARGET)

SRC1=obj_reloc.c sys_nim.c xstrdup.c insmod.c rmmod.c \
    sys_oim.c obj_common.c sys_cm.c sys_qm.c logger.c \
    sys_dm.c xmalloc.c obj_load.c sys_gks.c xrealloc.c
ifeq "$(ARCH)" "alpha"
    SRC2=obj_alpha.c
    INC=elf_alpha.h
else
    SRC2=obj_i386.c
    INC=elf_i386.h
endif
SRC=$(SRC1) $(SRC2)
OBJ=$(SRC:.c=.o)

$(TARGET): $(SRC) $(OBJ) $(INC)
	ar rcs $(TARGET) $(OBJ)

dep:    $(SRC) $(INC)
	$(CC) -M $(CFLAGS) $(SRC) > .depend

clean:
	rm $(TARGET) *.o

%.o:    %.c
	$(CC) $(CFLAGS) $(WARN) -o $@ $<

ifeq (.depend,$(wildcard .depend))
include .depend
endif

