/* bptypes.h */

#ifndef	BPTYPES_H
#define	BPTYPES_H

/*
 * 32 bit integers are different types on various architectures
 */

/* I hope that this test actually works! */

#ifndef	int32
/* Assume that int is 32bits -- we can't test better than this in cpp
   If this is wrong, then define int32 externally to override this */
# define int32 int
#endif

/* typedef unsigned int32 u_int32; */

/*
 * Nice typedefs. . .
 */

typedef int boolean;
typedef unsigned char byte;

#endif	/* BPTYPES_H */
