#ifndef _INTERNAL_LIBC_LOCK_H
#define _INTERNAL_LIBC_LOCK_H

#ifdef _POSIX_THREADS

#include <pthread.h>
#include <gnu-stabs.h>

#define __libc_lock_define(CLASS,NAME) \
	CLASS pthread_mutex_t *NAME

#ifdef _MIT_POSIX_THREADS
#ifdef PTHREAD_KERNEL
#define __libc_lock_define_initialized(CLASS,NAME) \
	static pthread_mutex_t libc_##NAME \
		= PTHREAD_MUTEX_INITIALIZER; \
	CLASS pthread_mutex_t *NAME = &libc_##NAME
#else
#define __libc_lock_define_initialized(CLASS, NAME) \
	CLASS void *NAME = 0; \
	weak_symbol (NAME)
#endif
#endif

#define __libc_lock_lock(NAME) \
	 pthread_mutex_lock (NAME)

#define __libc_lock_unlock(NAME) \
	 pthread_mutex_unlock (NAME)

#else

#define __libc_lock_define(CLASS,NAME)
#define __libc_lock_define_initialized(CLASS,NAME)
#define __libc_lock_lock(NAME)
#define __libc_lock_unlock(NAME)

#endif

#endif /*  _INTERNAL_LIBC_LOCK_H */
