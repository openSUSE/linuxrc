/*
 * loadunimap.c - aeb
 *
 * Version 0.92
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <sysexits.h>
#include <string.h>
#include <ctype.h>
#include <fcntl.h>
#include <linux/kd.h>
#include <sys/ioctl.h>
#include "paths.h"

/* the two exported functions */
void loadunicodemap(int fd, char *ufil);


static char *unidirpath[] = { "", DATADIR "/" TRANSDIR "/", 0 };
static char *unisuffixes[] = { "", ".uni", 0 };

/*
 * Skip spaces and read U+1234 or return -1 for error.
 * Return first non-read position in *p0 (unchanged on error).
 */ 
int getunicode(char **p0) {
  char *p = *p0;

  while (*p == ' ' || *p == '\t')
    p++;
  if (*p != 'U' || p[1] != '+' || !isxdigit(p[2]) || !isxdigit(p[3]) ||
      !isxdigit(p[4]) || !isxdigit(p[5]) || isxdigit(p[6]))
    return -1;
  *p0 = p+6;
  return strtol(p+2,0,16);
}

struct unimapinit advice;

struct unimapdesc descr;

struct unipair *list = 0;
int listsz = 0;
int listct = 0;
int fd;

void outlist(void) {
    advice.advised_hashsize = 0;
    advice.advised_hashstep = 0;
    advice.advised_hashlevel = 1;
  again:
    if(ioctl(fd, PIO_UNIMAPCLR, &advice)) {
	perror("PIO_UNIMAPCLR");
	exit(1);
    }
    descr.entry_ct = listct;
    descr.entries = list;
    if(ioctl(fd, PIO_UNIMAP, &descr)) {
	if (errno == ENOMEM && advice.advised_hashlevel < 100) {
	    advice.advised_hashlevel++;
#ifdef MAIN
	    printf("trying hashlevel %d\n", advice.advised_hashlevel);
#endif
	    goto again;
	}
	perror("PIO_UNIMAP");
	exit(1);
    }
    listct = 0;
}

void addpair(int fp, int un) {
    if (listct == listsz) {
	listsz += 4096;
	list = realloc((char *)list, listsz);
	if (!list) {
	    fprintf(stderr, "loadunimap: out of memory\n");
	    exit(1);
	}
    }
    list[listct].fontpos = fp;
    list[listct].unicode = un;
    listct++;
}

void
loadunicodemap(int fd, char *tblname) {
    FILE *mapf;
    char buffer[65536];
    int fontlen = 512;
    int i;
    int fp0, fp1, un0, un1;
    char *p, *p1;

    mapf = findfile(tblname, unidirpath, unisuffixes);
    if ( !mapf ) {
	perror(tblname);
	exit(EX_NOINPUT);
    }

    if (verbose)
      printf("Loading unicode map from file %s\n", pathname);

    while ( fgets(buffer, sizeof(buffer), mapf) != NULL ) {
	if ( (p = strchr(buffer, '\n')) != NULL )
	  *p = '\0';
	else
	  fprintf(stderr, "loadunimap: %s: Warning: line too long\n", tblname);

	p = buffer;

/*
 * Syntax accepted:
 *	<fontpos>	<unicode> <unicode> ...
 *	<range>		idem
 *	<range>		<unicode range>
 *
 * where <range> ::= <fontpos>-<fontpos>
 * and <unicode> ::= U+<h><h><h><h>
 * and <h> ::= <hexadecimal digit>
 */

	while (*p == ' ' || *p == '\t')
	  p++;
	if (!*p || *p == '#')
	  continue;	/* skip comment or blank line */

	fp0 = strtol(p, &p1, 0);
	if (p1 == p) {
	    fprintf(stderr, "Bad input line: %s\n", buffer);
	    exit(EX_DATAERR);
	}
	p = p1;

	while (*p == ' ' || *p == '\t')
	  p++;
	if (*p == '-') {
	    p++;
	    fp1 = strtol(p, &p1, 0);
	    if (p1 == p) {
		fprintf(stderr, "Bad input line: %s\n", buffer);
		exit(EX_DATAERR);
	    }
	    p = p1;
	}
	else
	  fp1 = 0;

	if ( fp0 < 0 || fp0 >= fontlen ) {
	    fprintf(stderr,
		    "%s: Glyph number (0x%x) larger than font length\n",
		    tblname, fp0);
	    exit(EX_DATAERR);
	}
	if ( fp1 && (fp1 < fp0 || fp1 >= fontlen) ) {
	    fprintf(stderr,
		    "%s: Bad end of range (0x%x)\n",
		    tblname, fp1);
	    exit(EX_DATAERR);
	}

	if (fp1) {
	    /* we have a range; expect the word "idem" or a Unicode range of the
	       same length */
	    while (*p == ' ' || *p == '\t')
	      p++;
	    if (!strncmp(p, "idem", 4)) {
		for (i=fp0; i<=fp1; i++)
		  addpair(i,i);
		p += 4;
	    } else {
		un0 = getunicode(&p);
		while (*p == ' ' || *p == '\t')
		  p++;
		if (*p != '-') {
		    fprintf(stderr,
			    "%s: Corresponding to a range of font positions, "
			    "there should be a Unicode range\n",
			    tblname);
		    exit(EX_DATAERR);
		}
		p++;
		un1 = getunicode(&p);
		if (un0 < 0 || un1 < 0) {
		    fprintf(stderr,
			    "%s: Bad Unicode range corresponding to "
			    "font position range 0x%x-0x%x\n",
			    tblname, fp0, fp1);
		    exit(EX_DATAERR);
		}
		if (un1 - un0 != fp1 - fp0) {
		    fprintf(stderr,
			    "%s: Unicode range U+%x-U+%x not of the same length"
			    "as font position range 0x%x-0x%x\n",
			    tblname, un0, un1, fp0, fp1);
		    exit(EX_DATAERR);
		}
		for(i=fp0; i<=fp1; i++)
		  addpair(i,un0-fp0+i);
	    }
	} else {
	    /* no range; expect a list of unicode values
	       for a single font position */

	    while ( (un0 = getunicode(&p)) >= 0 )
	      addpair(fp0, un0);
	}
	while (*p == ' ' || *p == '\t')
	  p++;
	if (*p && *p != '#')
	  fprintf(stderr, "%s: trailing junk (%s) ignored\n", tblname, p);
    }

    fpclose(mapf);

    outlist();
}
