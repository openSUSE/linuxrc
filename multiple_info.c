/*
 *
 * multiple_info.h        Header file for multiple_info.c
 *
 * Copyright (c) 2000-2001  Matthias Eckermann, SuSE (mge@suse.de)
 *
 */

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/stat.h>

#include "global.h"
#include "dialog.h"
#include "util.h"
#include "multiple_info.h"

#define	MULTI_INFO_FILE	"/info"
#define	MULTI_LIST_FILE	"/info.lst"
#define MULTI_MSG_LEN 	60
#define MULTI_ARR_SIZE	16

int	read_info_list_file(
		char		*filename,
		char		*choice)	{
	char		tmp_string[MULTI_MSG_LEN+1];
	char		*tmp_ptr=tmp_string;
	item_t		data_array[MULTI_ARR_SIZE];
	char		*name_array[MULTI_ARR_SIZE];
	int		retval, i, choice_ii, max_len=0;
	FILE 		*info_list = fopen(MULTI_LIST_FILE, "r");
	if(!info_list){
		return(0);
	}else{	
		i=0;
		while(!feof(info_list)) {
			fscanf(info_list, "%60s", tmp_ptr);
			if(!feof(info_list)&&(strlen(tmp_ptr)>3)){
				name_array[i]=malloc(MULTI_MSG_LEN+1);
				strcpy(name_array[i],strtok(tmp_ptr,":"));
				data_array[i].text=malloc(MULTI_MSG_LEN+1);
				strcpy(data_array[i].text,strtok(NULL,":"));
				if(strlen(data_array[i].text)>max_len){
					max_len=strlen(data_array[i].text);
				}
				data_array[i].func=0;
				if(i<MULTI_ARR_SIZE)
					i++;
			}
		}
		retval=i;
		fclose(info_list);
		if(i>0){
			for (i=retval-1; i>-1; i--) {
				util_center_text(data_array[i].text, max_len+1);
			}
                }else{
                	return(0);
                }
    		choice_ii = dia_menu ("MultiInfo", data_array, retval, 1);
		strcpy(choice, data_array[choice_ii-1].text);
		strcpy(filename, name_array[choice_ii-1]);
    		util_free_items(data_array, retval);
		for(i=retval-1; i>-1; i--){free(name_array[i]);}
		return(choice_ii);	
	}
}

int	rename_info_file (void) {
	char	to_rename[MULTI_MSG_LEN+1];
	char	choice[MULTI_MSG_LEN+1];
	int     tmpval,choosen;
	struct stat mystat;
	char	my_err_msg[MULTI_MSG_LEN+1];
	if((choosen=read_info_list_file(to_rename, choice))) {
		errno=0;
		tmpval=stat(to_rename, &mystat);
		if(errno>0){
			snprintf(my_err_msg, MULTI_MSG_LEN, "%s: %s", 
				strerror(errno), to_rename);
			dia_message(my_err_msg, MSGTYPE_ERROR);
			return(errno);
		}
		rename(to_rename, MULTI_INFO_FILE);
		errno=0;
		tmpval=stat(MULTI_INFO_FILE, &mystat);
		if(errno!=EEXIST && errno>0){
			snprintf(my_err_msg, MULTI_MSG_LEN, "%s: %s", 
				strerror(errno), MULTI_INFO_FILE);
			dia_message(my_err_msg, MSGTYPE_ERROR);
			return(errno);
		}else{
			snprintf(my_err_msg, MULTI_MSG_LEN, "%s: %s -> %s", 
				choice, to_rename, MULTI_INFO_FILE);
			dia_message(my_err_msg, MSGTYPE_INFO);
		}
		return(0);
	}else{
		/*	
		snprintf(my_err_msg, MULTI_MSG_LEN, "%s", 
			 "No list file. Sorry:-(");
		dia_message(my_err_msg, MSGTYPE_ERROR);
		*/
		return(0);
	}
}

