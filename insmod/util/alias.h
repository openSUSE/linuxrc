/*
 * This file is split out from config.c for easier editing
 */

/*
 * tbpath and tbtype are used to build the complete set of paths for finding
 * modules, but only when we search for individual directories, they are not
 * used for [boot] and [toplevel] searches.
 */
static char *tbpath[] =
{
	"/lib/modules",
	NULL			/* marks the end of the list! */
};

char *tbtype[] =
{
	"kernel",		/* as of 2.3.14 this must be first */
	"fs",
	"net",
	"scsi",
	"block",
	"cdrom",
	"ipv4",
	"ipv6",
	"sound",
	"fc4",
	"video",
	"misc",
	"pcmcia",
	"atm",
	"usb",
	"ide",
	"ieee1394",
	"mtd",
	NULL			/* marks the end of the list! */
};

/*
 * This is the list of pre-defined aliases.
 * Each entry can be overridden by an entry in /etc/modules.conf
 */
char *aliaslist[] =
{
	"binfmt-0000 off",
	"binfmt-204 binfmt_aout",
	"binfmt-263 binfmt_aout",
	"binfmt-264 binfmt_aout",
	"binfmt-267 binfmt_aout",
	"binfmt-387 binfmt_aout",
	"binfmt-332 iBCS",
	"binfmt--310 binfmt_java",

	"block-major-1 rd",
	"block-major-2 floppy",
	"block-major-3 ide-probe-mod",
	"block-major-7 loop",
	"block-major-8 sd_mod",
	"block-major-9 md", /* For modular RAID */
	"block-major-11 sr_mod",
	"block-major-13 xd",
	"block-major-15 cdu31a",
	"block-major-16 gscd",
	"block-major-17 optcd",
	"block-major-18 sjcd",
	"block-major-20 mcdx",
	"block-major-22 ide-probe-mod",
	"block-major-23 mcd",
	"block-major-24 sonycd535",
	"block-major-25 sbpcd",
	"block-major-26 sbpcd",
	"block-major-27 sbpcd",
	"block-major-29 aztcd",
	"block-major-32 cm206",
	"block-major-33 ide-probe-mod",
	"block-major-34 ide-probe-mod",
	"block-major-37 ide-tape",
	"block-major-44 ftl",		/* from David Woodhouse <dwmw2@infradead.org> */
	"block-major-46 pcd",
	"block-major-47 pf",
	"block-major-56 ide-probe-mod",
	"block-major-57 ide-probe-mod",
	"block-major-58 lvm-mod",
	"block-major-88 ide-probe-mod",
	"block-major-89 ide-probe-mod",
	"block-major-90 ide-probe-mod",
	"block-major-91 ide-probe-mod",
	"block-major-93 nftl",		/* from David Woodhouse <dwmw2@infradead.org> */
	"block-major-97 pg",

#if !defined(__s390__) && !defined(__s390x__)
	"char-major-4 serial",
#else
 	"char-major-4 off",
#endif
	"char-major-5 serial",
	"char-major-6 lp",
	"char-major-9 st",
	"char-major-10 off",		/* was: mouse, was: misc */
	"char-major-10-0 busmouse",	/* /dev/logibm Logitech bus mouse */
	"char-major-10-1 off",		/* /dev/psaux PS/2-style mouse port */
	"char-major-10-2 msbusmouse",	/* /dev/inportbm Microsoft Inport bus mouse */
	"char-major-10-3 atixlmouse",	/* /dev/atibm ATI XL bus mouse */
					/* /dev/jbm J-mouse */
					/* /dev/amigamouse Amiga mouse (68k/Amiga) */
					/* /dev/atarimouse Atari mouse */
					/* /dev/sunmouse Sun mouse */
					/* /dev/beep Fancy beep device */
					/* /dev/modreq Kernel module load request */
	"char-major-10-130 wdt",	/* /dev/watchdog Watchdog timer port */
	"char-major-10-131 wdt",	/* /dev/temperature Machine internal temperature */
					/* /dev/hwtrap Hardware fault trap */
					/* /dev/exttrp External device trap */
	"char-major-10-135 rtc",	/* /dev/rtc Real time clock */
	"char-major-10-139 openprom",	/* /dev/openprom Linux/Sparc interface */
	"char-major-10-144 nvram",	/* from Tigran Aivazian <tigran@sco.COM> */
	"char-major-10-157 applicom",	/* from David Woodhouse <dwmw2@infradead.org> */
	"char-major-10-175 agpgart",    /* /dev/agpgart GART AGP mapping access */
	"char-major-10-184 microcode",	/* Tigran Aivazian <tigran@veritas.com> */

	"char-major-14 soundcore",
	"char-major-19 cyclades",
	"char-major-20 cyclades",
	"char-major-21 sg",
	"char-major-22 pcxx", /* ?? */
	"char-major-23 pcxx", /* ?? */
	"char-major-27 ftape",
	"char-major-34 scc",
	"char-major-35 tclmidi",
	"char-major-36 netlink",
	"char-major-37 ide-tape",
	"char-major-48 riscom8",
	"char-major-49 riscom8",
	"char-major-57 esp",
	"char-major-58 esp",
	"char-major-63 kdebug",
	"char-major-90 mtdchar",	/* from David Woodhouse <dwmw2@infradead.org> */
	"char-major-96 pt",
	"char-major-99 ppdev",
	"char-major-107 3dfx", /* from Tigran Aivazian <tigran@sco.COM> */
	"char-major-108 ppp_generic",
	"char-major-109 lvm-mod",
	"char-major-161 ircomm-tty",
	"char-major-200 vxspec",
	"char-major-206 osst",	/* OnStream SCSI tape */

	"dos msdos",
	"dummy0 dummy",
	"dummy1 dummy",
	"eth0 off",
	"iso9660 isofs",
	"md-personality-1 linear",
	"md-personality-2 raid0",
        "md-personality-3 raid1",
        "md-personality-4 raid5",

	"net-pf-1 unix",	/* PF_UNIX	1  Unix domain sockets */
	"net-pf-2 ipv4",	/* PF_INET	2  Internet IP Protocol */
	"net-pf-3 off",		/* PF_AX25	3  Amateur Radio AX.25 */
	"net-pf-4 ipx",		/* PF_IPX	4  Novell IPX */
	"net-pf-5 appletalk",	/* PF_APPLETALK	5  Appletalk DDP */
	"net-pf-6 off",		/* PF_NETROM	6  Amateur radio NetROM */
				/* PF_BRIDGE	7  Multiprotocol bridge */
				/* PF_AAL5	8  Reserved for Werner's ATM */
				/* PF_X25	9  Reserved for X.25 project */
	"net-pf-10 off",	/* PF_INET6	10 IP version 6 */

	/* next two from <dairiki@matthews.dairiki.org>  Thanks! */
	"net-pf-17 af_packet",
	"net-pf-19 off",	/* acorn econet */

	"netalias-2 ip_alias",
	"plip0 plip",
	"plip1 plip",
	"tunl0 ipip",
	"cipcb0 cipcb",
	"cipcb1 cipcb",
	"cipcb2 cipcb",
	"cipcb3 cipcb",
#if	defined(__s390__) || defined(__s390x__)
	"ctc0 ctc",
	"ctc1 ctc",
	"ctc2 ctc",
	"iucv0 netiucv",
	"iucv1 netiucv",
#endif
	"ppp0 ppp",
	"ppp1 ppp",
	"scsi_hostadapter off",	/* if not in config file */
	"slip0 slip",
	"slip1 slip",
	"tty-ldisc-1 slip",
	"tty-ldisc-3 ppp_async",
	"tty-ldisc-11 irtty",
	"tty-ldisc-14 ppp_synctty",
	"ppp-compress-18 ppp_mppe",
	"ppp-compress-21 bsd_comp",
	"ppp-compress-24 ppp_deflate",
	"ppp-compress-26 ppp_deflate",

#ifndef __sparc__
	"parport_lowlevel parport_pc",
#else
        "parport_lowlevel parport_ax",
#endif

	"usbdevfs usbcore",

	NULL			/* marks the end of the list! */
};

/*
 * This is the list of pre-defined options.
 * Each entry can be overridden by an entry in /etc/modules.conf
 */
char *optlist[] =
{
	"dummy0 -o dummy0",
	"dummy1 -o dummy1",
	"sb io=0x220 irq=7 dma=1 dma16=5 mpu_io=0x330",
	NULL			/* marks the end of the list! */
};

/*
 * This is the list of pre-defined "above"s,
 * used for pull-in of additional modules
 * Each entry can be overridden by an entry in /etc/modules.conf
 */
char *above[] =
{
	NULL			/* marks the end of the list! */
};

/*
 * This is the list of pre-defined "below"s,
 * used for push-in of additional modules
 * Each entry can be overridden by an entry in /etc/modules.conf
 */
char *below[] =
{
	NULL			/* marks the end of the list! */
};

/*
 * This is the list of pre-defined "prune"s,
 * used to exclude paths from scan of /lib/modules.
 * /etc/modules.conf can add entries but not remove them.
 */
char *prune[] =
{
	"modules.dep",
	"modules.generic_string",
	"modules.pcimap",
	"modules.isapnpmap",
	"modules.usbmap",
	"modules.parportmap",
	"modules.ieee1394map",
	"modules.pnpbiosmap",
	"System.map",
	".config",
	"build",		/* symlink to source tree */
	"vmlinux",
	"vmlinuz",
	"bzImage",
	"zImage",
	".rhkmvtag",		/* wish RedHat had told me before they did this */
	NULL			/* marks the end of the list! */
};
