#include "amqp_conn_pool.h"
#include <pthread.h>

using namespace std;

amqpConnection::amqpConnection(const char* connectionStr, const char *_id_, int n)
{
    if(connectionStr != NULL && _id_ != NULL)
    {
        connectionString.append(connectionStr);
        _id.append(_id_);
        state = AMQP_FAULTED;
        noChannels = 0;
        qp = NULL;
        ex = NULL;
        UWS_MUTEX_INIT_ASSIGN(&_lock, NULL, state, AMQP_DEAD)
        heart_beat_time = n;
        _closing = NULL;
        if(EventInitialize(&_closing) < 0)
            state = AMQP_DEAD;
    }
    else
    {
        state = AMQP_DEAD;
    }
    failCnt = 0;
    useCustomExchange = false;
}
    
amqpConnection::~amqpConnection()
{
    map<string, void*>::const_iterator _pos;
    int val = 0; 
    
    state = AMQP_DEAD;
    
    if(SetEvent(_closing, USR_CUSTOM_EVENT_1) < 0) {
        print_time();
        fprintf(stderr,"Error setting closing handle\n");
    }
    
    for(_pos = hmap.begin(); _pos!= hmap.end(); _pos++)
    {
        amqpChannel *channel = (amqpChannel*)_pos->second;
        hmap.erase(_pos->first);
        delete channel;
    }
 
    UWS_MUTEX_DEINIT(&_lock, val) 
    
    usleep(5);
    
    CloseHandle(&_closing);
  
    if(qp)
        delete qp;
}
    
void amqpConnection::setExchFlag(bool value)
{
    useCustomExchange = value;
}

int amqpConnection::addChannel(const char* queueName)
{
    int val = 0;
    
    if(state == AMQP_DEAD || !qp)
    {
        setLastError(CP_ERR_CONN_DEAD);
        setLastMsg(CP_ERR_CONN_DEAD_MSG);
        return UWS_FAILURE;
    }
    
    if(!queueName)
    {
        setLastError(CP_ERR_QUEUE_NAME_NULL);
        setLastMsg(CP_ERR_QUEUE_NAME_NULL_MSG);
        return UWS_FAILURE;
    }
    
    if(noChannels >= CP_MAXX_AMQP_CHANNELS)
    {
        setLastError(CP_ERR_MAXX_AMQP_CHANNELS_REACHED);
        setLastMsg(CP_ERR_MAXX_AMQP_CHANNELS_REACHED_MSG);
        return UWS_FAILURE;
    }
    
    amqpChannel *channel = NULL;
    map<string, void*>::const_iterator _pos;
    int status = UWS_SUCCESS;
    
    UWS_MUTEX_LOCK(&_lock, val);
    _pos = hmap.find(queueName);
    if(_pos != hmap.end()) 
    {
        UWS_RETURN_LOCKED(&_lock, UWS_SUCCESS, val)
    }
    
    channel = new amqpChannel();
    if(channel == NULL)
    {
        setLastError(UWS_ERR_OUT_OF_MEMORY);
        setLastMsg(UWS_ERR_OUT_OF_MEMORY_MSG);
        UWS_RETURN_LOCKED(&_lock, UWS_FAILURE, val)
    }
    
    try {
        channel->queueName = queueName;
        channel->ex = ex;
        if (useCustomExchange){
            channel->qu = qp->createQueue(queueName);
            channel->qu->Bind("ex", queueName);
        }
        else {
            channel->qu = qp->createQueue(queueName);
        }
        hmap.insert(pair<string,void*>(queueName,(void*)channel));
        noChannels++;
    }
    catch(AMQPException e)
    {
        status = UWS_FAILURE;
        setLastError(e.getReplyCode());
        setLastMsg(e.getMessage().c_str());
        if(e.getReplyCode() != 404) {
                if(SetEvent(_closing, USR_CUSTOM_EVENT_3) < 0) {
                print_time();
                fprintf(stderr,"Error setting closing handle\n");
            }
        }
    }
    UWS_MUTEX_UNLOCK(&_lock, val)
    
    return status;
}

int amqpConnection::removeChannel(const char* queueName)
{
    if(queueName == NULL)
    {
        return UWS_SUCCESS;
    }
    
    amqpChannel *channel = NULL;
    map<string, void*>::const_iterator _pos;
    int val = 0;
    
    _pos = hmap.find(queueName);
    if(_pos == hmap.end()) 
    {
        return UWS_SUCCESS;
    }
    else
    {
        UWS_MUTEX_LOCK(&_lock, val)
        channel = (amqpChannel*)_pos->second;
        print_time();
        fprintf(stderr,"_id:%s removing channel %s\n", _id.c_str(), queueName);
        hmap.erase(_pos->first);
        delete channel;
        UWS_MUTEX_UNLOCK(&_lock, val)
    }
    
    return UWS_SUCCESS;
}

int amqpConnection::publishMessage(const char* queueName, const char* msg)
{
    if(state == AMQP_DEAD || state == AMQP_FAULTED)
    {
        setLastError(CP_ERR_CONN_DEAD);
        setLastMsg(CP_ERR_CONN_DEAD_MSG);
        return UWS_FAILURE;
    }
    
    if(!queueName)
    {
        setLastError(CP_ERR_QUEUE_NAME_NULL);
        setLastMsg(CP_ERR_QUEUE_NAME_NULL_MSG);
        return UWS_FAILURE;
    }
    
    if(!msg)
    {
        return UWS_SUCCESS;
    }
    
    amqpChannel *channel = NULL;
    map<string, void*>::const_iterator _pos;
    int status = UWS_SUCCESS;
    
    _pos = hmap.find(queueName);
    if(_pos == hmap.end()) 
    {
        print_time();
        fprintf(stderr,"_id:%s adding channel %s\n", _id.c_str(), queueName);
        if(addChannel(queueName))
        {
            _pos = hmap.find(queueName);
            if(_pos == hmap.end())
            {
                return UWS_FAILURE;
            }
            else{
                channel = (amqpChannel*)_pos->second;
            }
        }
    }
    else
    {
        channel = (amqpChannel*)_pos->second;
    }
    
    if(!channel)
    {
        removeChannel(queueName);
        setLastError(CP_ERR_CHANNEL_UNKNOWN_ERROR);
        setLastMsg(CP_ERR_CHANNEL_UNKNOWN_ERROR_MSG);
        return UWS_FAILURE;
    }
    
    try {
         print_time();
         fprintf(stderr,"%s Publishing %s\n", _id.c_str(), queueName);
         channel->ex->Publish((char*)msg, strlen(msg), queueName);
    }
    catch(AMQPException e)
    {
        status = UWS_FAILURE;
        setLastError(e.getReplyCode());
        setLastMsg(e.getMessage().c_str());
        if(SetEvent(_closing, USR_CUSTOM_EVENT_2) < 0) {
            print_time();
            fprintf(stderr,"Error setting closing handle\n");
        }
    }
    
    return status;
}

bool amqpConnection::isActive()
{
    return state != AMQP_DEAD;
}

void* amqpConnection::moniterThread()
{
    int ret = 0;
    eventType evt;
    bool condition = true;
    map<string, void*>::const_iterator _pos;
    
    while(condition == true) {
        if(state == AMQP_FAULTED) {
            reConnect();
        }
        ret = WaitForSingleObject(_closing, heart_beat_time, &evt);
        if(ret == ERROR_TIMEOUT_0) {
            try {
                if(qp)
                    ret = qp->checkConnection();
            }
            catch(AMQPException e)
            {
                if(failCnt < 3) {
                    print_time();
                    fprintf(stderr,"recon_on_timeout err_msg: %s\n", e.getMessage().c_str());
                }
                state = AMQP_FAULTED;
                reConnect();
            }
        }
        else if(ret == WAIT_OBJECT_0){
            if(evt == USR_CUSTOM_EVENT_2)
            {
                print_time();
                fprintf(stderr,"recon_on_pub_fail...\n");
                state = AMQP_FAULTED;
                reConnect();
            }
            else if(evt == USR_CUSTOM_EVENT_3)
            {
                print_time();
                fprintf(stderr,"recon_on_add_channel_fail...\n");
                state = AMQP_FAULTED;
                reConnect();
            }
            else if(evt == USR_CUSTOM_EVENT_1){
                condition = false;
                print_time();
                fprintf(stderr,"_id:%s monit thread exiting1.....\n", _id.c_str());
            }
            else {
                print_time();
                fprintf(stderr,"_id:%s monit thread unknown event: %d.\n", _id.c_str(), (int)evt);
            }
        }
        else {
            print_time();
            fprintf(stderr,"_id:%s monit thread unknown error: %d.\n", _id.c_str(), ret);
        }
    }
}

void amqpConnection::reConnect()
{
    print_time();
    fprintf(stderr,"_id:%s reconnecting......\n", _id.c_str());
    int val = 0;
    
    UWS_MUTEX_LOCK_VOID(&_lock, val)
    
    if(state == AMQP_ALIVE)
    {
        print_time();
        fprintf(stderr,"_id:%s already alive......\n", _id.c_str());
        UWS_MUTEX_UNLOCK_VOID(&_lock, val)
        return;
    }
   
    try {
        if(qp){
            map<string, void*>::const_iterator _pos;

            for(_pos = hmap.begin(); _pos!= hmap.end(); _pos++)
            {
                amqpChannel *channel = (amqpChannel*)_pos->second;
                hmap.erase(_pos->first);
                delete channel;
            }
            
            delete qp;
            qp = NULL;
        }
        qp = new AMQP(connectionString);
        if (useCustomExchange){
            ex = qp->createExchange("ex");
            ex->Declare("ex" , "direct");
        }
        else {
            ex = qp->createExchange("");
        }
        ex->setHeader("priority", UWS_RMQ_DEF_PRIORITY);
        ex->setHeader("Content-type" , UWS_RMQ_DEF_CONTENT_TYPE, false);
        ex->setHeader("Content-encoding", UWS_RMQ_DEF_ENCODING, false);
        state = AMQP_ALIVE;
        if(failCnt >= 3)
            failCnt = 0;
    }
    catch(AMQPException e)
    {
        if(failCnt < 3) {
            failCnt++;
            cerr<<"Exception creating connection.\n Msg: "<<e.getMessage()<<" Code: "<<e.getReplyCode()<<endl;
        }
    }
    
    UWS_MUTEX_UNLOCK_VOID(&_lock, val)
}

connPoolFactory::connPoolFactory()
{
    int val = 0;
    activeConnections = 0;
    UWS_MUTEX_INIT_ASSIGN(&_lock, NULL, val, UWS_FAILURE)
}
    
connPoolFactory::~connPoolFactory()
{
    int val = 0;
    map<string, void*>::const_iterator _pos;
    
    UWS_MUTEX_LOCK_VOID(&_lock , val)
    for(_pos = poolMap.begin(); _pos!= poolMap.end(); _pos++)
    {
        amqpConnection *conn = (amqpConnection*)_pos->second;
        poolMap.erase(_pos->first);
        delete conn;
    }
    UWS_MUTEX_UNLOCK_VOID(&_lock , val)
    
    UWS_MUTEX_DEINIT(&_lock, val)
}
    
int connPoolFactory::addConnection(const char* connectionStr, const char* idStr, connType _type, int hbt_in_ms, bool flag)
{
    if(!connectionStr)
    {
        setLastError(CP_ERR_CONN_STR_NULL);
        setLastMsg(CP_ERR_CONN_STR_NULL_MSG);
        return UWS_FAILURE;
    }
    
    if(!idStr)
    {
        setLastError(CP_ERR_CONN_ID_NULL);
        setLastMsg(CP_ERR_CONN_ID_NULL_MSG);
        return UWS_FAILURE;
    }
    
    if(activeConnections >= CP_MAXX_CONNECTIONS)
    {
        setLastError(CP_ERR_MAXX_CONN_REACHED);
        setLastMsg(CP_ERR_MAXX_CONN_REACHED_MSG);
        return UWS_FAILURE;
    }
    
    int status = UWS_SUCCESS;
    amqpConnection *conn = NULL;
    string _id(idStr);
    int val = 0;
    
    switch(_type)
    {
        case AMQP_CONN:
            conn = new amqpConnection(connectionStr , idStr, hbt_in_ms);
            if(conn == NULL)
            {
                setLastError(UWS_ERR_OUT_OF_MEMORY);
                setLastMsg(UWS_ERR_OUT_OF_MEMORY_MSG);
                status = UWS_FAILURE;
            }
            else {
                if(!conn->isActive())
                {
                    setLastError(CP_ERR_CONN_DEAD);
                    setLastMsg(CP_ERR_CONN_DEAD_MSG);
                    status = UWS_FAILURE;
                }
                else {
                    if(flag)
                        conn->setExchFlag(flag);
                    UWS_MUTEX_LOCK(&_lock , val)
                    activeConnections++;
                    poolMap.insert(pair<string, void*>(_id, (void*)conn));
                    UWS_MUTEX_UNLOCK(&_lock , val)
                    tHANDLE h;
                    int res = pthread_create(&h, NULL, launch_moniter, (void*)conn);
                    if (res != 0) {
                        print_time();
                        fprintf(stderr,"conn_pool_error in pthread_create: %d\n", res);
                    }
                    res = pthread_detach(h);
                    if (res != 0) {
                        print_time();
                        fprintf(stderr,"conn_pool_error in pthread_detach: %d\n", res);
                    }
                }
            }
            break;
        default:
            setLastError(CP_ERR_CONNECTION_TYPE_UNSUPPORTED);
            setLastMsg(CP_ERR_CONNECTION_TYPE_UNSUPPORTED_MSG);
            status = UWS_FAILURE;
    }
    
    return status;
}
    
int connPoolFactory::removeConnection(const char* idStr, connType _type)
{
    if(!idStr)
    {
        return UWS_SUCCESS;
    }
    
    map<string, void*>::const_iterator _pos;
    amqpConnection *conn = NULL;
    string _id(idStr);
    int status = UWS_SUCCESS;
    int val = 0;
    
    switch(_type)
    {
        case AMQP_CONN:
            _pos = poolMap.find(_id);
            if(_pos != poolMap.end())
            {
                conn = (amqpConnection*)_pos->second;
                UWS_MUTEX_LOCK(&_lock , val)
                delete conn;
                poolMap.erase(_pos->first);
                activeConnections--;
                UWS_MUTEX_UNLOCK(&_lock , val)
            }
            break;
        default:
            setLastError(CP_ERR_CONNECTION_TYPE_UNSUPPORTED);
            setLastMsg(CP_ERR_CONNECTION_TYPE_UNSUPPORTED_MSG);
            status = UWS_FAILURE;
    }
    
    return status;
}
    
int connPoolFactory::publishMsgAmqp(const char* _id, const char* queueName ,const char* msg, connType _type)
{
    if(!_id)
    {
        setLastError(CP_ERR_CONN_ID_NULL);
        setLastMsg(CP_ERR_CONN_ID_NULL_MSG);
        return UWS_FAILURE;
    }   
    
    string id_(_id);
    map<string, void*>::const_iterator _pos;
    amqpConnection *conn = NULL;
    int status = UWS_SUCCESS;
    
    _pos = poolMap.find(id_);
    if(_pos == poolMap.end())
    {
        setLastError(CP_ERR_CONN_NOT_FOUND);
        setLastMsg(CP_ERR_CONN_NOT_FOUND_MSG);
        return UWS_FAILURE;
    }
    
    switch(_type)
    {
        case AMQP_CONN:
            conn = (amqpConnection*)_pos->second;
            status = conn->publishMessage(queueName, msg);
            if(status != UWS_SUCCESS)
            {
                setLastError(conn->getLastErrorCode());
                setLastMsg(conn->getLastErrorMsg());
            }
            break;
        default:
            setLastError(CP_ERR_CONNECTION_TYPE_UNSUPPORTED);
            setLastMsg(CP_ERR_CONNECTION_TYPE_UNSUPPORTED_MSG);
            status = UWS_FAILURE;
    }
        
    return status;
}

void* connPoolFactory::launch_moniter(void* data)
{
    amqpConnection *con = (amqpConnection*)data;
    if(con) {
        return con->moniterThread();
    }
    else {
        return NULL;
    }
}