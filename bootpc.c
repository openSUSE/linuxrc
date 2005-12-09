/*
  Last updated : Mon Sep 16 15:20:57 1996
  Modified by JSP from code by Charles Hawkins <ceh@eng.cam.ac.uk>,

    J.S.Peatfield@damtp.cam.ac.uk

  Copyright (c) University of Cambridge, 1993-1996
  See the file NOTICE for conditions of use and distribution.

  $Revision: 1.11 $
  $Date: 2003/03/03 17:27:51 $
*/

/* Standard headers */
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/ip_icmp.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <net/if_arp.h>
#include <sys/time.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <ctype.h>
#include <sys/uio.h>

/* local headers */
#include "global.h"
#include "bootpc.h"

#define logMessage my_logmessage
#define perror logMessage

#define BOOTP_BUFSIZ	2096

extern int my_logmessage (char *buffer_pci, ...);

/* My global variables */
int bootp_verbose = 0 ;   /* verbose mode or not 10/02/94 JSP */
int bootp_debug   = 0 ;   /* debug mode or not 14/02/94 JSP */
int bootp_testing = 0;

static int returniffail ;  /* Return to the user if we fail */
static int printflag;      /* Print control */
static int sockfd;

int performBootp(char *device,
		 char *server,
		 char *bootfile,
		 int timeout_wait,
		 int givenhwaddr,
		 struct ifreq *their_ifr,
		 int waitformore,
		 int bp_rif,
		 int bp_pr,
		 int broadcast)
{
  struct ifreq ifr;
  struct sockaddr_in cli_addr, serv_addr;
  struct bootp *bootp_xmit, *bootp_recv;
  fd_set rfds, wfds, xfds;
  struct timeval timeout ;
  int32 rancopy ;
  int cookielength ;
  long plen ;
  int retry_wait, waited=0 ;
  int one=1, i ;
  struct timeval tp;
  int received_packet = 0 ;
/* See RFC1497, RFC1542  09/02/94   JSP  */
  unsigned char mincookie[] = {99,130,83,99,255} ;
  struct msghdr mh;
  char cbuf[CMSG_SPACE(sizeof(struct in_pktinfo))];
  struct iovec iov;

  returniffail=bp_rif ;
  printflag=bp_pr ;

/* zero structure before use */
  memset((char *) &serv_addr, 0, sizeof(serv_addr));

  serv_addr.sin_family = AF_INET;
  serv_addr.sin_addr.s_addr = inet_addr(server) ;
  serv_addr.sin_port = htons(IPPORT_BOOTPS);

  if((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
    perror("bootpc: socket failed");
    return BootpFatal();
  }
  
  if (setsockopt(sockfd,SOL_SOCKET,SO_BROADCAST,&one,sizeof(one))==-1) {
    perror("bootpc: setsockopt failed");
    return BootpFatal();
  }
  
  memset((char *) &cli_addr, 0, sizeof(cli_addr));
  cli_addr.sin_family = AF_INET;
  cli_addr.sin_addr.s_addr = htonl(INADDR_ANY);
  cli_addr.sin_port = htons(IPPORT_BOOTPC);

  if(bind(sockfd, (struct sockaddr *) &cli_addr, sizeof(cli_addr)) < 0) {
    perror("bootpc: bind failed");
    return BootpFatal();
  }

/* allocate bootp packet before we use it */
  bootp_xmit = (struct bootp *) malloc(BOOTP_BUFSIZ) ;
  memset((char *) bootp_xmit, 0, BOOTP_BUFSIZ) ;

  bootp_recv = (struct bootp *) malloc(BOOTP_BUFSIZ) ;
  memset((char *) bootp_recv, 0, BOOTP_BUFSIZ) ;

  /* Server needs to broadcast for me to see it */
  if (broadcast || givenhwaddr)
    bootp_xmit->bp_flags |= htons(BPFLAG_BROADCAST);

/* Don't do this if we were given the MAC address to use.  27/09/94  JSP */
  if (givenhwaddr) {
    /* Assuming ETHER if given HW */
    ifr.ifr_hwaddr.sa_family = ARPHRD_ETHER ;
    ifr.ifr_hwaddr = their_ifr->ifr_hwaddr ;
  } else {
/* Get the hardware address, and family information */

    memcpy(ifr.ifr_name, device, strlen(device)+1);

    if(!bootp_testing) {
      /* Set the interface flags. */
      ifr.ifr_flags = IFF_UP | IFF_BROADCAST | IFF_NOTRAILERS | IFF_RUNNING;
      if (ioctl(sockfd, SIOCSIFFLAGS, &ifr)) {
        perror("bootpc: ioctl SIOCSIFFLAGS");
        return BootpFatal();
      }

      /* Set a non-zero internet address. */
      memset((struct sockaddr_in *)&ifr.ifr_addr, 0, sizeof(struct sockaddr_in));
      ((struct sockaddr_in *)&ifr.ifr_addr)->sin_addr.s_addr = htonl(1);
      ((struct sockaddr_in *)&ifr.ifr_addr)->sin_family = AF_INET;
      if (ioctl(sockfd, SIOCSIFADDR, &ifr)) {
        perror("bootpc: ioctl SIOCSIFADDR");
        return BootpFatal();
      }
    }

    if (ioctl(sockfd, SIOCGIFHWADDR, &ifr) < 0) {
      perror("bootpc: ioctl failed");
      return BootpFatal();
    }
  }

/* Check the network family if in NET3 or later, before NET3 you couldn't
   examine this info (as far as I know.) */

/* set the htype field from the sa.family of the hardware address, if
   this doesn't work for your interface type let me know please. */

  bootp_xmit->bp_htype = ifr.ifr_hwaddr.sa_family;
  bootp_xmit->bp_hlen = IFHWADDRLEN ;  /* All MAC addresses are the same length */
  
  if (bootp_debug)
    logMessage("Got family=%d (Ether=%d)",
	       bootp_xmit->bp_htype, ARPHRD_ETHER);

/* If we have the time seed with it xor the hardware address, otherwise
   use the hardware address 12/02/94 JSP */
  if (gettimeofday(&tp, NULL) == -1)
    rancopy = 0 ;
  else
    rancopy = tp.tv_sec + tp.tv_usec ;

/* Do the XOR */
  for (i=0; i < IFHWADDRLEN ; ++i) {
    ((unsigned char *)&rancopy)[ i % sizeof(rancopy) ] ^=
      ((unsigned char *)(ifr.use_hwaddr))[i] ;
  }
/* and set the seed */
  srand(rancopy) ;

  if(bootp_debug) {
    logMessage("hardware addr is :") ;
    for (i=0; i < bootp_xmit->bp_hlen ; ++i)
      logMessage("%2.2X ", ((unsigned char *)(ifr.use_hwaddr))[i]) ;
  }

/* Now fill in the packet. */
  bootp_xmit->bp_op = BOOTREQUEST ;

/* Now with my understanding of the bootp protocol we *should* just
   need to copy the hwaddr over, but it seems that at least ARCNET
   bootb servers are wird in this respect.  So here is a switch in
   case of other weirdness.  JSP */

  switch(bootp_xmit->bp_htype) {
/* ARCNET uses a "fake" ethernet address, with the ARCNET address at
   the wrong end.  At least the Novell bootp server on ARCNET assumes
   this.  Thanks to Tomasz Motylewski <motyl@tichy.ch.uj.edu.pl> for
   reporting this.  */
  case ARPHRD_ARCNET :
    memcpy(bootp_xmit->bp_chaddr+IFHWADDRLEN-1, (char *)(ifr.use_hwaddr), 1) ;
    bootp_xmit->bp_htype=ARPHRD_ETHER;
    bootp_xmit->bp_hlen=IFHWADDRLEN;
    break ;

/* Add other network weirdness here */

/* For sensible networks the rest is normal */
  default :
    memcpy(bootp_xmit->bp_chaddr,
	   (char *)(ifr.use_hwaddr),
	   bootp_xmit->bp_hlen) ;
  }

/* Must start with zero here, see RFC1542 09/02/94 JSP */
  bootp_xmit->bp_secs = 0;

/* Put in the minimal RFC1497 Magic cookie 09/02/94 JSP */
  memcpy(bootp_xmit->bp_vend, mincookie, sizeof(mincookie));

/* Put the user precified bootfile name in place 12/02/94 */
  memcpy(bootp_xmit->bp_file, bootfile, strlen(bootfile)+1);

/* put a random value in here, but keep a copy to check later 09/02/94  JSP */
  bootp_xmit->bp_xid = rancopy = rand() ;

  retry_wait = 2 ;
  if (bootp_verbose)
    logMessage("BOOTPclient broadcast...");

  while (((waited <= timeout_wait) && !received_packet) ||
	 ((waited <= waitformore) && received_packet)) {
    
    if (!received_packet) {  /* Move this to a sendpacket function */
      /* set time of this timeout  09/02/94  JSP */
      bootp_xmit->bp_secs = htons(waited) ;
      if (bootp_verbose) {
	logMessage("."); fflush(stderr);
      }
      if (bootp_debug) {
	logMessage("Size = %ld", (long)sizeof(struct bootp)) ;
      }

      /* Build a message that contains the interface number. */
      memset(&mh, 0, sizeof(mh));
      mh.msg_control = cbuf;
      mh.msg_controllen = sizeof(cbuf);
      mh.msg_name = (struct sockaddr *)&serv_addr;
      mh.msg_namelen = sizeof(struct sockaddr_in);
      mh.msg_iov = &iov;
      mh.msg_iovlen = 1;
      iov.iov_base = bootp_xmit;
      iov.iov_len = sizeof(struct bootp);
      {
	struct cmsghdr *cmsg = CMSG_FIRSTHDR(&mh);
	struct in_pktinfo *pki = (struct in_pktinfo *)CMSG_DATA(cmsg);
	cmsg->cmsg_len = CMSG_LEN(sizeof(struct in_pktinfo));
	cmsg->cmsg_level = SOL_IP;
	cmsg->cmsg_type = IP_PKTINFO;
	if (ioctl(sockfd, SIOCGIFINDEX, &ifr) < 0)
	  perror("bootpc: SIOCGIFINDEX");
	pki->ipi_ifindex = ifr.ifr_ifindex;
	pki->ipi_spec_dst.s_addr = 0;
	pki->ipi_addr.s_addr = 0xffffffff;
      }

      if(sendmsg(sockfd, &mh, 0) < 0) {
	perror("bootpc: sendmsg");
	return BootpFatal();
      }
    }

    /* Move rest of this loop to a receivepacket function */
    FD_ZERO(&rfds);
/* The above was missing, thanks to
   Gilles Detillieux <grdetil@cliff.scrc.UManitoba.CA> for pointing it out */
    FD_SET(sockfd,&rfds);
    FD_ZERO(&wfds);
    FD_ZERO(&xfds);

/* Randomise the delays a little as suggested in RFC1542  09/02/94  JSP */
    timeout.tv_sec = retry_wait + (1+(rand() & (retry_wait-1))) ;
    timeout.tv_usec = 0;
    waited += timeout.tv_sec ;  /* Add this to the total time we have waited */

    if(select(sockfd+1, &rfds, &wfds, &xfds, &timeout)<0) {
      perror("bootpc: select");
      return BootpFatal();
    }

    if(!FD_ISSET(sockfd, &rfds)) {
      retry_wait = retry_wait*2;
    } else {
      if ((plen = recvfrom(sockfd, bootp_recv, BOOTP_BUFSIZ, 0,
			   (struct sockaddr *)NULL, (int *)NULL)) < 0){
	perror("bootpc: recvfrom");
	return BootpFatal();
      }

      if (bootp_debug) {
	logMessage("plen = %ld  plen - sizeof(struct bootp) = %ld",
		   (long)plen, (long)(plen - sizeof(struct bootp))) ;
      }
      cookielength = 64 + plen - sizeof(struct bootp) ;
      
      if (bootp_recv->bp_xid == (uint32_t) rancopy) {
	if (!received_packet) {
	  /* If we haven't already recieved a packet then set the time to wait
	     further to be now + time user specified */
	  waitformore += waited ;
	  received_packet = 1 ;
	} else {
	  /* To make it look a bit prettier */
	  if (printflag & BP_PRINT_OUT)
	    printf("\n") ;
	}
	/* Pass the cookie info, the mincookie to look for and our address to
	   the cookie parser.  It needs our address to get the network and
	   broadcast bits right if the SUBNET is defined in the cookie.
	   10/02/94  JSP */
	ParsePacket(bootp_recv,
		    cookielength,
		    mincookie) ;
      } else {
	/* xid mismatch so normally silently ignore */
	if (bootp_verbose) {
	  logMessage("WARNING bp_xid mismatch got 0x%lx sent 0x%lx",
		     (long)bootp_recv->bp_xid, (long)rancopy) ;
	}
      }
    }
  }
  if (!received_packet) {
    logMessage("No response from BOOTP server");
    return BootpFatal();
  }

  if (sockfd)
    close (sockfd) ;
  return 0 ;  /* Normal exit */
}
    
int BootpFatal()
{
  if (sockfd)
    close (sockfd) ;

  if (bootp_debug)
    logMessage("In BootpFatal(), errno was %d", errno) ;

  if (returniffail) {
    logMessage("bootpc failed to locate a network address") ;
    return 1 ;
  }

  logMessage(" Unable to locate an IP address for this host.\n"
	     "     ***Please report this problem**\n\n"
	     "          [Unable to continue]\n");

  if (bootp_debug)
    logMessage("Will now loop forerver, break out of this to fix") ;

  while(1) {
    /* your eyes are getting heavy.... */
    sleep(1000) ;
  }
}

/* Parse Magic cookies as specified in RFC1497, well only the bits we
   are actually interested in...  09/02/94 JSP
*/
void ParsePacket(struct bootp * bootp_recv,
		 int cookielength,
		 unsigned char *match)
{
  int i=0, len, tag ;
  int subnet = 0 ;
  struct in_addr temp ;
  unsigned char *cookie = (unsigned char *)(bootp_recv->bp_vend) ;
  struct in_addr temp_addr, my_addr ;

  temp_addr.s_addr = bootp_recv->bp_siaddr.s_addr ;
  OutString("SERVER", (unsigned char *)inet_ntoa(temp_addr), -1);
  my_addr.s_addr = bootp_recv->bp_yiaddr.s_addr ;
  OutString("IPADDR", (unsigned char *)inet_ntoa(my_addr), -1);
  if (bootp_verbose) {
    logMessage("bp_file len is %d", strlen(bootp_recv->bp_file)) ;
  }	
  OutString("BOOTFILE",
	      (unsigned char *)bootp_recv->bp_file, -1) ;

  if (bootp_debug) {  /* dump cookie contents in HEX 10/02/94  JSP */
    for (i=0; i<cookielength; i++) {
      if ((i%8) == 0)
	logMessage("\n %2.2d :", i) ;
      logMessage(" 0x%2.2X", cookie[i]) ;
    }
    logMessage("") ;
  }

/* Must get the same cookie back as we sent  09/02/94  JSP */
  for (i=0; i < 4; ++i) {
    if (cookie[i] != match[i]) {
      if (bootp_verbose)
	logMessage("RFC1497 Cookie mismatch at offset %d", i) ;
      return ;
    }
  }

  if (bootp_verbose)
    logMessage("found valid RFC1497 cookie, parsing...") ;

/* Carry on after the cookie for other data  09/02/94  JSP */
  while (i < cookielength) {
    tag = cookie[i] ;

    if (bootp_verbose)
      logMessage("cookie position: %d - tag: %d", i, tag) ;

/* If we arn't at the end of the cookie and we will need it extract len */
    if ((i < cookielength - 1) && (tag != TAG_PAD) && (tag != TAG_END))
      len = cookie[i+1] ;
    else
      len = 0 ;

/* Warn if the "length" takes us out of the cookie and truncate */
    if (len + i > cookielength) {
      if (bootp_verbose)
	logMessage("TAG %d at %d.  len %d, overrun %d",
		   cookie[i], i, len, i + len - cookielength) ;
      /* And truncate in any case even with no warning */
      len = cookielength - i ;
    }

    switch (cookie[i]) {  /* The monster switch statement ... */
/* PAD cookie */
    case TAG_PAD :
      i++ ;
      break ;

/* SUBNET we are in */
    case TAG_SUBNET_MASK :
      if (bootp_verbose && len != 4)
	logMessage("WARNING len of tag 1 is %d not 4", len) ;
      memcpy((char *)&temp, cookie + i + 2, 4) ;
      OutString("NETMASK", (unsigned char *)inet_ntoa(temp), -1) ;

/* Both values are in network order so this doesn't care about the
   ordering 10/02/94 JSP */
      my_addr.s_addr &=  temp.s_addr ;
      OutString("NETWORK", (unsigned char *)inet_ntoa(my_addr), -1) ;
      my_addr.s_addr |= ~temp.s_addr ;
      OutString("BROADCAST", (unsigned char *)inet_ntoa(my_addr), -1) ;

/* defined so we know later that subnet info has been printed 11/02/94  JSP */
      subnet = 1 ;
      i += len + 2 ;
      break ;

/* Time of day */
    case TAG_TIME_OFFSET :
      /* ignored */
      i += len + 2 ;
      break ;

/* IP Gateways (routers) */
    case TAG_GATEWAY :
      OutList("GATEWAYS", cookie+i+2, len) ;
      i += len + 2 ;
      break ; 

/* Timeservers (see RFC-868) */
    case TAG_TIME_SERVER :
      OutList("TIMESRVS", cookie+i+2, len) ;
      i += len + 2 ;
      break ; 

/* IEN-116 Nameservers */
    case TAG_NAME_SERVER :
      OutList("IEN116SRVS", cookie+i+2, len) ;
      i += len + 2 ;
      break ; 

/* DNS Nameservers */
    case TAG_DOMAIN_SERVER :
      OutList("DNSSRVS", cookie+i+2, len) ;
      i += len + 2 ;
      break ; 

/* LOGGING servers */
    case TAG_LOG_SERVER :
      OutList("LOGSRVS", cookie+i+2, len) ;
      i += len + 2 ;
      break ; 

/* Quote of day/Cookie servers */
    case TAG_COOKIE_SERVER :
      OutList("QODSRVS", cookie+i+2, len) ;
      i += len + 2 ;
      break ; 

/* LPR servers */
    case TAG_LPR_SERVER :
      OutList("LPRSRVS", cookie+i+2, len) ;
      i += len + 2 ;
      break ; 

/* Impress (Imogen) servers */
    case TAG_IMPRESS_SERVER :
      OutList("IMPRESSSRVS", cookie+i+2, len) ;
      i += len + 2 ;
      break ; 

/* Remote Location Protocol servers */
    case TAG_RLP_SERVER :
      OutList("RLPSRVS", cookie+i+2, len) ;
      i += len + 2 ;
      break ; 

/* HOSTNAME (may be fqdn or leaf) */
    case TAG_HOST_NAME :
      OutString("HOSTNAME", cookie+i+2, len) ;
      i += len + 2 ;
      break ;

/* BOOT File Size (ignored) */
    case TAG_BOOT_SIZE :
      i += len + 2 ;
      break ;

/* Merit DUMP File name (ignored) */
    case TAG_DUMP_FILE :
      i += len + 2 ;
      break ;

/* DOMAIN */
    case TAG_DOMAIN_NAME :
      OutString("DOMAIN", cookie+i+2, len) ;
      OutSearch("SEARCH", cookie+i+2, len) ;
      i += len + 2 ;
      break ;

/* SWAPServer address */
    case TAG_SWAP_SERVER :
      OutList("SWAPSRVR", cookie+i+2, len) ;
      i += len + 2 ;
      break ;

/* Root pathname to mount as root filesystem  */
    case TAG_ROOT_PATH :
      OutString("ROOT_PATH", cookie+i+2, len) ;
      i += len + 2 ;
      break ;

/* Extensions.  Name of further Cookie data */
    case TAG_EXTEN_FILE :
      OutString("EXTEN_FILE", cookie+i+2, len) ;
      i += len + 2 ;
      break ;

/* NIS (formerly YP) domain name */
    case TAG_NIS_DOMAIN :
      OutString("YPDOMAIN", cookie+i+2, len) ;
      i += len + 2 ;
      break ;
       
/* NIS (formerly YP) server */
    case TAG_NIS_SERVER :
      OutList("YPSRVR", cookie+i+2, len) ;
      i += len + 2 ;
      break ;
       
/* Time servers */
    case TAG_NTP_SERVER :
      OutList("NTPSRVS", cookie+i+2, len) ;
      i += len + 2 ;
      break ;

/* DHCP Server Identifier */
    case TAG_DHCP_SERVER_ID :
      OutList("DHCPSID", cookie+i+2, len) ;
      i += len + 2 ;
      break ; 

/* END of cookie (phew) */
    case TAG_END :
      if (bootp_verbose)
	logMessage("end of cookie parsing, END tag found") ;
      return ;

    default:
      { char name[30] ;
	if (bootp_verbose) {
	  if (tag >= 128 && tag <= 254) /* reserved */
	    logMessage("Reserved TAG %d at %d (len %d)", tag, i, len) ;
	  else
	    logMessage("Unknown TAG %d at %d (len %d)", tag, i, len) ;
	}
	sprintf(name, "T%3.3d", tag) ;
	OutString(name, cookie+i+2, len) ;
	i += 2 + len ;
      }
      break ;
    }
  }

  /* No SUBNET TAG in the cookie so we fake guess here, if this is wrong
     then fix your bootp server to tell us the answer rather than
     hacking this code. */

  if (!subnet) {
    struct in_addr netmask ;
    int type ;
	
    if (bootp_verbose)
      logMessage("Guessing netmask from IP address range") ;

    type = ntohl(temp_addr.s_addr) ;
    if ((type & 0x80000000) == 0) {
      /* Class A */
      netmask.s_addr = htonl(0xFF000000) ;
    } else if ((type & 0x40000000) == 0) {
      /* Class B */
      netmask.s_addr = htonl(0xFFFF0000) ;
    } else if ((type & 0x20000000) == 0) {
      /* Class C */
      netmask.s_addr = htonl(0xFFFFFF00) ;
    } else { /* GOD KNOWS... other classes are weird */
      if (bootp_verbose)
	logMessage("IP number not Class A,B or C. Setting NETMASK to zero") ;
      netmask.s_addr = htonl(0x00000000) ;
    }
    OutString("NETMASK", (unsigned char *)inet_ntoa(netmask), -1);
    temp_addr.s_addr &= netmask.s_addr ;
    OutString("NETWORK", (unsigned char *)inet_ntoa(temp_addr), -1);
    temp_addr.s_addr |= ~netmask.s_addr ;
    OutString("BROADCAST", (unsigned char *)inet_ntoa(temp_addr), -1);
  }
}


/* Print out a list of IP addresses */
void OutList(char *name,
	       unsigned char *cookie,
	       int len)
{
  struct in_addr temp ;
  char lenv[BOOTP_BUFSIZ], *ptr ;
  int n, c, i;

  if (bootp_verbose)
    logMessage("%s found len=%d", name, len) ;

  if ((len % 4) != 0) {
    if (bootp_verbose)
      logMessage("ERROR %s length (%d) not 4 div", name, len) ;
    return ;
  }
  if (len == 0) /* Nothing to do  10/02/94  JSP */
    return ;

  for (n=0,i=1 ; len; len -= 4, cookie += 4, i++) {
    char lbuf[BOOTP_BUFSIZ] ;
    memcpy((char *)&temp, cookie, 4) ;
    ptr = inet_ntoa(temp) ;
    c = strlen(ptr) ;
    sprintf(lbuf, "%s_%d", name, i) ;
    OutString(lbuf, (unsigned char *)ptr, c) ;
    strncpy(lenv+n, ptr, c) ;
    n += c ;
    if (len > 4)
      lenv[n++] = ' ';
  }
  lenv[n] = 0 ;

  doOut(name, lenv) ;
}

/* Prints the string passed */
void OutString(char *name,
	       unsigned char *cookie,
	       int len)
{
  char lenv[BOOTP_BUFSIZ];
  if (len == -1)
    len = strlen((char *)cookie) ;

  safecopy((unsigned char *)lenv, cookie, len);
  doOut(name, lenv) ;
}

/* Prints the string as usable in a DNS search.  This is doing the
   same as the old default BIND (pre 4.9.3) did with a DOMAIN line,
   for backwards compatibility, and since BOOTP doesn't allow a way to
   specify the search path explicitly */
void OutSearch(char *name,
		 unsigned char *cookie,
		 int len)
{
  unsigned char *ptr, *nptr ;
  unsigned char buf[258] ;  /* Max len is 255 */
  char lenv[BOOTP_BUFSIZ] ;
  int n=0;

  strncpy((char *)buf, (char *)(cookie), len) ;
  buf[ len + 1 ] = 0 ;  /* Null terminate it */
  ptr = buf ;

  while (len) {
    safecopy((unsigned char *)(lenv+n), ptr, len) ;
    n += len ;
    /* Goto next bit */
    nptr = (unsigned char *)strchr((char *)ptr, '.') ;  /* Cast cast cast */
    if (nptr == NULL) {
      len = 0 ; /* End of string I hope */
    } else {
      if (strchr((char *)nptr + 1, '.') == NULL) {
	/* Trad to not use last component */
	len = 0 ;
      } else {
	len -= (nptr - ptr) + 1 ;
	ptr = nptr + 1 ;
	lenv[n++] = ' ' ;
      }
    }
  }
  lenv[n] = 0 ;

  doOut(name, lenv) ;
}

/* Copy those bits of a string which are alphanumeric or in a
   "safe" list of characters. */
void safecopy(unsigned char *out,
	      unsigned char *string,
	      int len)
{
  char safe[] = "./:-_=+[]~()%&*^#@! " ;
  int i, c ;

  for (i =0 ; i < len; ++i) {
    c = string[i] ;
    if (isalnum(c))
      out[i] = c ;   /* alphanumeric */
    else {  /* Not alphanumeric */
      if (strchr(safe, c) != NULL) {
	out[i] = c ; /* but safe */
      } else {
	out[i] = '?' ; /* NOT safe */
	if (bootp_verbose)
	  logMessage("Illegal char 0x%2.2X", c) ;
      }
    }
  }
  out[i] = 0 ;
}

void doOut(char *name,
	   char *lenv)
{
  if (printflag & BP_PRINT_OUT) {
    printf("%s='%s'\n", name, lenv) ;
  }
  if (printflag & BP_PUT_ENV) {
    char envb[BOOTP_BUFSIZ], *envp ;
    sprintf(envb, "BOOTP_%s=%s", name, lenv) ;
    envp = strdup(envb) ;
    if (bootp_debug)
      logMessage("ENV setting :%s:", envp) ;
    putenv(envp) ;
  }
}
