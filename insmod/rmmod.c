/* Remove a module from a running kernel.
   Copyright 1996, 1997 Linux International.

   New implementation contributed by Richard Henderson <rth@tamu.edu>
   Based on original work by Bjorn Eckwall <bj0rn@blox.se>

   This file is part of the Linux modutils.

   This program is free software; you can redistribute it and/or modify it
   under the terms of the GNU General Public License as published by the
   Free Software Foundation; either version 2 of the License, or (at your
   option) any later version.

   This program is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software Foundation,
   Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.  */

#ident "$Id: rmmod.c,v 1.1 1999/12/14 12:38:12 snwint Exp $"

#include <sys/types.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>

#include "module.h"
#include "util.h"
#include "version.h"

#include "logger.h"

/*======================================================================*/

#define WANT_TO_REMOVE 1
#define CAN_REMOVE 2

struct extmodule
{
  char *name;
  struct extmodule **refs;
  int nrefs;
  int status;			/* WANT_TO_REMOVE | CAN_REMOVE */
};


static struct extmodule *modules;
static size_t nmodules;


/* If we don't have query_module ... */

static int
old_get_modules(void)
{
  int fd, nmod, len, bufsize;
  char *buffer, *p;
  struct extmodule *mod;

  /* Read the module information from /proc in one go.  */

  if ((fd = open("/proc/modules", O_RDONLY)) < 0)
    {
      error("/proc/modules: %m");
      return 0;
    }

  buffer = xmalloc(bufsize = 8*1024);
retry_read:
  len = read(fd, buffer, bufsize);
  if (len < 0)
    {
      error("/proc/modules: %m");
      return 0;
    }
  else if (len == sizeof(buffer))
    {
      lseek(fd, 0, SEEK_SET);
      buffer = xrealloc(buffer, bufsize *= 2);
      goto retry_read;
    }

  close(fd);

  /* Break the buffer into lines and deal with them individually.  */

  mod = NULL;
  nmod = 0;

  buffer[len+1] = '\0';
  p = buffer;
  while (p < buffer+len)
    {
      char *e;
      struct extmodule *m;

      mod = xrealloc(mod, ++nmod * sizeof(struct extmodule));
      m = &mod[nmod-1];

      m->refs = NULL;
      m->nrefs = 0;
      m->status = 0;

      /* Save the end of the line.  */
      e = strchr(p, '\n');
      *e = '\0';

      /* Save the module name.  */
      m->name = p;

      p = strchr(p, ' ');
      *p = '\0';

      if ((p = strchr(p+1, '[')) != NULL)
	{
	  *strrchr(++p, ']') = '\0';

	  for (p = strtok(p, " "); p ; p = strtok(NULL, " "))
	    {
	      struct extmodule *rm;
	      int i;

	      for (i = 0, rm = mod; i < nmod; ++i, ++rm)
		if (strcmp(rm->name, p) == 0)
		  goto found_ref;

	      error("strange module reference: %s used by %s?\n", m->name, p);
	      return 0;

	    found_ref:
	      m->refs = xrealloc(m->refs,
				 ++m->nrefs * sizeof(struct extmodule *));
	      m->refs[m->nrefs-1] = rm;
	    }
	}

      p = e+1;
    }

  modules = mod;
  nmodules = nmod;

  return 1;
}

/* If we do have query_module ... */

static int
new_get_modules(void)
{
  char *module_names, *mn, *refs;
  struct extmodule *mod, *m;
  size_t bufsize, ret, nmod, i;

  /* Fetch the list of modules.  */

  module_names = xmalloc(bufsize = 1024);
retry_mod_load:
  if (query_module(NULL, QM_MODULES, module_names, bufsize, &ret))
    {
      if (errno == ENOSPC)
	{
	  module_names = xrealloc(module_names, bufsize = ret);
	  goto retry_mod_load;
	}
      error("QM_MODULES: %m");
      return 0;
    }
  nmod = ret;

  mod = xmalloc(nmod * sizeof(struct extmodule));
  memset(mod, 0, nmod * sizeof(struct extmodule));

  for (i = 0, mn = module_names, m = mod;
       i < nmod;
       ++i, mn += strlen(mn)+1, ++m)
    m->name = mn;

  /* Fetch the module references.  */

  refs = xmalloc(bufsize = 1024);
  for (i = 0, m = mod; i < nmod; ++i, ++m)
    {
      size_t j, nrefs;
      char *r;

    retry_ref_load:
      if (query_module(m->name, QM_REFS, refs, bufsize, &ret))
	{
	  if (errno == ENOSPC)
	    {
	      refs = xrealloc(refs, bufsize = ret);
	      goto retry_ref_load;
	    }
	  error("QM_REFS: %m");
	  return 0;
	}
      m->nrefs = nrefs = ret;
      m->refs = xmalloc(nrefs * sizeof(struct extmodule *));

      for (j = 0, r = refs; j < ret; ++j, r += strlen(r)+1)
	{
	  struct extmodule *rm;
	  size_t k;

	  for (k = 0, rm = mod; k < nmod; ++k, ++rm)
	    if (strcmp(rm->name, r) == 0)
	      goto found_ref;

	  error("strange module reference: %s used by %s?\n", m->name, r);
	  return 0;

	found_ref:
	  m->refs[j] = rm;
	}
    }

  free(refs);

  modules = mod;
  nmodules = nmod;

  return 1;
}

int
rmmod_main(int argc, char **argv)
{
  int i, j, ret = 0;

  error_file = "rmmod";

  while ((i = getopt(argc, argv, "asV")) != EOF)
    switch (i)
      {
      case 'a':
	/* Remove all unused modules and stacks.  */
	if (delete_module(NULL))
	  {
	    perror("rmmod");
	    return 1;
	  }
	return 0;

      case 's':
	/* Start syslogging.  */
	setsyslog("rmmod");
	break;

      case 'V':
	fputs("rmmod version " MODUTILS_VERSION "\n", stderr);
	break;

      default:
      usage:
	fputs("Usage: rmmod [-a] [-s] module ...\n", stderr);
	return 1;
      }

  if (optind >= argc)
    goto usage;

  /* Fetch all of the currently loaded modules and their dependencies.  */

  if (query_module(NULL, 0, NULL, 0, NULL) == 0
      ? !new_get_modules()
      : !old_get_modules())
    return 1;

  /* Find out which ones we want to remove.  */

  for (i = optind; i < argc; ++i)
    {
      for (j = 0; j < nmodules; ++j)
	if (strcmp(modules[j].name, argv[i]) == 0)
	  goto found_module;

      error("module %s not loaded", argv[i]);
      ret = 1;
      continue;

    found_module:
      modules[j].status |= WANT_TO_REMOVE;
    }

  if (ret)
    return ret;

  /* Remove them if we can.  */

  for (i = 0; i < nmodules ; ++i)
    {
      struct extmodule *m = &modules[i];

      if (m->nrefs == 0 && m->status == WANT_TO_REMOVE)
	m->status |= CAN_REMOVE;

      for (j = 0; j < m->nrefs; ++j)
	{
	  struct extmodule *r = m->refs[j];
	  switch (r->status)
	    {
	    case CAN_REMOVE:
	    case WANT_TO_REMOVE | CAN_REMOVE:
	      break;

	    case WANT_TO_REMOVE:
	      if (r->nrefs == 0)
		break;
	    default:
	      m->status &= ~CAN_REMOVE;
	      break;
	    }
	}

      switch (m->status)
	{
	case CAN_REMOVE:
	case WANT_TO_REMOVE | CAN_REMOVE:
	  if (delete_module(m->name) < 0)
	    {
	      error("%s: %m", m->name);
	      ret = 1;
	    }
	  break;

	case WANT_TO_REMOVE:
	  error("%s is in use", m->name);
	  ret = 1;
	  break;
	}
    }

  return ret;
}
