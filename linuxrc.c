/*
 *
 * linuxrc.c     Load modules and rootimage to ramdisk
 *
 * Copyright (c) 1996-2000  Hubert Mantel, SuSE GmbH  (mantel@suse.de)
 *
 */

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mount.h>
#include <sys/reboot.h>
#include <fcntl.h>
#include <dirent.h>
#include <string.h>
#include <signal.h>
#include <stdarg.h>

#include "global.h"
#include "text.h"
#include "info.h"
#include "util.h"
#include "display.h"
#include "keyboard.h"
#include "dialog.h"
#include "window.h"
#include "module.h"
#include "net.h"
#if WITH_PCMCIA
#include "pcmcia.h"
#endif
#include "install.h"
#include "settings.h"
#include "file.h"
#include "linuxrc.h"
#include "auto2.h"

#include "multiple_info.h"

extern int  insmod_main        (int argc, char **argv);
extern int  rmmod_main         (int argc, char **argv);
extern void cardmgr_main       (int argc, char **argv);
extern int  probe_main         (int argc, char **argv);
extern int  loadkeys_main      (unsigned int argc, char *argv[]);
extern int  setfont_main       (int argc, char **argv);
extern int  portmap_main       (int argc, char **argv);

static void lxrc_main_menu     (void);
static void lxrc_do_shell      (int argc, char **argv, char **env);
static void lxrc_init          (void);
static int  lxrc_main_cb       (int what_iv);
static void lxrc_memcheck      (void);
static void lxrc_check_console (void);
static void lxrc_set_bdflush   (int percent_iv);
static int is_rpc_prog         (pid_t pid);

static pid_t  lxrc_mempid_rm;
static int    lxrc_sig11_im = FALSE;


int main (int argc, char **argv, char **env)
    {
    int   rc_ii = 0;
    char *progname_pci;


    progname_pci = strrchr (argv [0], '/');
    if (progname_pci)
        progname_pci++;
    else
        progname_pci = argv [0];

    if (!strcmp (progname_pci, "sh"))
        lxrc_do_shell (argc, argv, env);
    else if (!strcmp (progname_pci, "insmod"))
        rc_ii = insmod_main (argc, argv);
    else if (!strcmp (progname_pci, "rmmod"))
        rc_ii = rmmod_main (argc, argv);
    else if (!strcmp (progname_pci, "loadkeys"))
        rc_ii = loadkeys_main ((unsigned int) argc, argv);
#if WITH_PCMCIA
    else if (!strcmp (progname_pci, "cardmgr"))
        cardmgr_main (argc, argv);
    else if (!strcmp (progname_pci, "probe"))
        rc_ii = probe_main (argc, argv);
#endif
    else if (!strcmp (progname_pci, "setfont"))
        rc_ii = setfont_main (argc, argv);
    else if (!strcmp (progname_pci, "portmap"))
        rc_ii = portmap_main (argc, argv);
    else if (!strcmp (progname_pci, "nothing"))
        rc_ii = 0;
    else
        {
        lxrc_init ();
        if (auto_ig)
            rc_ii = inst_auto_install ();
        else if (demo_ig)
            rc_ii = inst_start_demo ();
#ifdef USE_LIBHD
        else if (auto2_ig) {
            rc_ii = inst_auto2_install ();
            deb_msg ("done inst_auto2_install()");
            deb_int (rc_ii);
        }
#endif
        else
             rc_ii = 99;

        if (rc_ii)
// was:       if (!(auto_ig || demo_ig || auto2_ig) || rc_ii)
            {
            util_disp_init();
            lxrc_main_menu ();
            }

        lxrc_end ();
        }

    return (rc_ii);
    }


int my_syslog (int type_iv, char *buffer_pci, ...)
    {
    va_list  args_ri;

    va_start (args_ri, buffer_pci);
    vfprintf (stderr, buffer_pci, args_ri);
    va_end (args_ri);
    fprintf (stderr, "\n");
    return (0);
    }


int my_logmessage (char *buffer_pci, ...)
    {
    va_list  args_ri;

    va_start (args_ri, buffer_pci);
    vfprintf (stderr, buffer_pci, args_ri);
    va_end (args_ri);
    fprintf (stderr, "\n");
    return (0);
    }


void lxrc_reboot (void)
    {
    if (auto_ig || auto2_ig || dia_yesno (txt_get (TXT_ASK_REBOOT), 1) == YES)
        reboot (RB_AUTOBOOT);
    }


void lxrc_end (void)
    {
    deb_msg("lxrc_end()");
    kill (lxrc_mempid_rm, 9);
    lxrc_killall (1);
    printf ("\033[9;15]");	/* screen saver on */
/*    reboot (RB_ENABLE_CAD); */
    mod_free_modules ();
    (void) util_umount (mountpoint_tg);
    lxrc_set_modprobe ("/sbin/modprobe");
    lxrc_set_bdflush (40);
    (void) util_umount ("/proc/bus/usb");
    (void) util_umount ("/proc");
    disp_cursor_on ();
    kbd_end ();
    disp_end ();
    }


char *lxrc_prog_name(pid_t pid)
{
  FILE *f;
  char proc_status[64];
  static char buf[64];

  *buf = 0;
  sprintf(proc_status, "/proc/%u/status", (unsigned) pid);
  if((f = fopen(proc_status, "r"))) {
    if(fscanf(f, "Name: %30s", buf) != 1) *buf = 0;
    fclose(f);
  }

  return buf;
}

/*
 * Check if pid is either portmap or rpciod.
 *
 */
int is_rpc_prog(pid_t pid)
{
  if(!strcmp(lxrc_prog_name(pid), "portmap")) return 1;
  if(!strcmp(lxrc_prog_name(pid), "rpciod")) return 1;

  return 0;
}


/*
 * really_all = 0: kill everything except rpc progs
 * really_all = 1: kill really everything
 *
 */
void lxrc_killall (int really_all_iv)
    {
    pid_t          mypid_ri;
    struct dirent *process_pri;
    DIR           *directory_ri;
    pid_t          pid_ri;


    if (testing_ig)
        return;

    mypid_ri = getpid ();
    directory_ri = opendir ("/proc");
    if (!directory_ri)
        return;

    process_pri = readdir (directory_ri);
    while (process_pri)
        {
        pid_ri = (pid_t) atoi (process_pri->d_name);
        if (pid_ri > mypid_ri && pid_ri != lxrc_mempid_rm &&
            (really_all_iv || !is_rpc_prog (pid_ri)))
            {
            fprintf (stderr, "Killing %s (%d)\n", lxrc_prog_name (pid_ri),
                                                  pid_ri);
            kill (pid_ri, 9);
            }

        process_pri = readdir (directory_ri);
        }

    (void) closedir (directory_ri);
    }


/*
 *
 * Local functions
 *
 */

static void lxrc_catch_signal (int sig_iv)
    {
    if (sig_iv)
        {
        fprintf (stderr, "Caught signal %d!\n", sig_iv);
        sleep (10);
        }

    if (sig_iv == SIGBUS || sig_iv == SIGSEGV)
        lxrc_sig11_im = TRUE;

    signal (SIGHUP,  lxrc_catch_signal);
    signal (SIGBUS,  lxrc_catch_signal);
    signal (SIGINT,  lxrc_catch_signal);
    signal (SIGTERM, lxrc_catch_signal);
    signal (SIGSEGV, lxrc_catch_signal);
    signal (SIGPIPE, lxrc_catch_signal);
    }


static void lxrc_init (void)
    {
    int    i_ii;
    char  *language_pci;
    char  *linuxrc_pci;
    int    rc_ii;

    printf(">>> SuSE installation program v" LXRC_VERSION " (c) 1996-2000 SuSE GmbH <<<\n");
    fflush(stdout);

    if (!testing_ig && getpid () > 19)
        {
        printf ("Seems we are on a running system; activating testmode...\n");
        testing_ig = TRUE;
        strcpy (console_tg, "/dev/tty");
        }

    if (txt_init ())
        {
        printf ("Corrupted texts!\n");
        exit (-1);
        }

    siginterrupt (SIGALRM, 1);
    siginterrupt (SIGHUP,  1);
    siginterrupt (SIGBUS,  1);
    siginterrupt (SIGINT,  1);
    siginterrupt (SIGTERM, 1);
    siginterrupt (SIGSEGV, 1);
    siginterrupt (SIGPIPE, 1);
    lxrc_catch_signal (0);
/*    reboot (RB_DISABLE_CAD); */

    language_pci = getenv ("lang");
    if (language_pci)
        {
        if (!strncasecmp (language_pci, "english", 7))
            language_ig = LANG_ENGLISH;
        else if (!strncasecmp (language_pci, "german", 6))
            language_ig = LANG_GERMAN;
        else if (!strncasecmp (language_pci, "italian", 7))
            language_ig = LANG_ITALIAN;
        else if (!strncasecmp (language_pci, "french", 6))
            language_ig = LANG_FRENCH;
        }

    linuxrc_pci = getenv ("linuxrc");
    if (linuxrc_pci)
        {
        char *s = malloc (strlen (linuxrc_pci) + 3);
        if (s)
           {
           *s = 0;
           strcat (strcat (strcat (s, ","), linuxrc_pci), ",");

           if (strstr (s, ",auto,"))
               auto_ig = TRUE;

#ifdef USE_LIBHD
           if (strstr (s, ",auto2,"))
               auto2_ig = TRUE;
           if (strstr (s, ",noauto2,"))
               auto2_ig = FALSE;
#endif

           if (strstr (s, ",demo,"))
               {
               demo_ig = TRUE;
               action_ig |= ACT_DEMO | ACT_DEMO_LANG_SEL | ACT_LOAD_DISK;
               }

           if (strstr (s, ",eval,"))
               {
               demo_ig = TRUE;
               action_ig |= ACT_DEMO | ACT_LOAD_DISK;
               }

           if (strstr (s, ",reboot,"))
               reboot_ig = TRUE;

           if (strstr (s, ",yast1,"))
               yast_version_ig = 1;

           if (strstr (s, ",yast2,"))
               yast_version_ig = 2;

           if (strstr (s, ",loadnet,"))
               action_ig |= ACT_LOAD_NET;

           if (strstr (s, ",loaddisk,"))
               action_ig |= ACT_LOAD_DISK;

           if (strstr (s, ",french,"))
               language_ig = LANG_FRENCH;

           free (s);
           }
        }

    freopen ("/dev/tty3", "a", stderr);
    (void) mount (0, "/proc", "proc", 0, 0);
    fprintf (stderr, "Remount of / ");
    rc_ii = mount (0, "/", 0, MS_MGC_VAL | MS_REMOUNT, 0);
    fprintf (stderr, rc_ii ? "failure\n" : "success\n");

    /* Check for special case with aborted installation */

    if (util_check_exist ("/.bin"))
        {
        unlink ("/bin");
        rename ("/.bin", "/bin");
        }

    lxrc_set_modprobe ("/etc/nothing");
    lxrc_set_bdflush (5);

    lxrc_check_console ();
    freopen (console_tg, "r", stdin);
    freopen (console_tg, "a", stdout);

    kbd_init ();
    util_redirect_kmsg ();
    disp_init ();

#ifdef USE_LIBHD
    auto2_chk_expert ();

    deb_int(text_mode_ig);
    deb_int(yast2_update_ig);
    deb_int(auto2_ig);
    deb_int(yast_version_ig);
    deb_int(guru_ig);

    printf ("\033[9;0]");	/* screen saver off */

    if (!auto2_ig)
#endif
        {
        for (i_ii = 1; i_ii < max_y_ig; i_ii++) printf ("\n");
        disp_cursor_off ();
        }

    info_init ();

    if (memory_ig < MEM_LIMIT_YAST2)
        yast_version_ig = 1;

    if (memory_ig > (yast_version_ig == 1 ? MEM_LIMIT1_RAMDISK : MEM_LIMIT2_RAMDISK))
        force_ri_ig = TRUE;

    if ((guru_ig & 8)) force_ri_ig = FALSE;

    lxrc_memcheck ();

    // #### drop this later!!!
    // if (yast_version_ig == 2) strcpy(rootimage_tg, "/suse/images/yast2");
    // deb_str(rootimage_tg);
    mod_init ();

#ifdef USE_LIBHD
    if(auto2_ig) {
      if(auto2_init()) {
        auto2_ig = TRUE;
        auto2_init_settings();
      } else {
        deb_msg("Automatic setup not possible.");
        util_manual_mode();
        yast_version_ig = 0;
        disp_cursor_off();
        disp_set_display(1);
        if(found_suse_cd_ig) {
          char s[200];
          sprintf(s, txt_get(TXT_INSERT_CD), 1);
          dia_message(s, MSGTYPE_INFO);
        }
#if defined(__i386__)
        else {
          dia_message("Could not find the SuSE Linux installation CD.\n\nActivating manual setup program.\n", MSGTYPE_INFO);
        }
#endif
      }
    }
#endif

    util_print_banner ();


    /* note: for auto2, file_read_info() is called inside auto2_init() */
    if (auto_ig) 
        {
	rename_info_file ();
        file_read_info ();
        }
	
    if (!auto2_ig && language_ig == LANG_UNDEF)
        set_choose_language ();

    util_print_banner ();

    deb_int(color_ig);

    if (!color_ig)	/* if it hasn't already been set */
        {
        set_choose_display ();
        util_print_banner ();
        }

    if (!serial_ig)
        set_choose_keytable (0);

    util_update_kernellog ();

    (void) net_setup_localhost ();
    }


static int lxrc_main_cb (int what_iv)
    {
    int    rc_ii = 1;


    switch (what_iv)
        {
        case 1:
            rc_ii = set_settings ();
            if (rc_ii == 1 || rc_ii == 2)
                rc_ii = 0;
            else if (rc_ii == 0)
                rc_ii = 1;
            break;
        case 2:
            info_menu ();
            break;
        case 3:
            mod_menu ();
            break;
        case 4:
            rc_ii = inst_menu ();
            break;
        case 5:
            lxrc_reboot ();
        default:
            break;
        }

    return (rc_ii);
    }


static void lxrc_main_menu (void)
    {
    int    width_ii = 40;
    item_t items_ari [5];
    int    i_ii;
    int    choice_ii;
    int    nr_items_ii = sizeof (items_ari) / sizeof (items_ari [0]);

    util_manual_mode();

    util_create_items (items_ari, nr_items_ii, width_ii);

    do
        {
        strcpy (items_ari [0].text, txt_get (TXT_SETTINGS));
        strcpy (items_ari [1].text, txt_get (TXT_MENU_INFO));
        strcpy (items_ari [2].text, txt_get (TXT_MENU_MODULES));
        strcpy (items_ari [3].text, txt_get (TXT_MENU_START));
        strcpy (items_ari [4].text, txt_get (TXT_END_REBOOT));
        for (i_ii = 0; i_ii < nr_items_ii; i_ii++)
            {
            util_center_text (items_ari [i_ii].text, width_ii);
            items_ari [i_ii].func = lxrc_main_cb;
            }

        choice_ii = dia_menu (txt_get (TXT_HDR_MAIN), items_ari,
                              nr_items_ii, 4);
        if (!choice_ii)
            if (dia_message (txt_get (TXT_NO_LXRC_END), MSGTYPE_INFO) == -42)
                choice_ii = 42;

        if (choice_ii == 1)
            {
            util_print_banner ();
            choice_ii = 0;
            }
        }
    while (!choice_ii);

    util_free_items (items_ari, nr_items_ii);
    }


static void lxrc_do_shell (int argc, char **argv, char **env)
    {
    char  command_ati [10][100];
    char  progname_ti [100];
    int   i_ii = 0;
    int   j_ii = 0;
    int   k_ii = 0;
    char *arguments_apci [10];

    freopen ("/dev/tty3", "a", stdout);
    freopen ("/dev/tty3", "a", stderr);
    printf ("Executing: »%s«\n", argv [2]);

    while (argv [2][i_ii] == ' ')
        i_ii++;

    do
        {
        if (argv [2][i_ii] == '\"')
            {
            i_ii++;
            while (argv [2][i_ii] && argv [2][i_ii] != '\"')
                command_ati [j_ii][k_ii++] = argv [2][i_ii++];

            if (argv [2][i_ii])
                i_ii++;
            }
        else
            command_ati [j_ii][k_ii++] = argv [2][i_ii++];

        if (argv [2][i_ii] == ' ' || argv [2][i_ii] == 0)
            {
            command_ati [j_ii][k_ii] = 0;
            arguments_apci [j_ii] = command_ati [j_ii];
            j_ii++;
            k_ii = 0;

            while (argv [2][i_ii] == ' ')
                i_ii++;
            }
        }
    while (argv [2][i_ii]);

    arguments_apci [j_ii] = 0;

    sprintf (progname_ti, "/bin/%s", arguments_apci [0]);
    execve (progname_ti, arguments_apci, env);
    exit (0);
    }


static void lxrc_memcheck (void)
    {
    int   nr_pages_ii;
    int   max_pages_ii;
    char *data_pci;
    int   i_ii;


    lxrc_mempid_rm = fork ();
    if (lxrc_mempid_rm)
        return;

    lxrc_catch_signal (0);

    if (memory_ig < 8000000)
        max_pages_ii = 40;
    else
        max_pages_ii = 100;

    while (1)
        {
        nr_pages_ii = 1 + (int) ((double) max_pages_ii * rand () / (RAND_MAX + 1.0));
        data_pci = malloc (nr_pages_ii * 4096);
        if (data_pci)
            {
            for (i_ii = 0; i_ii < nr_pages_ii && !lxrc_sig11_im; i_ii++)
                {
                data_pci [i_ii * 4096] = 42;
                usleep (10000);
                }
            lxrc_sig11_im = FALSE;
            free (data_pci);
            }
        sleep (1);
        }
    }


void lxrc_set_modprobe (char *program_tv)
    {
    FILE  *proc_file_pri;

    proc_file_pri = fopen ("/proc/sys/kernel/modprobe", "w");
    if (proc_file_pri)
        {
        fprintf (proc_file_pri, "%s\n", program_tv);
        fclose (proc_file_pri);
        }
    }


/* Check if we start linuxrc on a serial console. On Intel and
   Alpha, we look if the "console" parameter was used on the
   commandline. On SPARC, we use the result from hardwareprobing. */
static void lxrc_check_console (void)
    {
#if !defined(__sparc__) && !defined(__PPC__)
    FILE  *fd_pri;
    char   buffer_ti [300];
    char  *tmp_pci = (char *) 0;
    char  *found_pci = (char *) 0;

    fd_pri = fopen ("/proc/cmdline", "r");
    if (!fd_pri)
        return;

    if (fgets (buffer_ti, sizeof (buffer_ti) - 1, fd_pri))
        {
        tmp_pci = strstr (buffer_ti, "console");
        while (tmp_pci)
            {
            found_pci = tmp_pci;
            tmp_pci = strstr (found_pci + 1, "console");
            }
        }

    if (found_pci)
        {
        while (*found_pci && *found_pci != '=')
            found_pci++;

        found_pci++;

	/* Find the whole console= entry for the install.inf file */
        tmp_pci = found_pci;
        while (*tmp_pci && *tmp_pci != '\t' && *tmp_pci != ' ' &&
	                   *tmp_pci != '\n')
            tmp_pci++;

        *tmp_pci = 0;
	strncpy (console_parms_tg, tmp_pci, sizeof (console_parms_tg));
	console_parms_tg[sizeof(console_parms_tg) - 1] = '\0';

	/* Now search only for the device name */
	tmp_pci = found_pci;
        while (*tmp_pci && *tmp_pci != ',' && *tmp_pci != '\t' &&
                           *tmp_pci != ' ' && *tmp_pci != '\n')
            tmp_pci++;

        *tmp_pci = 0;
        sprintf (console_tg, "/dev/%s", found_pci);
        if (!strncmp (found_pci, "ttyS", 4))
            serial_ig = TRUE;

        fprintf (stderr, "Console: %s, serial=%d\n", console_tg, serial_ig);
        }

    fclose (fd_pri);
#else
#ifdef USE_LIBHD
    char *cp;

    cp = auto2_serial_console ();
    if (cp && strlen (cp) > 0)
      {
	serial_ig = TRUE;

#ifdef __PPC__
        yast_version_ig = 1;
#endif

	strcpy (console_tg, cp);

        fprintf (stderr, "Console: %s, serial=%d\n", console_tg, serial_ig);
      }
#endif
#endif
    }


static void lxrc_set_bdflush (int percent_iv)
    {
    FILE  *fd_pri;


    fd_pri = fopen ("/proc/sys/vm/bdflush", "w");
    if (!fd_pri)
        return;

    fprintf (fd_pri, "%d", percent_iv);
    fclose (fd_pri);
    }
