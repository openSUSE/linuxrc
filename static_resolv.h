#if 0
#if !defined(DIET) && !defined(UCLIBC)

struct hostent *res_gethostbyaddr(const char *addr, int len, int type);
struct hostent *res_gethostbyname(const char *name);

#define gethostbyaddr res_gethostbyaddr
#define gethostbyname res_gethostbyname

#endif
#endif
