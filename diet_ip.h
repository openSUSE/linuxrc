#ifdef DIET

/*
 * Structure of an internet header, naked of options.
 */
struct ip
  {
#if __BYTE_ORDER == __LITTLE_ENDIAN
    unsigned int ip_hl:4;               /* header length */
    unsigned int ip_v:4;                /* version */
#else
    unsigned int ip_v:4;                /* version */
    unsigned int ip_hl:4;               /* header length */
#endif
    u_int8_t ip_tos;                    /* type of service */
    u_short ip_len;                     /* total length */
    u_short ip_id;                      /* identification */
    u_short ip_off;                     /* fragment offset field */
#define IP_RF 0x8000                    /* reserved fragment flag */
#define IP_DF 0x4000                    /* dont fragment flag */
#define IP_MF 0x2000                    /* more fragments flag */
#define IP_OFFMASK 0x1fff               /* mask for fragmenting bits */
    u_int8_t ip_ttl;                    /* time to live */
    u_int8_t ip_p;                      /* protocol */
    u_short ip_csum;                    /* checksum */
    struct in_addr ip_src, ip_dst;      /* source and dest address */
  };

/*
 * Time stamp option structure.
 */
struct ip_timestamp
  {
    u_int8_t ipt_code;                  /* IPOPT_TS */
    u_int8_t ipt_len;                   /* size of structure (variable) */
    u_int8_t ipt_ptr;                   /* index of current entry */
#if __BYTE_ORDER == __LITTLE_ENDIAN
    unsigned int ipt_flg:4;             /* flags, see below */
    unsigned int ipt_oflw:4;            /* overflow counter */
#else
    unsigned int ipt_oflw:4;            /* overflow counter */
    unsigned int ipt_flg:4;             /* flags, see below */
#endif
    u_int32_t data[9];
  };

#endif
