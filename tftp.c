#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <netinet/in.h>
#include <netdb.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <errno.h>
#include <string.h>

#include "tftp.h"

/* send RRQ/ACK of tftp->nr and get data package tftp->nr+1 */
static int sendget(tftp, code, len)
struct tftp *tftp;
int code;
int len;
{
  fd_set rs;
  int r, nr;
  int retry;
  struct sockaddr_in osa;
  struct timeval tv;
  socklen_t osl;
  unsigned char sendbuf[4 + 512];

  tftp->buf[0] = code >> 8;
  tftp->buf[1] = code;
  memcpy(sendbuf, tftp->buf, len);
  for (retry = 0; retry < tftp->timo * 5; retry++)
    {
      if ((r = sendto(tftp->s, sendbuf, len, 0, (struct sockaddr *)&tftp->sa, sizeof(tftp->sa))) == -1)
	{
	  sprintf(tftp->buf, "sendto: %s", strerror(errno));
	  return -1;
	}
      if (r != len)
	{
	  sprintf(tftp->buf, "sendto: only %d of %d bytes transmitted", r, len);
	  return -1;
	}
      if (tftp->nr == -1)
	return 0;
      FD_ZERO(&rs);
      FD_SET(tftp->s, &rs);
      tv.tv_sec = 0;
      tv.tv_usec = 1000000 / 5;
      if ((r = select(tftp->s + 1, &rs, 0, 0, &tv)) == -1)
	{
	  sprintf(tftp->buf, "select: %s", strerror(errno));
	  return -1;
	}
      if (r == 0)
	continue;
      osl = sizeof(osa);
      if ((r = recvfrom(tftp->s, tftp->buf, sizeof(tftp->buf), 0, (struct sockaddr *)&osa, &osl)) == -1)
	continue;
      if (r < 4)
	continue;
      tftp->len = r - 4;
      r = tftp->buf[0] * 256 + tftp->buf[1];
      if (r == 3)
	{
	  nr = tftp->buf[2] * 256 + tftp->buf[3];
	  if (nr != ((tftp->nr + 1) & 65535))
	    continue;
	}
      if (tftp->nr == 0)
	tftp->sa.sin_port = osa.sin_port;
      else if (tftp->sa.sin_port != osa.sin_port)
	continue;
      if (r == 3)
        tftp->nr++;
      return r;
    }
  strcpy(tftp->buf, "timeout");
  return -1;
}

int
tftp_open(tftp, sa, filename, timo)
struct tftp *tftp;
struct sockaddr_in *sa;
char *filename;
int timo;
{
  struct sockaddr_in msa;
  int s;
  int r, nl;
  static int rid;

  nl = strlen(filename);
  if (nl > 500)
    {
      strcpy(tftp->buf, "file name too long");
      return -2;
    }
  tftp->sa = *sa;
  tftp->sa.sin_port = htons(69);
  if ((s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0)
    {
      sprintf(tftp->buf, "socket: %s", strerror(errno));
      return -1;
    }
  for (r = 0; r <= 8192; r++)
    {
      msa.sin_family = AF_INET;
      msa.sin_addr.s_addr = INADDR_ANY;
      msa.sin_port = r == 8192 ? 0 : htons(((r + rid) & 8191) + 30000);
      if (!bind(s, (struct sockaddr *)&msa, sizeof(msa)))
	break;
    }
  if (r == 8193)
    {
      sprintf(tftp->buf, "bind: %s", strerror(errno));
      close(s);
      return -1;
    }
  rid = r + 1;
  tftp->nr = 0;
  tftp->s = s;
  tftp->timo = timo;
  strcpy((char *)tftp->buf + 2, filename);
  strcpy((char *)tftp->buf + 2 + nl + 1, "octet");
  r = sendget(tftp, 1, 2 + nl + 1 + 6);
  if (r == 3)
    {
      if (tftp->len != 512)
        tftp->nr = -1;
      return 0;
    }
  tftp->nr = -1;
  tftp->s = -1;
  tftp->len = 0;
  close(s);
  if (r == 5)
    {
      char *s;

      if(!*filename) return 0;

      tftp->buf[sizeof tftp->buf - 1 - 10] = 0;
      s = strdup(tftp->buf + 4);
      sprintf(tftp->buf, "%d: %s", (tftp->buf[2] << 8) + tftp->buf[3], s);
      free(s);
      return -2;
    }
  if (r >= 0)
    sprintf(tftp->buf, "#%d", r);
  return -1;
}

void
tftp_close(tftp)
struct tftp *tftp;
{
  if (tftp->nr == -1)
    sendget(tftp, 4, 4);
  close(tftp->s);
  tftp->s = -1;
  tftp->nr = -1;
  tftp->len = 0;
}

int
tftp_read(tftp, bp, len)
struct tftp *tftp;
unsigned char *bp;
int len;
{
  int r;

  if (tftp->len == 0)
    {
      if (tftp->nr == -1)
        return 0;
      tftp->buf[2] = tftp->nr >> 8;
      tftp->buf[3] = tftp->nr;
      r = sendget(tftp, 4, 4);
      if (r != 3)
        {
          sprintf(tftp->buf, "#%d", r);
          return -1;
        }
      if (tftp->len != 512)
        tftp->nr = -1;
    }
  if (len > tftp->len)
    len = tftp->len;  
  if (len <= 0)
    return 0;  
  memcpy(bp, tftp->buf + 4, len);
  tftp->len -= len;
  if (tftp->len)   
    memmove(tftp->buf + 4, tftp->buf + 4 + len, tftp->len);
  return len;
}
 
char *
tftp_error(tftp)
struct tftp *tftp;
{
  return tftp->buf;
}
