#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

static int verbose = 0;
static int sigalrm = 0;

/* ---------------------------------------------------------------------- */

static char *strfamily(int family)
{
    switch (family) {
    case PF_INET6: return "ipv6";
    case PF_INET:  return "ipv4";
    }
    return "????";
}

static int
do_connect(struct addrinfo *ai, char *host, char *serv)
{
    struct addrinfo *res,*e;
    char uhost[INET6_ADDRSTRLEN+1];
    char userv[33];
    int sock,rc;

    /* lookup peer */
    ai->ai_flags = AI_CANONNAME;
    if (0 != (rc = getaddrinfo(host, serv, ai, &res))) {
	if (verbose)
	    fprintf(stderr,"getaddrinfo: %s\n",gai_strerror(rc));
	return -1;
    }
    for (e = res; e != NULL; e = e->ai_next) {
	if (0 != getnameinfo((struct sockaddr*)e->ai_addr,e->ai_addrlen,
			     uhost,INET6_ADDRSTRLEN,userv,32,
			     NI_NUMERICHOST | NI_NUMERICSERV)) {
	    if (verbose)
		fprintf(stderr,"getnameinfo: oops\n");
	    continue;
	}
	if (-1 == (sock = socket(e->ai_family, e->ai_socktype,
				 e->ai_protocol))) {
	    if (verbose)
		fprintf(stderr,"socket (%s): %s\n",
			strfamily(e->ai_family),strerror(errno));
	    continue;
	}

	if (-1 == connect(sock,e->ai_addr,e->ai_addrlen)) {
	    if (verbose)
		fprintf(stderr,"%s %s [%s] %s connect: %s\n",
			strfamily(e->ai_family),e->ai_canonname,uhost,userv,
			strerror(errno));
	    close(sock);
	    continue;
	}
	if (verbose)
	    fprintf(stderr,"%s %s [%s] %s open\n",
		    strfamily(e->ai_family),e->ai_canonname,uhost,userv);
	return sock;
    }
    return -1;
}

/* ---------------------------------------------------------------------- */

static void
usage(char *name)
{
    char           *h;

    h = strrchr(name,'/');
    if (h) h++; else h=name;
    fprintf(stderr,
	    "usage: %s [ options ] url\n"
	    "options:\n"
	    "  -h         this text\n"
	    "  -4         use ipv4\n"
	    "  -6         use ipv6\n"
	    "  -v         verbose\n"
	    "  -w sec     set timeout\n"
	    "  -o file    save to file\n",
	    h);
    exit(1);
}

void catchalarm() { sigalrm=1; }

int
wget_main(int argc, char **argv)
{
    struct sigaction act,old;
    struct addrinfo ask;
    char proto[16];
    char host[128];
    char path[1024];
    char port[16];
    char proxy_host[128];
    char proxy_port[16];
    char *proxy,*filename;
    int c, sock, status, length, block, total;
    char buf[4096];
    FILE *fp,*out;
    int timeout = 60;

    memset(&ask,0,sizeof(ask));
    ask.ai_family = PF_UNSPEC;
    ask.ai_socktype = SOCK_STREAM;
    proxy = NULL;
    filename = NULL;
    length = 0;
    total = 0;
    
    /* parse options */
    for (;;) {
	if (-1 == (c = getopt(argc,argv,"h46vo:w:p:")))
	    break;
	switch (c) {
	case 'h':
	    usage(argv[0]);
	    break;
	case '4':
	    ask.ai_family = PF_INET;
	    break;
	case '6':
	    ask.ai_family = PF_INET6;
	    break;
	case 'v':
	    verbose++;
	    break;
	case 'o':
	    filename = optarg;
	    break;
	case 'p':
	    proxy = optarg;
	    break;
	case 'w':
	    timeout = atoi(optarg);
	    break;
	default:
	    exit(1);
	}
    }

    if (optind == argc) {
	usage(argv[0]);
	exit(1);
    }

    /* parse url */
    strcpy(port,"80");
    if (4 != sscanf(argv[optind],"%16[^:]://%127[^:/]:%15[0-9]%1023s",
		    proto,host,port,path) &&
	3 != sscanf(argv[optind],"%16[^:]://%127[^:/]%1023s",
		    proto,host,path)) {
	fprintf(stderr,"url parse error\n");
	exit(1);
    }

    /* look for proxy */
    if (NULL == proxy) {
	if (0 == strcasecmp("ftp",proto)) {
	    proxy = getenv("ftp_proxy");
	} else {
	    proxy = getenv("http_proxy");
	}
    }
    if (0 != strcasecmp("http",proto) && NULL == proxy) {
	fprintf(stderr,"need a proxy for %s requests\n",proto);
	exit(1);
    }
    if (proxy && 2 != sscanf(proxy,"http://%127[^:]:%15[0-9]",
		    proxy_host,proxy_port)) {
	fprintf(stderr,"proxy url parse error\n");
	exit(1);
    }

    /* open file */
    if (NULL != filename) {
	out = fopen(filename,"w");
	if (NULL == out) {
	    fprintf(stderr,"open %s: %s\n",filename,strerror(errno));
	    exit(2);
	}
    } else {
	out = stdout;
    }

    /* signal handler for timeout via SIGALRM */
    memset(&act,0,sizeof(act));
    act.sa_handler  = catchalarm;
    sigemptyset(&act.sa_mask);
    sigaction(SIGALRM,&act,&old);

    /* connect */
    alarm(timeout);
    sock = do_connect(&ask,
		      proxy ? proxy_host : host,
		      proxy ? proxy_port : port);
    if (-1 == sock) {
	fprintf(stderr,"can't connect server\n");
	exit(3);
    }
    fp = fdopen(sock,"r+");
    setvbuf(fp,NULL,_IONBF,0);

    /* send request */
    if (proxy) {
	fprintf(fp,
		"GET %s://%s:%s%s HTTP/1.0\r\n"
		"Connection: close\r\n"
		"\r\n", proto, host, port, path);
    } else {
	fprintf(fp,
		"GET %s HTTP/1.0\r\n"
		"Host: %s\r\n"
		"Connection: close\r\n"
		"\r\n", path, host);
    }
    fflush(fp);

    /* read header */
    alarm(timeout);
    if (NULL == fgets(buf,sizeof(buf),fp)) {
	fprintf(stderr,"timeout / connection closed (status)\n");
	exit(4);
    }
    status = atoi(buf+9);
    if (200 != status) {
	fprintf(stderr,"%s\n",buf+9);
	exit(5);
    }
    for (;!sigalrm;) {
	if (NULL == fgets(buf,sizeof(buf),fp)) {
	    fprintf(stderr,"timeout / connection closed (header)\n");
	    exit(6);
	}
	if (strlen(buf) < 3)
	    break;
	if (0 == strncasecmp("Content-length:",buf,15))
	    length = atoi(buf+15);
    }

    /* save content */
    for (;!sigalrm;) {
	alarm(timeout);
	block = fread(buf,1,sizeof(buf),fp);
	if (0 == block)
	    break;
	fwrite(buf,1,block,out);
	total += block;
    }
    alarm(0);
    if (length != 0 && length != total) {
	fprintf(stderr,"file incomplete (got %d, expected %d)\n",
		total,length);
	exit(7);
    }

    if (NULL != filename)
	fclose(out);
    fclose(fp);
    exit(0);
}
