#ifndef lint
static char yysccsid[] = "@(#)yaccpar	1.9 (Berkeley) 02/21/93";
#endif
#define YYBYACC 1
#define YYMAJOR 1
#define YYMINOR 9
#define yyclearin (yychar=(-1))
#define yyerrok (yyerrflag=0)
#define YYRECOVERING (yyerrflag!=0)
#define YYPREFIX "yy"
#line 2 "yacc_config.y"
/*
 * yacc_config.y 1.48 1999/10/25 20:00:14
 *
 * The contents of this file are subject to the Mozilla Public License
 * Version 1.1 (the "License"); you may not use this file except in
 * compliance with the License. You may obtain a copy of the License
 * at http://www.mozilla.org/MPL/
 *
 * Software distributed under the License is distributed on an "AS IS"
 * basis, WITHOUT WARRANTY OF ANY KIND, either express or implied. See
 * the License for the specific language governing rights and
 * limitations under the License. 
 *
 * The initial developer of the original code is David A. Hinds
 * <dhinds@pcmcia.sourceforge.org>.  Portions created by David A. Hinds
 * are Copyright (C) 1999 David A. Hinds.  All Rights Reserved.
 *
 * Alternatively, the contents of this file may be used under the
 * terms of the GNU Public License version 2 (the "GPL"), in which
 * case the provisions of the GPL are applicable instead of the
 * above.  If you wish to allow the use of your version of this file
 * only under the terms of the GPL and not to allow others to use
 * your version of this file under the MPL, indicate your decision by
 * deleting the provisions above and replace them with the notice and
 * other provisions required by the GPL.  If you do not delete the
 * provisions above, a recipient may use your version of this file
 * under either the MPL or the GPL.
 */
    
#include <stdlib.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <syslog.h>
#include <sys/types.h>

#include <pcmcia/cs_types.h>
#include <pcmcia/cs.h>
#include <pcmcia/cistpl.h>
#include <pcmcia/ds.h>
    
#include "cardmgr.h"

/* If bison: generate nicer error messages */ 
#define YYERROR_VERBOSE 1
 
/* from lex_config, for nice error messages */
extern char *current_file;
extern int current_lineno;

void yyerror(char *msg, ...);

static int add_binding(card_info_t *card, char *name, int fn);
static int add_module(device_info_t *card, char *name);

#line 65 "yacc_config.y"
typedef union {
    char *str;
    u_long num;
    struct device_info_t *device;
    struct card_info_t *card;
    struct mtd_ident_t *mtd;
    struct adjust_list_t *adjust;
} YYSTYPE;
#line 77 "y.tab.c"
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
#define YYERRCODE 256
short yylhs[] = {                                        -1,
    0,    0,    0,    0,    0,    0,    0,    0,    1,    1,
    1,    1,    2,    2,    2,    3,    3,    3,    3,    7,
    7,    7,    7,    7,    7,    7,    7,    8,    9,   10,
   11,   11,   12,   14,   13,   13,   13,   13,    4,   20,
    5,    5,    5,    6,   15,   15,   15,   15,   17,   16,
   18,   19,   19,   21,
};
short yylen[] = {                                         2,
    0,    2,    2,    2,    2,    2,    2,    2,    2,    2,
    2,    3,    2,    4,    4,    2,    1,    1,    1,    2,
    1,    1,    1,    1,    1,    1,    1,    2,    7,    5,
    3,    3,    3,    3,    3,    5,    3,    5,    2,    4,
    3,    3,    3,    3,    2,    1,    1,    1,    3,    4,
    2,    3,    3,    4,
};
short yydefred[] = {                                      1,
    0,    8,    0,    0,    0,    0,    0,    0,    0,    0,
    0,    0,   17,    0,   19,    0,   21,   22,   23,    0,
   25,    0,   27,    0,   47,   46,   48,    0,    6,    7,
   16,   20,    0,   45,    0,    0,    0,    0,    9,   10,
   11,    0,   39,    0,    0,    0,    0,   28,    0,    0,
    0,    0,    0,    0,    0,    0,    0,    0,   51,    0,
    0,    0,    0,   13,    0,    0,   12,   41,   44,   42,
   43,    0,    0,   31,   33,    0,   34,   32,    0,    0,
   49,   52,   53,   40,   54,    0,    0,    0,    0,    0,
    0,   50,   14,   15,    0,   30,   36,   38,    0,   29,
};
short yydgoto[] = {                                       1,
   11,   39,   12,   13,   14,   15,   16,   17,   18,   19,
   20,   21,   22,   23,   24,   25,   26,   27,   28,   29,
   30,
};
short yysindex[] = {                                      0,
 -250,    0, -278, -277, -273, -265, -259, -242, -242, -242,
  -11, -248,    0,  -44,    0, -249,    0,    0,    0,  -10,
    0,   -8,    0, -243,    0,    0,    0, -229,    0,    0,
    0,    0, -228,    0, -227, -240, -239, -237,    0,    0,
    0, -242,    0, -234, -233, -232, -231,    0, -230, -226,
 -224, -223, -221, -220, -219, -218, -217, -216,    0, -213,
 -211, -210, -209,    0,    7,    9,    0,    0,    0,    0,
    0,   11,   12,    0,    0, -207,    0,    0, -201, -208,
    0,    0,    0,    0,    0, -206, -205, -204, -203, -202,
 -200,    0,    0,    0,   30,    0,    0,    0, -198,    0,
};
short yyrindex[] = {                                      0,
    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,
  102,  142,    0,   86,    0,  118,    0,    0,    0,   47,
    0,   70,    0,    0,    0,    0,    0,  134,    0,    0,
    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,
    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,
    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,
    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,
    0,    0,    0,    0,    0,    1,    0,    0,   24,    0,
    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,
    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,
};
short yygindex[] = {                                      0,
    0,   -7,    0,    0,    0,    0,    0,    0,    0,    0,
    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,
    0,
};
#define YYTABLESIZE 420
short yytable[] = {                                      47,
   35,   40,   41,   31,   32,    2,    3,    4,   33,   48,
   49,   50,   51,   52,   53,   54,   34,    5,   43,   44,
    6,   45,   35,   37,    7,    8,    9,   10,   57,   58,
   59,   60,   42,   55,   67,   56,   36,   37,   38,   61,
   62,   63,   64,   65,   35,   66,   24,   68,   69,   70,
   71,   86,   72,   87,   88,   89,   73,   74,   90,   75,
   76,   77,   78,   79,   91,   80,   81,   37,   82,   26,
   83,   84,   85,   99,   92,    0,   93,   94,   95,   96,
   97,    0,   98,  100,    0,   18,    0,    0,    0,    0,
    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,
    0,    2,    0,    0,    0,    0,    0,    0,    0,    0,
    0,    0,    0,    0,    0,    0,    0,    5,    0,    0,
    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,
    0,    0,    0,    4,    0,    0,    0,    0,    0,    0,
    0,    3,    0,    0,    0,    0,    0,    0,    0,    0,
    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,
    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,
    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,
    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,
    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,
    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,
    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,
    0,    0,    0,    0,   46,    0,    0,    0,    0,    0,
    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,
    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,
    0,    0,    0,    0,    0,    0,   35,   35,   35,   35,
   35,   35,   35,   35,   35,   35,    0,    0,   35,    0,
    0,   35,    0,    0,    0,   35,   35,   35,   35,   37,
   37,   37,   37,   37,   37,   37,   37,   37,   37,    0,
    0,   37,    0,    0,   37,    0,    0,    0,   37,   37,
   37,   37,   24,   24,   24,   24,   24,   24,   24,   24,
   24,   24,    0,    0,   24,    0,    0,   24,    0,    0,
    0,   24,   24,   24,   24,   26,   26,   26,   26,   26,
   26,   26,   26,   26,   26,    0,    0,   26,    0,    0,
   26,   18,   18,   18,   26,   26,   26,   26,    0,    0,
    0,    0,   18,   18,    0,   18,   18,    2,    2,    2,
   18,   18,   18,   18,    0,    0,    0,    0,    0,    2,
    0,    0,    2,    5,    5,    5,    2,    2,    2,    2,
    0,    0,    0,    0,    0,    5,    0,    0,    5,    4,
    4,    4,    5,    5,    5,    5,    0,    3,    3,    3,
    0,    4,    0,    0,    4,    0,    0,    0,    4,    4,
    4,    4,    3,    0,    0,    0,    3,    3,    3,    3,
};
short yycheck[] = {                                      44,
    0,    9,   10,  282,  282,  256,  257,  258,  282,  259,
  260,  261,  262,  263,  264,  265,  282,  268,  267,  268,
  271,  270,  282,    0,  275,  276,  277,  278,  272,  273,
  274,  275,   44,   44,   42,   44,  279,  280,  281,  269,
  269,  269,  283,  283,   44,  283,    0,  282,  282,  282,
  282,   45,  283,   45,   44,   44,  283,  282,  266,  283,
  282,  282,  282,  282,  266,  283,  283,   44,  282,    0,
  282,  282,  282,   44,  283,   -1,  283,  283,  283,  283,
  283,   -1,  283,  282,   -1,    0,   -1,   -1,   -1,   -1,
   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
   -1,    0,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
   -1,   -1,   -1,   -1,   -1,   -1,   -1,    0,   -1,   -1,
   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
   -1,   -1,   -1,    0,   -1,   -1,   -1,   -1,   -1,   -1,
   -1,    0,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
   -1,   -1,   -1,   -1,  269,   -1,   -1,   -1,   -1,   -1,
   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
   -1,   -1,   -1,   -1,   -1,   -1,  256,  257,  258,  259,
  260,  261,  262,  263,  264,  265,   -1,   -1,  268,   -1,
   -1,  271,   -1,   -1,   -1,  275,  276,  277,  278,  256,
  257,  258,  259,  260,  261,  262,  263,  264,  265,   -1,
   -1,  268,   -1,   -1,  271,   -1,   -1,   -1,  275,  276,
  277,  278,  256,  257,  258,  259,  260,  261,  262,  263,
  264,  265,   -1,   -1,  268,   -1,   -1,  271,   -1,   -1,
   -1,  275,  276,  277,  278,  256,  257,  258,  259,  260,
  261,  262,  263,  264,  265,   -1,   -1,  268,   -1,   -1,
  271,  256,  257,  258,  275,  276,  277,  278,   -1,   -1,
   -1,   -1,  267,  268,   -1,  270,  271,  256,  257,  258,
  275,  276,  277,  278,   -1,   -1,   -1,   -1,   -1,  268,
   -1,   -1,  271,  256,  257,  258,  275,  276,  277,  278,
   -1,   -1,   -1,   -1,   -1,  268,   -1,   -1,  271,  256,
  257,  258,  275,  276,  277,  278,   -1,  256,  257,  258,
   -1,  268,   -1,   -1,  271,   -1,   -1,   -1,  275,  276,
  277,  278,  271,   -1,   -1,   -1,  275,  276,  277,  278,
};
#define YYFINAL 1
#ifndef YYDEBUG
#define YYDEBUG 0
#endif
#define YYMAXTOKEN 283
#if YYDEBUG
char *yyname[] = {
"end-of-file",0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,"','","'-'",0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,"DEVICE","CARD",
"ANONYMOUS","TUPLE","MANFID","VERSION","FUNCTION","BIND","CIS","TO","NEEDS_MTD",
"MODULE","OPTS","CLASS","REGION","JEDEC","DTYPE","DEFAULT","MTD","INCLUDE",
"EXCLUDE","RESERVE","IRQ_NO","PORT","MEMORY","STRING","NUMBER",
};
char *yyrule[] = {
"$accept : list",
"list :",
"list : list adjust",
"list : list device",
"list : list mtd",
"list : list card",
"list : list opts",
"list : list mtd_opts",
"list : list error",
"adjust : INCLUDE resource",
"adjust : EXCLUDE resource",
"adjust : RESERVE resource",
"adjust : adjust ',' resource",
"resource : IRQ_NO NUMBER",
"resource : PORT NUMBER '-' NUMBER",
"resource : MEMORY NUMBER '-' NUMBER",
"device : DEVICE STRING",
"device : needs_mtd",
"device : module",
"device : class",
"card : CARD STRING",
"card : anonymous",
"card : tuple",
"card : manfid",
"card : version",
"card : function",
"card : bind",
"card : cis",
"anonymous : card ANONYMOUS",
"tuple : card TUPLE NUMBER ',' NUMBER ',' STRING",
"manfid : card MANFID NUMBER ',' NUMBER",
"version : card VERSION STRING",
"version : version ',' STRING",
"function : card FUNCTION NUMBER",
"cis : card CIS STRING",
"bind : card BIND STRING",
"bind : card BIND STRING TO NUMBER",
"bind : bind ',' STRING",
"bind : bind ',' STRING TO NUMBER",
"needs_mtd : device NEEDS_MTD",
"opts : MODULE STRING OPTS STRING",
"module : device MODULE STRING",
"module : module OPTS STRING",
"module : module ',' STRING",
"class : device CLASS STRING",
"region : REGION STRING",
"region : dtype",
"region : jedec",
"region : default",
"dtype : region DTYPE NUMBER",
"jedec : region JEDEC NUMBER NUMBER",
"default : region DEFAULT",
"mtd : region MTD STRING",
"mtd : mtd OPTS STRING",
"mtd_opts : MTD STRING OPTS STRING",
};
#endif
#ifdef YYSTACKSIZE
#undef YYMAXDEPTH
#define YYMAXDEPTH YYSTACKSIZE
#else
#ifdef YYMAXDEPTH
#define YYSTACKSIZE YYMAXDEPTH
#else
#define YYSTACKSIZE 500
#define YYMAXDEPTH 500
#endif
#endif
int yydebug;
int yynerrs;
int yyerrflag;
int yychar;
short *yyssp;
YYSTYPE *yyvsp;
YYSTYPE yyval;
YYSTYPE yylval;
short yyss[YYSTACKSIZE];
YYSTYPE yyvs[YYSTACKSIZE];
#define yystacksize YYSTACKSIZE
#line 456 "yacc_config.y"
void yyerror(char *msg, ...)
{
     va_list ap;
     char str[256];

     va_start(ap, msg);
     sprintf(str, "config error, file '%s' line %d: ",
	     current_file, current_lineno);
     vsprintf(str+strlen(str), msg, ap);
#if YYDEBUG
     fprintf(stderr, "%s\n", str);
#else
     syslog(LOG_INFO, "%s", str);
#endif
     va_end(ap);
}

static int add_binding(card_info_t *card, char *name, int fn)
{
    device_info_t *dev = root_device;
    if (card->bindings == MAX_BINDINGS) {
	yyerror("too many bindings\n");
	return -1;
    }
    for (; dev; dev = dev->next)
	if (strcmp((char *)dev->dev_info, name) == 0) break;
    if (dev == NULL) {
	yyerror("unknown device: %s", name);
	return -1;
    }
    card->device[card->bindings] = dev;
    card->dev_fn[card->bindings] = fn;
    card->bindings++;
    free(name);
    return 0;
}

static int add_module(device_info_t *dev, char *name)
{
    if (dev->modules == MAX_MODULES) {
	yyerror("too many modules");
	return -1;
    }
    dev->module[dev->modules] = name;
    dev->opts[dev->modules] = NULL;
    dev->modules++;
    return 0;
}

#if YYDEBUG
adjust_list_t *root_adjust = NULL;
device_info_t *root_device = NULL;
card_info_t *root_card = NULL, *blank_card = NULL, *root_func = NULL;
mtd_ident_t *root_mtd = NULL, *default_mtd = NULL;

void main(int argc, char *argv[])
{
    yydebug = 1;
    if (argc > 1)
	parse_configfile(argv[1]);
}
#endif
#line 418 "y.tab.c"
#define YYABORT goto yyabort
#define YYREJECT goto yyabort
#define YYACCEPT goto yyaccept
#define YYERROR goto yyerrlab
int
yyparse()
{
    register int yym, yyn, yystate;
#if YYDEBUG
    register char *yys;
    extern char *getenv();

    if (yys = getenv("YYDEBUG"))
    {
        yyn = *yys;
        if (yyn >= '0' && yyn <= '9')
            yydebug = yyn - '0';
    }
#endif

    yynerrs = 0;
    yyerrflag = 0;
    yychar = (-1);

    yyssp = yyss;
    yyvsp = yyvs;
    *yyssp = yystate = 0;

yyloop:
    if (yyn = yydefred[yystate]) goto yyreduce;
    if (yychar < 0)
    {
        if ((yychar = yylex()) < 0) yychar = 0;
#if YYDEBUG
        if (yydebug)
        {
            yys = 0;
            if (yychar <= YYMAXTOKEN) yys = yyname[yychar];
            if (!yys) yys = "illegal-symbol";
            printf("%sdebug: state %d, reading %d (%s)\n",
                    YYPREFIX, yystate, yychar, yys);
        }
#endif
    }
    if ((yyn = yysindex[yystate]) && (yyn += yychar) >= 0 &&
            yyn <= YYTABLESIZE && yycheck[yyn] == yychar)
    {
#if YYDEBUG
        if (yydebug)
            printf("%sdebug: state %d, shifting to state %d\n",
                    YYPREFIX, yystate, yytable[yyn]);
#endif
        if (yyssp >= yyss + yystacksize - 1)
        {
            goto yyoverflow;
        }
        *++yyssp = yystate = yytable[yyn];
        *++yyvsp = yylval;
        yychar = (-1);
        if (yyerrflag > 0)  --yyerrflag;
        goto yyloop;
    }
    if ((yyn = yyrindex[yystate]) && (yyn += yychar) >= 0 &&
            yyn <= YYTABLESIZE && yycheck[yyn] == yychar)
    {
        yyn = yytable[yyn];
        goto yyreduce;
    }
    if (yyerrflag) goto yyinrecovery;
#ifdef lint
    goto yynewerror;
#endif
yynewerror:
    yyerror("syntax error");
#ifdef lint
    goto yyerrlab;
#endif
yyerrlab:
    ++yynerrs;
yyinrecovery:
    if (yyerrflag < 3)
    {
        yyerrflag = 3;
        for (;;)
        {
            if ((yyn = yysindex[*yyssp]) && (yyn += YYERRCODE) >= 0 &&
                    yyn <= YYTABLESIZE && yycheck[yyn] == YYERRCODE)
            {
#if YYDEBUG
                if (yydebug)
                    printf("%sdebug: state %d, error recovery shifting\
 to state %d\n", YYPREFIX, *yyssp, yytable[yyn]);
#endif
                if (yyssp >= yyss + yystacksize - 1)
                {
                    goto yyoverflow;
                }
                *++yyssp = yystate = yytable[yyn];
                *++yyvsp = yylval;
                goto yyloop;
            }
            else
            {
#if YYDEBUG
                if (yydebug)
                    printf("%sdebug: error recovery discarding state %d\n",
                            YYPREFIX, *yyssp);
#endif
                if (yyssp <= yyss) goto yyabort;
                --yyssp;
                --yyvsp;
            }
        }
    }
    else
    {
        if (yychar == 0) goto yyabort;
#if YYDEBUG
        if (yydebug)
        {
            yys = 0;
            if (yychar <= YYMAXTOKEN) yys = yyname[yychar];
            if (!yys) yys = "illegal-symbol";
            printf("%sdebug: state %d, error recovery discards token %d (%s)\n",
                    YYPREFIX, yystate, yychar, yys);
        }
#endif
        yychar = (-1);
        goto yyloop;
    }
yyreduce:
#if YYDEBUG
    if (yydebug)
        printf("%sdebug: state %d, reducing by rule %d (%s)\n",
                YYPREFIX, yystate, yyn, yyrule[yyn]);
#endif
    yym = yylen[yyn];
    yyval = yyvsp[1-yym];
    switch (yyn)
    {
case 2:
#line 84 "yacc_config.y"
{
		    adjust_list_t **tail = &root_adjust;
		    while (*tail != NULL) tail = &(*tail)->next;
		    *tail = yyvsp[0].adjust;
		}
break;
case 3:
#line 90 "yacc_config.y"
{
		    yyvsp[0].device->next = root_device;
		    root_device = yyvsp[0].device;
		}
break;
case 4:
#line 95 "yacc_config.y"
{
		    if (yyvsp[0].mtd->mtd_type == 0) {
			yyerror("no ID method for this card");
			YYERROR;
		    }
		    if (yyvsp[0].mtd->module == NULL) {
			yyerror("no MTD module specified");
			YYERROR;
		    }
		    yyvsp[0].mtd->next = root_mtd;
		    root_mtd = yyvsp[0].mtd;
		}
break;
case 5:
#line 108 "yacc_config.y"
{
		    if (yyvsp[0].card->ident_type == 0) {
			yyerror("no ID method for this card");
			YYERROR;
		    }
		    if (yyvsp[0].card->bindings == 0) {
			yyerror("no function bindings");
			YYERROR;
		    }
		    if (yyvsp[0].card->ident_type == FUNC_IDENT) {
			yyvsp[0].card->next = root_func;
			root_func = yyvsp[0].card;
		    } else {
			yyvsp[0].card->next = root_card;
			root_card = yyvsp[0].card;
		    }
		}
break;
case 9:
#line 131 "yacc_config.y"
{
		    yyvsp[0].adjust->adj.Action = ADD_MANAGED_RESOURCE;
		    yyval.adjust = yyvsp[0].adjust;
		}
break;
case 10:
#line 136 "yacc_config.y"
{
		    yyvsp[0].adjust->adj.Action = REMOVE_MANAGED_RESOURCE;
		    yyval.adjust = yyvsp[0].adjust;
		}
break;
case 11:
#line 141 "yacc_config.y"
{
		    yyvsp[0].adjust->adj.Action = ADD_MANAGED_RESOURCE;
		    yyvsp[0].adjust->adj.Attributes |= RES_RESERVED;
		    yyval.adjust = yyvsp[0].adjust;
		}
break;
case 12:
#line 147 "yacc_config.y"
{
		    yyvsp[0].adjust->adj.Action = yyvsp[-2].adjust->adj.Action;
		    yyvsp[0].adjust->adj.Attributes = yyvsp[-2].adjust->adj.Attributes;
		    yyvsp[0].adjust->next = yyvsp[-2].adjust;
		    yyval.adjust = yyvsp[0].adjust;
		}
break;
case 13:
#line 156 "yacc_config.y"
{
		    yyval.adjust = calloc(sizeof(adjust_list_t), 1);
		    yyval.adjust->adj.Resource = RES_IRQ;
		    yyval.adjust->adj.resource.irq.IRQ = yyvsp[0].num;
		}
break;
case 14:
#line 162 "yacc_config.y"
{
		    if ((yyvsp[0].num < yyvsp[-2].num) || (yyvsp[0].num > 0xffff)) {
			yyerror("invalid port range");
			YYERROR;
		    }
		    yyval.adjust = calloc(sizeof(adjust_list_t), 1);
		    yyval.adjust->adj.Resource = RES_IO_RANGE;
		    yyval.adjust->adj.resource.io.BasePort = yyvsp[-2].num;
		    yyval.adjust->adj.resource.io.NumPorts = yyvsp[0].num - yyvsp[-2].num + 1;
		}
break;
case 15:
#line 173 "yacc_config.y"
{
		    if (yyvsp[0].num < yyvsp[-2].num) {
			yyerror("invalid address range");
			YYERROR;
		    }
		    yyval.adjust = calloc(sizeof(adjust_list_t), 1);
		    yyval.adjust->adj.Resource = RES_MEMORY_RANGE;
		    yyval.adjust->adj.resource.memory.Base = yyvsp[-2].num;
		    yyval.adjust->adj.resource.memory.Size = yyvsp[0].num - yyvsp[-2].num + 1;
		}
break;
case 16:
#line 186 "yacc_config.y"
{
		    yyval.device = calloc(sizeof(device_info_t), 1);
		    yyval.device->refs = 1;
		    strcpy(yyval.device->dev_info, yyvsp[0].str);
		    free(yyvsp[0].str);
		}
break;
case 20:
#line 198 "yacc_config.y"
{
		    yyval.card = calloc(sizeof(card_info_t), 1);
		    yyval.card->refs = 1;
		    yyval.card->name = yyvsp[0].str;
		}
break;
case 28:
#line 213 "yacc_config.y"
{
		    if (yyvsp[-1].card->ident_type != 0) {
			yyerror("ID method already defined");
			YYERROR;
		    }
		    if (blank_card) {
			yyerror("Anonymous card already defined");
			YYERROR;
		    }
		    yyvsp[-1].card->ident_type = BLANK_IDENT;
		    blank_card = yyvsp[-1].card;
		}
break;
case 29:
#line 228 "yacc_config.y"
{
		    if (yyvsp[-6].card->ident_type != 0) {
			yyerror("ID method already defined");
			YYERROR;
		    }
		    yyvsp[-6].card->ident_type = TUPLE_IDENT;
		    yyvsp[-6].card->id.tuple.code = yyvsp[-4].num;
		    yyvsp[-6].card->id.tuple.ofs = yyvsp[-2].num;
		    yyvsp[-6].card->id.tuple.info = yyvsp[0].str;
		}
break;
case 30:
#line 241 "yacc_config.y"
{
		    if (yyvsp[-4].card->ident_type != 0) {
			yyerror("ID method already defined");
			YYERROR;
		    }
		    yyvsp[-4].card->ident_type = MANFID_IDENT;
		    yyvsp[-4].card->id.manfid.manf = yyvsp[-2].num;
		    yyvsp[-4].card->id.manfid.card = yyvsp[0].num;
		}
break;
case 31:
#line 252 "yacc_config.y"
{
		    if (yyvsp[-2].card->ident_type != 0) {
			yyerror("ID method already defined\n");
			YYERROR;
		    }
		    yyvsp[-2].card->ident_type = VERS_1_IDENT;
		    yyvsp[-2].card->id.vers.ns = 1;
		    yyvsp[-2].card->id.vers.pi[0] = yyvsp[0].str;
		}
break;
case 32:
#line 262 "yacc_config.y"
{
		    if (yyvsp[-2].card->id.vers.ns == 4) {
			yyerror("too many version strings");
			YYERROR;
		    }
		    yyvsp[-2].card->id.vers.pi[yyvsp[-2].card->id.vers.ns] = yyvsp[0].str;
		    yyvsp[-2].card->id.vers.ns++;
		}
break;
case 33:
#line 273 "yacc_config.y"
{
		    if (yyvsp[-2].card->ident_type != 0) {
			yyerror("ID method already defined\n");
			YYERROR;
		    }
		    yyvsp[-2].card->ident_type = FUNC_IDENT;
		    yyvsp[-2].card->id.func.funcid = yyvsp[0].num;
		}
break;
case 34:
#line 284 "yacc_config.y"
{ yyvsp[-2].card->cis_file = strdup(yyvsp[0].str); }
break;
case 35:
#line 288 "yacc_config.y"
{
		    if (add_binding(yyvsp[-2].card, yyvsp[0].str, 0) != 0)
			YYERROR;
		}
break;
case 36:
#line 293 "yacc_config.y"
{
		    if (add_binding(yyvsp[-4].card, yyvsp[-2].str, yyvsp[0].num) != 0)
			YYERROR;
		}
break;
case 37:
#line 298 "yacc_config.y"
{
		    if (add_binding(yyvsp[-2].card, yyvsp[0].str, 0) != 0)
			YYERROR;
		}
break;
case 38:
#line 303 "yacc_config.y"
{
		    if (add_binding(yyvsp[-4].card, yyvsp[-2].str, yyvsp[0].num) != 0)
			YYERROR;
		}
break;
case 39:
#line 310 "yacc_config.y"
{
		    yyvsp[-1].device->needs_mtd = 1;
		}
break;
case 40:
#line 316 "yacc_config.y"
{
		    device_info_t *d;
		    int i, found = 0;
		    for (d = root_device; d; d = d->next) {
			for (i = 0; i < d->modules; i++)
			    if (strcmp(yyvsp[-2].str, d->module[i]) == 0) break;
			if (i < d->modules) {
			    if (d->opts[i])
				free(d->opts[i]);
			    d->opts[i] = strdup(yyvsp[0].str);
			    found = 1;
			}
		    }
		    free(yyvsp[-2].str); free(yyvsp[0].str);
		    if (!found) {
			yyerror("module name not found!");
			YYERROR;
		    }
		}
break;
case 41:
#line 338 "yacc_config.y"
{
		    if (add_module(yyvsp[-2].device, yyvsp[0].str) != 0)
			YYERROR;
		}
break;
case 42:
#line 343 "yacc_config.y"
{
		    if (yyvsp[-2].device->opts[yyvsp[-2].device->modules-1] == NULL) {
			yyvsp[-2].device->opts[yyvsp[-2].device->modules-1] = yyvsp[0].str;
		    } else {
			yyerror("too many options");
			YYERROR;
		    }
		}
break;
case 43:
#line 352 "yacc_config.y"
{
		    if (add_module(yyvsp[-2].device, yyvsp[0].str) != 0)
			YYERROR;
		}
break;
case 44:
#line 359 "yacc_config.y"
{
		    if (yyvsp[-2].device->class != NULL) {
			yyerror("extra class string");
			YYERROR;
		    }
		    yyvsp[-2].device->class = yyvsp[0].str;
		}
break;
case 45:
#line 369 "yacc_config.y"
{
		    yyval.mtd = calloc(sizeof(mtd_ident_t), 1);
		    yyval.mtd->refs = 1;
		    yyval.mtd->name = yyvsp[0].str;
		}
break;
case 49:
#line 380 "yacc_config.y"
{
		    if (yyvsp[-2].mtd->mtd_type != 0) {
			yyerror("ID method already defined");
			YYERROR;
		    }
		    yyvsp[-2].mtd->mtd_type = DTYPE_MTD;
		    yyvsp[-2].mtd->dtype = yyvsp[0].num;
		}
break;
case 50:
#line 391 "yacc_config.y"
{
		    if (yyvsp[-3].mtd->mtd_type != 0) {
			yyerror("ID method already defined");
			YYERROR;
		    }
		    yyvsp[-3].mtd->mtd_type = JEDEC_MTD;
		    yyvsp[-3].mtd->jedec_mfr = yyvsp[-1].num;
		    yyvsp[-3].mtd->jedec_info = yyvsp[0].num;
		}
break;
case 51:
#line 403 "yacc_config.y"
{
		    if (yyvsp[-1].mtd->mtd_type != 0) {
			yyerror("ID method already defined");
			YYERROR;
		    }
		    if (default_mtd) {
			yyerror("Default MTD already defined");
			YYERROR;
		    }
		    yyvsp[-1].mtd->mtd_type = DEFAULT_MTD;
		    default_mtd = yyvsp[-1].mtd;
		}
break;
case 52:
#line 418 "yacc_config.y"
{
		    if (yyvsp[-2].mtd->module != NULL) {
			yyerror("extra MTD entry");
			YYERROR;
		    }
		    yyvsp[-2].mtd->module = yyvsp[0].str;
		}
break;
case 53:
#line 426 "yacc_config.y"
{
		    if (yyvsp[-2].mtd->opts == NULL) {
			yyvsp[-2].mtd->opts = yyvsp[0].str;
		    } else {
			yyerror("too many options");
			YYERROR;
		    }
		}
break;
case 54:
#line 437 "yacc_config.y"
{
		    mtd_ident_t *m;
		    int found = 0;
		    for (m = root_mtd; m; m = m->next)
			if (strcmp(yyvsp[-2].str, m->module) == 0) break;
		    if (m) {
			if (m->opts) free(m->opts);
			m->opts = strdup(yyvsp[0].str);
			found = 1;
		    }
		    free(yyvsp[-2].str); free(yyvsp[0].str);
		    if (!found) {
			yyerror("MTD name not found!");
			YYERROR;
		    }
		}
break;
#line 946 "y.tab.c"
    }
    yyssp -= yym;
    yystate = *yyssp;
    yyvsp -= yym;
    yym = yylhs[yyn];
    if (yystate == 0 && yym == 0)
    {
#if YYDEBUG
        if (yydebug)
            printf("%sdebug: after reduction, shifting from state 0 to\
 state %d\n", YYPREFIX, YYFINAL);
#endif
        yystate = YYFINAL;
        *++yyssp = YYFINAL;
        *++yyvsp = yyval;
        if (yychar < 0)
        {
            if ((yychar = yylex()) < 0) yychar = 0;
#if YYDEBUG
            if (yydebug)
            {
                yys = 0;
                if (yychar <= YYMAXTOKEN) yys = yyname[yychar];
                if (!yys) yys = "illegal-symbol";
                printf("%sdebug: state %d, reading %d (%s)\n",
                        YYPREFIX, YYFINAL, yychar, yys);
            }
#endif
        }
        if (yychar == 0) goto yyaccept;
        goto yyloop;
    }
    if ((yyn = yygindex[yym]) && (yyn += yystate) >= 0 &&
            yyn <= YYTABLESIZE && yycheck[yyn] == yystate)
        yystate = yytable[yyn];
    else
        yystate = yydgoto[yym];
#if YYDEBUG
    if (yydebug)
        printf("%sdebug: after reduction, shifting from state %d \
to state %d\n", YYPREFIX, *yyssp, yystate);
#endif
    if (yyssp >= yyss + yystacksize - 1)
    {
        goto yyoverflow;
    }
    *++yyssp = yystate;
    *++yyvsp = yyval;
    goto yyloop;
yyoverflow:
    yyerror("yacc stack overflow");
yyabort:
    return (1);
yyaccept:
    return (0);
}
