#ifdef DIET

typedef unsigned char u_int8_t;
typedef unsigned short u_int16_t;
typedef unsigned u_int32_t;
typedef unsigned char u_char;
typedef unsigned short u_short;
typedef unsigned u_int;
typedef unsigned uint;
typedef unsigned long u_long;

typedef char *caddr_t;

#define RB_AUTOBOOT     0x01234567
#define RB_POWER_OFF    0x4321fedc

#define index strchr
#define rindex strrchr

#define SYS_pivot_root
int pivot_root(const char *new_root, const char *put_old);

#endif
