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

#include "global.h"
#include "util.h"

static int sigalrm = 0;

static void catchalarm(int sig);
// static char *strfamily(int family);
static int do_connect(struct addrinfo *ai, char *host, int port);
static int read_line(int fd, char *buf, int len);


void catchalarm(int sig)
{
  sigalrm = 1;
}


#if 0
char *strfamily(int family)
{
  switch(family) {
    case PF_INET6: return "ipv6";
    case PF_INET:  return "ipv4";
  }

  return "????";
}
#endif

int do_connect(struct addrinfo *ai, char *host, int port)
{
  struct addrinfo *res, *e;
  char uhost[INET6_ADDRSTRLEN + 1];
  char userv[33];
  int sock, rc;
  char serv[32];
  char buf[256];

  sprintf(serv, "%d", port);

  /* lookup peer */
  ai->ai_flags = AI_CANONNAME;
  if((rc = getaddrinfo(host, serv, ai, &res))) {
    str_copy(&config.net.error, (char *) gai_strerror(rc));
    return -1;
  }

  for(e = res; e; e = e->ai_next) {
    if(
      getnameinfo(
        (struct sockaddr *) e->ai_addr,
        e->ai_addrlen,
        uhost,
        INET6_ADDRSTRLEN,
        userv,
        32,
        NI_NUMERICHOST | NI_NUMERICSERV
      )
    ) continue;

    if((sock = socket(e->ai_family, e->ai_socktype, e->ai_protocol)) == -1) {
      if(!config.net.error) {
        sprintf(buf, "socket: %s", strerror(errno));
        str_copy(&config.net.error, buf);
      }
      continue;
    }

    if(connect(sock, e->ai_addr, e->ai_addrlen) == -1) {
      if(!config.net.error) {
        sprintf(buf, "connect: %s", strerror(errno));
        str_copy(&config.net.error, buf);
      }
      close(sock);
      continue;
    }

    str_copy(&config.net.error, NULL);

    return sock;
  }

  return -1;
}


int read_line(int fd, char *buf, int len)
{
  int cnt = 0;

  while(cnt < len && read(fd, buf + cnt, 1) > 0) {
    cnt++;
    if(buf[cnt - 1] == '\n') break;
  }
  buf[cnt] = 0;

  return cnt;
}


int http_connect(inet_t *server, char *name, char *proto, inet_t *proxy, int server_port, int proxy_port, int *file_len)
{
  struct sigaction act, old;
  struct addrinfo ask;
  int i, sock, port;
  char buf[0x1000];
  int timeout = 60;
  inet_t *host;

  memset(&ask, 0, sizeof ask);
  ask.ai_family = PF_INET;		/* PF_INET6 */
  ask.ai_socktype = SOCK_STREAM;

//  fprintf(stderr, "proto = %s, server = %s:%d, proxy = %s:%d\n", proto, server ? server->name : "", server_port, proxy ? proxy->name : "", proxy_port);

  proto = proto ?: "http";

  if(proxy && proxy_port) {
    host = proxy;
    port = proxy_port;
  }
  else {
    host = server;
    server_port = server_port > 0 ? server_port : 80;
    port = server_port;
  }

  /* signal handler for timeout via SIGALRM */
  memset(&act, 0, sizeof act);
  act.sa_handler  = catchalarm;
  sigemptyset(&act.sa_mask);
  sigaction(SIGALRM, &act, &old);

  /* connect */
  alarm(timeout);
  if((sock = do_connect(&ask, inet_ntoa(host->ip), port)) < 0) {

    alarm(0);
    sigaction(SIGALRM, &old, NULL);

    return -1;
  }

  if(!name) return sock;

  /* send request */
  if(proxy) {
    if(server_port) {
      sprintf(buf,
        "GET %s://%s:%d%s HTTP/1.0\r\n" "Connection: close\r\n" "\r\n",
        proto, server->name, server_port, name
      );
    }
    else {
      sprintf(buf,
        "GET %s://%s%s HTTP/1.0\r\n" "Connection: close\r\n" "\r\n",
        proto, server->name, name
      );
    }
  }
  else {
    sprintf(buf,
      "GET %s HTTP/1.0\r\n"
      "Host: %s\r\n"
      "Connection: close\r\n"
      "\r\n",
      name, host->name
    );
  }
  write(sock, buf, strlen(buf));

  /* read header */
  alarm(timeout);
  if(!read_line(sock, buf, sizeof buf)) {
    str_copy(&config.net.error, "timeout / connection closed");
    close(sock);

    alarm(0);
    sigaction(SIGALRM, &old, NULL);

    return -1;
  }

  if(atoi(buf + 9) != 200) {
    for(i = 9; i < sizeof buf; i++) {
      if(buf[i] == '\n' || buf[i] == '\r') buf[i] = 0;
      if(!buf[i]) break;
    }
    str_copy(&config.net.error, buf + 9);
    close(sock);

    alarm(0);
    sigaction(SIGALRM, &old, NULL);

    return -1;
  }

  for(; !sigalrm;) {
    if(!read_line(sock, buf, sizeof buf)) {
      str_copy(&config.net.error, "timeout / connection closed");
      close(sock);

      alarm(0);
      sigaction(SIGALRM, &old, NULL);

      return -1;
    }
    if(strlen(buf) < 3) break;
    if(!strncasecmp("Content-length:", buf, 15)) {
      if(file_len) *file_len = atoi(buf + 15);
    }
  }

  alarm(0);
  sigaction(SIGALRM, &old, NULL);

  return sock;
}
