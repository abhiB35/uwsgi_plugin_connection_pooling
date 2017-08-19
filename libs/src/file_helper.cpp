#include "file_helper.h"
#include "uws_pg_config.h"
#include <fstream>
#include <ctime>

void get_rmq_conn_str(const char* fileName, const char* conn_str_id, std::vector<std::string> *vec)
{
    char connstr[UWS_MEDIUM_BUFFER_SIZE], _id[UWS_SMALL_BUFFER_SIZE];
    char str[UWS_LARGE_BUFFER_SIZE];
    std::string line;
    std::fstream ifile;
    int i=0, j=0, k=0, flag = 0, pos = 0;
    
    if(!fileName || !conn_str_id || !vec)
        return;
    
    ifile.open(fileName , std::ios::in);
    if(!ifile.is_open())
    {
        print_time();
        fprintf(stderr,"unable to open file : %s\n", fileName);
        return;
    }
    
    while(getline(ifile, line))
    {
        flag = 1;
        
        if(line[0] == '#')
            continue;

		for(i=0;conn_str_id[i]!='\0';i++)
		{
			if(conn_str_id[i] != line[i]) {
				flag = 0;
				break;
			}
		}
        
        k = 0;
        if(flag)
        {
            for(j=i+1;line[j]!=',';j++)
            {
                if(line[j]=='\0') {
                    flag = 0;
                    break;
                }
                _id[k++] = line[j];
            }
            _id[k] = '\0';
        }
        
        pos = 65;
        while(flag) {
            flag = 0;
			k=0;
			for(j=j+1;line[j]!='\0';j++)
			{
                if(line[j] == ',') {
                    flag = 1;
                    break;
                }
                connstr[k] = line[j];
                k++;
			}
			connstr[k] = '\0';
            sprintf(str,"%s%c,%s", _id, (char)pos, connstr);
            vec->push_back(str);
            pos++;
		}
    }
    
    ifile.close();
}

int EventInitialize(HANDLE* _h)
{
    _HANDLE *_handle = NULL;
    int ret = 0;
    
    _handle = new _HANDLE;
    if(!_handle)
        goto on_error;
            
    _handle->event = NULL;
    _handle->is_set = 0;
    _handle->event = new EVENT;
    if(!_handle->event)
        goto on_error;
 
    _handle->event->efd = eventfd(0, 0);		//faster as saves extra calls to fcntl() to achieve the same result
    if (_handle->event->efd == -1){
			goto on_error;
    }

    _handle->event->pfd = epoll_create(1024);
    if (_handle->event->pfd == -1)
        goto on_error;
    _handle->event->read_event.events = EPOLLHUP | EPOLLERR | EPOLLIN;
    _handle->event->read_event.data.fd = _handle->event->efd;
    ret = epoll_ctl(_handle->event->pfd, EPOLL_CTL_ADD, _handle->event->efd, &(_handle->event->read_event));
    if (ret == -1)
        goto on_error;

    _handle->is_set = 1;
    *_h = _handle;
    return 1;
    
    on_error:
    if(_handle)
    {
        if(_handle->event)
            delete _handle->event;
        delete _handle;
    }
    return -1;
}

int SetEvent(HANDLE _h, eventType event_type)
{
	ssize_t nr;
	uint64_t value = (uint64_t)event_type;
    if(!_h->is_set)
    {
        return -1;
    }
	nr = write(_h->event->efd, &value, sizeof(uint64_t));
	if (nr != sizeof(uint64_t))
	{
        print_time();
        fprintf(stderr,"SetEvent : Critical error in writing to eventfd.\n");
        return -1;
	}
    return 1;
}

int WaitForSingleObject(HANDLE _h, unsigned long wait, eventType *event_type)
{
	ssize_t nr;
	uint64_t value = 0;
    
    if(!_h->is_set)
    {
        *event_type = USR_ERR_EVENT;
        return -1;
    }

	epoll_event events[10];
	int n = 0;
	n = epoll_wait(_h->event->pfd, &events[0], 10, wait);
	if (n > 0)
	{
		nr = read(_h->event->efd, &value, sizeof(uint64_t));
		if (nr != sizeof(uint64_t)) {
			print_time();
            fprintf(stderr,"Critical error in reading from eventfd.\n");
            *event_type = USR_ERR_EVENT;
            return -1;
        }
        else {
            *event_type = (eventType)value;
        }
	}
	else
	{
		return ERROR_TIMEOUT_0;
	}
    
	return WAIT_OBJECT_0;
}

void CloseHandle(HANDLE* _h)
{
    _HANDLE *_handle = *_h;
    if(_handle && _handle->is_set)
    {
        close(_handle->event->efd);
        close(_handle->event->pfd);
        delete _handle->event;
        delete _handle;
        *_h = NULL;
    }
}

void print_time()
{
    time_t rawtime; 
    struct tm * timeinfo = NULL;
    char buffer[128];
    time (&rawtime); 
    timeinfo = localtime(&rawtime); 
    strftime(buffer,sizeof(buffer),"%d-%m-%Y %I:%M:%S, ",timeinfo);
    fprintf(stderr,"%s",buffer);
    return;
}