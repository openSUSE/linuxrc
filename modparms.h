/*
 *
 * modparms.h    Header file for modparms.c
 *
 * Copyright (c) 1996-2000  Hubert Mantel, SuSE GmbH  (mantel@suse.de)
 *
 */

extern int  mpar_get_params       (module_t *module_prv, char *params_pcr);
extern void mpar_save_modparams   (char *module_tv, char *params_tv);
extern void mpar_delete_modparams (char *module_tv);
extern void mpar_write_modparms   (FILE *file_prv);
