#define DEVICE 257
#define CARD 258
#define ANONYMOUS 259
#define TUPLE 260
#define MANFID 261
#define VERSION 262
#define FUNCTION 263
#define BIND 264
#define CIS 265
#define TO 266
#define NEEDS_MTD 267
#define MODULE 268
#define OPTS 269
#define CLASS 270
#define REGION 271
#define JEDEC 272
#define DTYPE 273
#define DEFAULT 274
#define MTD 275
#define INCLUDE 276
#define EXCLUDE 277
#define RESERVE 278
#define IRQ_NO 279
#define PORT 280
#define MEMORY 281
#define STRING 282
#define NUMBER 283
typedef union {
    char *str;
    u_long num;
    struct device_info_t *device;
    struct card_info_t *card;
    struct mtd_ident_t *mtd;
    struct adjust_list_t *adjust;
} YYSTYPE;
extern YYSTYPE yylval;
