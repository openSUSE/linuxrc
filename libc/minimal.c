#include <libintl.h>
#include <stdio.h>

#undef stderr

int _nl_msg_cat_cntr;

void __force_mini_libc_symbols (void)
{
  fprintf (stderr, "");
  snprintf (0, 0, 0);
}

char *__dcgettext (const char *domainname, const char *msgid, int category)
{
  return (char *) msgid;
}
