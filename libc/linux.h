#ifndef _CONFIG_LINUX_H
#define  _CONFIG_LINUX_H

#undef _STDIO_USES_IOSTREAM
#define _STDIO_USES_IOSTREAM 	1

#undef _IO_HAVE_ST_BLKSIZE
#define _IO_HAVE_ST_BLKSIZE	1

#undef _IO_DEBUG
#define _IO_DEBUG

#define _IO_open	open
#define _IO_close	close
#define	_IO_fork	fork
#define	_IO_fcntl	fcntl
#define _IO__exit	_exit
#define _IO_read	read
#define _IO_write	write
#define _IO_lseek	lseek
#define	_IO_getdtablesize	getdtablesize
#define _IO_pipe	pipe
#define _IO_dup2	dup2
#define _IO_execl	execl
#define _IO_waitpid	waitpid
#define _IO_stat        stat
#define _IO_getpid      getpid
#define _IO_geteuid     geteuid
#define _IO_getegid     getegid
#define _IO_fstat	fstat

#endif /* _CONFIG_LINUX_H */
