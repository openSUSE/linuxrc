/*
 * modutils specific implementation of ftw().
 *
 * Copyright 2000:
 *  Keith Owens <kaos@ocs.com.au> August 2000
 *
 * This file is part of the Linux modutils.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

/*
    modutils requires special processing during the file tree walk
    of /lib/modules/<version> and any paths that the user specifies.
    The standard ftw() does a blind walk of all paths and can end
    up following the build symlink down the kernel source tree.
    Although nftw() has the option to get more control such as not
    automatically following symbolic links, even that is not enough
    for modutils.  The requirements are:

    Paths must be directories or symlinks to directories.

    Each directory is read and sorted into alphabetical order
    before processing.

    A directory is type 1 iff it was specified on a path statement
    (either explicit or default) and the directory contains a
    subdirectory with one of the known names and the directory name
    does not end with "/kernel".  Otherwise it is type 2.

    In a type 1 directory, walk the kernel subdirectory if it exists,
    then the old known names in their historical order then any
    remaining directory entries in alphabetical order and finally any
    non-directory entries in alphabetical order.

    Entries in a type 1 directory are filtered against the "prune"
    list.  A type 1 directory can contain additional files which
    are not modules nor symlinks to modules.  The prune list skips
    known additional files, if a distribution wants to store
    additional text files in the top level directory they should be
    added to the prune list.

    A type 2 directory must contain only modules or symlinks to
    modules.  They are processed in alphabetical order, without
    pruning.  Symlinks to directories are an error in type 2
    directories.

    The user function is not called for type 1 directories, nor for
    pruned entries.  It is called for type 2 directories and their
    contents.  It is also called for any files left in a type 1
    directory after pruning and processing type 2 subdirectories.
    The user function never sees symlinks, they are resolved before
    calling the function.

    Why have different directory types?  The original file tree
    walk was not well defined.  Some users specified each directory
    individually, others just pointed at the top level directory.
    Either version worked until the "build" symlink was added.  Now
    users who specify the top level directory end up running the
    entire kernel source tree looking for modules, not nice.  We
    cannot just ignore symlinks because pcmcia uses symlinks to
    modules for backwards compatibility.

    Type 1 is when a user specifies the top level directory which needs
    special processing, type 2 is individual subdirectories.  But the
    only way to tell the difference is by looking at the contents.  The
    "/kernel" directory introduced in 2.3.12 either contains nothing
    (old make modules_install) or contains all the kernel modules using
    the same tree structure as the source.  Because "/kernel" can
    contain old names but is really a type 2 directory, it is detected
    as a special case.
 */

#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <malloc.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "util.h"
#include "config.h"

extern char *tbpath[];

extern OPT_LIST *prune_list;
extern int n_prune_list;

extern char *tbtype[];

struct xftw_dirent {
    struct stat statbuf;
    char *name;
    char *fullname;
};

#define XFTW_MAXDEPTH 64    /* Maximum directory depth handled */

typedef struct {
    struct xftw_dirent *contents;
    int size;
    int used;
} xftw_tree_t;

static xftw_tree_t tree[XFTW_MAXDEPTH];

/* Free all data for one tree level */
static void xftw_free_tree(int depth)
{
    int i;
    xftw_tree_t *t = tree+depth;
    for (i = 0; i < t->size; ++i) {
	free(t->contents[i].name);
	free(t->contents[i].fullname);
    }
    free(t->contents);
    t->contents = NULL;
    t->size = 0;
    t->used = 0;
}

/* Increment dirents used at this depth, resizing if necessary */
static void xftw_add_dirent(int depth)
{
    xftw_tree_t *t = tree+depth;
    int i, size = t->size;
    if (++t->used < size)
	return;
    size += 10; /* arbitrary increment */
    t->contents = xrealloc(t->contents, size*sizeof(*(t->contents)));
    for (i = t->size; i < size; ++i) {
	memset(&(t->contents[i].statbuf), 0, sizeof(t->contents[i].statbuf));
	t->contents[i].name = NULL;
	t->contents[i].fullname = NULL;
    }
    t->size = size;
}

/* Concatenate directory name and entry name into one string.
 * Note: caller must free result or leak.
 */
static char *xftw_dir_name(const char *directory, const char *entry)
{
    int i = strlen(directory);
    char *name;
    if (entry)
	i += strlen(entry);
    i += 2;
    name = xmalloc(i);
    strcpy(name, directory);	/* safe, xmalloc */
    if (*directory && entry)
	strcat(name, "/");	/* safe, xmalloc */
    if (entry)
	strcat(name, entry);	/* safe, xmalloc */
    return(name);
}

/* Call the user function for a directory entry */
static int xftw_do_name(const char *directory, const char *entry, struct stat *sb, xftw_func_t funcptr)
{
    int ret = 0;
    char *name = xftw_dir_name(directory, entry);

    if (S_ISLNK(sb->st_mode)) {
	char real[PATH_MAX], *newname;
	verbose("resolving %s symlink to ", name);
	if (!(newname = realpath(name, real))) {
	    if (errno == ENOENT) {
		verbose("%s: does not exist, dangling symlink ignored\n", real);
		goto cleanup;
	    }
	    perror("... failed");
	    goto cleanup;
	}
	verbose("%s ", newname);
	if (lstat(newname, sb)) {
	    error("lstat on %s failed ", newname);
	    perror("");
	    goto cleanup;
	}
	free(name);
	name = xstrdup(newname);
    }

    if (!S_ISREG(sb->st_mode) &&
	!S_ISDIR(sb->st_mode)) {
	error("%s is not plain file nor directory\n", name);
	goto cleanup;
    }
	
    verbose("user function %s\n", name);
    ret = (*funcptr)(name, sb);
cleanup:
    free(name);
    return(ret);
}

/* Sort directory entries into alphabetical order */
static int xftw_sortdir(const void *a, const void *b)
{
    return(strcmp(((struct xftw_dirent *)a)->name, ((struct xftw_dirent *)b)->name));
}

/* Read a directory and sort it, ignoring "." and ".." */
static int xftw_readdir(const char *directory, int depth)
{
    DIR *d;
    struct dirent *ent;
    verbose("xftw_readdir %s\n", directory);
    if (!(d = opendir(directory))) {
	perror(directory);
	return(1);
    }
    while ((ent = readdir(d))) {
	char *name;
	struct xftw_dirent *f;
	if (strcmp(ent->d_name, ".") == 0 ||
	    strcmp(ent->d_name, "..") == 0)
	    continue;
	name = xftw_dir_name(directory, ent->d_name);
	xftw_add_dirent(depth); 
	f = tree[depth].contents+tree[depth].used-1;
	f->name = xstrdup(ent->d_name);
	f->fullname = name;     /* do not free name, it is in use */
	if (lstat(name, &(f->statbuf))) {
	    perror(name);
	    return(1);
	}
    }
    closedir(d);
    qsort(tree[depth].contents, tree[depth].used, sizeof(*(tree[0].contents)), &xftw_sortdir);
    return(0);
}

/* Process a type 2 directory */
int xftw_type2(const char *directory, const char *entry, int depth, xftw_func_t funcptr)
{
    int ret, i;
    xftw_tree_t *t = tree+depth;
    struct stat statbuf;
    char *dirname = xftw_dir_name(directory, entry);

    verbose("type 2 %s\n", dirname);
    if (depth > XFTW_MAXDEPTH) {
	error("xftw_type2 exceeded maxdepth\n");
	ret = 1;
	goto cleanup;
    }
    if ((ret = xftw_readdir(dirname, depth)))
	goto cleanup;

    t = tree+depth;
    /* user function sees type 2 directories */
    if ((ret = lstat(dirname, &statbuf)) ||
	(ret = xftw_do_name("", dirname, &statbuf, funcptr)))
	goto cleanup;

    /* user sees all contents of type 2 directory, no pruning */
    for (i = 0; i < t->used; ++i) {
	struct xftw_dirent *c = t->contents+i;
	if (S_ISLNK(c->statbuf.st_mode)) {
	    if (!stat(c->name, &(c->statbuf))) {
		if (S_ISDIR(c->statbuf.st_mode)) {
		    error("symlink to directory is not allowed, %s ignored\n", c->name);
		    *(c->name) = '\0';  /* ignore it */
		}
	    }
	}
	if (!*(c->name))
	    continue;
	if (S_ISDIR(c->statbuf.st_mode)) {
	    /* recursion is the curse of the programming classes */
	    ret = xftw_type2(dirname, c->name, depth+1, funcptr);
	    if (ret)
		goto cleanup;
	}
	else if ((ret = xftw_do_name(dirname, c->name, &(c->statbuf), funcptr)))
	    goto cleanup;
	*(c->name) = '\0';  /* processed */
    }

    ret = 0;
cleanup:
    free(dirname);
    return(ret);
}

/* Only external visible function.  Decide on the type of directory and
 * process accordingly.
 */
int xftw(const char *directory, xftw_func_t funcptr)
{
    struct stat statbuf;
    int ret, i, j, type;
    xftw_tree_t *t;
    struct xftw_dirent *c;

    verbose("xftw starting at %s ", directory);
    if (lstat(directory, &statbuf)) {
	verbose("lstat on %s failed\n", directory);
	return(0);
    }
    if (S_ISLNK(statbuf.st_mode)) {
	char real[PATH_MAX];
	verbose("resolving symlink to ");
	if (!(directory = realpath(directory, real))) {
	    if (errno == ENOENT) {
		verbose("%s: does not exist, dangling symlink ignored\n", real);
		return(0);
	    }
	    perror("... failed");
	    return(-1);
	}
	verbose("%s ", directory);
	if (lstat(directory, &statbuf)) {
	    error("lstat on %s failed ", directory);
	    perror("");
	    return(-1);
	}
    }
    if (!S_ISDIR(statbuf.st_mode)) {
	error("%s is not a directory\n", directory);
	return(-1);
    }
    verbose("\n");

    /* All returns after this point must be via cleanup */

    if ((ret = xftw_readdir(directory, 0)))
	goto cleanup;

    t = tree;   /* depth 0 */
    type = 2;
    for (i = 0 ; type == 2 && i < t->used; ++i) {
	c = t->contents+i;
	for (j = 0; tbtype[j]; ++j) {
	    if (strcmp(c->name, tbtype[j]) == 0 &&
		S_ISDIR(c->statbuf.st_mode)) {
		const char *p = directory + strlen(directory) - 1;
		if (*p == '/')
		    --p;
		if (p - directory >= 6 && strncmp(p-6, "/kernel", 7) == 0)
		    continue;	/* "/kernel" path is a special case, type 2 */
		type = 1;   /* known subdirectory */
		break;
	    }
	}
    }

    if (type == 1) {
	OPT_LIST *p;
	/* prune entries in type 1 directories only */
	for (i = 0 ; i < t->used; ++i) {
	    for (p = prunelist; p->name; ++p) {
		c = t->contents+i;
		if (strcmp(p->name, c->name) == 0) {
		    verbose("pruned %s\n", c->name);
		    *(c->name) = '\0';  /* ignore */
		}
	    }
	}
	/* run known subdirectories first in historical order, "kernel" is now top of list */
	for (i = 0 ; i < t->used; ++i) {
	    c = t->contents+i;
	    for (j = 0; tbtype[j]; ++j) {
		if (*(c->name) &&
		    strcmp(c->name, tbtype[j]) == 0 &&
		    S_ISDIR(c->statbuf.st_mode)) {
		    if ((ret = xftw_type2(directory, c->name, 1, funcptr)))
			goto cleanup;
		    *(c->name) = '\0';  /* processed */
		}
	    }
	}
	/* any other directories left, in alphabetical order */
	for (i = 0 ; i < t->used; ++i) {
	    c = t->contents+i;
	    if (*(c->name) &&
	        S_ISDIR(c->statbuf.st_mode)) {
		if ((ret = xftw_type2(directory, c->name, 1, funcptr)))
		    goto cleanup;
		*(c->name) = '\0';  /* processed */
	    }
	}
	/* anything else is passed to the user function */
	for (i = 0 ; i < t->used; ++i) {
	    c = t->contents+i;
	    if (*(c->name)) {
		verbose("%s found in type 1 directory %s\n", c->name, directory);
		if ((ret = xftw_do_name(directory, c->name, &(c->statbuf), funcptr)))
		    goto cleanup;
		*(c->name) = '\0';  /* processed */
	    }
	}
    }
    else {
	/* type 2 */
	xftw_free_tree(0);
	if ((ret = xftw_type2(directory, NULL, 0, funcptr)))
	    goto cleanup;
    }

    /* amazing, it all worked */
    ret = 0;
cleanup:
    for (i = 0; i < XFTW_MAXDEPTH; ++i)
	xftw_free_tree(i);
    return(ret);
}
