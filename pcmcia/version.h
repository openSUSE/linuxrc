/* version.h 1.113 2002/11/04 03:21:28 (David Hinds) */

#define CS_PKG_RELEASE		"3.2.3"
#define CS_PKG_RELEASE_CODE	0x3203

#define VERSION(v,p,s)		(((v)<<16)+(p<<8)+s)

#ifdef CONFIG_PCMCIA
#include_next <pcmcia/version.h>
#else
#define CS_RELEASE		CS_PKG_RELEASE
#define CS_RELEASE_CODE		CS_PKG_RELEASE_CODE
#endif
