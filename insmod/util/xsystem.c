/* Misc utility functions.
   Copyright 2000 Keith Owens <kaos@ocs.com.au>

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

#ident "$Id"

#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>
#include <unistd.h>


/*======================================================================*/

/* Clone of the system() function From Steven's Advanced Programming in a Unix
 * Environment.  Modified to use *argv[] and execvp to avoid shell expansion
 * problems, modutils runs as root so system() is unsafe.
 */

int
xsystem(const char *file, char *const argv[])
{
  pid_t pid;
  int status;
  if ((pid = fork()) < 0)
    return(-1);
  if (pid == 0) {
    execvp(file, argv);
    _exit(127);
  }
  while (waitpid(pid, &status, 0) < 0) {
    if (errno != EINTR)
      return(-1);
  }
  return(status);
}
