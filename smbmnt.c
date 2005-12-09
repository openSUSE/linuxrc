/*
 *  smbmnt.c
 *
 *  Copyright (C) 1995-1998 by Paal-Kr. Engstad and Volker Lendecke
 *  extensively modified by Tridge
 *
 */

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/types.h>

#include <asm/types.h>
#include <asm/posix_types.h>
#include <linux/smb.h>
#include <linux/smb_mount.h>
#include <asm/unistd.h>

#include <sys/stat.h>
#include <sys/mount.h>
#include <sys/utsname.h>

#define VERSION "2.2.3a"
#define MAXPATHLEN 256

static uid_t mount_uid;
static gid_t mount_gid;
static int mount_ro;
static unsigned mount_fmask;
static unsigned mount_dmask;
static int user_mount;
static char *options;

static void
help(void)
{
        printf("\n");
        printf("Usage: smbmnt mount-point [options]\n");
	printf("Version %s\n\n",VERSION);
        printf("-s share       share name on server\n"
               "-r             mount read-only\n"
               "-u uid         mount as uid\n"
               "-g gid         mount as gid\n"
               "-f mask        permission mask for files\n"
               "-d mask        permission mask for directories\n"
               "-o options     name=value, list of options\n"
               "-h             print this help text\n");
}

static int
parse_args(int argc, char *argv[], struct smb_mount_data *data, char **share)
{
        int opt;

        while ((opt = getopt (argc, argv, "s:u:g:rf:d:o:")) != EOF)
	{
                switch (opt)
		{
                case 's':
                        *share = optarg;
                        break;
                case 'u':
			if (!user_mount) {
				mount_uid = strtol(optarg, NULL, 0);
			}
                        break;
                case 'g':
			if (!user_mount) {
				mount_gid = strtol(optarg, NULL, 0);
			}
                        break;
                case 'r':
                        mount_ro = 1;
                        break;
                case 'f':
                        mount_fmask = strtol(optarg, NULL, 8);
                        break;
                case 'd':
                        mount_dmask = strtol(optarg, NULL, 8);
                        break;
		case 'o':
			options = optarg;
			break;
                default:
                        return -1;
                }
        }
        return 0;
        
}

static char *
fullpath(const char *p)
{
        char path[MAXPATHLEN];

	if (strlen(p) > MAXPATHLEN-1) {
		return NULL;
	}

        if (realpath(p, path) == NULL) {
		fprintf(stderr,"Failed to find real path for mount point\n");
		exit(1);
	}
	return strdup(path);
}

/* Check whether user is allowed to mount on the specified mount point. If it's
   OK then we change into that directory - this prevents race conditions */
static int mount_ok(char *mount_point)
{
	struct stat st;

	if (chdir(mount_point) != 0) {
		return -1;
	}

        if (stat(".", &st) != 0) {
		return -1;
        }

        if (!S_ISDIR(st.st_mode)) {
                errno = ENOTDIR;
                return -1;
        }

        if ((getuid() != 0) && 
	    ((getuid() != st.st_uid) || 
	     ((st.st_mode & S_IRWXU) != S_IRWXU))) {
                errno = EPERM;
                return -1;
        }

        return 0;
}

/* Tries to mount using the appropriate format. For 2.2 the struct,
   for 2.4 the ascii version. */
static int
do_mount(char *share_name, unsigned int flags, struct smb_mount_data *data)
{
	char opts[1024];
	struct utsname uts;
	char *release, *major, *minor;
	char *data1, *data2;

	uname(&uts);
	release = uts.release;
	major = strsep(&release, ".");
	minor = strsep(&release, ".");
	if (major && minor && atoi(major) == 2 && atoi(minor) < 4) {
		/* < 2.4, assume struct */
		data1 = (char *) data;
		data2 = opts;
	} else {
		/* >= 2.4, assume ascii but fall back on struct */
		data1 = opts;
		data2 = (char *) data;
	}

	sprintf(opts,
		 "version=7,uid=%d,gid=%d,file_mode=0%o,dir_mode=0%o,%s",
		 data->uid, data->gid, data->file_mode, data->dir_mode,options);
	if (mount(share_name, ".", "smbfs", flags, data1) == 0)
		return 0;
	return mount(share_name, ".", "smbfs", flags, data2);
}

 int smbmnt_main(int argc, char *argv[])
{
	char *mount_point, *share_name = NULL;
	unsigned int flags;
	struct smb_mount_data data;

	memset(&data, 0, sizeof(struct smb_mount_data));

	if (argc < 2) {
		help();
		exit(1);
	}

	if (argv[1][0] == '-') {
		help();
		exit(1);
	}

	if (getuid() != 0) {
		user_mount = 1;
	}

        if (geteuid() != 0) {
                fprintf(stderr, "smbmnt must be installed suid root for direct user mounts (%d,%d)\n", getuid(), geteuid());
                exit(1);
        }

	mount_uid = getuid();
	mount_gid = getgid();
	mount_fmask = umask(0);
        umask(mount_fmask);
	mount_fmask = ~mount_fmask;

        mount_point = fullpath(argv[1]);

        argv += 1;
        argc -= 1;

        if (mount_ok(mount_point) != 0) {
                fprintf(stderr, "cannot mount on %s: %s\n",
                        mount_point, strerror(errno));
                exit(1);
        }

	data.version = SMB_MOUNT_VERSION;

        /* getuid() gives us the real uid, who may umount the fs */
        data.mounted_uid = getuid();

        if (parse_args(argc, argv, &data, &share_name) != 0) {
                help();
                return -1;
        }

        data.uid = mount_uid;
        data.gid = mount_gid;
        data.file_mode = (S_IRWXU|S_IRWXG|S_IRWXO) & mount_fmask;
        data.dir_mode  = (S_IRWXU|S_IRWXG|S_IRWXO) & mount_dmask;

        if (mount_dmask == 0) {
                data.dir_mode = data.file_mode;
                if ((data.dir_mode & S_IRUSR) != 0)
                        data.dir_mode |= S_IXUSR;
                if ((data.dir_mode & S_IRGRP) != 0)
                        data.dir_mode |= S_IXGRP;
                if ((data.dir_mode & S_IROTH) != 0)
                        data.dir_mode |= S_IXOTH;
        }

	flags = MS_MGC_VAL;

	if (mount_ro) flags |= MS_RDONLY;

	if (do_mount(share_name, flags, &data) < 0) {
		switch (errno) {
		case ENODEV:
			fprintf(stderr, "ERROR: smbfs filesystem not supported by the kernel\n");
			break;
		default:
			perror("mount error");
		}
		fprintf(stderr, "Please refer to the smbmnt(8) manual page\n");
		return -1;
	}

	return 0;
}	
