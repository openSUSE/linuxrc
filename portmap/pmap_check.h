/* @(#) pmap_check.h 1.4 96/07/06 23:06:22 */

extern int from_local();
extern void check_startup();
extern int check_default();
extern int check_setunset();
extern int check_privileged_port();
extern int check_callit();
extern int verboselog;
extern int allow_severity;
extern int deny_severity;

#ifdef LOOPBACK_SETUNSET
#define CHECK_SETUNSET	check_setunset
#else
#define CHECK_SETUNSET(xprt,ludp,ltcp,proc,prog,port) \
	check_setunset(svc_getcaller(xprt),proc,prog,port)
#endif
