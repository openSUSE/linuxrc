/* A Bison parser, made from yacc_config.y, by GNU bison 1.75.  */

/* Skeleton parser for Yacc-like parsing with Bison,
   Copyright (C) 1984, 1989, 1990, 2000, 2001, 2002 Free Software Foundation, Inc.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place - Suite 330,
   Boston, MA 02111-1307, USA.  */

/* As a special exception, when this file is copied by Bison into a
   Bison output file, you may use that output file without restriction.
   This special exception was added by the Free Software Foundation
   in version 1.24 of Bison.  */

#ifndef BISON_Y_TAB_H
# define BISON_Y_TAB_H

/* Tokens.  */
#ifndef YYTOKENTYPE
# define YYTOKENTYPE
   /* Put the tokens into the symbol table, so that GDB and other debuggers
      know about them.  */
   enum yytokentype {
     DEVICE = 258,
     CARD = 259,
     ANONYMOUS = 260,
     TUPLE = 261,
     MANFID = 262,
     VERSION = 263,
     FUNCTION = 264,
     PCI = 265,
     BIND = 266,
     CIS = 267,
     TO = 268,
     NEEDS_MTD = 269,
     MODULE = 270,
     OPTS = 271,
     CLASS = 272,
     REGION = 273,
     JEDEC = 274,
     DTYPE = 275,
     DEFAULT = 276,
     MTD = 277,
     INCLUDE = 278,
     EXCLUDE = 279,
     RESERVE = 280,
     IRQ_NO = 281,
     PORT = 282,
     MEMORY = 283,
     STRING = 284,
     NUMBER = 285,
     SOURCE = 286
   };
#endif
#define DEVICE 258
#define CARD 259
#define ANONYMOUS 260
#define TUPLE 261
#define MANFID 262
#define VERSION 263
#define FUNCTION 264
#define PCI 265
#define BIND 266
#define CIS 267
#define TO 268
#define NEEDS_MTD 269
#define MODULE 270
#define OPTS 271
#define CLASS 272
#define REGION 273
#define JEDEC 274
#define DTYPE 275
#define DEFAULT 276
#define MTD 277
#define INCLUDE 278
#define EXCLUDE 279
#define RESERVE 280
#define IRQ_NO 281
#define PORT 282
#define MEMORY 283
#define STRING 284
#define NUMBER 285
#define SOURCE 286




#ifndef YYSTYPE
#line 65 "yacc_config.y"
typedef union {
    char *str;
    u_long num;
    struct device_info_t *device;
    struct card_info_t *card;
    struct mtd_ident_t *mtd;
    struct adjust_list_t *adjust;
} yystype;
/* Line 1281 of /usr/share/bison/yacc.c.  */
#line 111 "y.tab.h"
# define YYSTYPE yystype
#endif

extern YYSTYPE yylval;


#endif /* not BISON_Y_TAB_H */

