/* Definitions for the Linux kerneld SYSV IPC interface.
   This file was part of the Linux kernel, and so is covered by the GPL.  */

#ifndef MODUTILS_KERNELD_H
#define MODUTILS_KERNELD_H

#define KERNELD_SYSTEM 1
#define KERNELD_REQUEST_MODULE 2		/* "insmod" */
#define KERNELD_RELEASE_MODULE 3		/* "rmmod" */
#define KERNELD_DELAYED_RELEASE_MODULE 4	/* "rmmod" */
#define KERNELD_CANCEL_RELEASE_MODULE 5		/* "rmmod" */
#define KERNELD_REQUEST_ROUTE 6			/* net/ipv4/route.c */
#define KERNELD_BLANKER 7			/* drivers/char/console.c */
#define KERNELD_PNP 8				/* drivers/pnp/kerneld.c */
#define KERNELD_ARP 256				/* net/ipv4/arp.c */

#ifdef NEW_KERNELD_PROTOCOL
# define OLDIPC_KERNELD 00040000	/* old kerneld message channel */
# define IPC_KERNELD 00140000		/* new kerneld message channel */
# define KDHDR (sizeof(long) + sizeof(short) + sizeof(short))
# define NULL_KDHDR 0, 2, 0
#else /* NEW_KERNELD_PROTOCOL */
# define IPC_KERNELD 00040000
# define KDHDR (sizeof(long))
# define NULL_KDHDR 0
#endif /* NEW_KERNELD_PROTOCOL */

#define KERNELD_MAXCMD 0x7ffeffff
#define KERNELD_MINSEQ 0x7fff0000 /* "commands" legal up to 0x7ffeffff */
#define KERNELD_WAIT 0x80000000
#define KERNELD_NOWAIT 0

struct kerneld_msg
  {
    long mtype;
    long id;
#ifdef NEW_KERNELD_PROTOCOL
    short version;
    short pid;
#endif /* NEW_KERNELD_PROTOCOL */
    char text[1];
  };

#endif /* kerneld.h */
