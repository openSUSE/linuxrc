/*
 * Thu Jul 14 07:32:40 1994: faith@cs.unc.edu added changes from Adam
 * J. Richter (adam@adam.yggdrasil.com) so that /proc/filesystems is used
 * if no -t option is given.  I modified his patches so that, if
 * /proc/filesystems is not available, the behavior of mount is the same as
 * it was previously.
 *
 * Wed Feb 8 09:23:18 1995: Mike Grupenhoff <kashmir@umiacs.UMD.EDU> added
 * a probe of the superblock for the type before /proc/filesystems is
 * checked.
 *
 * Fri Apr  5 01:13:33 1996: quinlan@bucknell.edu, fixed up iso9660 autodetect
 *
 * Wed Nov  11 11:33:55 1998: K.Garloff@ping.de, try /etc/filesystems before
 * /proc/filesystems
 * [This was mainly in order to specify vfat before fat; these days we often
 *  detect *fat and then assume vfat, so perhaps /etc/filesystems isnt
 *  so useful anymore.]
 *
 * 1999-02-22 Arkadiusz Mi¶kiewicz <misiek@pld.ORG.PL>
 * added Native Language Support
 *
 * 2000-12-01 Sepp Wijnands <mrrazz@garbage-coderz.net>
 * added probes for cramfs, hfs, hpfs.
 *
 * 2001-10-26 Tim Launchbury
 * added sysv magic.
 *
 * aeb - many changes.
 *
 */

#define _GNU_SOURCE	/* stat64 */

#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include "linux_fs.h"
#include "fstype.h"

#include "global.h"

#define ALL_TYPES

#define SIZE(a) (sizeof(a)/sizeof(a[0]))

/* Most file system types can be recognized by a `magic' number
   in the superblock.  Note that the order of the tests is
   significant: by coincidence a filesystem can have the
   magic numbers for several file system types simultaneously.
   For example, the romfs magic lives in the 1st sector;
   xiafs does not touch the 1st sector and has its magic in
   the 2nd sector; ext2 does not touch the first two sectors. */

static inline unsigned short
swapped(unsigned short a) {
     return (a>>8) | (a<<8);
}

static inline int
assemble4le(unsigned char *p) {
	return (p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24));
}

/*
    char *guess_fstype_from_superblock(const char *device);

    Probes the device and attempts to determine the type of filesystem
    contained within.

    Original routine by <jmorriso@bogomips.ww.ubc.ca>; made into a function
    for mount(8) by Mike Grupenhoff <kashmir@umiacs.umd.edu>.
    Corrected the test for xiafs - aeb
    Read the superblock only once - aeb
    Added a very weak heuristic for vfat - aeb
    Added iso9660, minix-v2, romfs, qnx4, udf, vxfs, swap - aeb
    Added a test for high sierra (iso9660) - quinlan@bucknell.edu
    Added ufs from a patch by jj. But maybe there are several types of ufs?
    Added ntfs from a patch by Richard Russon.
    Added xfs - 2000-03-21 Martin K. Petersen <mkp@linuxcare.com>
    Added cramfs, hfs, hpfs, adfs - Sepp Wijnands <mrrazz@garbage-coderz.net>
    Added ext3 - Andrew Morton
    Added jfs - Christoph Hellwig
    Added sysv - Tim Launchbury
*/

#if 0
static char
*magic_known[] = {
	"bfs", "cramfs", "ext", "ext2", "ext3",
	"hfs", "hpfs", "iso9660", "jfs", "minix", "ntfs",
	"qnx4", "reiserfs", "romfs", "swap", "sysv", "udf", "ufs",
	"vxfs", "xfs", "xiafs"
};
#endif

#ifdef ALL_TYPES
/* udf magic - I find that trying to mount garbage as an udf fs
   causes a very large kernel delay, almost killing the machine.
   So, we do not try udf unless there is positive evidence that it
   might work. Try iso9660 first, it is much more likely.
   Strings below taken from ECMA 167. */
static char
*udf_magic[] = { "BEA01", "BOOT2", "CD001", "CDW02", "NSR02",
		 "NSR03", "TEA01" };

static int
may_be_udf(const char *id) {
    char **m;

    for (m = udf_magic; m - udf_magic < (ssize_t) SIZE(udf_magic); m++)
       if (!strncmp(*m, id, 5))
	  return 1;
    return 0;
}
#endif

static int
may_be_swap(const char *s) {
	return (strncmp(s-10, "SWAP-SPACE", 10) == 0 ||
		strncmp(s-10, "SWAPSPACE2", 10) == 0);
}

static int is_reiserfs_magic_string (struct reiserfs_super_block * rs)
{
    return (!strncmp (rs->s_magic, REISERFS_SUPER_MAGIC_STRING, 
		      strlen ( REISERFS_SUPER_MAGIC_STRING)) ||
	    !strncmp (rs->s_magic, REISER2FS_SUPER_MAGIC_STRING, 
		      strlen ( REISER2FS_SUPER_MAGIC_STRING)));
}

char *
fstype(const char *device) {
    int fd;
    char *type = NULL;
    union {
	struct minix_super_block ms;
	struct ext_super_block es;
	struct ext2_super_block e2s;
	struct vxfs_super_block vs;
    } sb;			/* stuff at 1024 */
    union {
	struct xiafs_super_block xiasb;
	char romfs_magic[8];
	char qnx4fs_magic[10];	/* ignore first 4 bytes */
	long bfs_magic;
	struct ntfs_super_block ntfssb;
	struct fat_super_block fatsb;
	struct xfs_super_block xfsb;
	struct cramfs_super_block cramfssb;
	unsigned char data[512];
    } xsb;
#ifdef ALL_TYPES
    struct ufs_super_block ufssb;
#endif
    union {
	struct iso_volume_descriptor iso;
	struct hs_volume_descriptor hs;
    } isosb;
    struct reiserfs_super_block reiserfssb;	/* block 64 or 8 */
    struct jfs_super_block jfssb;		/* block 32 */
#ifdef ALL_TYPES
    struct hfs_super_block hfssb;
    struct hpfs_super_block hpfssb;
    struct sysv_super_block svsb;
#endif
    struct stat64 statbuf;

    /* opening and reading an arbitrary unknown path can have
       undesired side effects - first check that `device' refers
       to a block device */
    if (stat64 (device, &statbuf) || !(S_ISBLK(statbuf.st_mode) || S_ISREG(statbuf.st_mode)))
      return 0;

    fd = open(device, O_RDONLY | O_LARGEFILE);
    /* try harder */
    if (fd < 0 && errno == ENOMEDIUM) fd = open(device, O_RDONLY | O_LARGEFILE);
    if (fd < 0) {
      if(config.debug) perror(device);
      return 0;
    }

    /* do seeks and reads in disk order, otherwise a very short
       partition may cause a failure because of read error */

    if (!type) {
	 /* block 0 */
	 if (lseek(fd, 0, SEEK_SET) != 0
	     || read(fd, (char *) &xsb, sizeof(xsb)) != sizeof(xsb))
	      goto io_error;

	 if (xiafsmagic(xsb.xiasb) == _XIAFS_SUPER_MAGIC)
	      type = "xiafs";
#ifdef ALL_TYPES
	 else if(!strncmp(xsb.romfs_magic, "-rom1fs-", 8))
	      type = "romfs";
#endif
	 else if(!strncmp(xsb.xfsb.s_magic, XFS_SUPER_MAGIC, 4))
	      type = "xfs";
#ifdef ALL_TYPES
	 else if(!strncmp(xsb.qnx4fs_magic+4, "QNX4FS", 6))
	      type = "qnx4";
	 else if(xsb.bfs_magic == 0x1badface)
	      type = "bfs";
	 else if(!strncmp(xsb.ntfssb.s_magic, NTFS_SUPER_MAGIC,
			  sizeof(xsb.ntfssb.s_magic)))
	      type = "ntfs";
#endif
	 else if(cramfsmagic(xsb.cramfssb) == CRAMFS_SUPER_MAGIC ||
	 	 cramfsmagic(xsb.cramfssb) == CRAMFS_SUPER_MAGIC_BIG)
	      type = "cramfs";
	 else if(
                xsb.data[0x1fe] == 0x55 &&
                xsb.data[0x1ff] == 0xaa &&
                xsb.data[0x0b] == 0 &&	/* bytes per sector, bits 0-7 */
                (
                  (	/* FAT12/16 */
                    xsb.data[0x26] == 0x29 && (
                      !strncmp(xsb.fatsb.s_fs, "FAT12   ", 8) ||
                      !strncmp(xsb.fatsb.s_fs, "FAT16   ", 8)
                    )
                  ) ||
                  (	/* FAT32 */
                    xsb.data[0x42] == 0x29 &&
                    !strncmp(xsb.fatsb.s_fs2, "FAT32   ", 8)
                  )
                )
              )
              type = "vfat";
    }

    if(!type) {
      char buf[7];

      if(
        lseek(fd, 0, SEEK_SET) != 0 ||
        read(fd, buf, sizeof buf - 1) != sizeof buf - 1
      ) goto io_error;

      buf[sizeof buf - 1] = 0;
      if(!strcmp(buf, "070701")) type = "cpio";
      else if(!memcmp(buf, "hsqs", 4) || !memcmp(buf, "sqsh", 4)) type = "squashfs";
      else if(!memcmp(buf, "\xed\xab\xee\xdb", 4) && buf[4] >= 3) type = "rpm";
    }

#ifdef ALL_TYPES
    if (!type) {
	    /* sector 1 */
	    if (lseek(fd, 512 , SEEK_SET) != 512
		|| read(fd, (char *) &svsb, sizeof(svsb)) != sizeof(svsb))
		    goto io_error;
	    if (sysvmagic(svsb) == SYSV_SUPER_MAGIC )
		    type = "sysv";
    }
#endif

    if (!type) {
        unsigned ntype = 0;
        char *type_str[] = { "ext2", "ext3", "ext4" };

	/* block 1 */
	if (lseek(fd, 1024, SEEK_SET) != 1024 ||
	    read(fd, (char *) &sb, sizeof(sb)) != sizeof(sb))
		goto io_error;

	/* ext2 has magic in little-endian on disk, so "swapped" is
	   superfluous; however, there have existed strange byteswapped
	   PPC ext2 systems */
	if (ext2magic(sb.e2s) == EXT2_SUPER_MAGIC ||
	    ext2magic(sb.e2s) == EXT2_PRE_02B_MAGIC ||
	    ext2magic(sb.e2s) == swapped(EXT2_SUPER_MAGIC)) {
		ntype = 2;

	     /* maybe even ext3? */
	     if ((assemble4le(sb.e2s.s_feature_compat)
		  & EXT3_FEATURE_COMPAT_HAS_JOURNAL) &&
		 assemble4le(sb.e2s.s_journal_inum) != 0)
		     ntype = 3;

	     /* maybe ext4 */
	     if((assemble4le(sb.e2s.s_feature_incompat)
		  & EXT4_FEATURE_INCOMPAT_EXTENTS) && ntype == 3)
		     ntype = 4;

             if(ntype) type = type_str[ntype - 2];
	}

	else if (minixmagic(sb.ms) == MINIX_SUPER_MAGIC ||
		 minixmagic(sb.ms) == MINIX_SUPER_MAGIC2 ||
		 minixmagic(sb.ms) == MINIX2_SUPER_MAGIC ||
		 minixmagic(sb.ms) == MINIX2_SUPER_MAGIC2)
		type = "minix";

#ifdef ALL_TYPES
	else if (extmagic(sb.es) == EXT_SUPER_MAGIC)
		type = "ext";
#endif

	else if (vxfsmagic(sb.vs) == VXFS_SUPER_MAGIC)
		type = "vxfs";
    }

#ifdef ALL_TYPES
    if (!type) {
	/* block 1 */
        if (lseek(fd, 0x400, SEEK_SET) != 0x400
            || read(fd, (char *) &hfssb, sizeof(hfssb)) != sizeof(hfssb))
             goto io_error;

        /* also check if block size is equal to 512 bytes,
           since the hfs driver currently only has support
           for block sizes of 512 bytes long, and to be
           more accurate (sb magic is only a short int) */
        if ((hfsmagic(hfssb) == HFS_SUPER_MAGIC &&
	     hfsblksize(hfssb) == 0x20000) ||
            (swapped(hfsmagic(hfssb)) == HFS_SUPER_MAGIC &&
             hfsblksize(hfssb) == 0x200))
             type = "hfs";
    }
#endif

#ifdef ALL_TYPES
    if (!type) {
	 /* block 8 */
	 if (lseek(fd, 8192, SEEK_SET) != 8192
	     || read(fd, (char *) &ufssb, sizeof(ufssb)) != sizeof(ufssb))
	      goto io_error;

	 if (ufsmagic(ufssb) == UFS_SUPER_MAGIC) /* also test swapped version? */
	      type = "ufs";
    }
#endif

    if (!type) {
	/* block 8 */
	if (lseek(fd, REISERFS_OLD_DISK_OFFSET_IN_BYTES, SEEK_SET) !=
				REISERFS_OLD_DISK_OFFSET_IN_BYTES
	    || read(fd, (char *) &reiserfssb, sizeof(reiserfssb)) !=
		sizeof(reiserfssb))
	    goto io_error;
	if (is_reiserfs_magic_string(&reiserfssb))
	    type = "reiserfs";
    }

#ifdef ALL_TYPES
    if (!type) {
	/* block 8 */
        if (lseek(fd, 0x2000, SEEK_SET) != 0x2000
            || read(fd, (char *) &hpfssb, sizeof(hpfssb)) != sizeof(hpfssb))
             goto io_error;

        if (hpfsmagic(hpfssb) == HPFS_SUPER_MAGIC)
             type = "hpfs";
    }
#endif

    if (!type) {
	 /* block 32 */
	 if (lseek(fd, JFS_SUPER1_OFF, SEEK_SET) != JFS_SUPER1_OFF
	     || read(fd, (char *) &jfssb, sizeof(jfssb)) != sizeof(jfssb))
	      goto io_error;
	 if (!strncmp(jfssb.s_magic, JFS_MAGIC, 4))
	      type = "jfs";
    }

    if (!type) {
	 /* block 32 */
	 if (lseek(fd, 0x8000, SEEK_SET) != 0x8000
	     || read(fd, (char *) &isosb, sizeof(isosb)) != sizeof(isosb))
	      goto io_error;

	 if(strncmp(isosb.iso.id, ISO_STANDARD_ID, sizeof(isosb.iso.id)) == 0
	    || strncmp(isosb.hs.id, HS_STANDARD_ID, sizeof(isosb.hs.id)) == 0)
	      type = "iso9660";
#ifdef ALL_TYPES
	 else if (may_be_udf(isosb.iso.id))
	      type = "udf";
#endif
    }

    if (!type) {
	/* block 64 */
	if (lseek(fd, REISERFS_DISK_OFFSET_IN_BYTES, SEEK_SET) !=
		REISERFS_DISK_OFFSET_IN_BYTES
	    || read(fd, (char *) &reiserfssb, sizeof(reiserfssb)) !=
		sizeof(reiserfssb))
	    goto io_error;
	if (is_reiserfs_magic_string(&reiserfssb))
	    type = "reiserfs";
    }

    if (!type) {
	    /* perhaps the user tries to mount the swap space
	       on a new disk; warn her before she does mke2fs on it */
	    int pagesize = getpagesize();
	    int rd;
	    char buf[pagesize + 32768];

	    rd = pagesize;
	    if (rd < 8192)
		    rd = 8192;
	    if (rd > (int) sizeof(buf))
		    rd = sizeof(buf);
	    if (lseek(fd, 0, SEEK_SET) != 0
		|| read(fd, buf, rd) != rd)
		    goto io_error;
	    if (may_be_swap(buf+pagesize) ||
		may_be_swap(buf+4096) || may_be_swap(buf+8192))
		    type = "swap";
    }

    close (fd);
    return(type);

io_error:
//    if (errno)
//	 perror(device);
//    else
//	 fprintf(stderr, "fstype: error while guessing filesystem type\n");
    close(fd);
    return 0;
}

