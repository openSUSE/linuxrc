#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/errno.h>

#define FLAG_ADDP   0x40
#define FLAG_ADD0   0x80
#define FLAG_REPEAT 0x20

static void
mkdevs_perror(name, str)
char *name;
char *str;
{
  int olderrno = errno;
  write(2, name, strlen(name));
  write(2, ": ", 2);
  errno = olderrno;
  perror(str);
}

static void
mkdevs_fixup(name, mode, uid, gid)
char *name;
int mode;
int uid;
int gid;
{
  if (lchown(name, uid, gid))
    mkdevs_perror(name, "lchown");
  if (mode != -1 && chmod(name, (mode_t)(mode & 07777)))
    mkdevs_perror(name, "chmod");
}


int
mkdevs(p, doit)
unsigned char *p;
int doit;
{
  int o, l;
  char name[256];
  char lname[256];
  char *np;
  int rdev;
  int uid, gid;
  int mode;
  int cnt, cnt2;
  int flg;
  int blkoff;
  int blkcnt;
  int doflags;
  int created = 0;
  static unsigned short modetab[4] = { 06600 ^ 0x000, 06660 ^ 0x100, 06666 ^ 0x200, 06640 ^ 0x300};

  strcpy(name, "/dev");
  for (;;)
    {
      o = *p++;
      if (o & 128)
	{
          l = *p++;
	  o &= 127;
	}
      else
	{
	  l = o >> 4 & 7;
	  o &= 15;
	}
      if (o == 0 && l == 0)
	break;
      o += sizeof "/dev" - 1;
      memcpy(name + o, p, l);
      p += l;
      l += o;
      name[l] = 0;
      mode = *p++ << 8;
      if ((mode & 0xc00) == 0xc00)
	mode ^= modetab[mode >> 8 & 3];
      else
        mode |= *p++;
      uid = *p++;
      if (uid == 255)
        {
          uid = *p++;
          gid = *p++;
        }
      else
        {
          gid = uid & 15;
          uid >>= 4;
	}
      if (S_ISDIR(mode))
	{
	  created++;
	  if (!doit)
	    continue;
	  if (mkdir(name, (mode_t)mode) && errno != EEXIST)
	    mkdevs_perror(name, "mkdir");
	  mkdevs_fixup(name, mode, uid, gid);
	  continue;
	}
      if (S_ISLNK(mode))
	{
	  l = *p++;
	  memcpy(lname, p, l);
	  p += l;
	  lname[l] = 0;
	  created++;
	  if (!doit)
	    continue;
	  if (symlink(lname, name) && errno != EEXIST)
	    mkdevs_perror(name, "symlink");
	  mkdevs_fixup(name, -1, uid, gid);
	  continue;
	}
      rdev = *p++ << 8;
      rdev |= *p++;
      cnt = *p++;
      flg = blkcnt = *p++;
      blkcnt &= 0x1f;
      if (blkcnt == 0x1f)
	blkcnt = *p++;
      blkoff = blkcnt ? *p++ : 0;
      blkoff -= cnt;
      for (;;)
	{
	  np = name + l;
	  doflags = 1;
	  cnt2 = cnt;
	  for (;;)
	    {
	      *np = 0;
	      created++;
	      if (doit)
		{
		  if (mknod(name, (mode_t)mode, (dev_t)rdev) && errno != EEXIST)
		    mkdevs_perror(name, "mknod");
		  mkdevs_fixup(name, mode, uid, gid);
		}
	      if (doflags)
		{
		  doflags = 0;
		  if (flg & FLAG_ADDP)
		    *np++ = 'p';
		  if (flg & FLAG_ADD0)
		    *np++ = '0';
		  if (flg & FLAG_REPEAT)
		    continue;
		}
	      if (cnt2-- == 0)
		break;
	      rdev++;
	      np--;
	      if (*np == '9')
		{
		  if (np[-1] >= '0' && np[-1] <= '9')
		    np[-1]++;
		  else
		    *np++ = '1';
		  *np = '0' - 1;
		}
	      np[0]++;
	      np++;
	    }
	  rdev += blkoff;
	  if (blkcnt-- == 0)
	    break;
	  np = name + l - 1;
	  if (*np == '9')
	    {
	      if (np[-1] >= '0' && np[-1] <= '9')
		np[-1]++;
	      else
		*np++ = '1';
	      *np = '0' - 1;
	    }
	  np[0]++;
	  l = np - name + 1;
	}
    }
  return created;
}

int mkdevs_main(int argc, char **argv)
{
  int l;
  unsigned char *d;

  l = getchar();
  l = l << 8 | getchar();
  d = malloc(l);
  fread(d, l, 1, stdin);
  fprintf(stderr, "%d inodes created\n", mkdevs(d, 1));

  return 0;
}
