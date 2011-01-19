/*
 *
 * dialog.c      User dialogs
 *
 * Copyright (c) 1996-2002  Hubert Mantel, SuSE Linux AG  (mantel@suse.de)
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>

#include <sys/types.h>
#include <sys/wait.h>

#include "global.h"
#include "window.h"
#include "keyboard.h"
#include "text.h"
#include "util.h"
#include "display.h"
#include "dialog.h"
#include "linuxrc.h"
#include "file.h"
#include "utf8.h"


#define MIN_WIN_SIZE    40
#define MAX_LINES     1000

struct {
  dia_item_t item;
  int text_id;
  char *text;
} dia_texts[] = {
  { di_main_start,       TXT_MENU_START,          },
  { di_main_settings,    TXT_SETTINGS,            },
  { di_main_expert,      TXT_EXPERT,              },
  { di_main_exit,        TXT_END_REBOOT,          },

  { di_expert_info,      TXT_MENU_INFO,           },
  { di_expert_modules,   TXT_MENU_MODULES,        },
  { di_expert_verify,    TXT_CHECK_CD,            },
  { di_expert_eject,     TXT_EJECT_CD,            },

  { di_exit_reboot,      TXT_END_REBOOT,          },
  { di_exit_halt,        TXT_POWER_OFF,           },

  { di_set_lang,         TXT_MENU_LANG,           },
  { di_set_display,      TXT_MENU_DISPLAY,        },
  { di_set_keymap,       TXT_MENU_KEYMAP,         },
  { di_set_animate,      TXT_ASK_ANIMATE,         },
  { di_set_forceroot,    TXT_FORCE_ROOTIMAGE,     },
  { di_set_rootimage,    TXT_NEW_ROOTIMAGE,       },
  { di_set_bootptimeout, TXT_BOOTP_TIMEOUT,       },
  { di_set_dhcp,         0, "DHCP/BOOTP"          },
  { di_set_vnc,          TXT_VNC_SWITCH,          },
  { di_set_usessh,       TXT_SSH_SWITCH,          },
  { di_set_startshell,   TXT_START_SHELL_YAST,    },
  { di_set_slp,          TXT_GET_SLP_INFO,        },

  { di_inst_install,     TXT_START_INSTALL,       },
  { di_inst_system,      TXT_BOOT_SYSTEM,         },
  { di_inst_rescue,      TXT_START_RESCUE,        },
  { di_inst_update_add,  TXT_ADD_DRIVER_UPDATE,   },
  { di_inst_update_show, TXT_SHOW_DRIVER_UPDATES, },

  { di_source_cdrom,     TXT_DVD,                 },
  { di_source_net,       TXT_NET,                 },
  { di_source_hd,        TXT_HARDDISK,            },
  { di_source_floppy,    TXT_FLOPPY,              },

  { di_netsource_nfs,    0, "NFS"                 },
  { di_netsource_smb,    0, "SMB / CIFS (Windows Share)" },
  { di_netsource_ftp,    0, "FTP"                 },
  { di_netsource_http,   0, "HTTP"                },
  { di_netsource_tftp,   0, "TFTP"                },

  { di_info_kernel,      TXT_INFO_KERNEL          },
  { di_info_drives,      TXT_DRIVES               },
  { di_info_modules,     TXT_INFO_MODULES         },
  { di_info_pci,         TXT_INFO_PCI             },
  { di_info_cpu,         TXT_INFO_CPU             },
  { di_info_mem,         TXT_INFO_MEM             },
  { di_info_ioports,     TXT_INFO_IOPORTS         },
  { di_info_interrupts,  TXT_INFO_IRQS            },
  { di_info_devices,     TXT_INFO_DEVICES         },
  { di_info_netdev,      TXT_INFO_NETDEV          },
  { di_info_dma,         TXT_INFO_DMA             },

  { di_extras_info,      TXT_SHOW_CONFIG          },
  { di_extras_change,    TXT_CHANGE_CONFIG        },
  { di_extras_shell,     TXT_START_SHELL          },
  { di_extras_command,   TXT_RUN_COMMAND          },
  { di_extras_quit,      TXT_QUIT_LINUXRC         },
  
  { di_display_vnc,    	 0, "VNC"	          },
  { di_display_x11,  	 0, "X11"	          },
  { di_display_ssh,      0, "SSH"	          },
  { di_display_console,  0, "ASCII Console"	  },
  
  { di_390net_osa,	 TXT_390NET_OSA           },
  { di_390net_ctc,	 TXT_390NET_CTC	          },
  { di_390net_escon,	 TXT_390NET_ESCON         },
  { di_390net_iucv,	 TXT_390NET_IUCV          },
  { di_390net_hsi,	 TXT_390NET_HSI	          },
  { di_390net_sep,	 0, "#--------------------" },
  
  { di_ctc_compat,	 TXT_CTC_PROT_COMPAT      },
  { di_ctc_ext,		 TXT_CTC_PROT_EXT         },
  { di_ctc_zos390,	 TXT_CTC_PROT_ZOS390      },
  
  { di_osa_eth,		 TXT_OSA_ETH              },
  { di_osa_tr,		 TXT_OSA_TR               },
  { di_osa_lcs,		 TXT_OSA_LCS              },
  { di_osa_qdio,	 TXT_OSA_QDIO             },

  { di_wlan_open,        0, "No Authentication"   },
  { di_wlan_wep_o,       0, "WEP - Open"          },
  { di_wlan_wep_r,       0, "WEP - Shared Key"    },
  { di_wlan_wpa,         0, "WPA-PSK"             },

};


/*
 *
 * local function prototypes
 *
 */

static int dia_binary(char *txt, char *button0, char *button1, int def);
static int dia_win_open (window_t *win_prr, char *txt_tv);
static int lgetchar(void);

/*
 *
 * exported functions
 *
 */

int dia_contabort(char *txt, int def)
{
  int i;

  i = dia_binary(txt, txt_get(TXT_CONTINUE), txt_get(TXT_ABORT), def == NO ? 1 : 0);

  switch(i) {
    case 0:
      return YES;
    case 1:
      return NO;
    default:
      return ESCAPE;
  }
}

int dia_yesno(char *txt, int def)
{
  int i;

  i = dia_binary(txt, txt_get(TXT_YES), txt_get(TXT_NO), def == NO ? 1 : 0);

  switch(i) {
    case 0:
      return YES;
    case 1:
      return NO;
    default:
      return ESCAPE;
  }
}

int dia_okcancel(char *txt, int def)
{
  int i;

  i = dia_binary(txt, txt_get(TXT_OK), txt_get(TXT_CANCEL), def == NO ? 1 : 0);

  switch(i) {
    case 0:
      return YES;
    case 1:
      return NO;
    default:
      return ESCAPE;
  }
}

static int dia_readnum()
{
  int c, n = 0, first = 1;
  for (;;)
    {
      c = lgetchar();
      if (c == '\r' || c == '\n' || c == EOF)
	return n;
      if (first && (c == 'x' || c == 033))
	{
	  n = c == 'x' ? -c : 0;
	  c = lgetchar();
          if (c == '\r' || c == '\n' || c == EOF)
	    return n;
	  c = 0;
	}
      if (c < '0' || c > '9')
	{
          while ((c = lgetchar()) != '\r' && c != '\n' && c != EOF)
	    ;
	  return -1;
	}
      n = n * 10 + (c - '0');
      first = 0;
    }
}

static int dia_printformatted(char *txt, int l, int width, int withnl)
{
  int i, j, r = 0;

  if (l == 0)
    l = strlen(txt);
  while (l > 0 && txt[l - 1] == ' ')
    l--;
  for (j = 0; j < l; )
    {
      while(txt[j] == ' ' && j < l)
	j++;
      for (i = 0; i < width; i++)
	if (i + j == l || txt[i + j] == '\n')
	  break;
      if (i == width)
	{
	  while (--i > 0 && txt[i + j] != ' ')
	    ;
	  if (i == 0)
	    i = width - 1;
	}
      r = i;
      while (i-- > 0)
	putchar(txt[j++]);
      if (j < l)
	{
	  putchar('\n');
          if (txt[j] == '\n')
	    j++;
	}
      i = 0;
    }
  if (withnl)
    putchar('\n');
  return withnl ? 0 : r;
}


/*
 * Show dialog with two buttons; returns the botton that was selected.
 */
int dia_binary(char *txt, char *button0_txt, char *button1_txt, int def)
{
  window_t win;
  button_t button0, button1;
  button_t *buttons[2] = { &button0, &button1 };
  int width, answer;
  int len0, len1, len_max;

  if (config.linemode)
    {
      int current_ii;

      for (;;)
	{
	  putchar('\n');
	  dia_printformatted(txt, 0, max_x_ig - 1, 1);
	  putchar('\n');
	  printf("1) %s\n", button0_txt);
	  printf("2) %s\n", button1_txt);
	  printf("\n> ");fflush(stdout);
	  current_ii = dia_readnum();
	  if(current_ii == -'x') dia_handle_ctrlc();
	  if (current_ii < 0 || current_ii > 2)
	    continue;
	  return current_ii - 1;
	}
    }
  disp_toggle_output(DISP_OFF);
  memset(&win, 0, sizeof win);
  win.bg_color = colors_prg->choice_win;
  win.fg_color = colors_prg->choice_fg;
  width = dia_win_open(&win, txt);

  len0 = utf8_strwidth(button0_txt);
  len1 = utf8_strwidth(button1_txt);
  len_max = len0 > len1 ? len0 : len1;

  util_generate_button(buttons[0], button0_txt, len_max);
  util_generate_button(buttons[1], button1_txt, len_max);

  len0 = utf8_strwidth(button0.text);
  len1 = utf8_strwidth(button1.text);

  win_add_button(&win, buttons[0], width / 3 - len0 / 2 - 3, len0);
  win_add_button(&win, buttons[1], width - width / 3 - len1 / 2 + 2, len1);

  disp_flush_area(&win);
  answer = win_choose_button(buttons, 2, def + 1) - 1;
  win_close(&win);

  return answer;
}


int dia_message (char *txt_tv, int msgtype_iv)
    {
    window_t  win_ri;
    int       width_ii;
    button_t  button_ri;
    int       key_ii;
    char *s;

    if(!config.win) util_disp_init();

    if (config.linemode)
      {
	putchar('\n');
	if (msgtype_iv == MSGTYPE_ERROR || msgtype_iv == MSGTYPE_REBOOT)
	  printf("*** ");
        dia_printformatted(txt_tv, 0, max_x_ig - 5, 1);
	if (msgtype_iv == MSGTYPE_INFOENTER)
	  {
	    int c;
	    do
	      c = lgetchar();
	    while (c != '\n' && c != '\r' && c != EOF);
	  }
	return 0;
      }
    disp_toggle_output (DISP_OFF);
    kbd_clear_buffer ();
    memset (&win_ri, 0, sizeof (window_t));
    if (msgtype_iv == MSGTYPE_ERROR || msgtype_iv == MSGTYPE_REBOOT)
        {
        win_ri.bg_color = colors_prg->error_win;
        win_ri.fg_color = colors_prg->error_fg;
        }
    else
        {
        win_ri.bg_color = colors_prg->msg_win;
        win_ri.fg_color = colors_prg->msg_fg;
        }
    width_ii = dia_win_open (&win_ri, txt_tv);
    s = txt_get(msgtype_iv == MSGTYPE_REBOOT ? TXT_REBOOT : TXT_OK);
    util_generate_button (&button_ri, s, utf8_strwidth(s));
    win_add_button (&win_ri, &button_ri,
                    width_ii / 2 - utf8_strwidth (button_ri.text) / 2 - 1,
                    utf8_strwidth (button_ri.text));
/*    win_button_select (&button_ri); */

    disp_flush_area (&win_ri);
    do
        {
        key_ii = kbd_getch (TRUE);
        if (key_ii == KEY_CTRL_C)
            dia_handle_ctrlc ();

        if (key_ii == KEY_ENTER)
            win_button_pressed (&button_ri, FALSE);
        }
    while (
      key_ii != KEY_ENTER &&
      key_ii != KEY_ESC &&
      key_ii != 'q' &&
      key_ii != 'r' &&
      key_ii != 's' &&
      key_ii != 'i' &&
      key_ii != 'f' &&
      key_ii != 'c'
    );

    win_close (&win_ri);

    if (key_ii == KEY_ENTER)
        return (0);
    else if (key_ii == KEY_ESC)
        return (-1);
    else if (key_ii == 'r')
        return (-69);
    else if (key_ii == 'f')
        return (-70);
    else if (key_ii == 'i')
        return (-71);
    else if (key_ii == 'c')
        return (-73);
    else if (key_ii == 's')
        return (-74);
    else
        return (-42);
    }


int dia_menu (char *head_tv,     item_t  items_arv [],
              int   nr_items_iv, int     default_iv)
    {
    int       width_ii;
    int       key_ii;
    int       current_ii;
    window_t  menu_win_ri;
    window_t  tmp_win_ri;
    button_t  yes_button_ri;
    button_t  no_button_ri;
    button_t *curr_button_pri;
    int       nr_lines_ii;
    unsigned char *lines_ati [MAX_Y];
    int       i_ii;
    int       phys_items_ii;
    int       offset_ii;
    int       need_redraw_ii;
    int       scrollbar_ii;
    int       rc_ii;
    int len0, len1, len_max;
    char *s0, *s1;
    int dir, cur_new, ofs_new;


    if (config.linemode)
      {
	int i, j, cnt;
	char *p;
	int *item_index = calloc(nr_items_iv + 1, sizeof (int));

        for (;;)
	  {
	    putchar('\n');
	    dia_printformatted(head_tv, 0, max_x_ig - 1, 1);
	    putchar('\n');
	    for (i = cnt = 0; i < nr_items_iv; i++)
	      {
	        for (p = items_arv[i].text; *p == ' '; p++)
		  ;
		if(items_arv[i].tag.head) {
		  printf("%s\n", p);
		  // printf(nr_items_iv >= 10 ? "    %s\n" : "   %s\n", p);
		}
		else {
		  item_index[cnt++] = i;
		  printf(nr_items_iv >= 10 ? "%2d) %s\n" : "%d) %s\n", cnt, p);
		}
	      }

	    printf("\n> "); fflush(stdout);

	    current_ii = dia_readnum();
	    if(current_ii == -'x') dia_handle_ctrlc();

	    if(current_ii < 0 || current_ii > cnt) continue;
	    current_ii--;

	    if(current_ii == -1) break;

            i = item_index[current_ii];
	    if(items_arv[i].func) {
	      j = items_arv[i].di ? : i + 1;
	      rc_ii = items_arv[i].func(j);
	      if(rc_ii == -1) {
	        current_ii = -1;
	      }
	      else if(rc_ii) {
	        continue;
	      }
	    }
	    break;
	  }

        i = current_ii >= 0 ? item_index[current_ii] : -1;

        free(item_index);

	return i + 1;
      }
    disp_toggle_output (DISP_OFF);
    kbd_clear_buffer ();
    width_ii = utf8_strwidth (head_tv) + 6;
    if (width_ii < utf8_strwidth(items_arv [0].text) + 6)
        width_ii = utf8_strwidth(items_arv [0].text) + 6;

    if (width_ii < MIN_WIN_SIZE)
        width_ii = MIN_WIN_SIZE;

    if (width_ii > max_x_ig - 4)
        width_ii = max_x_ig - 4;

    nr_lines_ii = util_format_txt (head_tv, lines_ati, width_ii - 4);

    if (nr_items_iv < max_y_ig - 17)
        phys_items_ii = nr_items_iv;
    else
        phys_items_ii = max_y_ig - 17;

    if (default_iv > nr_items_iv)
        default_iv = nr_items_iv;
    if (default_iv < 1)
        default_iv = 1;

    memset (&menu_win_ri, 0, sizeof (window_t));
    menu_win_ri.x_left = max_x_ig / 2 - width_ii / 2;
    menu_win_ri.y_left = max_y_ig / 2 - 3 - (nr_lines_ii + phys_items_ii + 1) / 2;
    menu_win_ri.x_right = max_x_ig / 2 + width_ii / 2;
    menu_win_ri.y_right = max_y_ig / 2 + 7 + (nr_lines_ii + phys_items_ii) / 2;
    menu_win_ri.head = 2 + nr_lines_ii;
    menu_win_ri.foot = 3;
    menu_win_ri.shadow = TRUE;
    menu_win_ri.style = STYLE_RAISED;
    menu_win_ri.bg_color = colors_prg->menu_win;
    menu_win_ri.fg_color = colors_prg->menu_fg;
    menu_win_ri.save_bg = TRUE;
    win_open (&menu_win_ri);
    win_clear (&menu_win_ri);

    memset (&tmp_win_ri, 0, sizeof (window_t));
    tmp_win_ri.x_left = menu_win_ri.x_left + 1;
    tmp_win_ri.y_left = menu_win_ri.y_left + 1;
    tmp_win_ri.x_right = menu_win_ri.x_right - 1;
    tmp_win_ri.y_right = menu_win_ri.y_left + 2 + nr_lines_ii;
    tmp_win_ri.style = STYLE_RAISED;
    tmp_win_ri.bg_color = menu_win_ri.bg_color;
    tmp_win_ri.fg_color = menu_win_ri.fg_color;
    win_open (&tmp_win_ri);

    for (i_ii = 1; i_ii <= nr_lines_ii; i_ii++)
        {
        win_print (&tmp_win_ri, 2, i_ii, lines_ati [i_ii - 1]);
        free (lines_ati [i_ii - 1]);
        }
    
    s0 = txt_get (TXT_OK);
    s1 = txt_get (TXT_CANCEL);

    len0 = utf8_strwidth(s0);
    len1 = utf8_strwidth(s1);
    len_max = len0 > len1 ? len0 : len1;

    util_generate_button (&yes_button_ri, s0, len_max);
    util_generate_button (&no_button_ri, s1, len_max);

    win_add_button (&menu_win_ri, &yes_button_ri,
                    width_ii / 3 - utf8_strwidth (yes_button_ri.text) / 2 - 3,
                    utf8_strwidth (yes_button_ri.text));
    win_add_button (&menu_win_ri, &no_button_ri,
                    width_ii - width_ii / 3 - utf8_strwidth (yes_button_ri.text) / 2 + 1,
                    utf8_strwidth (no_button_ri.text));
    curr_button_pri = &yes_button_ri;
    win_button_select (curr_button_pri);

    if (width_ii > utf8_strwidth (items_arv [0].text) + 4)
        width_ii = utf8_strwidth (items_arv [0].text) + 4;
    memset (&tmp_win_ri, 0, sizeof (window_t));
    tmp_win_ri.x_left = max_x_ig / 2 - width_ii / 2;
    tmp_win_ri.y_left = menu_win_ri.y_left + menu_win_ri.head + 2;
    tmp_win_ri.x_right = max_x_ig / 2 + width_ii / 2;
    tmp_win_ri.y_right = menu_win_ri.y_right - menu_win_ri.foot - 2;
    tmp_win_ri.style = STYLE_SUNKEN;
    tmp_win_ri.bg_color = menu_win_ri.bg_color;
    tmp_win_ri.fg_color = menu_win_ri.fg_color;
    win_open (&tmp_win_ri);

    if (default_iv <= phys_items_ii)
        {
        offset_ii = 0;
        current_ii = default_iv - 1;
        }
    else
        {
        offset_ii = default_iv - phys_items_ii;
        current_ii = phys_items_ii - 1;
        }


    if (phys_items_ii < nr_items_iv)
        {
        disp_graph_on ();
        scrollbar_ii = (offset_ii + current_ii) * phys_items_ii / nr_items_iv;
        if (scrollbar_ii == 0 && offset_ii + current_ii != 0)
            scrollbar_ii++;
        if (scrollbar_ii == phys_items_ii - 1 && offset_ii + current_ii < nr_items_iv - 1)
            scrollbar_ii--;

        for (i_ii = 0; i_ii < phys_items_ii; i_ii++)
            {
            disp_gotoxy (tmp_win_ri.x_right - 1, tmp_win_ri.y_left + 1 + i_ii);
            if (i_ii == scrollbar_ii)
                {
                disp_set_color (tmp_win_ri.bg_color, tmp_win_ri.fg_color);
                disp_write_char (' ');
                }
            else
                {
                disp_set_color (tmp_win_ri.fg_color, tmp_win_ri.bg_color);
                disp_write_char (GRAPH_BLOCK);
                }
            }
        disp_graph_off ();
        }

    disp_set_color (tmp_win_ri.fg_color, tmp_win_ri.bg_color);
    for (i_ii = 0; i_ii < phys_items_ii; i_ii++)
        win_print (&tmp_win_ri, 2, i_ii + 1, items_arv [offset_ii + i_ii].text);

    disp_set_color (colors_prg->input_fg, colors_prg->input_bg);
    win_print (&tmp_win_ri, 2, current_ii + 1,
               items_arv [offset_ii + current_ii].text);

    disp_flush_area (&menu_win_ri);
    do
        {
        key_ii = kbd_getch (TRUE);
        if (key_ii == KEY_CTRL_C)
            dia_handle_ctrlc ();

        if (key_ii != KEY_LEFT  && key_ii != KEY_RIGHT &&
            key_ii != KEY_TAB   && key_ii != KEY_BTAB  &&
            key_ii != KEY_ENTER && key_ii != KEY_ESC)
            {
            disp_set_color (tmp_win_ri.fg_color, tmp_win_ri.bg_color);
            win_print (&tmp_win_ri, 2, current_ii + 1,
                       items_arv [offset_ii + current_ii].text);
            }

        need_redraw_ii = FALSE;

	dir = 0;
	ofs_new = offset_ii;

        switch (key_ii)
            {
            case KEY_DOWN:
                #if 0
                if (current_ii < phys_items_ii - 1)
                    current_ii++;
                else if (offset_ii + current_ii < nr_items_iv - 1)
                    {
                    offset_ii++;
                    need_redraw_ii = TRUE;
                    }
                else if (phys_items_ii == nr_items_iv)
                    current_ii = (current_ii + 1) % phys_items_ii;
                #endif
                cur_new = current_ii + offset_ii + 1;
                if(phys_items_ii == nr_items_iv && cur_new == nr_items_iv) cur_new = 0;
                dir = 1;
                break;
            case KEY_UP:
                #if 0
                if (current_ii > 0)
                    current_ii--;
                else if (offset_ii > 0)
                    {
                    offset_ii--;
                    need_redraw_ii = TRUE;
                    }
                else if (phys_items_ii == nr_items_iv)
                    current_ii = (current_ii + phys_items_ii - 1) %
                                         phys_items_ii;
                #endif
                cur_new = current_ii + offset_ii - 1;
                if(phys_items_ii == nr_items_iv && cur_new == -1) cur_new = nr_items_iv - 1;
                dir = -1;
                break;
            case KEY_HOME:
                // offset_ii = 0;
                // current_ii = 0;
                // need_redraw_ii = TRUE;
                cur_new = 0;
                dir = 1;
                break;
            case KEY_PGUP:
                #if 0
                if (offset_ii == 0)
                    current_ii = 0;
                else
                    {
                    offset_ii -= phys_items_ii;
                    if (offset_ii < 0)
                        offset_ii = 0;
                    }
                need_redraw_ii = TRUE;
                #endif
                cur_new = current_ii + offset_ii - phys_items_ii;
                ofs_new -= phys_items_ii;
                dir = -1;
                break;
            case KEY_END:
                // offset_ii = nr_items_iv - phys_items_ii;
                // current_ii = phys_items_ii - 1;
                // need_redraw_ii = TRUE;
                cur_new = nr_items_iv - 1;
                dir = -1;
                break;
            case KEY_PGDOWN:
                #if 0
                if (offset_ii + phys_items_ii >= nr_items_iv - 1)
                    current_ii = phys_items_ii - 1;
                else
                    {
                    offset_ii += phys_items_ii;
                    if (offset_ii > nr_items_iv - phys_items_ii)
                        offset_ii = nr_items_iv - phys_items_ii;
                    }
                need_redraw_ii = TRUE;
                #endif
                cur_new = current_ii + offset_ii + phys_items_ii;
                ofs_new += phys_items_ii;
                dir = 1;
                break;
            case KEY_LEFT:
            case KEY_RIGHT:
            case KEY_TAB:
            case KEY_BTAB:
                win_button_unselect (curr_button_pri);
                if (key_ii == KEY_LEFT ||
                    (key_ii != KEY_RIGHT && curr_button_pri == &no_button_ri))
                    curr_button_pri = &yes_button_ri;
                else if (key_ii == KEY_RIGHT ||
                    (key_ii != KEY_LEFT && curr_button_pri == &yes_button_ri))
                    curr_button_pri = &no_button_ri;
                win_button_select (curr_button_pri);
                break;
            case KEY_ENTER:
                win_button_pressed (curr_button_pri, TRUE);
                if (curr_button_pri == &no_button_ri)
                    key_ii = KEY_ESC;
                else
                    {
                    if (items_arv [offset_ii + current_ii].func)
                        {
                        i_ii = items_arv[offset_ii + current_ii].di ?: offset_ii + current_ii + 1;
                        rc_ii = items_arv[offset_ii + current_ii].func(i_ii);
                        if (rc_ii == -1)
                            key_ii = KEY_ESC;
                        else if (rc_ii)
                            key_ii = 0;
                        }
                    else
                        usleep (BUTTON_TIME);
                    }
                win_button_unpressed (curr_button_pri);
                if (!key_ii)
                    win_button_select (curr_button_pri);
                kbd_clear_buffer ();
                break;
            case KEY_ESC:
                win_button_unselect (curr_button_pri);
                curr_button_pri = &no_button_ri;
                win_button_pressed (curr_button_pri, FALSE);
                break;
            default:
                break;
            }

	if(dir) {
	  if(cur_new < 0) cur_new = 0;
	  if(cur_new > nr_items_iv - 1) cur_new = nr_items_iv - 1;

          while(cur_new + dir >= 0 && cur_new + dir < nr_items_iv && items_arv[cur_new].tag.head) {
            cur_new += dir;
          }

          if(items_arv[cur_new].tag.head) {
            dir *= -1;
            while(cur_new + dir >= 0 && cur_new + dir < nr_items_iv && items_arv[cur_new].tag.head) {
              cur_new += dir;
            }
          }

          if(items_arv[cur_new].tag.head) {
            cur_new = current_ii + offset_ii;
            ofs_new = offset_ii;
          }

          if(ofs_new < 0) ofs_new = 0;
          if(ofs_new > nr_items_iv - phys_items_ii) ofs_new = nr_items_iv - phys_items_ii;

          if(cur_new < ofs_new) {
            ofs_new = cur_new;
          }

          if(cur_new > ofs_new + phys_items_ii - 1) {
            ofs_new = cur_new - phys_items_ii + 1;
          }

          if(ofs_new != offset_ii) need_redraw_ii = 1;

          current_ii = cur_new - ofs_new;
          offset_ii = ofs_new;
	}

        if (need_redraw_ii)
            {
            disp_set_color (tmp_win_ri.fg_color, tmp_win_ri.bg_color);
            for (i_ii = 0; i_ii < phys_items_ii; i_ii++)
                win_print (&tmp_win_ri, 2, i_ii + 1,
                           items_arv [offset_ii + i_ii].text);
            }

        if (phys_items_ii < nr_items_iv)
            {
            disp_graph_on ();
            scrollbar_ii = (offset_ii + current_ii) * phys_items_ii / nr_items_iv;
            if (scrollbar_ii == 0 && offset_ii + current_ii != 0)
                scrollbar_ii++;
            if (scrollbar_ii == phys_items_ii - 1 && offset_ii + current_ii < nr_items_iv - 1)
                scrollbar_ii--;

            for (i_ii = 0; i_ii < phys_items_ii; i_ii++)
                {
                disp_gotoxy (tmp_win_ri.x_right - 1, tmp_win_ri.y_left + 1 + i_ii);
                if (i_ii == scrollbar_ii)
                    {
                    disp_set_color (tmp_win_ri.bg_color, tmp_win_ri.fg_color);
                    disp_write_char (' ');
                    }
                else
                    {
                    disp_set_color (tmp_win_ri.fg_color, tmp_win_ri.bg_color);
                    disp_write_char (GRAPH_BLOCK);
                    }
                }
            disp_graph_off ();
            }

        if (key_ii != KEY_LEFT  && key_ii != KEY_RIGHT &&
            key_ii != KEY_TAB   && key_ii != KEY_BTAB  &&
            key_ii != KEY_ENTER && key_ii != KEY_ESC)
            {
            disp_set_color (colors_prg->input_fg, colors_prg->input_bg);
            win_print (&tmp_win_ri, 2, current_ii + 1,
                       items_arv [offset_ii + current_ii].text);
            }

        fflush (stdout);
        }
    while (key_ii != KEY_ENTER && key_ii != KEY_ESC);

    win_close (&menu_win_ri);
    if (key_ii == KEY_ESC)
        return (0);
    else
        return (offset_ii + current_ii + 1);
    }


void dia_status_on (window_t *win_prr, char *txt_tv)
    {
    window_t  tmp_win_ri;
    char      tmp_txt_ti [STATUS_SIZE * 6 + 1];

    if(!config.win || config.linemode) {
      printf("%s", txt_tv);
      fflush(stdout);
      return;
    }

    disp_toggle_output (DISP_OFF);
    strncpy (tmp_txt_ti, txt_tv, STATUS_SIZE);
    tmp_txt_ti [STATUS_SIZE] = 0;
    util_center_text (tmp_txt_ti, STATUS_SIZE);

    memset (win_prr, 0, sizeof (window_t));
    win_prr->x_left = max_x_ig / 2 - (STATUS_SIZE + 6) / 2;
    win_prr->y_left = max_y_ig / 2 - 4;
    win_prr->x_right = max_x_ig / 2 + (STATUS_SIZE + 6) / 2 - 1;
    win_prr->y_right = max_y_ig / 2 + 4;
    win_prr->style = STYLE_RAISED;
    win_prr->shadow = TRUE;
    win_prr->save_bg = TRUE;
    win_prr->bg_color = colors_prg->msg_win;
    win_prr->fg_color = colors_prg->msg_fg;
    win_prr->foot = 3;
    win_open (win_prr);

    memset (&tmp_win_ri, 0, sizeof (window_t));
    tmp_win_ri.x_left = win_prr->x_left + 1;
    tmp_win_ri.y_left = win_prr->y_left + 1;
    tmp_win_ri.x_right = win_prr->x_right - 1;
    tmp_win_ri.y_right = win_prr->y_left + 3;
    tmp_win_ri.style = STYLE_SUNKEN;
    tmp_win_ri.bg_color = win_prr->bg_color;
    tmp_win_ri.fg_color = win_prr->fg_color;
    win_open (&tmp_win_ri);
    win_clear (&tmp_win_ri);
    disp_set_color (tmp_win_ri.fg_color, tmp_win_ri.bg_color);
    win_print (&tmp_win_ri, 2, 1, tmp_txt_ti);

    tmp_win_ri.x_left = win_prr->x_left + 1;
    tmp_win_ri.y_left = win_prr->y_right - 3;
    tmp_win_ri.x_right = win_prr->x_right - 1;
    tmp_win_ri.y_right = win_prr->y_right - 1;
    tmp_win_ri.style = STYLE_RAISED;
    win_open (&tmp_win_ri);
    win_clear (&tmp_win_ri);

    memset (tmp_txt_ti, GRAPH_BLOCK, STATUS_SIZE);
    tmp_txt_ti [STATUS_SIZE] = 0;
    disp_gotoxy (win_prr->x_left + 3, win_prr->y_right - 2);
    disp_graph_on ();
    disp_write_string (tmp_txt_ti);
    disp_graph_off ();

    disp_flush_area (win_prr);
    }


void dia_status (window_t *win, int p)
{
  char buf[STATUS_SIZE + 1];
  int i;

  if(p > 100) p = 100;

  if(!config.win || config.linemode) {
    printf("\x08\x08\x08\x08%3d%%", p);
  }
  else {
    for(i = 0; i < p * STATUS_SIZE / 100; i++) buf[i] = ' ';
    buf[i] = 0;

    disp_set_color(win->bg_color, win->fg_color);
    disp_gotoxy(win->x_left + 3, win->y_right - 2);
    disp_write_string(buf);
  }

  fflush(stdout);
}

void dia_status_off (window_t *win_prv)
{
    if(!config.win || config.linemode) {
      printf("\n");
      fflush(stdout);
      return;
    }
  win_close(win_prv);
}

int dia_input (char *txt_tv, char *input_tr, int len_iv, int fieldlen_iv, int pw_mode)
{
    window_t  win_ri;
    int       width_ii;
    window_t  tmp_win_ri;
    int       rc_ii;

    if (config.linemode)
      {
	int i, c;

ctrlc:
	putchar('\n');
	i = strlen(txt_tv);
	if (i > 0 && txt_tv[i - 1] == '.')
	  i--;
	c = i = dia_printformatted(txt_tv, i, max_x_ig - 1, 0);
	if(c) {
	  putchar('\n');
	  c = i = 0;
	}
	if (*input_tr)
	  i += strlen(input_tr) + 3;
	if (i > max_x_ig - fieldlen_iv - 2)
	  {
	    putchar('\n');
	    c = 0;
	  }
	if (*input_tr && !pw_mode)
	  printf(" [%s]> " + (c == 0), input_tr);
        else
	  printf("> ");
	fflush(stdout);
	if (pw_mode)
	  kbd_echo_off();
	for (i = 0; ; i++)
	  {
	    c = lgetchar();
	    if (c == '\n' || c == '\r' || c == EOF)
	      {
		if (i == 0)
		  break;
	        input_tr[i < len_iv - 1 ? i : len_iv - 1] = 0;
	        break;
	      }
	    if (i == 0 && c == 033 /* escape */)
	      {
		c = lgetchar();
		if (c == '\n' || c == '\r' || c == EOF)
		  {
		    if (pw_mode)
		      kbd_reset();
		    return -1;
		  }
		if (c == 033 || c == 'x')
		  {
		    while (c = lgetchar(), c != '\n' && c != '\r' && c != EOF);
		    if (pw_mode) kbd_reset();
		    dia_handle_ctrlc();
		    goto ctrlc;
		  }
	      }
	    if ((unsigned char)c < ' ')
	      {
		i--;
		continue;
	      }
	    if (i < len_iv - 1)
	      input_tr[i] = c;
	  }
	if (pw_mode)
	  kbd_reset();
        rc_ii = 0;
      }
    else
    {
      disp_toggle_output (DISP_OFF);
      memset (&win_ri, 0, sizeof (window_t));
      win_ri.bg_color = colors_prg->input_win;
      win_ri.fg_color = colors_prg->msg_fg;
      width_ii = dia_win_open (&win_ri, txt_tv);

      memset (&tmp_win_ri, 0, sizeof (window_t));
      tmp_win_ri.x_left = win_ri.x_left + 1;
      tmp_win_ri.y_left = win_ri.y_right - 3;
      tmp_win_ri.x_right = win_ri.x_right - 1;
      tmp_win_ri.y_right = win_ri.y_right - 1;
      tmp_win_ri.style = STYLE_RAISED;
      tmp_win_ri.bg_color = win_ri.bg_color;
      tmp_win_ri.fg_color = win_ri.fg_color;
      win_open (&tmp_win_ri);
      win_clear (&tmp_win_ri);
      disp_flush_area (&win_ri);

      rc_ii = win_input (max_x_ig / 2 - fieldlen_iv / 2, win_ri.y_right - 2,
                         input_tr, len_iv, fieldlen_iv, pw_mode);

      win_close (&win_ri);
    }
    
#if defined(__s390__) || defined(__s390x__)
    /* HMC console is bugged and sends SPACE LF instead of LF when
       user presses enter on an empty line */
    if(strcmp(input_tr, " ") == 0) {
      *input_tr = 0;
    }
#endif
    if(strcmp(input_tr, "+++") == 0)	/* escape sequence */
      return -1;
    else
      return (rc_ii);
}


int dia_show_lines (char *head_tv, char *lines_atv [], int nr_lines_iv,
                     int   width_iv, int eof_iv)
    {
    window_t  file_win_ri;
    window_t  tmp_win_ri;
    button_t  button_ri;
    int       key_ii;
    int       offset_ii;
    int       i_ii;
    int       textlines_ii;
    int       need_redraw_ii;
    unsigned char *lines_ati [MAX_Y];
    int       line_length_ii;
    int       h_offset_ii;
    char      tmp_ti [MAX_X];
    int       visible_lines_ii;
    int       sb_len_ii;
    int       sb_start_ii;
    int       needflush_ii;
    char *s;


    if (!nr_lines_iv || width_iv < 8)
        return (-1);
    if (config.linemode)
      {
	int l;

	printf("\n%s\n\n", head_tv);
        for (i_ii = 0; i_ii < nr_lines_iv; i_ii++)
	  {
	    l = strlen(lines_atv[i_ii]);
	    while (l && lines_atv[i_ii][l - 1] == ' ')
	      l--;
	    printf("%.*s\n", l, lines_atv[i_ii]);
	  }
        return 0;
      }

    disp_toggle_output (DISP_OFF);
    line_length_ii = width_iv;
    if (width_iv > max_x_ig - 6)
        width_iv = max_x_ig - 6;
    if (nr_lines_iv > max_y_ig - 14)
        visible_lines_ii = max_y_ig - 14;
    else
        visible_lines_ii = nr_lines_iv;

    textlines_ii = util_format_txt (head_tv, lines_ati, width_iv - 4);

    memset (&file_win_ri, 0, sizeof (window_t));
    file_win_ri.x_left = max_x_ig / 2 - width_iv / 2;
    file_win_ri.y_left = max_y_ig / 2 - 4 - (textlines_ii + visible_lines_ii + 1) / 2;
    file_win_ri.x_right = max_x_ig / 2 + (width_iv + 1) / 2;
    file_win_ri.y_right = max_y_ig / 2 + 6 + (textlines_ii + visible_lines_ii) / 2;
    file_win_ri.head = 2 + textlines_ii;
    file_win_ri.foot = 3;
    file_win_ri.shadow = TRUE;
    file_win_ri.style = STYLE_RAISED;
    file_win_ri.bg_color = colors_prg->msg_win;
    file_win_ri.fg_color = colors_prg->msg_fg;
    file_win_ri.save_bg = TRUE;
    win_open (&file_win_ri);
    win_clear (&file_win_ri);

    memset (&tmp_win_ri, 0, sizeof (window_t));
    tmp_win_ri.x_left = file_win_ri.x_left + 1;
    tmp_win_ri.y_left = file_win_ri.y_left + 1;
    tmp_win_ri.x_right = file_win_ri.x_right - 1;
    tmp_win_ri.y_right = file_win_ri.y_left + 2 + textlines_ii;
    tmp_win_ri.style = STYLE_RAISED;
    tmp_win_ri.bg_color = file_win_ri.bg_color;
    tmp_win_ri.fg_color = file_win_ri.fg_color;
    win_open (&tmp_win_ri);

    for (i_ii = 1; i_ii <= textlines_ii; i_ii++)
        {
        win_print (&tmp_win_ri, 2, i_ii, lines_ati [i_ii - 1]);
        free (lines_ati [i_ii - 1]);
        }
    
    s = txt_get (TXT_OK);
    util_generate_button (&button_ri, s, utf8_strwidth(s));

    win_add_button (&file_win_ri, &button_ri,
                    width_iv / 2 - utf8_strwidth (button_ri.text) / 2 - 1,
                    utf8_strwidth (button_ri.text));

    memset (&tmp_win_ri, 0, sizeof (window_t));
    tmp_win_ri.x_left = file_win_ri.x_left + 1;
    tmp_win_ri.y_left = file_win_ri.y_left + file_win_ri.head + 2;
    tmp_win_ri.x_right = file_win_ri.x_right - 1;
    tmp_win_ri.y_right = file_win_ri.y_right - file_win_ri.foot - 2;
    tmp_win_ri.style = STYLE_SUNKEN;
    tmp_win_ri.bg_color = file_win_ri.bg_color;
    tmp_win_ri.fg_color = file_win_ri.fg_color;
    win_open (&tmp_win_ri);
    win_clear (&tmp_win_ri);


    if (eof_iv == TRUE)
        key_ii = KEY_END;
    else
        key_ii = KEY_HOME;

    offset_ii = 0;
    h_offset_ii = 0;
    needflush_ii = TRUE;
    kbd_clear_buffer ();
    do
        {
        need_redraw_ii = FALSE;

        switch (key_ii)
            {
            case KEY_HOME:
                offset_ii = 0;
                need_redraw_ii = TRUE;
                break;
            case KEY_END:
                if (visible_lines_ii < nr_lines_iv)
                    offset_ii = nr_lines_iv - visible_lines_ii;
                need_redraw_ii = TRUE;
                break;
            case KEY_DOWN:
                if (offset_ii + visible_lines_ii < nr_lines_iv)
                    {
                    offset_ii++;
                    need_redraw_ii = TRUE;
                    }
                break;
            case KEY_UP:
                if (offset_ii)
                    {
                    offset_ii--;
                    need_redraw_ii = TRUE;
                    }
                break;
            case KEY_PGDOWN:
                if (visible_lines_ii < nr_lines_iv)
                    {
                    offset_ii += visible_lines_ii;
                    if (offset_ii + visible_lines_ii >= nr_lines_iv)
                        offset_ii = nr_lines_iv - visible_lines_ii;
                    need_redraw_ii = TRUE;
                    }
                break;
            case KEY_PGUP:
                if (visible_lines_ii < nr_lines_iv)
                    {
                    offset_ii -= visible_lines_ii;
                    if (offset_ii < 0)
                        offset_ii = 0;
                    need_redraw_ii = TRUE;
                    }
                break;
            case KEY_RIGHT:
                if (h_offset_ii + width_iv < line_length_ii)
                    {
                    h_offset_ii++;
                    need_redraw_ii = TRUE;
                    }
                break;
            case KEY_LEFT:
                if (h_offset_ii)
                    {
                    h_offset_ii--;
                    need_redraw_ii = TRUE;
                    }
                break;
            default:
                break;
            }

        if (need_redraw_ii)
            {
            disp_set_color (tmp_win_ri.fg_color, tmp_win_ri.bg_color);
            for (i_ii = 0; i_ii < visible_lines_ii; i_ii++)
                if (i_ii + offset_ii < nr_lines_iv)
                    {
                    strncpy (tmp_ti, &lines_atv [offset_ii + i_ii][h_offset_ii],
                             width_iv - 4);
                    tmp_ti [width_iv - 5] = 0;
                    win_print (&tmp_win_ri, 2, i_ii + 1, tmp_ti);
                    }

            if (visible_lines_ii < nr_lines_iv)
                {
                disp_graph_on ();
                sb_len_ii = (visible_lines_ii * visible_lines_ii + 1) / nr_lines_iv;
                if (sb_len_ii >= visible_lines_ii - 1)
                    sb_len_ii--;
                sb_start_ii = (offset_ii * visible_lines_ii) / nr_lines_iv;
                if (offset_ii + visible_lines_ii < nr_lines_iv &&
                    sb_start_ii + sb_len_ii == visible_lines_ii - 1)
                    sb_start_ii--;
                if (offset_ii && !sb_start_ii)
                    sb_start_ii++;

                for (i_ii = 0; i_ii < visible_lines_ii; i_ii++)
                    {
                    disp_gotoxy (tmp_win_ri.x_right - 1, tmp_win_ri.y_left + 1 + i_ii);
                    if (i_ii >= sb_start_ii && i_ii <= sb_start_ii + sb_len_ii)
                        {
                        disp_set_color (tmp_win_ri.bg_color, tmp_win_ri.fg_color);
                        disp_write_char (' ');
                        }
                    else
                        {
                        disp_set_color (tmp_win_ri.fg_color, tmp_win_ri.bg_color);
                        disp_write_char (GRAPH_BLOCK);
                        }
                    }
                disp_graph_off ();
                }

            if (needflush_ii)
                {
                needflush_ii = FALSE;
                disp_flush_area (&file_win_ri);
                }

            fflush (stdout);
            }

        key_ii = kbd_getch (TRUE);
        if (key_ii == KEY_CTRL_C)
            dia_handle_ctrlc ();
        }
    while (key_ii != KEY_ENTER && key_ii != KEY_ESC);

    win_button_pressed (&button_ri, FALSE);
    win_close (&file_win_ri);
    return (0);
    }


int dia_show_file (char *head_tv, char *file_tv, int eof_iv)
    {
    FILE     *fd_pri;
    char     *lines_ati [MAX_LINES];
    int       nr_lines_ii;
    int       i_ii;
    char      buffer_ti [MAX_X];
    int       width_ii;
    int       rc_ii = 0;

    fd_pri = fopen (file_tv, "r");
    if (!fd_pri)
        return (-1);

    nr_lines_ii = 0;
    width_ii = 0;
    while (fgets (buffer_ti, MAX_X - 1, fd_pri) &&
           nr_lines_ii < MAX_LINES)
        {
        lines_ati [nr_lines_ii] = malloc (MAX_X);
        if (strrchr (buffer_ti, '\n'))
            *strrchr (buffer_ti, '\n') = 0;
        for (i_ii = 0; buffer_ti [i_ii]; i_ii++)
            if (buffer_ti [i_ii] == '\t' || buffer_ti [i_ii] == 0x0d)
                buffer_ti [i_ii] = ' ';
        strncpy (lines_ati [nr_lines_ii], buffer_ti, MAX_X - 1);
        lines_ati [nr_lines_ii][MAX_X - 1] = 0;
        if ((ssize_t) utf8_strwidth (lines_ati [nr_lines_ii]) + 6 > width_ii)
            width_ii = utf8_strwidth (lines_ati [nr_lines_ii]) + 6;
        util_fill_string (lines_ati [nr_lines_ii], MAX_X);
        nr_lines_ii++;
        }
    fclose (fd_pri);

    rc_ii = dia_show_lines (head_tv, lines_ati, nr_lines_ii, width_ii, eof_iv);

    for (i_ii = 0; i_ii < nr_lines_ii; i_ii++)
        free (lines_ati [i_ii]);

    return (rc_ii);
    }


void dia_info (window_t *win_prr, char *txt_tv, int type)
    {
    int        width_ii;
    window_t   tmp_win_ri;
    unsigned char *lines_ati [MAX_Y];
    int        nr_lines_ii;
    int        i_ii;


    if (config.linemode)
      {
	dia_printformatted(txt_tv, 0, max_x_ig - 1, 1);
	memset(win_prr, 0, sizeof *win_prr);
	return;
      }

    disp_toggle_output (DISP_OFF);
    width_ii = utf8_strwidth (txt_tv) + 6;
    if (width_ii < MIN_WIN_SIZE)
        width_ii = MIN_WIN_SIZE;

    if (width_ii > max_x_ig - 16)
        width_ii = max_x_ig - 16;

    nr_lines_ii = util_format_txt (txt_tv, lines_ati, width_ii - 4);

    memset (win_prr, 0, sizeof (window_t));
    win_prr->x_left = max_x_ig / 2 - width_ii / 2;
    win_prr->y_left = max_y_ig / 2 - nr_lines_ii / 2 - 1;
    win_prr->x_right = max_x_ig / 2 + width_ii / 2;
    win_prr->y_right = max_y_ig / 2 + (nr_lines_ii + 1) / 2 + 2;
    win_prr->shadow = TRUE;
    win_prr->style = STYLE_RAISED;
    win_prr->save_bg = TRUE;
    if(type == MSGTYPE_ERROR) {
      win_prr->bg_color = colors_prg->error_win;
      win_prr->fg_color = colors_prg->error_fg;
    }
    else {
      win_prr->bg_color = colors_prg->msg_win;
      win_prr->fg_color = colors_prg->msg_fg;
    }
    win_open (win_prr);
    win_clear (win_prr);

    memset (&tmp_win_ri, 0, sizeof (window_t));
    tmp_win_ri.x_left = win_prr->x_left + 1;
    tmp_win_ri.y_left = win_prr->y_left + 1;
    tmp_win_ri.x_right = win_prr->x_right - 1;
    tmp_win_ri.y_right = win_prr->y_right - 1;
    tmp_win_ri.style = STYLE_SUNKEN;
    tmp_win_ri.bg_color = win_prr->bg_color;
    tmp_win_ri.fg_color = win_prr->fg_color;
    win_open (&tmp_win_ri);
    win_clear (&tmp_win_ri);

    for (i_ii = 1; i_ii <= nr_lines_ii; i_ii++)
        {
        win_print (&tmp_win_ri, 2, i_ii, lines_ati [i_ii - 1]);
        free (lines_ati [i_ii - 1]);
        }

    disp_flush_area (win_prr);
    }

/*
 *
 *  local functions
 *
 */

static int dia_win_open (window_t *win_prr, char *txt_tv)
    {
    int        width_ii;
    window_t   tmp_win_ri;
    unsigned char *lines_ati [MAX_Y];
    int        nr_lines_ii;
    int        i_ii;


    width_ii = utf8_strwidth (txt_tv) + 6;
    if (width_ii < MIN_WIN_SIZE)
        width_ii = MIN_WIN_SIZE;

    if (width_ii > max_x_ig - 16)
        width_ii = max_x_ig - 16;

    nr_lines_ii = util_format_txt (txt_tv, lines_ati, width_ii - 4);

    win_prr->x_left = max_x_ig / 2 - width_ii / 2;
    win_prr->y_left = max_y_ig / 2 - 3 - nr_lines_ii / 2;
    win_prr->x_right = max_x_ig / 2 + width_ii / 2;
    win_prr->y_right = max_y_ig / 2 + 4 + (nr_lines_ii + 1) / 2;
    win_prr->foot = 3;
    win_prr->shadow = TRUE;
    win_prr->style = STYLE_RAISED;
    win_prr->save_bg = TRUE;
    win_open (win_prr);
    win_clear (win_prr);

    memset (&tmp_win_ri, 0, sizeof (window_t));
    tmp_win_ri.x_left = max_x_ig / 2 - width_ii / 2 + 1;
    tmp_win_ri.y_left = win_prr->y_left + 1;
    tmp_win_ri.x_right = max_x_ig / 2 + width_ii / 2 - 1;
    tmp_win_ri.y_right = win_prr->y_right - 5;
    tmp_win_ri.style = STYLE_SUNKEN;
    tmp_win_ri.bg_color = win_prr->bg_color;
    tmp_win_ri.fg_color = win_prr->fg_color;
    win_open (&tmp_win_ri);
    disp_set_color (tmp_win_ri.fg_color, tmp_win_ri.bg_color);

    for (i_ii = 1; i_ii <= nr_lines_ii; i_ii++)
        {
        win_print (&tmp_win_ri, 2, i_ii, lines_ati [i_ii - 1]);
        free (lines_ati [i_ii - 1]);
        }
    
    return (width_ii);
    }


void dia_handle_ctrlc (void)
{
    int i, j;
    static int is_in_ctrlc_is = FALSE;
    static char s[64] = { };
    char *t;
    file_t *f;

    if (is_in_ctrlc_is)
        return;

    is_in_ctrlc_is = TRUE;

    for (;;) {
      if (!config.linemode) {
	i = dia_message(txt_get(TXT_NO_CTRLC), MSGTYPE_ERROR);
      } else {
	static dia_item_t items[] = {
	  di_extras_info,
	  di_extras_change,
	  di_extras_shell,
	  di_extras_command,
	  di_extras_quit,
	  di_none
	};
	switch (dia_menu2("Linuxrc extras", 30, 0, items, di_extras_info)) {
	  case di_extras_info:     i = -71; break;
	  case di_extras_change:   i = -73; break;
	  case di_extras_shell:    i = -74; break;
	  case di_extras_command:  i = -69; break;
	  case di_extras_quit:     i = -42; break;
	  default:                 i = 0; break;
	}
      }
      if(i == -42) {
        util_umount_all();
        util_clear_downloads();
	lxrc_end();
	exit(0);
      }
      else if(i == -69) {
	i = dia_input("Run Command", s, sizeof s - 1, 35, 0);
	if(!i) {
	  if(strstr(s, "exec ") == s) {
	    t = s + 5;
	    while(isspace(*t)) t++;
	    kbd_end(0);	/* restore terminal settings */
	    j = execlp(t, t, NULL);
	    kbd_init(0);
	  }
	  else {
	    j = system(s);
	  }
	  if(j) fprintf(stderr, "  exit code: %d\n", WIFEXITED(j) ? WEXITSTATUS(j) : -1);
	}
      }
      else if(i == -70) {
	/* force segfault */
	*((unsigned char *) NULL) = 7;
      }
      else if(i == -71) {
	util_status_info(0);
      }
      else if(i == -73) {
	i = dia_input(txt_get(TXT_CHANGE_CONFIG), s, sizeof s - 1, 35, 0);
	if(!i) {
	  f = file_parse_buffer(s, kf_cfg + kf_cmd + kf_cmd_early);
	  file_do_info(f, kf_cfg + kf_cmd + kf_cmd_early);
	  file_free_file(f);
	}
      }
      else if(i == -74) {
        kbd_end(0);
        if(config.win) {
          disp_cursor_on();
        }
        if(!config.linemode) {
          printf("\033c");
          if(config.utf8) printf("\033%%G");
          fflush(stdout);
        }

        // system(util_check_exist("/lbin/lsh") ? "/lbin/lsh 2>&1" : "/bin/sh 2>&1");
        system("PS1='\\w # ' /bin/bash 2>&1");

        kbd_init(0);
        if(config.win) {
          disp_cursor_off();
          if(!config.linemode) disp_restore_screen();
        }

      } else {
	break;
      }

      if (!config.linemode)
	break;
    }
    is_in_ctrlc_is = FALSE;
}


char *dia_get_text(dia_item_t di)
{
  int i;
  char *s = "";

  for(i = 0; (unsigned) i < sizeof dia_texts / sizeof *dia_texts; i++) {
    if(dia_texts[i].item == di) {
      s = dia_texts[i].text ?: txt_get(dia_texts[i].text_id);
      break;
    }
  }

  return s;
}


/*
 * returns selected menu item, or di_none (ESC pressed)
 */
dia_item_t dia_menu2(char *title, int width, int (*func)(dia_item_t), dia_item_t *items, dia_item_t default_item)
{
  int item_cnt, default_idx, i;
  dia_item_t *it, di;
  item_t *item_list;
  char *s;

  if(!config.win) util_disp_init();

  for(item_cnt = 0, it = items; *it != di_none; it++) {
    if(*it != di_skip) item_cnt++;
  }

  if(!item_cnt) return di_none;

  item_list = calloc(item_cnt, sizeof *item_list);

  util_create_items(item_list, item_cnt, width);

  default_idx = 1;
  for(i = 0, it = items; *it != di_none; it++) {
    if(*it != di_skip) {
      if(*it == default_item) default_idx = i + 1;
      s = dia_get_text(*it);
      if(*s == '#') {
        s++;
        item_list[i].tag.head = 1;
      }
      utf8_strwcpy(item_list[i].text, s, width);
      item_list[i].di = *it;
      item_list[i].func = (int (*)(int)) func;
      util_center_text(item_list[i].text, width);
      i++;
    }
  }

  i = dia_menu(title, item_list, item_cnt, default_idx);

  if(i > 0 && i <= item_cnt) {
    di = item_list[i - 1].di;
  }
  else {
    di = di_none;
  }

  util_free_items(item_list, item_cnt);

  return di;
}


/*
 * returns selected item (1 based), or 0 (ESC pressed)
 */
int dia_list(char *title, int width, int (*func)(int), char **items, int default_item, dia_align_t align)
{
  int item_cnt, i;
  char **it, *s;
  item_t *item_list;

  for(item_cnt = 0, it = items; *it; it++) item_cnt++;

  if(!item_cnt) return 0;

  if(!config.win) util_disp_init();

  item_list = calloc(item_cnt, sizeof *item_list);

  util_create_items(item_list, item_cnt, width);

  if(default_item < 1 || default_item > item_cnt) default_item = 1;

  for(i = 0, it = items; *it; it++, i++) {
    s = *it;
    if(*s == '#') {
      s++;
      item_list[i].tag.head = 1;
    }
    utf8_strwcpy(item_list[i].text, s, width - 1);
    item_list[i].func = func;
    if(align == align_center) {
      util_center_text(item_list[i].text, width);
    }
    else {
      util_fill_string(item_list[i].text, width);
    }
  }

  i = dia_menu(title, item_list, item_cnt, default_item);

  util_free_items(item_list, item_cnt);

  return i;
}

int dia_show_lines2(char *head, slist_t *sl0, int width)
{
  int cnt = 0, i, j;
  slist_t *sl;
  char **lines, *s;

  for(sl = sl0; sl; sl = sl->next) cnt++;

  if(!cnt) return 0;

  lines = malloc(cnt * sizeof *lines);

  for(i = 0, sl = sl0; sl; sl = sl->next, i++) {
    s = malloc(width + 1);
    strncpy(s, sl->key, width);
    s[width] = 0;
    util_fill_string(s, width - 4);
    lines[i] = s;
  }

  j = dia_show_lines(head, lines, cnt, width, FALSE);

  for(i = 0; i < cnt; i++) free(lines[i]);
  free(lines);

  return j;
}

int dia_input2(char *txt, char **input, int fieldlen, int pw_mode)
{
  char buf[256];
  int i;

  if(!input) return 0;

  if(!config.win) util_disp_init();

  *buf = 0;
  if(*input) strncpy(buf, *input, sizeof buf - 1);
  buf[sizeof buf - 1] = 0;

  i = dia_input(txt, buf, sizeof buf - 1, fieldlen, pw_mode);

  if(*input) {
    free(*input);
    *input = NULL;
  }
  if(*buf) *input = strdup(buf);

  return i;
}

int dia_input2_chopspace(char* txt, char** input, int fieldlen, int pw_mode)
{
  int retval;
  char* final;	/* stripped string */
  char* oinput;	/* original pointer to input string */
  
  retval = dia_input2(txt, input, fieldlen, pw_mode);
  oinput = *input;
  
  /* null pointer or empty string */
  if(!*input || !**input) return retval;
  
  while(**input && isspace(**input)) /* skip leading whitespace */
    (*input)++;
  while(isspace(*(*input+strlen(*input)-1))) /* overwrite trailing whitespace */
    *(*input+strlen(*input)-1) = 0;
  
  /* copy result and discard original string */
  final = strdup(*input);
  free(oinput);
  *input = final;
  
  return retval;
}


int lgetchar()
{
  int i;

  while((i = getchar()) == '\r' && config.listen);

  return i;
}

