/*
  Last updated : Fri Sep 13 22:04:22 1996

    J.S.Peatfield@damtp.cam.ac.uk

  Copyright (c) University of Cambridge, 1993-1996
  See the file NOTICE for conditions of use and distribution.

  $Revision: 1.3 $
  $Date: 2002/01/24 16:59:26 $
*/

#define BPCVERSION "BOOTPclient V0.64"

/* Tell the server to broadcast to reach me flag */
#define BPFLAG_BROADCAST ( 1 << 15 )

/* Back in NET2 (and before?) the ifreq.ifr_hwaddr was a char array,
   but in NET3 it is now a "sockaddr", and we need the data part.

   The code to work on older kernels has now been removed as it caused
   problems on some systems where this test no longer works (e.g. the
   AlphaLinux).  Is anyone really running 1.1.13 or earlier kernels
   anymore?

   If you still have an old kernel and need bootpc stick with version
   0.45 which is the last release to support the NET2 code.  

*/

/* local headers */
#include "bootp.h"
/* #include "log.h" */

/* for extracting the right part... */
#define use_hwaddr ifr_hwaddr.sa_data

/* Needed for getopt stuff */
extern char *optarg;
extern int optind, opterr, optopt;

/* declarations */
int BootpFatal(void);

void ParsePacket(struct bootp *bootp_recv,
		 int cookielength,
		 unsigned char *match) ;

void OutList(char *name,
	       unsigned char *cookie,
	       int len) ;

void OutString(char *name,
	       unsigned char *cookie,
	       int len) ;

void OutSearch(char *name,
		 unsigned char *cookie,
		 int len) ;

void safecopy(unsigned char *out,
	      unsigned char *string,
	      int len) ;

void doOut(char *name,
	   char *lenv) ;

int in2host(char *address,
	    int print) ;

int performBootp(char *device,
		 char *server,
		 char *bootfile,
		 int timeout_wait,
		 int givenhwaddr,
		 struct ifreq *ifr,
		 int waitformore,
		 int returniffail,
		 int print,
		 int broadcast) ;

/* My global variables */
extern int bootp_verbose ;   /* verbose mode or not 10/02/94 JSP */
extern int bootp_debug ;     /* debug mode or not 14/02/94 JSP */
extern int bootp_testing ;     /* debug mode or not 14/02/94 JSP */

#define BP_PRINT_OUT (1)
#define BP_PUT_ENV (2)
