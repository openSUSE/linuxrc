CC	 = gcc
CFLAGS	 = -Wall -O2 -fomit-frame-pointer

.PHONY: all fonts font1 font2 clean

all: mkpsfu

mkpsfu: mkpsfu.c
	$(CC) $(CFLAGS) $< -o $@

linuxrc.txt:
	cat ../po/*.po ../settings.c >linuxrc.txt

fonts: font1 font2

font1: linuxrc.txt
	./mkpsfu -t linuxrc.txt \
	  -a 0xfffd \
	  -a lat9u-16:0xa4=0x20ac \
	  -a lat2-16:0x2500,0x2502,0x250c,0x2510,0x2514,0x2518,0x251c,0x2524,0x252c,0x2534,0x253c,0x2592 \
	  -a 0x20-0x7e \
	  --gfx-char 0x2500-0x257f \
	  --fsize 0,1 -f lat1-16 \
	  --fsize 0,0 -f lat2-16 \
	  --fsize 0,1 -f lat9u-16 \
	  --fsize 0,1 -f iso07u-16 \
	  --fsize 0,1 -f koi8u_8x16 \
	  --fsize 0,0 -f LatArCyrHeb-16 \
	  -v linuxrc-16.psfu >linuxrc-16.psfu.log

font2: linuxrc.txt
	./mkpsfu -t linuxrc.txt \
	  -a 0xfffd \
	  -a lat9u-16:0xa4=0x20ac \
	  -a lat2-16:0x2500,0x2502,0x250c,0x2510,0x2514,0x2518,0x251c,0x2524,0x252c,0x2534,0x253c,0x2592 \
          -a lat2-16:0x2551,0x2552,0x2554,0x2557,0x255e,0x255f,0x2560,0x2561,0x2563,0x2566,0x256a,0x256c,0x2584,0x2588,0x258c,0x2590,0x2591,0x2593 \
          -c iso-8859-2 -c iso-8859-7 -c iso-8859-15 -c koi8-r \
	  --gfx-char 0x2500-0x257f \
	  --fsize 0,1 -f lat1-16 \
	  --fsize 0,0 -f lat2-16 \
	  --fsize 0,1 -f lat9u-16 \
	  --fsize 0,1 -f iso07u-16 \
	  --fsize 0,1 -f koi8u_8x16 \
	  --fsize 0,0 -f LatArCyrHeb-16 \
	  -v linuxrc2-16.psfu >linuxrc2-16.psfu.log

clean:
	@rm -f mkpsfu *~ *.log linuxrc.txt
