/* Copyright (C) 1991, 92, 93, 94, 95, 96 Free Software Foundation, Inc.
This file is part of the GNU C Library.

The GNU C Library is free software; you can redistribute it and/or
modify it under the terms of the GNU Library General Public License as
published by the Free Software Foundation; either version 2 of the
License, or (at your option) any later version.

The GNU C Library is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
Library General Public License for more details.

You should have received a copy of the GNU Library General Public
License along with the GNU C Library; see the file COPYING.LIB.  If
not, write to the Free Software Foundation, Inc., 675 Mass Ave,
Cambridge, MA 02139, USA.  */

#include <ctype.h>
#include <errno.h>
#include <float.h>
#include <limits.h>
#include <math.h>
#include <printf.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <printf.h>
#include <stddef.h>
#include "_itoa.h"
#ifndef MINI_LIBC
#include "../locale/localeinfo.h"
#endif

/* Include the shared code for parsing the format string.  */
#include "printf-parse.h"


/* This function from the GNU C library is also used in libio.
   To compile for use in libio, compile with -DUSE_IN_LIBIO.  */

#ifdef USE_IN_LIBIO
/* This code is for use in libio.  */
# include <libioP.h>
# define PUT(F, S, N)	_IO_sputn (F, S, N)
# define PAD(Prefix, Padchar)						      \
  if (Prefix width > 0)							      \
    done += _IO_padn (s, Padchar, Prefix width)
# define PUTC(C, F)	_IO_putc (C, F)
# define vfprintf	_IO_vfprintf
# define size_t		_IO_size_t
# define FILE		_IO_FILE
# define va_list	_IO_va_list
# undef	BUFSIZ
# define BUFSIZ		_IO_BUFSIZ
# define ARGCHECK(S, Format)						      \
  do									      \
    {									      \
      /* Check file argument for consistence.  */			      \
      CHECK_FILE (S, -1);						      \
      if (S->_flags & _IO_NO_WRITES || Format == NULL)			      \
	{								      \
	  MAYBE_SET_EINVAL;						      \
	  return -1;							      \
	}								      \
    } while (0)
# define UNBUFFERED_P(S) ((S)->_IO_file_flags & _IO_UNBUFFERED)
#else /* ! USE_IN_LIBIO */
/* This code is for use in the GNU C library.  */
# include <stdio.h>
# define PUTC(C, F)	putc (C, F)
# define PUT(F, S, N)	fwrite (S, 1, N, F)
ssize_t __printf_pad __P ((FILE *, char pad, size_t n));
# define PAD(Prefix, Padchar)						      \
  if (Prefix width > 0)							      \
    { if (__printf_pad (s, Padchar, Prefix width) == -1)		      \
	return -1; else done += Prefix width; }
# define ARGCHECK(S, Format)						      \
  do									      \
    {									      \
      /* Check file argument for consistence.  */			      \
      if (!__validfp(S) || !S->__mode.__write || Format == NULL)	      \
	{								      \
	  errno = EINVAL;						      \
	  return -1;							      \
	}								      \
      if (!S->__seen)							      \
	{								      \
	  if (__flshfp (S, EOF) == EOF)					      \
	    return -1;							      \
	}								      \
    }									      \
   while (0)
# define UNBUFFERED_P(s) ((s)->__buffer == NULL)
#endif /* USE_IN_LIBIO */


#define	outchar(Ch)							      \
  do									      \
    {									      \
      register const int outc = (Ch);					      \
      if (PUTC (outc, s) == EOF)					      \
	return -1;							      \
      else								      \
	++done;								      \
    }									      \
  while (0)

#define outstring(String, Len)						      \
  do									      \
    {									      \
      if (PUT (s, String, Len) != Len)					      \
	return -1;							      \
      done += Len;							      \
    }									      \
  while (0)


/* Global variables.  */
static const char null[] = "(null)";

/* Helper function to provide temporary buffering for unbuffered streams.  */
static int buffered_vfprintf __P ((FILE *stream, const char *fmt, va_list));

static int printf_unknown __P ((FILE *, const struct printf_info *,
				const void **const));

extern printf_function *__printf_function_table;

static char *group_number __P ((char *, char *, const char *, wchar_t));


int
vfprintf (FILE *s, const char *format, va_list ap)
{
  /* The character used as thousands separator.  */
  wchar_t thousands_sep;

  /* The string describing the size of groups of digits.  */
  const char *grouping;

  /* Buffer intermediate results.  */
  char work_buffer[1000];
#define workend (&work_buffer[sizeof (work_buffer) - 1])

  /* End of leading constant string.  */
  const unsigned char *lead_str_end;

  /* Points to next format specifier.  */
  const unsigned char *end_of_spec;

  /* Current character in format string.  */
  const unsigned char *f;

  /* Count number of specifiers we already processed.  */
  int nspecs_done;

  /* Number of characters written.  */
  size_t done;

  /* We have to save the original argument pointer.  */
  va_list ap_save;


  ARGCHECK (s, format);

  if (UNBUFFERED_P (s))
    /* Use a helper function which will allocate a local temporary buffer
       for the stream and then call us again.  */
    return buffered_vfprintf (s, format, ap);

  /* Initialize variables.  */
  done = 0;

#ifndef MINI_LIBC
  /* Reset multibyte characters to their initial state.  */
  (void) mblen ((char *) NULL, 0);
#endif

  /* Find the first format specifier.  */
  f = lead_str_end = find_spec (format);

  /* Write the literal text before the first format.  */
  outstring ((const unsigned char *) format,
	     lead_str_end - (const unsigned char *) format);

  /* If we have only to print a string terminate now.  */
  if (*f == '\0')
    return done;

  /* Initialize variables, mark II.  */
  nspecs_done = 0;
  ap_save = ap;
  grouping = (const char *) -1;

  /* So far, so good.  We don't know by now whether we have to handle
     positional parameters or not.  We assume not until convinced this
     is wrong.  */
  do
    {
      int alt = 0;	/* Alternate format.  */
      int space = 0;	/* Use space prefix if no sign is needed.  */
      int left = 0;	/* Left-justify output.  */
      int showsign = 0;	/* Always begin with plus or minus sign.  */
      int group = 0;	/* Print numbers according grouping rules.  */
      int is_long_double = 0; /* Argument is long double/ long long int.  */
#ifndef is_longlong
# define is_longlong is_long_double
#endif
      int is_short = 0;	/* Argument is long int.  */
      int is_long = 0;	/* Argument is short int.  */
      int is_negative;	/* Flag for negative number.  */
      int width = 0;	/* Width of output; 0 means none specified.  */
      int prec = -1;	/* Precision of output; -1 means none specified.  */
      char pad = ' ';	/* Padding character.  */
      printf_function function; /* Pointer to special printing function.  */
      union
      {
	unsigned long long int longlong;
	unsigned long int word;
      } number;
      int base;
      union printf_arg the_arg;
      char *string;	/* Pointer to argument string.  */
      char spec;

      spec = *++f;
      switch (spec)	/* Use short cuts if possible.  */
	{
	case ' ': case '+': case '-': case '#': case '0': case '\'':
	  /* Handle flags.  */
	  do
	    {
	      switch (spec)
		{
		case ' ':
		  space = 1;
		  break;
		case '+':
		  showsign = 1;
		  break;
		case '-':
		  left = 1;

		  pad = ' ';
		  break;
		case '#':
		  alt = 1;
		  break;
		case '0':
		  if (!left)
		    pad = '0';
		  break;
		case '\'':
		  group = 1;

		  if (grouping == (const char *) -1)
		    {
#ifndef MINI_LIBC
		      /* Figure out the thousands separator character.  */
		      if (mbtowc (&thousands_sep,
				  _NL_CURRENT (LC_NUMERIC, THOUSANDS_SEP),
				  strlen (_NL_CURRENT (LC_NUMERIC,
						       THOUSANDS_SEP))) <= 0)
			thousands_sep = (wchar_t)
			  *_NL_CURRENT (LC_NUMERIC, THOUSANDS_SEP);
		      grouping = _NL_CURRENT (LC_NUMERIC, GROUPING);
		      if (*grouping == '\0' || *grouping == CHAR_MAX
			  || thousands_sep == L'\0')
			grouping = NULL;
#else
		      thousands_sep = L'\0';
		      grouping = NULL;
#endif
		    }
		  break;

		case '1' ... '9':
		  goto found_width;
		case '.':
		  goto found_precision;

		case 'h': case 'l': case 'L': case 'q': case 'Z':
		  goto try_argument_width;

		case '%':
		  goto do_percent;
		case 'd':
		  goto do_d_specifier;
		case 'i':
		  goto do_i_specifier;
		case 'u':
		  goto do_u_specifier;
		case 'o':
		  goto do_o_specifier;
		case 'X':
		  goto do_X_specifier;
		case 'x':
		  goto do_x_specifier;
		case 'e':
		  goto do_e_specifier;
		case 'E':
		  goto do_E_specifier;
		case 'f':
		  goto do_f_specifier;
		case 'g':
		  goto do_g_specifier;
		case 'G':
		  goto do_G_specifier;
		case 'c':
		  goto do_c_specifier;
		case 's':
		  goto do_s_specifier;
		case 'p':
		  goto do_p_specifier;
		case 'n':
		  goto do_n_specifier;
		case 'm':
		  goto do_m_specifier;
		default:
		  /* User-specified or unknown specifiers.  */
		  goto do_unknown;

		case '\0':
		  goto do_null_char;
		}
	      spec = *++f;
	    }
	  while (spec != '*');
	  /* FALLTHROUGH */

	case '*':	/* Entry point from outer switch.  */
	  /* We have the width in an parameter.  */
	  {
	    const unsigned char *tmp;	/* Temporary value.  */

	    tmp = ++f;
	    if (isdigit (*tmp) && read_int (&tmp) && *tmp == '$')
	      /* The width comes from an positional parameter.  */
	      goto do_positional;

	    width = va_arg (ap, int);
	    /* We have to check negative width. H.J. */
	    if (width < 0)
	    {
#if 0
	      /* We should echeck %--. But we don't. H.J. */
	      if (left)
	      {
	      }
	      else
#endif
	      {
		width = -width;
		pad = ' ';
		left = 1;
	      }
	    }
	  }
	  goto try_precision;

	case '1' ... '9':
	found_width:
	  width = read_int (&f);
	  if (*f == '$')
	    /* Oh, oh.  The argument comes from an positional parameter.  */
	    goto do_positional;

	try_precision:
	  if (*f != '.')
	    goto try_argument_width;
	  /* FALLTHROUGH */

	case '.':	/* Entry point from outer switch.  */
	found_precision:
	  ++f;
	  if (*f == '*')
	    {
	      const unsigned char *tmp;	/* Temporary value.  */

	      tmp = ++f;
	      if (isdigit (*tmp) && read_int (&tmp) > 0 && *tmp == '$')
	      /* The precision comes from an positional parameter.  */
		goto do_positional;

	      prec = va_arg (ap, int);
	    }
	  else if (isdigit (*f))
	    prec = read_int (&f);
	  else
	    prec = 0;
	  /* FALLTHROUGH */

	case 'h': case 'l': case 'L': case 'Z': case 'q':
	try_argument_width:

	  while (1)
	    {
	      spec = *f;
	      switch (spec)
		{
		case 'h':
		  is_short = 1;
		  break;
		case 'l':
		  if (is_long)
		    is_longlong = 1;
		  else
		    is_long = 1;
		  break;
		case 'L':
		  is_long_double = 1;
		  break;
		case 'Z':
		  is_longlong = sizeof(size_t) > sizeof(unsigned long int);
		  is_long = sizeof(size_t) > sizeof(unsigned int);
		  break;
		case 'q':
		  is_longlong = 1;
		  break;

		case '%':
		  goto do_percent;
		case 'd':
		  goto do_d_specifier;
		case 'i':
		  goto do_i_specifier;
		case 'u':
		  goto do_u_specifier;
		case 'o':
		  goto do_o_specifier;
		case 'X':
		  goto do_X_specifier;
		case 'x':
		  goto do_x_specifier;
		case 'e':
		  goto do_e_specifier;
		case 'E':
		  goto do_E_specifier;
		case 'f':
		  goto do_f_specifier;
		case 'g':
		  goto do_g_specifier;
		case 'G':
		  goto do_G_specifier;
		case 'c':
		  goto do_c_specifier;
		case 's':
		  goto do_s_specifier;
		case 'p':
		  goto do_p_specifier;
		case 'n':
		  goto do_n_specifier;
		case 'm':
		  goto do_m_specifier;

		case '\0':
		  goto do_null_char;
		default:
		  /* User-specified or unknown specifiers.  */
		  goto do_unknown;
		}
	      ++f;
	    }
	  /* NOTREACHED */

	case '%':	/* Entry point from outer switch.  */
	do_percent:
	  /* Write a literal "%".  */
	  outchar ('%');
	  break;

	case 'd': case 'i':	/* Entry point from outer switch.  */
	  do_i_specifier: do_d_specifier:
	  /* Signed decimal integer.  */
	  base = 10;

	  if (is_longlong)
	    {
	      long long int signed_number;

	      signed_number = va_arg (ap, long long int);

	      is_negative = signed_number < 0;
	      number.longlong = is_negative ? (- signed_number)
					    : signed_number;

	      goto longlong_number;
	    }
	  else
	    {
	      long int signed_number;

	      if (is_long)
		signed_number = va_arg (ap, long int);
	      else	/* `short int' will be promoted to `int'.  */
		signed_number = va_arg (ap, int);

	      is_negative = signed_number < 0;
	      number.word = is_negative ? (- signed_number) : signed_number;

	      goto number;
	    }
	  /* NOTREACHED */

	case 'u':	/* Entry point from outer switch.  */
	do_u_specifier:
	  /* Unsigned decimal integer.  */
	  base = 10;
	  goto unsigned_number;
	  /* NOTREACHED */

	case 'o':	/* Entry point from outer switch.  */
	do_o_specifier:
	  /* Unsigned octal integer.  */
	  base = 8;
	  goto unsigned_number;
	  /* NOTREACHED */

	case 'X': case 'x':	/* Entry point from outer switch.  */
	do_X_specifier: do_x_specifier:
	  /* Unsigned hexadecimal integer.  */
	  base = 16;

	unsigned_number:	  /* Unsigned number of base BASE.  */

	  /* ANSI specifies the `+' and ` ' flags only for signed
	     conversions.  */
	  is_negative = 0;
	  showsign = 0;
	  space = 0;

	  if (is_longlong)
	    {
	      number.longlong = va_arg (ap, unsigned long long int);

	    longlong_number:
	      if (prec < 0)
		/* Supply a default precision if none was given.  */
		prec = 1;
	      else
		/* We have to take care for the '0' flag.  If a
		   precision is given it must be ignored.  */
		pad = ' ';

	      /* If the precision is 0 and the number is 0 nothing has
		 to be written for the number.  */
	      if (prec == 0 && number.longlong == 0)
		string = workend;
	      else
		{
		  /* Put the number in WORK.  */
		  string = _itoa (number.longlong, workend + 1, base,
				  spec == 'X');
		  string -= 1;
		  if (group && grouping)
		    string = group_number (string, workend, grouping,
					   thousands_sep);
		}
	      /* Simply further test for num != 0.  */
	      number.word = number.longlong != 0;
	    }
	  else
	    {
	      if (is_long)
		number.word = va_arg (ap, unsigned long int);
	      else if (!is_short)
	        number.word = va_arg (ap, unsigned int);
	      else
	        number.word = (unsigned short int) va_arg (ap, unsigned int);

	    number:
	      if (prec < 0)
		/* Supply a default precision if none was given.  */
		prec = 1;
	      else
		/* We have to take care for the '0' flag.  If a
		   precision is given it must be ignored.  */
		pad = ' ';

	      /* If the precision is 0 and the number is 0 nothing has
		 to be written for the number.  */
	      if (prec == 0 && number.word == 0)
		string = workend;
	      else
		{
		  /* Put the number in WORK.  */
		  string = _itoa_word (number.word, workend + 1, base,
				       spec == 'X');
		  string -= 1;
		  if (group && grouping)
		    string = group_number (string, workend, grouping,
					   thousands_sep);
		}
	    }

	  width -= workend - string;
	  prec -= workend - string;

	  if (number.word != 0 && alt && base == 8 && prec <= 0)
	    {
	      /* Add octal marker.  */
	      *string-- = '0';
	      --width;
	    }

	  if (prec > 0)
	    {
	      /* Add zeros to the precision.  */
	      width -= prec;
	      while (prec-- > 0)
		*string-- = '0';
	    }

	  if (number.word != 0 && alt && base == 16)
	    /* Account for 0X hex marker.  */
	    width -= 2;

	  if (is_negative || showsign || space)
	    --width;

	  if (!left && pad == '0')
	    while (width-- > 0)
	      *string-- = '0';

	  if (number.word != 0 && alt && base == 16)
	    {
	      *string-- = spec;
	      *string-- = '0';
	    }

	  if (is_negative)
	    *string-- = '-';
	  else if (showsign)
	    *string-- = '+';
	  else if (space)
	    *string-- = ' ';

	  if (!left && pad == ' ')
	    while (width-- > 0)
	      *string-- = ' ';

	  outstring (string + 1, workend - string);

	  if (left)
	    PAD (, ' ');
	  break;

	case 'e': case 'E': case 'f': case 'g': case 'G':
	do_e_specifier: do_E_specifier: do_f_specifier:
	do_g_specifier: do_G_specifier:
#ifndef MINI_LIBC
	  {
	    /* Floating-point number.  This is handled by printf_fp.c.  */
	    extern int __printf_fp __P ((FILE *, const struct printf_info *,
					 const void *const *));
	    struct printf_info info = { prec: prec,
					width: width,
					spec: spec,
					is_long_double: is_long_double,
					is_short: is_short,
					is_long: is_long,
					alt: alt,
					space: space,
					left: left,
					showsign: showsign,
					group: group,
					pad: pad };
	    const void *ptr;
	    int function_done;

	    function = __printf_fp;

	    if (is_long_double)
	      the_arg.pa_long_double = va_arg (ap, long double);
	    else
	      the_arg.pa_double = va_arg (ap, double);

	    ptr = (const void *) &the_arg;

	    function_done = (*function) (s, &info, &ptr);
	    if (function_done < 0)
	      /* Error in print handler.  */
	      return -1;

	    done += function_done;
	  }
#else
	  goto do_unknown;
#endif
	  break;

	case 'c':	/* Entry point from outer switch.  */
	do_c_specifier:
	  /* Character.  */
	  --width;	/* Account for the character itself.  */
	  if (!left)
	    PAD (, ' ');
	  outchar ((unsigned char) va_arg (ap, int));	/* Promoted.  */
	  if (left)
	    PAD (, ' ');
	  break;

	case 's':	/* Entry point from outer switch.  */
	do_s_specifier:
	  {
	    size_t len;

	    string = (char *) va_arg (ap, const char *);

	  print_string:

	    if (string == NULL)
	      {
		/* Write "(null)" if there's space.  */
		if (prec == -1 || prec >= (int) sizeof (null) - 1)
		  {
		    string = (char *) null;
		    len = sizeof (null) - 1;
		  }
		else
		  {
		    string = (char *) "";
		    len = 0;
		  }
	      }
	    else if (prec != -1)
	      {
		/* Search for the end of the string, but don't search
		   past the length specified by the precision.  */
		const char *end = memchr (string, '\0', prec);
		if (end)
		  len = end - string;
		else
		  len = prec;
	      }
	    else
	      {
		len = strlen (string);

		if (width == 0)
		  {
		    outstring (string, len);
		    break;
		  }
	      }

	    width -= len;

	    if (!left)
	      PAD (, ' ');
	    outstring (string, len);
	    if (left)
	      PAD (, ' ');
	  }
	  break;

	case 'p':	/* Entry point from outer switch.  */
	do_p_specifier:
	  /* Generic pointer.  */
	  {
	    const void *ptr;
	    ptr = va_arg (ap, void *);
	    if (ptr != NULL)
	      {
		/* If the pointer is not NULL, write it as a %#x spec.  */
		base = 16;
		number.word = (unsigned long int) ptr;
		is_negative = 0;
		alt = 1;
		group = 0;
		spec = 'x';
		goto number;
	      }
	    else
	      {
		/* Write "(nil)" for a nil pointer.  */
		string = (char *) "(nil)";
		/* Make sure the full string "(nil)" is printed.  */
		if (prec < 5)
		  prec = 5;
		goto print_string;
	      }
	  }
	  /* NOTREACHED */

	case 'n':	/* Entry point from outer switch.  */
	do_n_specifier:
	  /* Answer the count of characters written.  */
	  if (is_longlong)
	    *(long long int *) va_arg (ap, void *) = done;
	  else if (is_long)
	    *(long int *) va_arg (ap, void *) = done;
	  else if (!is_short)
	    *(int *) va_arg (ap, void *) = done;
	  else
	    *(short int *) va_arg (ap, void *) = done;
	  break;

	case 'm':
	do_m_specifier:
	  {
	    extern char *_strerror_internal __P ((int, char *buf, size_t));

	    string = (char *)
	      _strerror_internal (errno, work_buffer, sizeof work_buffer);

	    goto print_string;
	  }

	case '\0':
	do_null_char:
	/* The format string ended before the specifier is complete.  */
	  return -1;

	default:	/* Entry point from outer switch.  */
	do_unknown:
	  /* These special need not be handle in the efficient loop.  */
	  goto do_positional;
	  /* NOTREACHED */
	}

      f = find_spec ((end_of_spec = ++f));

      /* Write the following constant string.  */
      outstring (end_of_spec, f - end_of_spec);
    }
  while (*f != '\0');

  /* We processed the whole format without any positional parameters.  */
  return done;

  /* Here starts the more complex loop to handle positional parameters.  */
do_positional:
  {
    /* Array with information about the needed arguments.  This has to
       be dynamically extendable.  */
    size_t nspecs;
    size_t nspecs_max;
    struct printf_spec *specs;

    /* The number of arguments the format string requests.  This will
       determine the size of the array needed to store the argument
       attributes.  */
    size_t nargs;
    int *args_type;
    union printf_arg *args_value;

    /* Positional parameters refer to arguments directly.  This could
       also determine the maximum number of arguments.  Track the
       maximum number.  */
    size_t max_ref_arg;

    /* Just a counter.  */
    int cnt;


    /* The variables, mark III.  */
    nspecs_max = 32;		/* A more or less arbitrary start value.  */
    specs = alloca (nspecs_max * sizeof (struct printf_spec));
    nspecs = 0;
    nargs = 0;
    max_ref_arg = 0;

    if (grouping == (const char *) -1)
      {
#ifndef MINI_LIBC
	/* Figure out the thousands separator character.  */
	if (mbtowc (&thousands_sep,
		    _NL_CURRENT (LC_NUMERIC, THOUSANDS_SEP),
		    strlen (_NL_CURRENT (LC_NUMERIC, THOUSANDS_SEP))) <= 0)
	  thousands_sep = (wchar_t) *_NL_CURRENT (LC_NUMERIC, THOUSANDS_SEP);
	grouping = _NL_CURRENT (LC_NUMERIC, GROUPING);
	if (*grouping == '\0' || *grouping == CHAR_MAX
	    || thousands_sep == L'\0')
#endif
	  grouping = NULL;
      }

    for (f = lead_str_end; *f != '\0'; f = specs[nspecs++].next_fmt)
      {
	if (nspecs >= nspecs_max)
	  {
	    /* Extend the array of format specifiers.  */
	    struct printf_spec *old = specs;

	    nspecs_max *= 2;
	    specs = alloca (nspecs_max * sizeof (struct printf_spec));

	    if (specs == &old[nspecs])
	      /* Stack grows up, OLD was the last thing allocated;
		 extend it.  */
	      nspecs_max += nspecs_max / 2;
	    else
	      {
		/* Copy the old array's elements to the new space.  */
		memcpy (specs, old, nspecs * sizeof (struct printf_spec));
		if (old == &specs[nspecs])
		  /* Stack grows down, OLD was just below the new
		     SPECS.  We can use that space when the new space
		     runs out.  */
		  nspecs_max += nspecs_max / 2;
	      }
	  }

	/* Parse the format specifier.  */
	nargs += parse_one_spec (f, nargs, &specs[nspecs], &max_ref_arg, NULL);
      }

    /* Determine the number of arguments the format string consumes.  */
    nargs = MAX (nargs, max_ref_arg);

    /* Allocate memory for the argument descriptions.  */
    args_type = alloca (nargs * sizeof (int));
    memset (args_type, 0, nargs * sizeof (int));
    args_value = alloca (nargs * sizeof (union printf_arg));

    /* XXX Could do sanity check here: If any element in ARGS_TYPE is
       still zero after this loop, format is invalid.  For now we
       simply use 0 as the value.  */

    /* Fill in the types of all the arguments.  */
    for (cnt = 0; cnt < nspecs; ++cnt)
      {
	/* If the width is determined by an argument this is an int.  */
	if (specs[cnt].width_arg != -1)
	  args_type[specs[cnt].width_arg] = PA_INT;

	/* If the precision is determined by an argument this is an int.  */
	if (specs[cnt].prec_arg != -1)
	  args_type[specs[cnt].prec_arg] = PA_INT;

	switch (specs[cnt].ndata_args)
	  {
	  case 0:		/* No arguments.  */
	    break;
	  case 1:		/* One argument; we already have the type.  */
	    args_type[specs[cnt].data_arg] = specs[cnt].data_arg_type;
	    break;
	  default:
	    /* We have more than one argument for this format spec.
	       We must call the arginfo function again to determine
	       all the types.  */
	    (void) (*__printf_arginfo_table[specs[cnt].info.spec])
	      (&specs[cnt].info,
	       specs[cnt].ndata_args, &args_type[specs[cnt].data_arg]);
	    break;
	  }
      }

    /* Now we know all the types and the order.  Fill in the argument
       values.  */
    for (cnt = 0, ap = ap_save; cnt < nargs; ++cnt)
      switch (args_type[cnt])
	{
#define T(tag, mem, type)						      \
	case tag:							      \
	  args_value[cnt].mem = va_arg (ap, type);			      \
	  break

	T (PA_CHAR, pa_char, int); /* Promoted.  */
	T (PA_INT|PA_FLAG_SHORT, pa_short_int, int); /* Promoted.  */
	T (PA_INT, pa_int, int);
	T (PA_INT|PA_FLAG_LONG, pa_long_int, long int);
	T (PA_INT|PA_FLAG_LONG_LONG, pa_long_long_int, long long int);
	T (PA_FLOAT, pa_float, double);	/* Promoted.  */
	T (PA_DOUBLE, pa_double, double);
	T (PA_DOUBLE|PA_FLAG_LONG_DOUBLE, pa_long_double, long double);
	T (PA_STRING, pa_string, const char *);
	T (PA_POINTER, pa_pointer, void *);
#undef T
	default:
	  if ((args_type[cnt] & PA_FLAG_PTR) != 0)
	    args_value[cnt].pa_pointer = va_arg (ap, void *);
	  else
	    args_value[cnt].pa_long_double = 0.0;
	  break;
	}

    /* Now walk through all format specifiers and process them.  */
    for (; nspecs_done < nspecs; ++nspecs_done)
      {
	printf_function function; /* Pointer to special printing function.  */
	union
	{
	  unsigned long long int longlong;
	  unsigned long int word;
	} number;
	int base;
	int is_negative;	/* Flag for negative number.  */
	char *string;

	/* Fill in last information.  */
	if (specs[nspecs_done].width_arg != -1)
	  {
	    /* Extract the field width from an argument.  */
	    specs[nspecs_done].info.width =
	      args_value[specs[nspecs_done].width_arg].pa_int;

	    if (specs[nspecs_done].info.width < 0)
	      /* If the width value is negative left justification is
		 selected and the value is taken as being positive.  */
	      {
		specs[nspecs_done].info.width *= -1;
		specs[nspecs_done].info.left = 1;
	      }
	  }

	if (specs[nspecs_done].prec_arg != -1)
	  {
	    /* Extract the precision from an argument.  */
	    specs[nspecs_done].info.prec =
	      args_value[specs[nspecs_done].prec_arg].pa_int;

	    if (specs[nspecs_done].info.prec < 0)
	      /* If the precision is negative the precision is
		 omitted.  */
	      specs[nspecs_done].info.prec = -1;
	  }

	switch (specs[nspecs_done].info.spec)
	  {
	  case '%':
	    /* Write a literal "%".  */
	    outchar ('%');
	    break;

	  case 'i':  case 'd':
	    /* Decimal integer.  */
	    base = 10;
	    if (specs[nspecs_done].info.is_longlong)
	      {
		long long int signed_number;

		signed_number =
		  args_value[specs[nspecs_done].data_arg].pa_long_long_int;
		is_negative = signed_number < 0;
		number.longlong = is_negative ? (- signed_number)
					      : signed_number;

		goto longlong_number2;
	      }
	    else
	      {
		long int signed_number;

		if (specs[nspecs_done].info.is_long)
		  signed_number =
		    args_value[specs[nspecs_done].data_arg].pa_long_int;
		else if (!specs[nspecs_done].info.is_short)
		  signed_number =
		    args_value[specs[nspecs_done].data_arg].pa_int;
		else
		  signed_number =
		    args_value[specs[nspecs_done].data_arg].pa_short_int;

		is_negative = signed_number < 0;
		number.word = is_negative ? (- signed_number) : signed_number;

		goto number2;
	      }
	    /* NOTREACHED */

	  case 'u':
	    /* Decimal unsigned integer.  */
	    base = 10;
	    goto unsigned_number2;

	  case 'o':
	    /* Octal unsigned integer.  */
	    base = 8;
	    goto unsigned_number2;

	  case 'X':
	    /* Hexadecimal unsigned integer.  */
	  case 'x':
	    /* Hex with lower-case digits.  */
	    base = 16;

	  unsigned_number2:
	    /* Unsigned number of base BASE.  */

	    /* ANSI specifies the `+' and ` ' flags only for signed
	       conversions.  */
	    is_negative = 0;
	    specs[nspecs_done].info.showsign = 0;
	    specs[nspecs_done].info.space = 0;

	    if (specs[nspecs_done].info.is_longlong)
	      {
		number.longlong =
		  args_value[specs[nspecs_done].data_arg].pa_u_long_long_int;

	      longlong_number2:
		if (specs[nspecs_done].info.prec < 0)
		  /* Supply a default precision if none was given.  */
		  specs[nspecs_done].info.prec = 1;
		else
		  /* We have to take care for the '0' flag.  If a
		     precision is given it must be ignored.  */
		  specs[nspecs_done].info.pad = ' ';

		/* If the precision is 0 and the number is 0 nothing has
		   to be written for the number.  */
		if (specs[nspecs_done].info.prec == 0 && number.longlong == 0)
		  string = workend;
		else
		  {
		    /* Put the number in WORK.  */
		    string = _itoa (number.longlong, workend + 1, base,
				    specs[nspecs_done].info.spec == 'X');
		    string -= 1;
		    if (specs[nspecs_done].info.group && grouping)
		      string = group_number (string, workend, grouping,
					     thousands_sep);
		  }
		/* Simply further test for num != 0.  */
		number.word = number.longlong != 0;
	      }
	    else
	      {
		if (specs[nspecs_done].info.is_long)
		  number.word =
		    args_value[specs[nspecs_done].data_arg].pa_u_long_int;
		else if (!specs[nspecs_done].info.is_short)
		  number.word =
		    args_value[specs[nspecs_done].data_arg].pa_u_int;
		else
		  number.word =
		    args_value[specs[nspecs_done].data_arg].pa_u_short_int;

	      number2:
		if (specs[nspecs_done].info.prec < 0)
		  /* Supply a default precision if none was given.  */
		  specs[nspecs_done].info.prec = 1;
		else
		  /* We have to take care for the '0' flag.  If a
		     precision is given it must be ignored.  */
		  specs[nspecs_done].info.pad = ' ';

		/* If the precision is 0 and the number is 0 nothing
		   has to be written for the number.  */
		if (specs[nspecs_done].info.prec == 0 && number.word == 0)
		  string = workend;
		else
		  {
		    /* Put the number in WORK.  */
		    string = _itoa_word (number.word, workend + 1, base,
					 specs[nspecs_done].info.spec == 'X');
		    string -= 1;
		    if (specs[nspecs_done].info.group && grouping)
		      string = group_number (string, workend, grouping,
					     thousands_sep);
		  }
	      }
	    specs[nspecs_done].info.width -= workend - string;
	    specs[nspecs_done].info.prec -= workend - string;

	    if (number.word != 0 && specs[nspecs_done].info.alt && base == 8
		&& specs[nspecs_done].info.prec <= 0)
	      {
		/* Add octal marker.  */
		*string-- = '0';
		--specs[nspecs_done].info.width;
	      }

	    if (specs[nspecs_done].info.prec > 0)
	      {
		/* Add zeros to the precision.  */
		specs[nspecs_done].info.width -= specs[nspecs_done].info.prec;
		while (specs[nspecs_done].info.prec-- > 0)
		  *string-- = '0';
	      }

	    if (number.word != 0 && specs[nspecs_done].info.alt && base == 16)
	      /* Account for 0X hex marker.  */
	      specs[nspecs_done].info.width -= 2;

	    if (is_negative || specs[nspecs_done].info.showsign
		|| specs[nspecs_done].info.space)
	      --specs[nspecs_done].info.width;

	    if (!specs[nspecs_done].info.left
		&& specs[nspecs_done].info.pad == '0')
	      while (specs[nspecs_done].info.width-- > 0)
		*string-- = '0';

	    if (number.word != 0 && specs[nspecs_done].info.alt && base == 16)
	      {
		*string-- = specs[nspecs_done].info.spec;
		*string-- = '0';
	      }

	    if (is_negative)
	      *string-- = '-';
	    else if (specs[nspecs_done].info.showsign)
	      *string-- = '+';
	    else if (specs[nspecs_done].info.space)
	      *string-- = ' ';

	    if (!specs[nspecs_done].info.left
		&& specs[nspecs_done].info.pad == ' ')
	      while (specs[nspecs_done].info.width-- > 0)
		*string-- = ' ';

	    outstring (string + 1, workend - string);

	    if (specs[nspecs_done].info.left)
	      PAD (specs[nspecs_done].info., ' ');

	    break;

#ifndef MINI_LIBC
	  case 'e':
	  case 'E':
	  case 'f':
	  case 'g':
	  case 'G':
	    {
	      /* Floating-point number.  This is handled by printf_fp.c.  */
	      extern int __printf_fp __P ((FILE *, const struct printf_info *,
					   const void *const *));
	      function = __printf_fp;
	      goto use_function2;
	    }
#endif

	  case 'c':
	    /* Character.  */
	    --specs[nspecs_done].info.width;/* Account for character itself. */
	    if (!specs[nspecs_done].info.left)
	      PAD (specs[nspecs_done].info., ' ');
	    outchar ((unsigned char)
		     args_value[specs[nspecs_done].data_arg].pa_char);
	    if (specs[nspecs_done].info.left)
	      PAD (specs[nspecs_done].info., ' ');
	    break;

	  case 's':
	    {
	      size_t len;

	      string = (char *)
		args_value[specs[nspecs_done].data_arg].pa_string;

	    print_string2:

	      if (string == NULL)
		{
		  /* Write "(null)" if there's space.  */
		  if (specs[nspecs_done].info.prec == -1
		      || (specs[nspecs_done].info.prec
			  >= (int) sizeof (null) - 1))
		    {
		      string = (char *) null;
		      len = sizeof (null) - 1;
		    }
		  else
		    {
		      string = (char *) "";
		      len = 0;
		    }
		}
	      else if (specs[nspecs_done].info.prec != -1)
		{
		  /* Search for the end of the string, but don't
		     search past the length specified by the
		     precision.  */
		  const char *end = memchr (string, '\0',
					    specs[nspecs_done].info.prec);
		  if (end)
		    len = end - string;
		  else
		    len = specs[nspecs_done].info.prec;
		}
	      else
		{
		  len = strlen (string);

		  if (specs[nspecs_done].info.width == 0)
		    {
		      outstring (string, len);
		      break;
		    }
		}

	      specs[nspecs_done].info.width -= len;

	      if (!specs[nspecs_done].info.left)
		PAD (specs[nspecs_done].info., ' ');
	      outstring (string, len);
	      if (specs[nspecs_done].info.left)
		PAD (specs[nspecs_done].info., ' ');
	    }
	    break;

	  case 'p':
	    /* Generic pointer.  */
	    {
	      const void *ptr;
	      ptr = args_value[specs[nspecs_done].data_arg].pa_pointer;
	      if (ptr != NULL)
		{
		  /* If the pointer is not NULL, write it as a %#x spec.  */
		  base = 16;
		  number.word = (unsigned long int) ptr;
		  is_negative = 0;
		  specs[nspecs_done].info.alt = 1;
		  specs[nspecs_done].info.spec = 'x';
		  specs[nspecs_done].info.group = 0;
		  goto number2;
		}
	      else
		{
		  /* Write "(nil)" for a nil pointer.  */
		  string = (char *) "(nil)";
		  /* Make sure the full string "(nil)" is printed.  */
		  if (specs[nspecs_done].info.prec < 5)
		    specs[nspecs_done].info.prec = 5;
		  goto print_string2;
		}
	    }
	    /* NOTREACHED */

	  case 'n':
	    /* Answer the count of characters written.  */
	    if (specs[nspecs_done].info.is_longlong)
	      *(long long int *)
		args_value[specs[nspecs_done].data_arg].pa_pointer = done;
	    else if (specs[nspecs_done].info.is_long)
	      *(long int *)
		args_value[specs[nspecs_done].data_arg].pa_pointer = done;
	    else if (!specs[nspecs_done].info.is_short)
	      *(int *)
		args_value[specs[nspecs_done].data_arg].pa_pointer = done;
	    else
	      *(short int *)
		args_value[specs[nspecs_done].data_arg].pa_pointer = done;
	    break;

	  case 'm':
	    {
	      extern char *_strerror_internal __P ((int, char *buf, size_t));

	      string = (char *)
		_strerror_internal (errno, work_buffer, sizeof work_buffer);

	      goto print_string2;
	    }

	  default:
	    /* User-specifier or unrecognized format specifier.  */
	    {
	      int function_done;
	      unsigned int i;
	      const void **ptr;

	      function =
		(__printf_function_table == NULL ? NULL :
		 __printf_function_table[specs[nspecs_done].info.spec]);

	      if (function == NULL)
		function = printf_unknown;

	    use_function2:
	      ptr = alloca (specs[nspecs_done].ndata_args
			    * sizeof (const void *));

	      /* Fill in an array of pointers to the argument values.  */
	      for (i = 0; i < specs[nspecs_done].ndata_args; ++i)
		ptr[i] = &args_value[specs[nspecs_done].data_arg + i];

	      /* Call the function.  */
	      function_done = (*function) (s, &specs[nspecs_done].info, ptr);

	      /* If an error occured we don't have information about #
		 of chars.  */
	      if (function_done < 0)
		return -1;

	      done += function_done;
	    }
	  }

	/* Write the following constant string.  */
	outstring (specs[nspecs_done].end_of_fmt,
		   specs[nspecs_done].next_fmt
		   - specs[nspecs_done].end_of_fmt);
      }
  }

  return done;
}

#ifdef USE_IN_LIBIO
# undef vfprintf
# ifdef strong_alias
/* This is for glibc.  */
strong_alias (_IO_vfprintf, vfprintf)
# else
#  if defined __ELF__ || defined __GNU_LIBRARY__
#   include <gnu-stabs.h>
#   ifdef weak_alias
weak_alias (_IO_vfprintf, vfprintf);
#   endif
#  endif
# endif
#endif

/* Handle an unknown format specifier.  This prints out a canonicalized
   representation of the format spec itself.  */
static int
printf_unknown (FILE *s, const struct printf_info *info,
		const void **const args)

{
  int done = 0;
  char work_buffer[BUFSIZ];
  register char *w;

  outchar ('%');

  if (info->alt)
    outchar ('#');
  if (info->group)
    outchar ('\'');
  if (info->showsign)
    outchar ('+');
  else if (info->space)
    outchar (' ');
  if (info->left)
    outchar ('-');
  if (info->pad == '0')
    outchar ('0');

  if (info->width != 0)
    {
      w = _itoa_word (info->width, workend + 1, 10, 0);
      while (++w <= workend)
	outchar (*w);
    }

  if (info->prec != -1)
    {
      outchar ('.');
      w = _itoa_word (info->prec, workend + 1, 10, 0);
      while (++w <= workend)
	outchar (*w);
    }

  if (info->spec != '\0')
    outchar (info->spec);

  return done;
}

/* Group the digits according to the grouping rules of the current locale.
   The interpretation of GROUPING is as in `struct lconv' from <locale.h>.  */
static char *
group_number (char *w, char *rear_ptr, const char *grouping,
	      wchar_t thousands_sep)
{
  int len;
  char *src, *s;

  /* We treat all negative values like CHAR_MAX.  */

#ifdef __CHAR_UNSIGNED__
  if (*grouping == CHAR_MAX )
#else
  if (*grouping == CHAR_MAX || *grouping < 0)
#endif
    /* No grouping should be done.  */
    return w;

  len = *grouping;

  /* Copy existing string so that nothing gets overwritten.  */
  src = (char *) alloca (rear_ptr - w);
  memcpy (src, w + 1, rear_ptr - w);
  s = &src[rear_ptr - w - 1];
  w = rear_ptr;

  /* Process all characters in the string.  */
  while (s >= src)
    {
      *w-- = *s--;

      if (--len == 0 && s >= src)
	{
	  /* A new group begins.  */
	  *w-- = thousands_sep;

	  len = *grouping++;
	  if (*grouping == '\0')
	    /* The previous grouping repeats ad infinitum.  */
	    --grouping;
#ifdef __CHAR_UNSIGNED__
	  else if (*grouping == CHAR_MAX )
#else
	  else if (*grouping == CHAR_MAX || *grouping < 0)
#endif
	    {
	      /* No further grouping to be done.
		 Copy the rest of the number.  */
	      do
		*w-- = *s--;
	      while (s >= src);
	      break;
	    }
	}
    }
  return w;
}

#ifdef USE_IN_LIBIO
/* Helper "class" for `fprintf to unbuffered': creates a temporary buffer.  */
struct helper_file
  {
    struct _IO_FILE_plus _f;
    _IO_FILE *_put_stream;
  };

static int
_IO_helper_overflow (_IO_FILE *s, int c)
{
  _IO_FILE *target = ((struct helper_file*) s)->_put_stream;
  int used = s->_IO_write_ptr - s->_IO_write_base;
  if (used)
    {
      size_t written = _IO_sputn (target, s->_IO_write_base, used);
      s->_IO_write_ptr -= written;
    }
  return _IO_putc (c, s);
}

static const struct _IO_jump_t _IO_helper_jumps =
  {
    JUMP_INIT_DUMMY,
    JUMP_INIT (finish, _IO_default_finish),
    JUMP_INIT (overflow, _IO_helper_overflow),
    JUMP_INIT (underflow, _IO_default_underflow),
    JUMP_INIT (uflow, _IO_default_uflow),
    JUMP_INIT (pbackfail, _IO_default_pbackfail),
    JUMP_INIT (xsputn, _IO_default_xsputn),
    JUMP_INIT (xsgetn, _IO_default_xsgetn),
    JUMP_INIT (seekoff, _IO_default_seekoff),
    JUMP_INIT (seekpos, _IO_default_seekpos),
    JUMP_INIT (setbuf, _IO_default_setbuf),
    JUMP_INIT (sync, _IO_default_sync),
    JUMP_INIT (doallocate, _IO_default_doallocate),
    JUMP_INIT (read, _IO_default_read),
    JUMP_INIT (write, _IO_default_write),
    JUMP_INIT (seek, _IO_default_seek),
    JUMP_INIT (close, _IO_default_close),
    JUMP_INIT (stat, _IO_default_stat)
  };

static int
buffered_vfprintf (s, format, args)
  register _IO_FILE *s;
  char const *format;
  _IO_va_list args;
{
  char buf[_IO_BUFSIZ];
  struct helper_file helper;
  register _IO_FILE *hp = (_IO_FILE *) &helper;
  int result, to_flush;

  /* Initialize helper.  */
  helper._put_stream = s;
  hp->_IO_write_base = buf;
  hp->_IO_write_ptr = buf;
  hp->_IO_write_end = buf + sizeof buf;
  hp->_IO_file_flags = _IO_MAGIC|_IO_NO_READS;
  _IO_JUMPS (hp) = (struct _IO_jump_t *) &_IO_helper_jumps;

  /* Now print to helper instead.  */
  result = _IO_vfprintf (hp, format, args);

  /* Now flush anything from the helper to the S. */
  if ((to_flush = hp->_IO_write_ptr - hp->_IO_write_base) > 0)
    {
      if (_IO_sputn (s, hp->_IO_write_base, to_flush) != to_flush)
	return -1;
    }

  return result;
}

#else /* !USE_IN_LIBIO */

static int
buffered_vfprintf (s, format, args)
  register FILE *s;
  char const *format;
  va_list args;
{
  char buf[BUFSIZ];
  int result;

  s->__bufp = s->__buffer = buf;
  s->__bufsize = sizeof buf;
  s->__put_limit = s->__buffer + s->__bufsize;
  s->__get_limit = s->__buffer;

  /* Now use buffer to print.  */
  result = vfprintf (s, format, args);

  if (fflush (s) == EOF)
    result = -1;
  s->__buffer = s->__bufp = s->__get_limit = s->__put_limit = NULL;
  s->__bufsize = 0;

  return result;
}

/* Pads string with given number of a specified character.
   This code is taken from iopadn.c of the GNU I/O library.  */
#define PADSIZE 16
static const char blanks[PADSIZE] =
{' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' '};
static const char zeroes[PADSIZE] =
{'0','0','0','0','0','0','0','0','0','0','0','0','0','0','0','0'};

ssize_t
__printf_pad (s, pad, count)
     FILE *s;
     char pad;
     size_t count;
{
  const char *padptr;
  register size_t i;

  padptr = pad == ' ' ? blanks : zeroes;

  for (i = count; i >= PADSIZE; i -= PADSIZE)
    if (PUT (s, padptr, PADSIZE) != PADSIZE)
      return -1;
  if (i > 0)
    if (PUT (s, padptr, i) != i)
      return -1;

  return count;
}
#undef PADSIZE
#endif /* USE_IN_LIBIO */

#ifdef USE_IN_LIBIO
#undef vfprintf
#if defined(__ELF__) || defined(__GNU_LIBRARY__)
#include <gnu-stabs.h>
#ifdef weak_alias
weak_alias (_IO_vfprintf, vfprintf);
#endif
#endif
#endif
