/*
 *
 * text.h        Header file for text.c
 *
 * Copyright (c) 1996-2001  Hubert Mantel, SuSE GmbH  (mantel@suse.de)
 *
 */

/* defines textid_t */
#include "po/text_textids.h"

extern char *txt_get  (enum textid_t text_id);
extern int   txt_init (void);

/* define which translations should actually be supported */

#ifdef __alpha__

#define TRANS_de
#define TRANS_en
#define TRANS_fr

#else

/* all we have */
#define TRANS_br
#define TRANS_cs
#define TRANS_de
#define TRANS_el
#define TRANS_en
#define TRANS_es
#define TRANS_fr
#define TRANS_hu
#define TRANS_id
#define TRANS_it
#define TRANS_nl
#define TRANS_pl
#define TRANS_pt
#define TRANS_pt_BR
#define TRANS_ro
#define TRANS_ru
#define TRANS_sk

#endif

#if LXRC_TINY >= 1
#undef TRANS_br
#undef TRANS_cs
#undef TRANS_el
#undef TRANS_hu
#undef TRANS_id
#undef TRANS_pl
#undef TRANS_pt_BR
#undef TRANS_ro
#undef TRANS_ru
#undef TRANS_sk
#endif

