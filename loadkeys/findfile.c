#include <stdio.h>
#include <string.h>

char pathname[1024];

/* find input file; leave name in pathname[] */
FILE *findfile(char *fnam, char **dirpath, char **suffixes) {
        char **dp, **sp;
	FILE *fp;

	for (dp = dirpath; *dp; dp++) {
	    if (*fnam == '/' && **dp)
	      continue;
	    for (sp = suffixes; *sp; sp++) {
		if (strlen(*dp) + strlen(fnam) + strlen(*sp) + 1
		    > sizeof(pathname))
		  continue;
		sprintf(pathname, "%s%s%s", *dp, fnam, *sp);
		if((fp = fopen(pathname, "r")) != NULL)
		  return fp;
	    }
	}
	return NULL;
}

char *
xstrdup(char *p) {
        char *q = strdup(p);
        if (q == NULL) {
                fprintf(stderr, "Out of Memory?\n");
                exit(1);
        }
        return q;
}

void fpclose(FILE *fp) {
        fclose(fp);
}
