struct tftp {
  struct sockaddr_in sa;
  int s;
  int nr;
  unsigned char buf[4 + 512];
  int len;
  int timo;
};

extern int tftp_open(struct tftp *, struct sockaddr_in *, char *, int);
extern int tftp_read(struct tftp *, unsigned char **);
extern void tftp_close(struct tftp *);
