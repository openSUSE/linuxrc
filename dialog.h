/*
 *
 * dialog.h      Header for dialog.h
 *
 * Copyright (c) 1996-1998  Hubert Mantel, S.u.S.E. GmbH  (mantel@suse.de)
 *
 */

#define MSGTYPE_INFO     0
#define MSGTYPE_ERROR    1

extern int  dia_yesno        (char *txt_tv, int default_iv);
extern int  dia_message      (char *txt_tv, int msgtype_iv);
extern int  dia_menu         (char *head_tv,     item_t items_arv [],
                              int   nr_items_iv, int    default_iv);
extern void dia_status_on    (window_t *win_prr, char *txt_tv);
extern void dia_status       (window_t *win_prv, int percent_iv);
extern int  dia_input        (char *txt_tv, char *input_tr,
                              int len_iv, int fieldlen_iv);
extern int  dia_show_file    (char *head_tv, char *file_tv, int eof_iv);
extern void dia_info         (window_t *win_prr, char *txt_tv);
extern int  dia_show_lines   (char *head_tv,   char *lines_atv [],
                              int nr_lines_iv, int   width_iv, int eof_iv);
extern void dia_handle_ctrlc (void);
