/*
 * setfont.c - Eugene Crosser & Andries Brouwer
 *
 * Version 1.00
 *
 * Loads the console font, and possibly the corresponding screen map(s).
 * We accept two kind of screen maps, one [-m] giving the correspondence
 * between some arbitrary 8-bit character set currently in use and the
 * font positions, and the second [-u] giving the correspondence between
 * font positions and Unicode values.
 */

#include "dietlibc.h"

#include <stdio.h>
#include <memory.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/kd.h>
#include <endian.h>
#include <sysexits.h>
#include "paths.h"
#include "getfd.h"
#include "findfile.h"
#include "loadunimap.h"
#include "psf.h"
#include "psffontop.h"
#include "kdfontop.h"
#include "xmalloc.h"
#include "nls.h"
#include "version.h"

static int position_codepage(int iunit);
static void saveoldfont(int fd, char *ofil);
static void saveoldfontplusunicodemap(int fd, char *Ofil);
static void loadnewfont(int fd, char *ifil,
			int iunit, int hwunit, int no_m, int no_u);
static void loadnewfonts(int fd, char **ifiles, int ifilct,
			int iunit, int hwunit, int no_m, int no_u);
static void restorefont(int fd);
extern void saveoldmap(int fd, char *omfil);
extern void loadnewmap(int fd, char *mfil);
extern void activatemap(void);
extern void disactivatemap(void);

extern int verbose;
int force = 0;
int debug = 0;

/* search for the font in these directories (with trailing /) */
char *fontdirpath[] = { "", DATADIR "/" FONTDIR "/", 0 };
char *fontsuffixes[] = { "", ".psfu", ".psf", ".cp", ".fnt", 0 };
/* hide partial fonts a bit - loading a single one is a bad idea */
char *partfontdirpath[] = { "", DATADIR "/" FONTDIR "/" PARTIALDIR "/", 0 };
char *partfontsuffixes[] = { "", 0 };

static inline FILE*
findfont(char *fnam) {
    return findfile(fnam, fontdirpath, fontsuffixes);
}

static inline FILE*
findpartialfont(char *fnam) {
    return findfile(fnam, partfontdirpath, partfontsuffixes);
}

static void
usage(void)
{
        fprintf(stderr, _(
"Usage: setfont [write-options] [-<N>] [newfont..] [-m consolemap] [-u unicodemap]\n"
"  write-options (take place before file loading):\n"
"    -o  <filename>	Write current font to <filename>\n"
"    -om <filename>	Write current consolemap to <filename>\n"
"    -ou <filename>	Write current unicodemap to <filename>\n"
"If no newfont and no -[o|om|ou|m|u] option is given, a default font is loaded:\n"
"    setfont             Load font \"default[.gz]\"\n"
"    setfont -<N>        Load font \"default8x<N>[.gz]\"\n"
"The -<N> option selects a font from a codepage that contains three fonts:\n"
"    setfont -{8|14|16} codepage.cp[.gz]   Load 8x<N> font from codepage.cp\n"
"Explicitly (with -m or -u) or implicitly (in the fontfile) given mappings will\n"
"be loaded and, in the case of consolemaps, activated.\n"
"    -h<N>       (no space) Override font height.\n"
"    -m none	Suppress loading and activation of a mapping table.\n"
"    -u none	Suppress loading of a unicode map.\n"
"    -v		Be verbose.\n"
"    -V		Print version and exit.\n"
"Files are loaded from the current directory or /usr/lib/kbd/*/.\n"
));
	exit(EX_USAGE);
}

#define MAXIFILES 256

int
setfont_main(int argc, char *argv[])
{
	char *ifiles[MAXIFILES], *mfil, *ufil, *Ofil, *ofil, *omfil, *oufil;
	int ifilct = 0, fd, i, iunit, hwunit, no_m, no_u;
	int restore = 0;

	set_progname(argv[0]);

#if 0
	setlocale(LC_ALL, "");
#endif
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);

	fd = getfd();

	ifiles[0] = mfil = ufil = Ofil = ofil = omfil = oufil = 0;
	iunit = hwunit = 0;
	no_m = no_u = 0;

	for (i = 1; i < argc; i++) {
	    if (!strcmp(argv[i], "-V")) {
		print_version_and_exit();
	    } else if (!strcmp(argv[i], "-v")) {
	        verbose = 1;
	    } else if (!strcmp(argv[i], "-d")) {
		debug = 1;
	    } else if (!strcmp(argv[i], "-R")) {
		restore = 1;
	    } else if (!strcmp(argv[i], "-O")) {
		if (++i == argc || Ofil)
		  usage();
		Ofil = argv[i];
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
	    } else if (!strcmp(argv[i], "-f")) {
		force = 1;
	    } else if (!strncmp(argv[i], "-h", 2)) {
		hwunit = atoi(argv[i]+2);
		if (hwunit <= 0 || hwunit > 32)
		  usage();
	    } else if (argv[i][0] == '-') {
		iunit = atoi(argv[i]+1);
		if(iunit <= 0 || iunit > 32)
		  usage();
	    } else {
		if (ifilct == MAXIFILES) {
		    fprintf(stderr, _("setfont: too many input files\n"));
		    exit(EX_USAGE);
		}
		ifiles[ifilct++] = argv[i];
	    }
	}

	if (ifilct && restore) {
	    fprintf(stderr, _("setfont: cannot both restore from character ROM"
			      " and from file. Font unchanged.\n"));
	    exit(EX_USAGE);
	}

	if (!ifilct && !mfil && !ufil &&
	    !Ofil && !ofil && !omfil && !oufil && !restore)
	  /* reset to some default */
	  ifiles[ifilct++] = "";

	if (Ofil)
	  saveoldfontplusunicodemap(fd, Ofil);

	if (ofil)
	  saveoldfont(fd, ofil);

	if (omfil)
	  saveoldmap(fd, omfil);

	if (oufil)
	  saveunicodemap(fd, oufil);

	if (mfil) {
	    loadnewmap(fd, mfil);
	    activatemap();
	    no_m = 1;
	}

	if (ufil)
	  no_u = 1;

	if (restore)
	  restorefont(fd);

	if (ifilct)
	  loadnewfonts(fd, ifiles, ifilct, iunit, hwunit, no_m, no_u);

	if (ufil)
	  loadunicodemap(fd, ufil);

	return 0;
}

static void
restorefont(int fd) {
	/* On most kernels this won't work since it is not supported
	   when BROKEN_GRAPHICS_PROGRAMS is defined, and that is defined
	   by default.  Moreover, this is not defined for vgacon. */
	if (ioctl(fd, PIO_FONTRESET, 0)) {
		perror("PIO_FONTRESET");
	}
}

/*
 * 0 - do not test, 1 - test and warn, 2 - test and wipe, 3 - refuse
 */
static int erase_mode = 1;

static void
do_loadfont(int fd, char *inbuf, int width, int unit, int hwunit, int fontsize,
	    char *pathname) {
	char *buf;
	int i, buflen;
	int bad_video_erase_char = 0;
	int bpl = (width + 7) / 8;

	buflen = 32*fontsize*bpl;
	if (buflen < 32*128*bpl)		/* below we access position 32 */
		buflen = 32*128*bpl;		/* so need at least 32*33 */
	buf = xmalloc(buflen);
	memset(buf,0,buflen);

	if (unit < 1 || unit > 32) {
		fprintf(stderr, _("Bad character height %d\n"), unit);
		exit(EX_DATAERR);
	}
	if (width < 1 || width > 32) {
		fprintf(stderr, _("Bad character width %d\n"), width);
		exit(EX_DATAERR);
	}

	if (!hwunit)
		hwunit = unit;
		//hwunit = (unit > 32? 64: (unit > 16? 32: (unit > 8? 16: 8)));


	for (i = 0; i < fontsize; i++)
		memcpy(buf+(32*i*bpl), inbuf+(unit*i*bpl), unit*bpl);

	
	/*
	 * Due to a kernel bug, font position 32 is used
	 * to erase the screen, regardless of maps loaded.
	 * So, usually this font position should be blank.
	 */
	if (erase_mode) {
		for (i = 0; i < 32*bpl; i++)
			if (buf[32*32*bpl+i])
				bad_video_erase_char = 1;
		if (bad_video_erase_char) {
			fprintf(stderr,
				_("%s: font position 32 is nonblank\n"),
				progname);
			switch(erase_mode) {
			case 3:
				exit(EX_DATAERR);
			case 2:
				for (i = 0; i < 32*bpl; i++)
					buf[32*32*bpl+i] = 0;
				fprintf(stderr, _("%s: wiped it\n"), progname);
				break;
			case 1:
				fprintf(stderr,
					_("%s: background will look funny\n"),
					progname);
			}
			fflush(stderr);
			sleep(2);
		}
	}

	if (verbose) {
		if (pathname)
			printf(_("Loading %d-char %dx%d (%d) font from file %s\n"),
			       fontsize, width, unit, hwunit, pathname);
		else
			printf(_("Loading %d-char %dx%d (%d) font\n"),
			       fontsize, width, unit, hwunit);
	}

	if (putfont(fd, buf, fontsize, width, unit, hwunit))
		exit(EX_OSERR);
}

static void
do_loadtable(int fd, struct unicode_list *uclistheads, int fontsize) {
	struct unimapinit advice;
	struct unimapdesc ud;
	struct unipair *up;
	int i, ct = 0, maxct;
	struct unicode_list *ul;
	struct unicode_seq *us;

	maxct = 0;
	for (i = 0; i < fontsize; i++) {
		ul = uclistheads[i].next;
		while(ul) {
			us = ul->seq;
			if (us && ! us->next)
				maxct++;
			ul = ul->next;
		}
	}
	up = xmalloc(maxct * sizeof(struct unipair));
	for (i = 0; i < fontsize; i++) {
		ul = uclistheads[i].next;
		if (debug) printf ("char %03x:", i);
		while(ul) {
			us = ul->seq;
			if (us && ! us->next) {
				up[ct].unicode = us->uc;
				up[ct].fontpos = i;
				ct++;
				if (debug) printf (" %04x", us->uc);
			}
			else
				if (debug) {
					printf (" seq: <");
					while (us) {
						printf (" %04x", us->uc);
						us = us->next;
					}
					printf (" >");
				}
			ul = ul->next;
			if (debug) printf (",");
		}
		if (debug) printf ("\n");
	}
	if (ct != maxct) {
		char *u = _("%s: bug in do_loadtable\n");
		fprintf(stderr, u, progname);
		exit(EX_SOFTWARE);
	}

	/* Note: after PIO_UNIMAPCLR and before PIO_UNIMAP
	   this printf did not work on many kernels */
	if (verbose)
	  printf(_("Loading Unicode mapping table...\n"));

	advice.advised_hashsize = 0;
	advice.advised_hashstep = 0;
	advice.advised_hashlevel = 0;
	if(ioctl(fd, PIO_UNIMAPCLR, &advice)) {
#ifdef ENOIOCTLCMD
	    if (errno == ENOIOCTLCMD) {
		fprintf(stderr,
			_("It seems this kernel is older than 1.1.92\n"
			  "No Unicode mapping table loaded.\n"));
	    } else
#endif
	      perror("PIO_UNIMAPCLR");
	    exit(EX_OSERR);
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
	    exit(EX_OSERR);
	}
}

static void
loadnewfonts(int fd, char **ifiles, int ifilct,
	     int iunit, int hwunit, int no_m, int no_u) {
	FILE *fpi;
	char *ifil, *inbuf, *fontbuf, *bigfontbuf;
	int inputlth, fontbuflth, fontsize, unit, width, bpl;
	int bigfontbuflth, bigfontsize, bigunit, bigwidth;
	struct unicode_list *uclistheads;
	int i;

	if (ifilct == 1) {
		loadnewfont(fd, ifiles[0], iunit, hwunit, no_m, no_u);
		return;
	}

	/* several fonts that must be merged */
	/* We just concatenate the bitmaps - only allow psf fonts */
	bigfontbuf = NULL;
	bigfontbuflth = 0;
	bigfontsize = 0;
	uclistheads = NULL;
	bigunit = 0;
	bigwidth = 0;

	for (i=0; i<ifilct; i++) {
		ifil = ifiles[i];
		if ((fpi = findfont(ifil)) == NULL &&
		    (fpi = findpartialfont(ifil)) == NULL) {
			fprintf(stderr, _("Cannot open font file %s\n"), ifil);
			exit(EX_NOINPUT);
		}

		inbuf = fontbuf = NULL;
		inputlth = fontbuflth = 0;
		fontsize = 0;

		if(readpsffont(fpi, &inbuf, &inputlth, &fontbuf, &fontbuflth,
			       &fontsize, &width, bigfontsize,
			       no_u ? NULL : &uclistheads)) {
			fprintf(stderr, _("When loading several fonts, all "
					  "must be psf fonts - %s isn't\n"),
				pathname);
			exit(EX_DATAERR);
		}
		bpl = (width + 7) / 8;
		unit = fontbuflth / (bpl * fontsize);
		if (verbose)
			printf(_("Read %d-char %dx%d font from file %s\n"),
			       fontsize, width, unit, pathname);
		if (bigunit == 0)
			bigunit = unit;
		else if (bigunit != unit) {
			fprintf(stderr, _("When loading several fonts, all "
					  "must have the same height\n"));
			exit(EX_DATAERR);
		}
		if (bigwidth == 0)
			bigwidth = width;
		else if (bigwidth != width) {
			fprintf(stderr, _("When loading several fonts, all "
					  "must have the same width\n"));
			exit(EX_DATAERR);
		}
			
		bigfontsize += fontsize;
		bigfontbuflth += fontbuflth;
		bigfontbuf = xrealloc(bigfontbuf, bigfontbuflth);
		memcpy(bigfontbuf+bigfontbuflth-fontbuflth,
		       fontbuf, fontbuflth);
	}
	do_loadfont(fd, bigfontbuf, bigwidth, bigunit, hwunit, bigfontsize, NULL);

	if (uclistheads && !no_u)
		do_loadtable(fd, uclistheads, bigfontsize);
}

static void
loadnewfont(int fd, char *ifil, int iunit, int hwunit, int no_m, int no_u) {
	FILE *fpi;
	char defname[20];
	int unit, width, bpl, def = 0;
	char *inbuf, *fontbuf;
	int inputlth, fontbuflth, fontsize, offset;
	struct unicode_list *uclistheads;

	if (!*ifil) {
		/* try to find some default file */

		def = 1;		/* maybe also load default unimap */

		if (iunit < 0 || iunit > 32)
			iunit = 0;
		if (iunit == 0) {
			if ((fpi = findfont(ifil = "default")) == NULL &&
			    (fpi = findfont(ifil = "default8x16")) == NULL &&
			    (fpi = findfont(ifil = "default8x14")) == NULL &&
			    (fpi = findfont(ifil = "default8x8")) == NULL) {
				fprintf(stderr, _("Cannot find default font\n"));
				exit(EX_NOINPUT);
			}
		} else {
			sprintf(defname, "default8x%d", iunit);
			if ((fpi = findfont(ifil = defname)) == NULL &&
			    (fpi = findfont(ifil = "default")) == NULL) {
				fprintf(stderr, _("Cannot find %s font\n"), ifil);
				exit(EX_NOINPUT);
			}
		}
	} else {
		if ((fpi = findfont(ifil)) == NULL) {
			fprintf(stderr, _("Cannot open font file %s\n"), ifil);
			exit(EX_NOINPUT);
		}
	}

	inbuf = fontbuf = NULL;
	inputlth = fontbuflth = fontsize = 0;
	uclistheads = NULL;
	if(readpsffont(fpi, &inbuf, &inputlth, &fontbuf, &fontbuflth,
		       &fontsize, &width, 0, no_u ? NULL : &uclistheads) == 0) {
		/* we've got a psf font */
		bpl = (width + 7) / 8;
		unit = fontbuflth / (bpl * fontsize);

		do_loadfont(fd, fontbuf, width, unit, hwunit, fontsize, pathname);
		if (uclistheads && !no_u)
			do_loadtable(fd, uclistheads, fontsize);
#if 1
		if (!uclistheads && !no_u && def)
			loadunicodemap(fd, "def.uni");
#endif
		return;
	}

	/* instructions to combine fonts? */
	{ char *combineheader = "# combine partial fonts\n";
	  int chlth = strlen(combineheader);
	  char *p, *q;
	  if (inputlth >= chlth && !strncmp(inbuf, combineheader, chlth)) {
		  char *ifiles[MAXIFILES];
		  int ifilct = 0;
		  q = inbuf + chlth;
		  while(q < inbuf + inputlth) {
			  p = q;
			  while (q < inbuf+inputlth && *q != '\n')
				  q++;
			  if (q == inbuf+inputlth) {
				  fprintf(stderr,
					  _("No final newline in combine file\n"));
				  exit(EX_DATAERR);
			  }
			  *q++ = 0;
			  if (ifilct == MAXIFILES) {
				  fprintf(stderr,
					  _("Too many files to combine\n"));
				  exit(EX_DATAERR);
			  }
			  ifiles[ifilct++] = p;
		  }
		  /* recursive call */
		  loadnewfonts(fd, ifiles, ifilct, iunit, hwunit, no_m, no_u);
		  return;
	  }
	}

	/* file with three code pages? */
	if (inputlth == 9780) {
		offset = position_codepage(iunit);
		unit = iunit;
		fontsize = 256;
		width = 8;
	} else if (inputlth == 32768) {
		/* restorefont -w writes a SVGA font to file
		   restorefont -r restores it
		   These fonts have size 32768, for two 512-char fonts.
		   In fact, when BROKEN_GRAPHICS_PROGRAMS is defined,
		   and it always is, there is no default font that is saved,
		   so probably the second half is always garbage. */
		fprintf(stderr, _("Hmm - a font from restorefont? "
				  "Using the first half.\n"));
		inputlth = 16384; 	/* ignore rest */
		fontsize = 512;
		offset = 0;
		unit = 32;
		width = 8;
		if (!hwunit)
			hwunit = 16;
	} else {
		int rem = (inputlth % 256);
		if (rem == 0 || rem == 40) {
			/* 0: bare code page bitmap */
			/* 40: preceded by .cp header */
			/* we might check some header details */
			offset = rem;
		} else {
			fprintf(stderr, _("Bad input file size\n"));
			exit(EX_DATAERR);
		}
		fontsize = 256;
		unit = inputlth/256;
		width = 8;
	}
	do_loadfont(fd, inbuf+offset, width, unit, hwunit, fontsize, pathname);
}

static int
position_codepage(int iunit) {
        int offset;

	/* code page: first 40 bytes, then 8x16 font,
	   then 6 bytes, then 8x14 font,
	   then 6 bytes, then 8x8 font */

	if (!iunit) {
	    fprintf(stderr,
		    _("This file contains 3 fonts: 8x8, 8x14 and 8x16."
		      " Please indicate\n"
		      "using an option -8 or -14 or -16 "
		      "which one you want loaded.\n"));
	    exit(EX_USAGE);
	}
	switch (iunit) {
	  case 8:
	    offset = 7732; break;
	  case 14:
	    offset = 4142; break;
	  case 16:
	    offset = 40; break;
	  default:
	    fprintf(stderr, _("You asked for font size %d, "
			      "but only 8, 14, 16 are possible here.\n"),
		    iunit);
	    exit(EX_USAGE);
	}
	return offset;
}

static void
do_saveoldfont(int fd, char *ofil, FILE *fpo, int unimap_follows, int *count, int *utf8) 
{
    int i, unit, width, height, bpl, ct;
    char buf[MAXFONTSIZE];

    ct = sizeof(buf)/(32*32/8);			/* max size 32x32, 8 bits/byte */
    if (getfont(fd, buf, &ct, &width, &height))
	exit(EX_OSERR);

    bpl = (width + 7) / 8;
    /* save as efficiently as possible */
    unit = font_charheight(buf, ct, bpl);

    /* Do we need a psf header? */
    /* Yes if ct=512 - otherwise we cannot distinguish
       a 512-char 8x8 and a 256-char 8x16 font. */
#define ALWAYS_PSF_HEADER	1

    if (ct != 256 || unimap_follows || width != 8 || ALWAYS_PSF_HEADER) {
	    /* We need to reorganise the font in memory ... */
	    if (height != 32) {
		    int c;
		    if (debug) printf ("Remap %i chars: 32 -> %i (%i)\n", ct, height, unit);
		    for (c = 0; c < ct; c++)
			    memmove (buf+height*bpl*c, buf+32*bpl*c, height*bpl);
	    }
	    if (unimap_follows)
		    *utf8 = writepsffont(fpo, buf, width, height*bpl, ct, 0, (struct unicode_list*)-1, fd);
	    else
		    writepsffont(fpo, buf, width, height*bpl, ct, 0, 0, fd);
	    if (verbose)
		    printf(_("Saved %d-char %dx%d font file on %s\n"), ct, width, unit, ofil);
	    if (count)
		    *count = ct;
	    return;
#if 0
	/* old font save routine */
        struct psf1_header psfhdr;

	psfhdr.magic[0] = PSF1_MAGIC0;
	psfhdr.magic[1] = PSF1_MAGIC1;
	psfhdr.mode = 0;
	if (ct != 256)
	    psfhdr.mode |= PSF1_MODE512;
	if (unimap_follows)
	    psfhdr.mode |= PSF1_MODEHASTAB;
	psfhdr.charsize = unit;
	if (fwrite(&psfhdr, sizeof(struct psf1_header), 1, fpo) != 1) {
	    fprintf(stderr, _("Cannot write font file header"));
	    exit(EX_IOERR);
	}
#endif
    }

    if (unit == 0)
      fprintf(stderr, _("Found nothing to save\n"));
    else {
      for (i = 0; i < ct; i++)
	if (fwrite(buf+(32*i), unit, 1, fpo) != 1) {
	   fprintf(stderr, _("Cannot write font file"));
	   exit(EX_IOERR);
	}
      if (verbose)
	printf(_("Saved %d-char 8x%d font file on %s\n"), ct, unit, ofil);
    }

    if (count)
      *count = ct;
}

static void
saveoldfont(int fd, char *ofil) {
    FILE *fpo;
    int utf8;

    if((fpo = fopen(ofil, "w")) == NULL) {
	perror(ofil);
	exit(EX_CANTCREAT);
    }
    do_saveoldfont(fd, ofil, fpo, 0, NULL, &utf8);
    fclose(fpo);
}

static void
saveoldfontplusunicodemap(int fd, char *Ofil) {
    FILE *fpo;
    int ct;
    int utf8 = 0;

    if((fpo = fopen(Ofil, "w")) == NULL) {
	perror(Ofil);
	exit(EX_CANTCREAT);
    }
    ct = 0;
    do_saveoldfont(fd, Ofil, fpo, 1, &ct, &utf8);
    appendunicodemap(fd, fpo, ct, utf8);
    fclose(fpo);
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
