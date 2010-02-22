/*
 *
 * dialog.h      Header for dialog.h
 *
 * Copyright (c) 1996-2002  Hubert Mantel, SuSE Linux AG  (mantel@suse.de)
 *
 */

#define MSGTYPE_INFO      0
#define MSGTYPE_ERROR     1
#define MSGTYPE_INFOENTER 2
#define MSGTYPE_REBOOT    3

typedef enum {
  di_none,	/* must be 0 */
  di_skip,

  di_main_start,
  di_main_settings,
  di_main_expert,
  di_main_exit,

  di_expert_info,
  di_expert_modules,
  di_expert_verify,
  di_expert_eject,

  di_extras_info,
  di_extras_change,
  di_extras_shell,
  di_extras_command,
  di_extras_quit,
  
  di_exit_reboot,
  di_exit_halt,

  di_set_lang,
  di_set_display,
  di_set_keymap,
  di_set_animate,
  di_set_forceroot,
  di_set_rootimage,
  di_set_bootptimeout,
  di_set_dhcp,
  di_set_vnc,
  di_set_usessh,
  di_set_startshell,
  di_set_slp,

  di_inst_install,
  di_inst_system,
  di_inst_rescue,
  di_inst_update_add,
  di_inst_update_show,

  di_source_cdrom,
  di_source_net,
  di_source_hd,
  di_source_floppy,

  di_netsource_nfs,
  di_netsource_smb,
  di_netsource_ftp,
  di_netsource_http,
  di_netsource_tftp,

  di_info_kernel,
  di_info_drives,
  di_info_modules,
  di_info_pci,
  di_info_cpu,
  di_info_mem,
  di_info_ioports,
  di_info_interrupts,
  di_info_devices,
  di_info_netdev,
  di_info_dma,

  di_display_x11,
  di_display_vnc,
  di_display_ssh,
  di_display_console,

  di_390net_osa,
  di_390net_ctc,
  di_390net_escon,
  di_390net_iucv,
  di_390net_hsi,
  di_390net_eth,
  di_390net_tr,
  di_390net_qdio,
  di_390net_lcs,
  di_390net_sep,
  
  di_ctc_compat,
  di_ctc_ext,
  di_ctc_zos390,
  
  di_osa_eth,
  di_osa_tr,
  di_osa_lcs,
  di_osa_qdio,

  di_wlan_open,
  di_wlan_wep_o,
  di_wlan_wep_r,
  di_wlan_wpa,

} dia_item_t;

typedef enum {
  align_none,
  align_center,
  align_left
} dia_align_t;

extern int  dia_yesno        (char *txt_tv, int default_iv);
extern int  dia_okcancel     (char *txt_tv, int default_iv);
extern int  dia_contabort    (char *txt_tv, int default_iv);
extern int  dia_message      (char *txt_tv, int msgtype_iv);
extern int  dia_menu         (char *head_tv,     item_t items_arv [],
                              int   nr_items_iv, int    default_iv);
extern void dia_status_on    (window_t *win_prr, char *txt_tv);
extern void dia_status       (window_t *win_prv, int percent_iv);
extern void dia_status_off   (window_t *win_prr);
extern int  dia_input        (char *txt_tv, char *input_tr,
                              int len_iv, int fieldlen_iv, int pw_mode);
extern int  dia_show_file    (char *head_tv, char *file_tv, int eof_iv);
extern void dia_info         (window_t *win_prr, char *txt_tv, int type);
extern int  dia_show_lines   (char *head_tv,   char *lines_atv [],
                              int nr_lines_iv, int   width_iv, int eof_iv);
extern void dia_handle_ctrlc (void);

char *dia_get_text(dia_item_t di);
dia_item_t dia_menu2(char *title, int width, int (*func)(dia_item_t), dia_item_t *items, dia_item_t default_item);
int dia_list(char *title, int width, int (*func)(int), char **items, int default_item, dia_align_t align);
int dia_show_lines2(char *head, slist_t *sl0, int width);
int dia_input2(char *txt, char **input, int fieldlen, int pw_mode);
int dia_input2_chopspace(char* txt, char** input, int fieldlen, int pw_mode);
