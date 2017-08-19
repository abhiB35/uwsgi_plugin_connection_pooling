#include "error_defs.h"
#include <string.h>
#include <pthread.h>

void uwsError::setLastError(int _errno_)
{
    int val = 0;
    
    UWS_MUTEX_LOCK_VOID(&lock, val)
    lastError = _errno_;
    UWS_MUTEX_UNLOCK_VOID(&lock, val)
}

void uwsError::setLastMsg(const char* msg)
{
    int val = 0;
    
    if(msg != NULL)
    {
        int len = strlen(msg);
        int size = (len<(CP_MAXX_ERROR_MSG_SIZE-1))?len:CP_MAXX_ERROR_MSG_SIZE;
        UWS_MUTEX_LOCK_VOID(&lock, val)
        memcpy(lastMsg, msg, size+1);
        UWS_MUTEX_UNLOCK_VOID(&lock, val)
    }  
    else
    {
        int len = strlen("UNDEFINED");
        UWS_MUTEX_LOCK_VOID(&lock, val)
        memcpy(lastMsg, "UNDEFINED", len+1);
        UWS_MUTEX_UNLOCK_VOID(&lock, val)
    }
}
    
uwsError::uwsError()
{
    int val = 0; 
    lastError = 0;
    memcpy(lastMsg, "NULL" , 5);
    UWS_MUTEX_INIT_ASSIGN(&lock, NULL, val, -1)
}
    
uwsError::~uwsError()
{
    int val = 0;
    UWS_MUTEX_DEINIT(&lock, val)
}
  
const char* uwsError::getLastErrorMsg(void)
{
    return lastMsg;    
}

int uwsError::getLastErrorCode(void)
{
    return lastError;    
}