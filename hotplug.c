#include <stdio.h>
#include "global.h"
#include "file.h"
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

extern char** environ;

int hotplug_main(int argc, char** argv)
{
  char** e=environ;
  char* eq;	/* position of the equal sign in each string */
  
  printf("/sbin/hotplug called\n");
  FILE* fp=fopen("/hotplug.msg","w");
  
  fprintf(fp,"%s\n",argc>1?argv[1]:"");	/* write type of hotplug event */
  
  while(e[0])
  {
    eq=strstr(e[0],"=");
    if(eq) 	/* has a value */
    {
      *eq++=0;	/* split string at '=' character */
      fprintf(fp,"%s: %s\n",e[0],eq);
      printf("%s: %s\n",e[0],eq);
    }
    e++;
  }
  fclose(fp);
  
  return 0;
}

int hotplug_wait_for_event(char* type)
{
  static char event[50];
  static int inited = 0;
  const int sleeps[10]={2,1,2,3,4,5,6,7,8,9};	/* number of seconds to sleep between tries */
  FILE* fp;
  int counter;
  
  if(!inited)
  {
    /* make sure we get hotplug events */
    fp=fopen("/proc/sys/kernel/hotplug","w");
    fputs("/sbin/hotplug",fp);
    fclose(fp);
    inited=1;
  }
  
  printf("waiting for event %s\n",type);
  for(counter=0;counter<10;counter++)
  {
    sleep(sleeps[counter]);
    fp=fopen("/hotplug.msg","r");
    if(!fp) continue;	/* no /hotplug.msg -> continue waiting */
    fgets(event,50,fp);	/* get event type */
    event[strlen(event)-1]=0;	/* chop LF */
    fclose(fp);
    if(!strcmp(event,type)) return 0;
  }
  return -1;	/* timeout */
}

int hotplug_wait_for_path(char* path)
{
  const int sleeps[10]={2,1,2,3,4,5,6,7,8,9};	/* number of seconds to sleep between tries */
  struct stat statbuf;
  int counter;
  
  for(counter=0;counter<10;counter++)
  {
    sleep(sleeps[counter]);
    if (stat(path,&statbuf) < 0)
	continue;	/* stat failed -> continue waiting */
    else
	return 0;
  }
  return -1;	/* timeout */
}

char* hotplug_get_info(char* key)
{
  static char info[100];
  file_t *f,*g;
  printf("reading key %s from /hotplug.msg\n",key);
  f=file_read_file("/hotplug.msg",kf_none);
  g=file_getentry(f,key);
  if(!g) return 0;	/* key not found */
  strcpy(info,g->value);
  file_free_file(f);
  return info;
}

void hotplug_event_handled(void)
{
  printf("deleting /hotplug.msg\n");
  unlink("/hotplug.msg");
}
