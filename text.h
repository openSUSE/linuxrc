#ifndef _TEXT_H
#define _TEXT_H

/*
 *
 * text.h        Header file for text.c
 *
 * Copyright (c) 1996-2002  Hubert Mantel, SuSE Linux AG  (mantel@suse.de)
 *
 */

/* defines textid_t */
#include "po/text_textids.h"

extern char *txt_get  (enum textid_t text_id);
extern int   txt_init (void);

/* define which translations should actually be supported */

/* all we have */
// #define TRANS_br
// #define TRANS_ar
#define TRANS_bg
#define TRANS_bn
#define TRANS_bs
#define TRANS_cs
#define TRANS_da
#define TRANS_de
#define TRANS_el
#define TRANS_en
#define TRANS_es
#define TRANS_fr
#define TRANS_he
#define TRANS_hu
// #define TRANS_id
#define TRANS_it
#define TRANS_ja
#define TRANS_ko
#define TRANS_nl
#define TRANS_nb
#define TRANS_pl
#define TRANS_pt
#define TRANS_pt_BR
// #define TRANS_ro
#define TRANS_ru
#define TRANS_sk
#define TRANS_sl
#define TRANS_sv
#define TRANS_uk
#define TRANS_zh_CN
#define TRANS_zh_TW

#endif  /* _TEXT_H */
