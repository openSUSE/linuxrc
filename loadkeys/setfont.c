/*
 * setfont.c - Eugene Crosser & Andries Brouwer
 *
 * Version 0.96
 *
 * Loads the console font, and possibly the corresponding screen map(s).
 * We accept two kind of screen maps, one [-m] giving the correspondence
 * between some arbitrary 8-bit character set currently in use and the
 * font positions, and the second [-u] giving the correspondence between
 * font positions and Unicode values.
 */
#define VERSION "0.96"

#include <stdio.h>
#include <memory.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/kd.h>
#include <endian.h>
#include "paths.h"
#include "psf.h"

char *progname;

static int position_codepage(int iunit);
static void loadnewfont(int fd, char *ifil, int iunit, int no_m, int no_u);
extern void loadnewmap(int fd, char *mfil);
extern void loadunicodemap(int fd, char *ufil);
extern void activatemap(void);
extern int getfd(void);

/* search for the font in these directories (with trailing /) */
char *fontdirpath[] = { "", DATADIR "/" FONTDIR "/", 0 };
char *fontsuffixes[] = { "", ".psf", ".cp", ".fnt", ".psfu", 0 };

static inline FILE*
findfont(char *fnam) {
    return findfile(fnam, fontdirpath, fontsuffixes);
}

static void
usage(void)
{
        fprintf(stderr, "\
Usage:  %s [-o oldfont] [-fontsize] [newfont] [-m consolemap] [-u unicodemap]
If no -o is given, and no newfont, then the font \"default\" is loaded.
Explicitly (with -m or -u) or implicitly (in the fontfile) given mappings will
be loaded and, in the case of consolemaps, activated.
Options:
 -8, -14, -16	Choose the right font from a codepage that contains three fonts
                (or choose default font, e.g. \"default8x16\").
 -o <filename>	Write current font to file before loading a new one.
 -om <filename>	Write current consolemap to file before loading a new one.
 -ou <filename>	Write current unicodemap to file before loading a new one.
 -m none	Suppress loading and activation of a mapping table.
 -u none	Suppress loading of a unicode map.
 -v		Verbose.
 -V		Print version.
", progname);
	exit(1);
}

int
setfont_main(int argc, char *argv[])
{
	char *ifil, *mfil, *ufil, *ofil, *omfil, *oufil;
	int fd, i, iunit, no_m, no_u;

	progname = argv[0];

	fd = getfd();

	ifil = mfil = ufil = ofil = omfil = oufil = 0;
	iunit = 0;
	no_m = no_u = 0;

	for (i = 1; i < argc; i++) {
	    if (!strcmp(argv[i], "-V")) {
	        printf("setfont version %s\n", VERSION);
	    } else if (!strcmp(argv[i], "-v")) {
	        verbose = 1;
	    } else if (!strcmp(argv[i], "-o")) {
		if (++i == argc || ofil)
		  usage();
		ofil = argv[i];
	    } else if (!strcmp(argv[i], "-om")) {
		if (++i == argc || omfil)
		  usage();
		omfil = argv[i];
	    } else if (!strcmp(argv[i], "-ou")) {
		if (++i == argc || oufil)
		  usage();
		oufil = argv[i];
	    } else if (!strcmp(argv[i], "-m")) {
		if (++i == argc || mfil)
		  usage();
		if (!strcmp(argv[i], "none"))
		  no_m = 1;
		else
		  mfil = argv[i];
	    } else if (!strcmp(argv[i], "-u")) {
		if (++i == argc || ufil)
		  usage();
		if (!strcmp(argv[i], "none"))
		  no_u = 1;
		else
		  ufil = argv[i];
	    } else if(argv[i][0] == '-') {
		iunit = atoi(argv[i]+1);
		if(iunit <= 0 || iunit > 32)
		  usage();
	    } else {
		if (ifil)
		  usage();
		ifil = argv[i];
	    }
	}

	if (!ifil && !mfil && !ufil && !ofil && !omfil && !oufil)
	  /* reset to some default */
	  ifil = "";

	if (mfil) {
	    loadnewmap(fd, mfil);
	    activatemap();
	    no_m = 1;
	}

	if (ufil)
	  no_u = 1;

	if (ifil)
	  loadnewfont(fd, ifil, iunit, no_m, no_u);

	if (ufil)
	  loadunicodemap(fd, ufil);

	return 0;
}

void
do_loadfont(int fd, char *inbuf, int unit, int fontsize) {
	char buf[16384];
	int i;

	memset(buf,0,sizeof(buf));

	if (unit < 1 || unit > 32) {
	    fprintf(stderr, "Bad character size %d\n", unit);
	    exit(1);
	}

	for (i = 0; i < fontsize; i++)
	    memcpy(buf+(32*i), inbuf+(unit*i), unit);

	if (verbose)
	  printf("Loading 8x%d font from file %s\n", unit, pathname);
#if defined( PIO_FONTX ) && !defined( sparc )
	{
	    struct consolefontdesc cfd;

	    cfd.charcount = fontsize;
	    cfd.charheight = unit;
	    cfd.chardata = buf;

	    if (ioctl(fd, PIO_FONTX, &cfd) == 0)
	      return;		/* success */
	    perror("PIO_FONTX ioctl error (trying PIO_FONT)");
	}
#endif
	if (ioctl(fd, PIO_FONT, buf)) {
	    perror("PIO_FONT ioctl error");
	    exit(1);
	}
}

static void
do_loadtable(int fd, unsigned char *inbuf, int tailsz, int fontsize) {
	struct unimapinit advice;
	struct unimapdesc ud;
	struct unipair *up;
	int ct = 0, maxct;
	int glyph;
	u_short unicode;

	maxct = tailsz;		/* more than enough */
	up = (struct unipair *) malloc(maxct * sizeof(struct unipair));
	if (!up) {
	    fprintf(stderr, "Out of memory?\n");
	    exit(1);
	}
	for (glyph = 0; glyph < fontsize; glyph++) {
	    while (tailsz >= 2) {
		unicode = (((u_short) inbuf[1]) << 8) + inbuf[0];
		tailsz -= 2;
		inbuf += 2;
		if (unicode == PSF_SEPARATOR)
		    break;
		up[ct].unicode = unicode;
		up[ct].fontpos = glyph;
		ct++;
	    }
	}

	/* Note: after PIO_UNIMAPCLR and before PIO_UNIMAP
	   this printf did not work on many kernels */
	if (verbose)
	  printf("Loading Unicode mapping table...\n");

	advice.advised_hashsize = 0;
	advice.advised_hashstep = 0;
	advice.advised_hashlevel = 0;
	if(ioctl(fd, PIO_UNIMAPCLR, &advice)) {
#ifdef ENOIOCTLCMD
	    if (errno == ENOIOCTLCMD) {
		fprintf(stderr, "It seems this kernel is older than 1.1.92\n");
		fprintf(stderr, "No Unicode mapping table loaded.\n");
	    } else
#endif
	      perror("PIO_UNIMAPCLR");
	    exit(1);
	}
	ud.entry_ct = ct;
	ud.entries = up;
	if(ioctl(fd, PIO_UNIMAP, &ud)) {
#if 0
	    if (errno == ENOMEM) {
		/* change advice parameters */
	    }
#endif
	    perror("PIO_UNIMAP");
	    exit(1);
	}
}

static void
loadnewfont(int fd, char *ifil, int iunit, int no_m, int no_u) {
	FILE *fpi;
	char defname[20];
	int unit;
	char inbuf[32768];	/* primitive */
	int inputlth, offset;

	if (!*ifil) {
	    /* try to find some default file */

	    if (iunit < 0 || iunit > 32)
	      iunit = 0;
	    if (iunit == 0) {
		if ((fpi = findfont(ifil = "default")) == NULL &&
		    (fpi = findfont(ifil = "default8x16")) == NULL &&
		    (fpi = findfont(ifil = "default8x14")) == NULL &&
		    (fpi = findfont(ifil = "default8x8")) == NULL) {
		    fprintf(stderr, "Cannot find default font\n");
		    exit(1);
		}
	    } else {
		sprintf(defname, "default8x%d", iunit);
		if ((fpi = findfont(ifil = defname)) == NULL) {
		    fprintf(stderr, "Cannot find %s font\n", ifil);
		    exit(1);
		}
	    }
	} else {
	    if ((fpi = findfont(ifil)) == NULL) {
		fprintf(stderr, "Cannot open font file %s\n", ifil);
		exit(1);
	    }
	}

	/*
	 * We used to look at the length of the input file
	 * with stat(); now that we accept compressed files,
	 * just read the entire file.
	 */
	inputlth = fread(inbuf, 1, sizeof(inbuf), fpi);
	if (ferror(fpi)) {
		perror("Error reading input font");
		exit(1);
	}
	/* use malloc/realloc in case of giant files;
	   maybe these do not occur: 16kB for the font,
	   and 16kB for the map leaves 32 unicode values
	   for each font position */
	if (!feof(fpi)) {
		fprintf(stderr, "
Setfont is so naive as to believe that font files
have a size of at most 32kB.  Unfortunately it seems
that you encountered an exception.  If this really is
a font file, (i) recompile setfont, (ii) tell aeb@cwi.nl .
");
		exit(1);
	}
	fpclose(fpi);

	/* test for psf first */
	{
	    struct psf_header psfhdr;
	    int fontsize;
	    int hastable;
	    int head0, head;

	    if (inputlth < sizeof(struct psf_header))
		goto no_psf;

	    psfhdr = * (struct psf_header *) &inbuf[0];

	    if (!PSF_MAGIC_OK(psfhdr))
		goto no_psf;

	    if (psfhdr.mode > PSF_MAXMODE) {
		fprintf(stderr, "Unsupported psf file mode\n");
		exit(1);
	    }
	    fontsize = ((psfhdr.mode & PSF_MODE512) ? 512 : 256);
#if !defined( PIO_FONTX ) || defined( sparc )
	    if (fontsize != 256) {
		fprintf(stderr, "Only fontsize 256 supported\n");
		exit(1);
	    }
#endif
	    hastable = (psfhdr.mode & PSF_MODEHASTAB);
	    unit = psfhdr.charsize;
	    head0 = sizeof(struct psf_header);
	    head = head0 + fontsize*unit;
	    if (head > inputlth || (!hastable && head != inputlth)) {
		fprintf(stderr, "Input file: bad length\n");
		exit(1);
	    }
	    do_loadfont(fd, inbuf + head0, unit, fontsize);
	    if (hastable && !no_u)
	      do_loadtable(fd, inbuf + head, inputlth-head, fontsize);
	    return;
	}
      no_psf:

	/* file with three code pages? */
	if (inputlth == 9780) {
	    offset = position_codepage(iunit);
	    unit = iunit;
	} else {
	    /* bare font */
	    if (inputlth & 0377) {
		fprintf(stderr, "Bad input file size\n");
		exit(1);
	    }
	    offset = 0;
	    unit = inputlth/256;
	}
	do_loadfont(fd, inbuf+offset, unit, 256);
}

static int
position_codepage(int iunit) {
        int offset;

	/* code page: first 40 bytes, then 8x16 font,
	   then 6 bytes, then 8x14 font,
	   then 6 bytes, then 8x8 font */

	if (!iunit) {
	    fprintf(stderr, "\
This file contains 3 fonts: 8x8, 8x14 and 8x16. Please indicate
using an option -8 or -14 or -16 which one you want loaded.\n");
	    exit(1);
	}
	switch (iunit) {
	  case 8:
	    offset = 7732; break;
	  case 14:
	    offset = 4142; break;
	  case 16:
	    offset = 40; break;
	  default:
	    fprintf(stderr, "\
You asked for font size %d, but only 8, 14, 16 are possible here.\n",
		    iunit);
	    exit(1);
	}
	return offset;
}

/* Only on the current console? On all allocated consoles? */
/* A newly allocated console has NORM_MAP by default -
   probably it should copy the default from the current console?
   But what if we want a new one because the current one is messed up? */
/* For the moment: only the current console, only the G0 set */
void
activatemap(void) {
    printf("\033(K");
}

void
disactivatemap(void) {
    printf("\033(B");
}
