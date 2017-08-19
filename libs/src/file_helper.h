#ifndef __FILE_HELPER_H__
#define __FILE_HELPER_H__

#include <vector>
#include <string>
#include <sys/eventfd.h>
#include <sys/epoll.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>

struct EVENT
{
	int				efd;						//	file-descriptor for eventfd
	int				pfd;						//  file-descriptor for epoll
	epoll_event		read_event;					//  epoll events
};

struct _HANDLE
{
	EVENT*			event;
	int				is_set;
};

typedef _HANDLE* HANDLE;
#define ERROR_TIMEOUT_0     100
#define WAIT_OBJECT_0       0

enum eventType {
    USR_CUSTOM_EVENT_1 = 1,
    USR_CUSTOM_EVENT_2 = 2,
    USR_CUSTOM_EVENT_3 = 3,
    USR_ERR_EVENT
};

int				EventInitialize(HANDLE*);
int 			SetEvent(HANDLE, eventType event_type);
int		        WaitForSingleObject(HANDLE, unsigned long, eventType *event_type);
void			CloseHandle(HANDLE*);

/* this funtion is used to get the RMQ connection strings from the config file in non DB mode
 * it assumes the config varibles in config file are comma separated
 * @param   fileName    the config file path
 * @param   conn_str_id the config key specifying RMQ connection string
 * @param   vec         a string vector containing the result
 * @example RMQ_CONN_STR    id,cnt_str_primary,cnt_str_secondary
 */
void get_rmq_conn_str(const char* fileName, const char* conn_str_id, std::vector<std::string> *vec);

void print_time();

#endif