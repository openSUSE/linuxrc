/*
 * mapscrn.c - version 0.92
 */

#include <stdio.h>
#include <memory.h>
#include <string.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/kd.h>
#include "paths.h"

/* the two exported functions */
void loadnewmap(int fd, char *mfil);

static int parsemap (FILE *, char*);
static int ctoi (unsigned char *);

/* search for the map file in these directories (with trailing /) */
static char *mapdirpath[] = { "", DATADIR "/" TRANSDIR "/", 0 };
static char *mapsuffixes[] = { "", 0 };

void
loadnewmap(int fd, char *mfil) {
	FILE *fp;
	struct stat stbuf;
	char buf[E_TABSZ];
	int i;

	if ((fp = findfile(mfil, mapdirpath, mapsuffixes)) == NULL) {
	        fprintf(stderr, "mapscrn: cannot open map file _%s_\n", mfil);
		exit(1);
	}
	if (stat(pathname, &stbuf)) {
		perror("Cannot stat map file");
		exit(1);
	}
	if (stbuf.st_size != E_TABSZ) {
		fprintf(stderr,
			"Loading symbolic screen map from file %s\n",
			pathname);

		if (parsemap(fp,buf)) {
			fprintf(stderr, "Error parsing symbolic map\n");
			exit(1);
		}
	} else 	{
		fprintf(stderr, "Loading binary screen map from file %s\n",
			pathname);

		if (fread(buf,E_TABSZ,1,fp) != 1) {
			perror("Cannot read map from file");
			exit(1);
		}
	}
	fpclose(fp);

	i = ioctl(fd,PIO_SCRNMAP,buf);
	if (i) {
	    perror("PIO_SCRNMAP ioctl error");
	    exit(1);
	}

	if (verbose)
	  printf("Loaded screen map from `%s'\n", mfil);
}

static int
parsemap(FILE *fp, char buf[]) {
  char buffer[256];
  int in, on;
  char *p, *q;

  for (in=0; in<256; in++) buf[in]=in;

  while (fgets(buffer,sizeof(buffer)-1,fp)) {
      p = strtok(buffer," \t\n");
      if (p && *p != '#') {
	  q = strtok(NULL," \t\n#");
	  if (q) {
	      in = ctoi(p);
	      on = ctoi(q);
	      if (in >= 0 && on >= 0) buf[in] = on;
	  }
      }
  }
  return(0);
}

int
ctoi(unsigned char *s) {
  int i;

  if ((strncmp(s,"0x",2) == 0) && 
      (strspn(s+2,"0123456789abcdefABCDEF") == strlen(s+2)))
    sscanf(s+2,"%x",&i);

  else if ((*s == '0') &&
	   (strspn(s,"01234567") == strlen(s)))
    sscanf(s,"%o",&i);

  else if (strspn(s,"0123456789") == strlen(s)) 
    sscanf(s,"%d",&i);

  else if ((strlen(s) == 3) && (s[0] == '\'') && (s[2] == '\''))
    i=s[1];

  else return(-1);

  if (i < 0 || i > 255) {
      fprintf(stderr, "mapscrn: format error detected in _%s_\n", s);
      exit(1);
  }

  return(i);
}

void
saveoldmap(int fd, char *omfil) {
    FILE *fp;
    int i;
    char buf[E_TABSZ];

    if ((fp = fopen(omfil, "w")) == NULL) {
	perror(omfil);
	exit(1);
    }
    i = ioctl(fd,GIO_SCRNMAP,buf);
    if (i) {
	perror("GIO_SCRNMAP ioctl error");
	exit(1);
    }
    if (fwrite(buf,E_TABSZ,1,fp) != 1) {
	perror("Error writing map to file");
	exit(1);
    }
    fclose(fp);

    if (verbose)
      printf("Saved screen map in `%s'\n", omfil);
}
