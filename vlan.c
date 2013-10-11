/*
 * Code based on http://www.candelatech.com/~greear/vlan.html tools
 *
 * Copyright (c) 2013 SuSE Linux AG
*/

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/if_vlan.h>
#include <linux/sockios.h>
#include <string.h>
#include <sys/socket.h>

// add or rem vlan
int configure_vlan(char *if_name, char *cmd, char *vid) 
{
   int fd;
   struct vlan_ioctl_args if_request;
   char* conf_file_name = "/proc/net/vlan/config";

   memset(&if_request, 0, sizeof(struct vlan_ioctl_args));

   if ((fd = open(conf_file_name, O_RDONLY)) < 0) {
      fprintf(stderr,"WARNING:  Could not open /proc/net/vlan/config.  Maybe you need to load the 8021q module, or maybe you are not using PROCFS??\n"); 
   } else {
      close(fd);
   }
   if ((fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
      fprintf(stderr, "FATAL:  Couldn't open a socket..go figure!\n");
      exit(2);
   }

   if (strlen(if_name) > 15) {
      fprintf(stderr,"ERROR:  if_name must be 15 characters or less.\n");
      exit(1);
   }

   if_request.u.VID = atoi(vid);
   strcpy(if_request.device1, if_name);
   if (strcasecmp(cmd, "add") == 0) {
       if_request.cmd = ADD_VLAN_CMD;
       if (ioctl(fd, SIOCSIFVLAN, &if_request) < 0) {
          fprintf(stderr,"ERROR: trying to add VLAN #%s to IF -:%s:-  error: %s\n", vid, if_name, strerror(errno));
       } else {
          fprintf(stderr,"Added VLAN with VID == %s to IF -:%s:-\n", vid, if_name);
       }
   }
   else if (strcasecmp(cmd, "rem") == 0) {
       if_request.cmd = DEL_VLAN_CMD;
       if (ioctl(fd, SIOCSIFVLAN, &if_request) < 0) {
          fprintf(stderr,"ERROR: trying to remove VLAN -:%s:- error: %s\n", if_name, strerror(errno));
       } else {
          fprintf(stderr,"Removed VLAN -:%s:-\n", if_name);
       }
   }
   return 0;
}

