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
 * 1999-02-22 Arkadiusz Miśkiewicz <misiek@pld.ORG.PL>
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

#include "global.h"
#include "util.h"
#include "linux_fs.h"
#include "fstype.h"

/*
 * Most file system types can be recognized by a `magic' number
 * in the superblock.  Note that the order of the tests is
 * significant: by coincidence a filesystem can have the
 * magic numbers for several file system types simultaneously.
 * For example, the romfs magic lives in the 1st sector;
 * xiafs does not touch the 1st sector and has its magic in
 * the 2nd sector; ext2 does not touch the first two sectors.
 */

static inline unsigned short swapped(unsigned short a)
{
  return (a>>8) | (a<<8);
}


static inline int assemble4le(unsigned char *p)
{
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

/*
 * udf magic - I find that trying to mount garbage as an udf fs
 * causes a very large kernel delay, almost killing the machine.
 *  So, we do not try udf unless there is positive evidence that it
 *  might work. Try iso9660 first, it is much more likely.
 *  Strings below taken from ECMA 167.
 */
static int may_be_udf(const char *id)
{
  char *udf_magic[] = {
    "BEA01", "BOOT2", "CD001", "CDW02", "NSR02", "NSR03", "TEA01"
  };
  char **m;

  for(m = udf_magic; m - udf_magic < sizeof udf_magic / sizeof *udf_magic; m++) {
    if(!strncmp(*m, id, 5)) return 1;
  }

  return 0;
}


static int may_be_swap(const char *s)
{
  return !strncmp(s - 10, "SWAP-SPACE", 10) || !strncmp(s - 10, "SWAPSPACE2", 10);
}


static int is_reiserfs_magic_string(struct reiserfs_super_block * rs)
{
  return
    !strncmp(rs->s_magic, REISERFS_SUPER_MAGIC_STRING, strlen(REISERFS_SUPER_MAGIC_STRING)) ||
    !strncmp(rs->s_magic, REISER2FS_SUPER_MAGIC_STRING, strlen(REISER2FS_SUPER_MAGIC_STRING));
}


char *fstype(const char *device)
{
  int fd;
  char *type = NULL;
  struct stat64 statbuf;

  /*
   * opening and reading an arbitrary unknown path can have
   * undesired side effects - first check that `device' refers
   * to a block device
   */
  if(
    stat64(device, &statbuf) ||
    !(S_ISBLK(statbuf.st_mode) || S_ISREG(statbuf.st_mode))
  ) {
    return 0;
  }

  fd = open(device, O_RDONLY | O_LARGEFILE);
  /* try harder */
  if(fd < 0 && errno == ENOMEDIUM) fd = open(device, O_RDONLY | O_LARGEFILE);
  if(fd < 0) {
    perror_debug((char *) device);
    return 0;
  }

  /*
   * do seeks and reads in disk order, otherwise a very short
   * partition may cause a failure because of read error
   */

  if(!type) {
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

    /* block 0 */
    if(
      lseek(fd, 0, SEEK_SET) == 0 &&
      read(fd, &xsb, sizeof xsb) == sizeof xsb
    ) {
      if(xiafsmagic(xsb.xiasb) == _XIAFS_SUPER_MAGIC) {
        type = "xiafs";
      }
      else if(!strncmp(xsb.romfs_magic, "-rom1fs-", 8)) {
        type = "romfs";
      }
      else if(!strncmp(xsb.xfsb.s_magic, XFS_SUPER_MAGIC, 4)) {
        type = "xfs";
      }
      else if(!strncmp(xsb.qnx4fs_magic+4, "QNX4FS", 6)) {
        type = "qnx4";
      }
      else if(xsb.bfs_magic == 0x1badface) {
        type = "bfs";
      }
      else if(!strncmp(xsb.ntfssb.s_magic, NTFS_SUPER_MAGIC, sizeof xsb.ntfssb.s_magic)) {
        type = "ntfs";
      }
      else if(
        cramfsmagic(xsb.cramfssb) == CRAMFS_SUPER_MAGIC ||
        cramfsmagic(xsb.cramfssb) == CRAMFS_SUPER_MAGIC_BIG
      ) {
        type = "cramfs";
      }
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
      ) {
        type = "vfat";
      }
    }
  }

  if(!type) {
    char buf[6];

    if(
      lseek(fd, 0, SEEK_SET) == 0 &&
      read(fd, buf, sizeof buf) == sizeof buf
    ) {
      if(!memcmp(buf, "070701", 6) || !memcmp(buf, "\xc7\x71", 2)) type = "cpio";
      else if(!memcmp(buf, "hsqs", 4) || !memcmp(buf, "sqsh", 4)) type = "squashfs";
      else if(!memcmp(buf, "\xed\xab\xee\xdb", 4) && buf[4] >= 3) type = "rpm";
    }
  }

  if(!type) {	/* sector 1 */
    struct sysv_super_block svsb;

    if(
      lseek(fd, 512 , SEEK_SET) == 512 &&
      read(fd, &svsb, sizeof svsb) == sizeof svsb &&
      sysvmagic(svsb) == SYSV_SUPER_MAGIC
    ) {
      type = "sysv";
    }
  }

  if(!type) {	/* block 1 */
    union {
      struct minix_super_block ms;
      struct ext_super_block es;
      struct ext2_super_block e2s;
      struct vxfs_super_block vs;
    } sb;

    if(
      lseek(fd, 1024, SEEK_SET) == 1024 &&
      read(fd, &sb, sizeof sb) == sizeof sb
    ) {
      /*
       * ext2 has magic in little-endian on disk, so "swapped" is
       * superfluous; however, there have existed strange byteswapped
       * PPC ext2 systems
       */
      if(
        ext2magic(sb.e2s) == EXT2_SUPER_MAGIC ||
        ext2magic(sb.e2s) == EXT2_PRE_02B_MAGIC ||
        ext2magic(sb.e2s) == swapped(EXT2_SUPER_MAGIC)
      ) {
        type = "ext2";

        if(
          (assemble4le(sb.e2s.s_feature_compat) & EXT3_FEATURE_COMPAT_HAS_JOURNAL) &&
          assemble4le(sb.e2s.s_journal_inum) != 0
        ) {
          type = "ext3";

          if((assemble4le(sb.e2s.s_feature_incompat) & EXT4_FEATURE_INCOMPAT_EXTENTS)) {
            type = "ext4";
          }
        }
      }
      else if(
        minixmagic(sb.ms) == MINIX_SUPER_MAGIC ||
        minixmagic(sb.ms) == MINIX_SUPER_MAGIC2 ||
        minixmagic(sb.ms) == MINIX2_SUPER_MAGIC ||
        minixmagic(sb.ms) == MINIX2_SUPER_MAGIC2
      ) {
        type = "minix";
      }
      else if(extmagic(sb.es) == EXT_SUPER_MAGIC) {
        type = "ext";
      }
      else if(vxfsmagic(sb.vs) == VXFS_SUPER_MAGIC) {
        type = "vxfs";
      }
    }
  }

  if(!type) {	/* block 1 */
    struct hfs_super_block hfssb;

    /*
     * also check if block size is equal to 512 bytes,
     * since the hfs driver currently only has support
     * for block sizes of 512 bytes long, and to be
     * more accurate (sb magic is only a short int)
     */
    if(
      lseek(fd, 0x400, SEEK_SET) == 0x400 &&
      read(fd, &hfssb, sizeof hfssb) == sizeof hfssb &&
      (
        (hfsmagic(hfssb) == HFS_SUPER_MAGIC && hfsblksize(hfssb) == 0x20000) ||
        (swapped(hfsmagic(hfssb)) == HFS_SUPER_MAGIC && hfsblksize(hfssb) == 0x200)
      )
    ) {
      type = "hfs";
    }
  }

  if(!type) {	/* block 8 */
    struct ufs_super_block ufssb;

    if(
      lseek(fd, 8192, SEEK_SET) == 8192 &&
      read(fd, &ufssb, sizeof ufssb) == sizeof ufssb &&
      ufsmagic(ufssb) == UFS_SUPER_MAGIC	/* also test swapped version? */
    ) {
      type = "ufs";
    }
  }

  if(!type) {	/* block 8 */
    struct reiserfs_super_block reiserfssb;

    if(
      lseek(fd, REISERFS_OLD_DISK_OFFSET_IN_BYTES, SEEK_SET) == REISERFS_OLD_DISK_OFFSET_IN_BYTES &&
      read(fd, &reiserfssb, sizeof(reiserfssb)) == sizeof(reiserfssb) &&
      is_reiserfs_magic_string(&reiserfssb)
    ) {
      type = "reiserfs";
    }
  }

  if(!type) {	/* block 8 */
    struct hpfs_super_block hpfssb;

    if(
      lseek(fd, 0x2000, SEEK_SET) == 0x2000 &&
      read(fd, &hpfssb, sizeof hpfssb) == sizeof hpfssb &&
      hpfsmagic(hpfssb) == HPFS_SUPER_MAGIC
    ) {
      type = "hpfs";
    }
  }

  if(!type) {	/* block 32 */
    struct jfs_super_block jfssb;

    if(
      lseek(fd, JFS_SUPER1_OFF, SEEK_SET) == JFS_SUPER1_OFF &&
      read(fd, &jfssb, sizeof jfssb) == sizeof jfssb &&
      !strncmp(jfssb.s_magic, JFS_MAGIC, 4)
    ) {
      type = "jfs";
    }
  }

  if(!type) {	/* block 32 */
    union {
      struct iso_volume_descriptor iso;
      struct hs_volume_descriptor hs;
    } isosb;

    if(
      lseek(fd, 0x8000, SEEK_SET) == 0x8000 &&
      read(fd, &isosb, sizeof isosb) == sizeof isosb
    ) {
      if(
        !strncmp(isosb.iso.id, ISO_STANDARD_ID, sizeof(isosb.iso.id)) ||
        !strncmp(isosb.hs.id, HS_STANDARD_ID, sizeof(isosb.hs.id))
      ) {
        type = "iso9660";
      }
      else if(may_be_udf(isosb.iso.id)) {
        type = "udf";
      }
    }
  }

  if(!type) {	/* block 64 */
    struct reiserfs_super_block reiserfssb;

    if(
      lseek(fd, REISERFS_DISK_OFFSET_IN_BYTES, SEEK_SET) == REISERFS_DISK_OFFSET_IN_BYTES &&
      read(fd, &reiserfssb, sizeof reiserfssb) == sizeof reiserfssb &&
      is_reiserfs_magic_string(&reiserfssb)
    ) {
      type = "reiserfs";
    }
  }

  if(!type) {
    char buf[8];

    if(
      lseek(fd, 0x10040, SEEK_SET) == 0x10040 &&
      read(fd, buf, sizeof buf) == sizeof buf &&
      !memcmp(buf, "_BHRfS_M", sizeof buf)
    ) {
      type = "btrfs";
    }
  }

  if(!type) {
    char buf[6];

    if(
      lseek(fd, 0x101, SEEK_SET) == 0x101 &&
      read(fd, buf, sizeof buf) == sizeof buf &&
      !memcmp(buf, "ustar", 6 /* with \0 */)
    ) {
      type = "tar";
    }
  }

  if(!type) {
    /*
     * perhaps the user tries to mount the swap space
     * on a new disk; warn her before she does mke2fs on it
     */
    int pagesize = getpagesize();
    int rd;
    char buf[pagesize + 32768];

    rd = pagesize;
    if(rd < 8192) rd = 8192;
    if(rd > sizeof buf) rd = sizeof buf;
    if(
      lseek(fd, 0, SEEK_SET) == 0 &&
      read(fd, buf, rd) == rd &&
      (
        may_be_swap(buf + pagesize) ||
        may_be_swap(buf + 4096) ||
        may_be_swap(buf + 8192)
      )
    ) {
      type = "swap";
    }
  }

  close(fd);

  return type;
}

