/* This is part of the iostream/stdio library, providing -*- C -*- I/O.
   Define ANSI C stdio on top of C++ iostreams.
   Copyright (C) 1991, 1994 Free Software Foundation

This library is free software; you can redistribute it and/or
modify it under the terms of the GNU Library General Public
License as published by the Free Software Foundation; either
version 2 of the License, or (at your option) any later version.


This library is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
Library General Public License for more details.

You should have received a copy of the GNU Library General Public
License along with this library; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
*/

/*
 *	ANSI Standard: 4.9 INPUT/OUTPUT <stdio.h>
 */

#ifndef _STDIO_H
#define _STDIO_H
#undef _STDIO_USES_IOSTREAM
#define _STDIO_USES_IOSTREAM 1

#ifdef __linux__
#include <features.h>
#endif

#include <libio.h>

#ifndef NULL
#ifdef __cplusplus
#define NULL 0
#else
#define NULL (void*)0
#endif
#endif

#ifndef EOF
#define EOF (-1)
#endif
#ifndef BUFSIZ
#define BUFSIZ 1024
#endif

#define _IOFBF 0 /* Fully buffered. */
#define _IOLBF 1 /* Line buffered. */
#define _IONBF 2 /* No buffering. */

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

 /* define size_t.  Crud in case <sys/types.h> has defined it. */
#if !defined(_SIZE_T) && !defined(_T_SIZE_) && !defined(_T_SIZE)
#if !defined(__SIZE_T) && !defined(_SIZE_T_) && !defined(___int_size_t_h)
#if !defined(_GCC_SIZE_T) && !defined(_SIZET_)
#define _SIZE_T
#define _T_SIZE_
#define _T_SIZE
#define __SIZE_T
#define _SIZE_T_
#define ___int_size_t_h
#define _GCC_SIZE_T
#define _SIZET_
typedef _IO_size_t size_t;
#endif
#endif
#endif

typedef struct _IO_FILE FILE;
typedef _IO_fpos_t fpos_t;

#define FOPEN_MAX    256
#define FILENAME_MAX 4095

/* limited by the number of possible unique combinations. see
 * libio/iotempname.c for details. */
#define TMP_MAX 238328

#define L_ctermid     9
#define L_cuserid     9
#define P_tmpdir      "/tmp"
#define L_tmpnam      20

/* For use by debuggers. These are linked in if printf
 * or fprintf are used. */
extern FILE *stdin, *stdout, *stderr;

#ifdef __SVR4_I386_ABI_L1__

/* This is for SVR4 Intel x86 ABI only. Don't use it directly. */
extern FILE __iob [];

#define stdin  (&__iob [0])
#define stdout (&__iob [1])
#define stderr (&__iob [2])

#else

#define stdin _IO_stdin
#define stdout _IO_stdout
#define stderr _IO_stderr

#endif

#if 0 && !defined(_LIBPTHREAD) && defined(__ELF__)
#define _IO_attr_weak __attribute__((weak))
#else
#define _IO_attr_weak /* nothing */
#endif

__BEGIN_DECLS

extern void clearerr __P((FILE*)) _IO_attr_weak;
extern int fclose __P((FILE*));
extern int feof __P((FILE*));
extern int ferror __P((FILE*));
extern int fflush __P((FILE*));
extern int fgetc __P((FILE *)) _IO_attr_weak;
extern int fgetpos __P((FILE* fp, fpos_t *pos));
extern char* fgets __P((char*, int, FILE*));
extern FILE* fopen __P((__const char*, __const char*));
extern int fprintf __P((FILE*, __const char* format, ...));
extern int fputc __P((int, FILE*)) _IO_attr_weak;
extern int fputs __P((__const char *str, FILE *fp));
extern size_t fread __P((void*, size_t, size_t, FILE*));
extern FILE* freopen __P((__const char*, __const char*, FILE*)) _IO_attr_weak;
extern int fscanf __P((FILE *fp, __const char* format, ...));
extern int fseek __P((FILE* fp, long int offset, int whence)) _IO_attr_weak;
extern int fsetpos __P((FILE* fp, __const fpos_t *pos));
extern long int ftell __P((FILE* fp));
extern size_t fwrite __P((__const void*, size_t, size_t, FILE*));
extern int getc __P((FILE *)) _IO_attr_weak;
extern int getchar __P((void)) _IO_attr_weak;
extern char* gets __P((char*));
extern void perror __P((__const char *));
extern int printf __P((__const char* format, ...));
extern int putc __P((int, FILE *)) _IO_attr_weak;
extern int putchar __P((int)) _IO_attr_weak;
extern int puts __P((__const char *str));
extern int remove __P((__const char*));
extern int rename __P((__const char* _old, __const char* _new));
extern void rewind __P((FILE*)) _IO_attr_weak;
extern int scanf __P((__const char* format, ...));
extern void setbuf __P((FILE*, char*));
extern void setlinebuf __P((FILE*));
extern void setbuffer __P((FILE*, char*, int));
extern int setvbuf __P((FILE*, char*, int mode, size_t size));
extern int sprintf __P((char*, __const char* format, ...));
extern int sscanf __P((__const char* string, __const char* format, ...));
extern FILE* tmpfile __P((void));
extern char* tmpnam __P((char*));
extern int ungetc __P((int c, FILE* fp));
extern int vfprintf __P((FILE *fp, char __const *fmt0, _G_va_list));
extern int vprintf __P((char __const *fmt, _G_va_list));
extern int vsprintf __P((char* string, __const char* format, _G_va_list));

#if !defined(__STRICT_ANSI__)
extern int vfscanf __P((FILE*, __const char *, _G_va_list)) _IO_attr_weak;
extern int vscanf __P((__const char *, _G_va_list));
extern int vsscanf __P((__const char *, __const char *, _G_va_list));

extern int getw __P((FILE*)) _IO_attr_weak;
extern int putw __P((int, FILE*)) _IO_attr_weak;

extern char* tempnam __P((__const char *__dir, __const char *__pfx));


#ifdef __GNU_LIBRARY__

#ifdef  __USE_BSD
extern int sys_nerr;
extern char *sys_errlist[];
#endif
#ifdef  __USE_GNU
extern int _sys_nerr;
extern char *_sys_errlist[];
#endif
 
#ifdef  __USE_MISC
/* Print a message describing the meaning of the given signal number. */
extern void psignal __P ((int __sig, __const char *__s));
#endif /* Non strict ANSI and not POSIX only.  */

#endif /* __GNU_LIBRARY__ */

#endif /* __STRICT_ANSI__ */

#ifdef __USE_GNU
extern _IO_ssize_t getdelim __P ((char **, size_t *, int, FILE*));
#if 0
extern _IO_ssize_t getline __P ((char **, size_t *, FILE *));
#endif

extern int snprintf __P ((char *, size_t, const char *, ...));
extern int vsnprintf __P ((char *, size_t, const char *, _G_va_list));

extern int asprintf __P((char **, const char *, ...));
extern int vasprintf __P((char **, const char *, _G_va_list));
#endif

#if !defined(__STRICT_ANSI__) || defined(_POSIX_SOURCE)
extern FILE *fdopen __P((int, __const char *));
extern int fileno __P((FILE*));
extern FILE* popen __P((__const char*, __const char*));
extern int pclose __P((FILE*));
#endif

extern int __underflow __P((struct _IO_FILE*));
extern int __overflow __P((struct _IO_FILE*, int));

#if defined(_POSIX_THREAD_SAFE_FUNCTIONS) || defined(_REENTRANT)

#ifndef __SVR4_I386_ABI_L1__
#define getc_unlocked(fp)	_IO_getc(fp)
#define getchar_unlocked()	getc_unlocked(stdin)
#define putc_unlocked(x, fp)	_IO_putc(x,fp)
#define putchar_unlocked(x)	putc_unlocked(x, stdout)
#endif

extern void flockfile __P((FILE *));
extern void funlockfile __P((FILE *));
extern int ftrylockfile __P((FILE *));

#else

#ifndef __SVR4_I386_ABI_L1__
#define getc(fp) _IO_getc(fp)
#define putc(c, fp) _IO_putc(c, fp)
#define putchar(c) putc(c, stdout)
#define getchar() getc(stdin)
#endif

#endif

__END_DECLS

#endif /*!_STDIO_H*/
