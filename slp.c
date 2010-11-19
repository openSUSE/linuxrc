#define _GNU_SOURCE	/* for FNM_CASEFOLD */

#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <stdlib.h>
#include <fnmatch.h>
#include <sys/socket.h>
#include <sys/utsname.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "global.h"
#include "file.h"
#include "dialog.h"
#include "util.h"

static int nextxid = 1;

static inline int slpgetw(unsigned char *p)
{
  return p[0] << 8 | p[1];
}


static int slprecv(int s, unsigned char *buf, int buflen, struct sockaddr_in *peer)
{
  fd_set fdset;
  int l2, l3;
  struct timeval tv;
  int pesal;

  FD_ZERO(&fdset);
  FD_SET(s, &fdset);
  tv.tv_sec = 0;
  tv.tv_usec = 500000;
  l2 = select(s + 1, &fdset, 0, 0, &tv);
  if (l2 < 0)
    {
      perror("select");
      close(s);
      return -1;
    }
  if (l2 == 0)
    return -2;
  pesal = sizeof(*peer);
  l2 = recvfrom(s, buf, 16, MSG_PEEK, (struct sockaddr *)peer, &pesal);
  if (l2 <= 0)
    return 0;
  if (l2 >= 16)
    l2 = buf[2] << 16 | buf[3] << 8 | buf[4];
  if (l2 > buflen)
    l2 = buflen;
  l2 = recvfrom(s, buf, l2, 0, (struct sockaddr *)peer, &pesal);
  if (l2 <= 16)
    return 0;
  l3 = buf[2] << 16 | buf[3] << 8 | buf[4];
  if (l2 != l3)
    return 0;
  return l2;
}

char *
slp_get_descr(struct sockaddr_in *peer, unsigned char *url, int urllen)
{
  int tries;
  int s, l, l2, l3;
  int xid, al;
  char *d;
  unsigned char sendbuf[8000];
  unsigned char recvbuf[8000];
  unsigned char *bp, *end;
  struct sockaddr_in dummy;

  xid = nextxid;
  if (++nextxid == 65536)
    nextxid = 1;
  memset(sendbuf, 0, 18);
  sendbuf[0] = 2;
  sendbuf[1] = 6;	/* AttrRqst */
  sendbuf[5] = 0;	/* flags: none */
  sendbuf[10] = xid >> 8;
  sendbuf[11] = xid & 255;
  sendbuf[13] = 2;
  sendbuf[14] = 'e';
  sendbuf[15] = 'n';
  sendbuf[18] = urllen >> 8;
  sendbuf[19] = urllen & 255;
  bp = sendbuf + 20;
  memcpy(bp, url, urllen);
  bp += urllen;
  memcpy(bp, "\000\007default", 7 + 2);
  bp += 7 + 2;
  memcpy(bp, "\000\013description", 11 + 2);
  bp += 11 + 2;
  *bp++ = 0;
  *bp++ = 0;	/* no spi */
  l = bp - sendbuf;
  sendbuf[3] = l >> 8;
  sendbuf[4] = l & 255;
  s = socket(PF_INET, SOCK_DGRAM, 0);
  if (s == -1)
    return 0;
  for (tries = 0; tries < 3; tries++)
    {
      l2 = sendto(s, sendbuf, l, 0, (struct sockaddr *)peer, sizeof(*peer));
      if (l2 != l)
	{
	  perror("sendto");
	  close(s);
	  return 0;
	}
      for (;;)
	{
	  l2 = slprecv(s, recvbuf, sizeof(recvbuf), &dummy);
	  if (l2 == -1)
	    {
	      close(s);
	      return 0;
	    }
	  if (l2 == -2)
	    break;
	  if (l2 == 0)
	    continue;
	  end = recvbuf + l2;
	  if (recvbuf[0] != 2)
	    continue;
	  if (recvbuf[1] != 7)	/* AttrRply */
	    continue;
	  if (slpgetw(recvbuf + 10) != xid)
	    continue;
	  close(s);
	  bp = recvbuf + 12;
	  l3 = slpgetw(bp);
	  bp += l3 + 2;
	  if (bp + 4 > end)
	    return 0;
	  if (slpgetw(bp))		/* error code */
	    return 0;
	  al = slpgetw(bp + 2);
	  bp += 4;
	  if (bp + al > end)
	    return 0;
	  if (al < 14 || strncasecmp(bp, "(description=", 13))
	    return 0;
	  d = malloc(al - 14 + 1);
	  if (d == 0)
	    return 0;
	  memcpy(d, bp + 13, al - 14);
	  d[al - 14] = 0;
	  return d;
	}
    }
  close(s);
  return 0;
}

int
slp_get_install()
{
  unsigned char sendbuf[8000];
  unsigned char recvbuf[8000];
  unsigned char *bp, *end;
  int xid, l, s, l2, l3, ec, comma, ulen, i, acnt;
  struct sockaddr_in mysa;
  struct sockaddr_in mcsa;
  struct sockaddr_in pesa;
  int tries;
  char urlbuf[256];
  char *iaddr, *d;
  char **urls = 0;
  char **descs = 0;
  char **ambg = 0;
  int urlcnt = 0;
  int win_old;
  file_t *f;
  unsigned char *origurl;
  int origurllen;
  struct utsname utsname;
  char *key = NULL;

  mysa.sin_family = AF_INET;
  mysa.sin_port = 0;
  mysa.sin_addr.s_addr = config.net.hostname.ip.s_addr;

  mcsa.sin_family = AF_INET;
  mcsa.sin_port = htons(427);
  mcsa.sin_addr.s_addr = htonl(0xeffffffd);

  xid = nextxid;
  if (++nextxid == 65536)
    nextxid = 1;

  memset(sendbuf, 0, 16);
  sendbuf[0] = 2;
  sendbuf[1] = 1;	/* SrvRqst */
  sendbuf[5] = 0x20;	/* flags: R */
  sendbuf[10] = xid >> 8;
  sendbuf[11] = xid & 255;
  sendbuf[13] = 2;
  sendbuf[14] = 'e';
  sendbuf[15] = 'n';

  bp = sendbuf + 16;
  *bp++ = 0;
  *bp++ = 0;	/* prlistlen */
  memcpy(bp, "\000\024service:install.suse", 20 + 2);
  bp += 20 + 2;
  memcpy(bp, "\000\007default", 7 + 2);
  bp += 7 + 2;

  if (uname(&utsname))
    {
      *bp++ = 0;
      *bp++ = 0;
    }
  else
    {
      int rell = strlen(utsname.release);
      int machl = strlen(utsname.machine);
      if (bp + rell + machl + 57 + 4 < sendbuf + sizeof(sendbuf))
	{
	  *bp++ = (57 + rell + machl) >> 8;
	  *bp++ = (57 + rell + machl) & 255;
	  sprintf(bp, "(&(|(!(machine=*))(machine=%s))(|(!(release=*))(release=%s)))", utsname.machine, utsname.release);
	  bp += 57 + rell + machl;
	}
    }
  *bp++ = 0;
  *bp++ = 0;
  l = bp - sendbuf;
  sendbuf[3] = l >> 8;
  sendbuf[4] = l & 255;
  s = socket(PF_INET, SOCK_DGRAM, 0);
  if (s == -1)
    {
      perror("socket");
      return 1;
    }
  if (fcntl(s, F_SETFL, O_NONBLOCK))
    {
      perror("fcntl O_NONBLOCK");
      return 1;
    }
  if (setsockopt(s, IPPROTO_IP, IP_MULTICAST_IF, (char *)&mysa.sin_addr, sizeof(mysa.sin_addr)))
    {
      perror("setsockopt IP_MULTICAST_IF");
      close(s);
      return 1;
    }
  i = 8;	/* like openslp */
  if (setsockopt(s, IPPROTO_IP, IP_MULTICAST_TTL, &i, sizeof(i)));
    {
      perror("setsockopt IP_MULTICAST_TTL");
    }
  for (tries = 0; tries < 3; tries++)
    {
      l2 = sendto(s, sendbuf, l, 0, (struct sockaddr *)&mcsa, sizeof(mcsa));
      if (l2 != l)
	{
	  perror("sendto");
	  close(s);
	  return 1;
	}
      for (;;)
	{
	  l2 = slprecv(s, recvbuf, sizeof(recvbuf), &pesa);
	  if (l2 == -1)
	    {
	      close(s);
	      return 1;
	    }
	  if (l2 == -2)
	    break;
	  if (l2 == 0)
	   continue;
	  end = recvbuf + l2;
	  if (recvbuf[0] != 2)
	    continue;
	  if (recvbuf[1] != 2)	/* SrvRply */
	    continue;
	  if (slpgetw(recvbuf + 10) != xid)
	    continue;

	  iaddr = inet_ntoa(pesa.sin_addr);
	  l3 = strlen(iaddr);
	  comma = sendbuf[16] != 0 || sendbuf[17] != 0;
	  if (l + l3 + comma <= sizeof(sendbuf))
	    {
	      bp = sendbuf + 18;
	      memmove(bp + l3 + comma, bp, l - 18);
	      memmove(bp, iaddr, l3);
	      if (comma)
		bp[l3] = ',';
	      l2 = slpgetw(sendbuf + 16) + l3 + comma;
	      sendbuf[16] = l2 >> 8;
	      sendbuf[17] = l2 & 255;
	      l += l3 + comma;
	      sendbuf[3] = l >> 8;
	      sendbuf[4] = l & 255;
	    }
	  bp = recvbuf + 12;
	  l3 = slpgetw(bp);
	  bp += l3 + 2;
	  if (bp + 4 > end)
	    continue;
	  if (slpgetw(bp))		/* error code */
	    continue;
	  ec = slpgetw(bp + 2);
	  bp += 4;
	  for (; ec > 0; ec--)
	    {
	      if (bp + 5 > end)
		break;
	      ulen =  slpgetw(bp + 3);
	      bp += 5;
	      if (bp + ulen + 1 > end)
		break;
	      origurl = bp;
	      origurllen = ulen;
	      if (ulen > 21 && !strncasecmp(bp, "service:install.suse:", 21))
		{
		  bp += 21;
		  ulen -= 21;
		}
	      /* 8: room for install= */
	      if (ulen > sizeof(urlbuf) - 1 - 8)
		{
		  bp += ulen;
		  if (*bp++)
		    break;
		  continue;
		}
	      memcpy(urlbuf, bp, ulen);
	      urlbuf[ulen] = 0;
	      bp += ulen;
	      for (i = 0; i < urlcnt; i++)
		if (!strcasecmp(urls[i], urlbuf))
		  break;
	      if (i == urlcnt)
		{
		  if ((urlcnt & 15) == 0)
		    {
		      if (urls)
			urls = realloc(urls, sizeof(char **) * (urlcnt + 16));
		      else
			urls = malloc(sizeof(char **) * (urlcnt + 16));
		      if (descs)
			descs = realloc(descs, sizeof(char **) * (urlcnt + 16));
		      else
			descs = malloc(sizeof(char **) * (urlcnt + 16));
		    }
		  if (!urls || !descs)
		    {
		      close(s);
		      return 1;
		    }
		  d = slp_get_descr(&pesa, origurl, origurllen);
		  if (!d)
		    d = strdup(urlbuf);
		  for (i = 0; i < urlcnt; i++)
		    if (strcmp(d, descs[i]) < 0 || (strcmp(d, descs[i]) == 0 && strcmp(urlbuf, urls[i]) < 0))
		      break;
		  if (i < urlcnt)
		    {
		      memmove(descs + i + 1, descs + i, sizeof(*descs) * (urlcnt - i));
		      memmove(urls + i + 1, urls + i, sizeof(*urls) * (urlcnt - i));
		    }
		  descs[i] = d;
		  urls[i] = strdup(urlbuf);;
		  urlcnt++;
		}
	      if (*bp++)
		break;
	    }
	}
    }
  close(s);
  if (urlcnt == 0)
    {
      fprintf(stderr, "SLP: no installation source found\n");
      return 1;
    }
  ambg = malloc((urlcnt + 1) * sizeof(char **));
  if (!ambg)
    return 1;
  win_old = config.win;
  set_activate_language(config.language);
  if(!config.win) util_disp_init();
  *urlbuf = 0;
  for (;;)
    {
      if(config.slp.key) strprintf(&key, "*%s*", config.slp.key);

      while(1) {
        for(i = acnt = 0; i < urlcnt; i++) {
          if(key && fnmatch(key, descs[i], FNM_CASEFOLD)) continue;
          if(acnt == 0 || strcmp(descs[i], ambg[acnt - 1])) {
            ambg[acnt++] = descs[i];
          }
        }
        ambg[acnt] = 0;
        if(acnt || !key) break;
        str_copy(&key, NULL);
      }

      i = acnt == 1 && !config.manual ? 1 : dia_list(txt_get(TXT_SLP_SOURCE), 60, NULL, ambg, 0, align_center);
      if (i <= 0 || i > acnt)
	break;
      d = ambg[i - 1];

      str_copy(&key, config.slp.proto);

      while(1) {
        for(i = acnt = 0; i < urlcnt; i++) {
          if(key && strncasecmp(urls[i], key, strlen(key))) continue;
          if(!strcmp(descs[i], d)) {
            ambg[acnt++] = urls[i];
          }
        }
        ambg[acnt] = 0;
        if(acnt || !key) break;
        str_copy(&key, NULL);
      }

      if(acnt == 0) break;

      i = acnt == 1 ? 1 : dia_list(txt_get(TXT_SLP_SOURCE), 60, NULL, ambg, 0, align_center);

      if (i > 0 && i - 1 < acnt)
	{
	  sprintf(urlbuf, "install=%s", ambg[i - 1]);
	  break;
	}
    }
  if(config.win && !win_old) util_disp_done();
  for (i = 0; i < urlcnt; i++)
    {
      free(descs[i]);
      free(urls[i]);
    }
  free(descs);
  free(urls);
  free(ambg);
  if (!*urlbuf)
    return 1;
  f = file_parse_buffer(urlbuf, kf_cfg + kf_cmd);
  file_do_info(f);
  file_free_file(f);
  return 0;
}

