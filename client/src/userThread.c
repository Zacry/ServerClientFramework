#include "userThread.h"
#include "threadManager.h"
#include "trace.h"
#include <unistd.h>
#include <sys/prctl.h>
#include "timer.h"
#include <semaphore.h>
#include <mqueue.h>
#include <errno.h>
#include "util.h"
#include <signal.h>
#include "sem.h"

int gMessageTaskIndex;
int gMqRcvTaskIndex;
int gMqSndTaskIndex;
int gHeartBeatTaskIndex;

thread_params gMessageThrParams = {24*1024, 0, "MessageTask", NULL, "message.log", THREAD_MQ_USED, 4, 10};
thread_params gMqRcvThrParams = {24*1024, 0, "MqRcv", NULL, "mqrcv.log", THREAD_MQ_NOT_USED, 0, 0};
thread_params gMqSndThrParams = {24*1024, 0, "MqSnd", NULL, "mqsnd.log", THREAD_MQ_NOT_USED, 0, 0};
thread_params gHeartBeatThrParams = {24*1024, 0, "HeartBeat", NULL, "heartbeat.log", THREAD_MQ_NOT_USED, 0, 0};

user_defined_task_info gUserDefinedTask[] = {
                     {taskMessage, (void*)&gMessageThrParams, &gMessageTaskIndex},
                     {taskMqRcv, (void*)&gMqRcvThrParams, &gMqRcvTaskIndex},
                     {taskMqSnd, (void*)&gMqSndThrParams, &gMqSndTaskIndex},
                     {taskHeartBeat, (void*)&gHeartBeatThrParams, &gHeartBeatTaskIndex}
                     };
int gUserDefinedTaskNum = sizeof(gUserDefinedTask)/sizeof(user_defined_task_info);

/* Received msg handler function */
msg_rcv_handler gMsgRcvHandler[] = {
    {MSG_ACTION_MAIN_MAX, {msgRcvHandleHeartBeat, NULL}},
    {MSG_ACTION_COMM_MAX, {NULL, NULL}}
};
int gMsgRcvHandlerNum = sizeof(gMsgRcvHandler)/sizeof(msg_rcv_handler);

/* HeatBeat task global resource */
heartbeat_attach gHeartBeatAttached = HEARTBEAT_NOT_ATTACHED;
heartbeat_data gHeartBeatData;
pthread_mutex_t gHeartBeatDataMutex;

/* Mq task global resource */
bitmap* gPtrBitmapPool;
mq_resp_msg gMqRespMsg;
sem_t gMqRequestSem;
sem_t gMqRespondSem;
mqd_t gMqQid;

void* taskMessage(void* arg)
{
    /*make the task sleep 3 secs to eliminate the misorder of initial trace at the beginning of the thread*/
    sleep(1);

    thread_params* pThrParams = (thread_params*) arg;
    int ret;

    TRACE(TRACE_INFO, TASK_INDEX_MESSAGE, "set thread name %s\n", pThrParams->threadName);
    if((ret=prctl(PR_SET_NAME, pThrParams->threadName)) != 0)
    {
        TRACE(TRACE_ERROR, TASK_INDEX_MESSAGE, "cannot set thread name, %s", strerror(ret));
    }
    /*
    prctl(PR_SET_NAME, pThrParams->threadName) && pthread_setname_np()
    */

    TimerAddAppUnit(TASK_INDEX_MESSAGE, MSG_EVENT_TIMER_EXPIRE, NULL, 150);
    
    while(1)
    {
        threadManagerRcvEvent(TASK_INDEX_MESSAGE, MSG_EVENT_SYNC_START | MSG_EVENT_SYNC_End | MSG_EVENT_TIMER_EXPIRE, MSG_EVENT_SYNC_INFO, THREAD_EVENT_NO_WAIT, taskMessageEventHandler);
    }
}

void taskMessageEventHandler(int threadIndex, int event, char* pMsg)
{
    if(event & MSG_EVENT_SYNC_START)
    {
        TRACE(TRACE_INFO, threadIndex, "...taskMessage syncing start...\n");
    }

    if(event & MSG_EVENT_SYNC_End)
    {
        TRACE(TRACE_INFO, threadIndex, "...taskMessage syncing end...\n");
    }

    if(event & MSG_EVENT_SYNC_INFO)
    {
        TRACE(TRACE_INFO, threadIndex, "...taskMessage syncing info: %x\n", pMsg);
    }

    if(event & MSG_EVENT_TIMER_EXPIRE)
    {
        static int timerHit = 1;
        
        /* Send request to mq task and get corresponding response */
        int mqRetVal;
        int semVal;
        int semRet;
        mq_req_msg mqReqMsg;
        int mqPriority = 8;
        static int allocateCnt = 0;
        static int deallocateCnt = 0;
        
        TRACE(TRACE_INFO, threadIndex, "...taskMessage timer expire: this will update status of entries in this thread and send mq\n");
        
        sem_getvalue(&gMqRequestSem, &semVal);
        TRACE(TRACE_DEBUG, threadIndex, "before take, gMqRequestSem value is %d\n", semVal);

        SEM_WAIT(gMqRequestSem, semRet);
        if(semRet == -1)
        {
            TRACE(TRACE_ERROR, threadIndex, "sem_wait() failed\n");
            return;
        }
        sem_getvalue(&gMqRequestSem, &semVal);
        TRACE(TRACE_DEBUG, threadIndex, "after take, gMqRequestSem value is %d\n", semVal);

        /* Fill the request message */
        if(timerHit%5 == 0)
        {
            mqReqMsg.msgType = MQ_REQ_INDEX_DEALLOCATE;
            sprintf(mqReqMsg.msg, "%d", rand()%MQ_INDEX_MAX);
        }
        else
        {
            mqReqMsg.msgType = MQ_REQ_INDEX_ALLOCATE;
            sprintf(mqReqMsg.msg, "%d", allocateCnt);
            allocateCnt++;
        }
        
        mqRetVal = mq_send(gMqQid, (char*)&mqReqMsg, sizeof(mqReqMsg), mqPriority);
        if(mqRetVal != 0)
        {
            TRACE(TRACE_ERROR, threadIndex, "mq_send error: %d\n", errno);
        }
        else
        {
            sem_getvalue(&gMqRespondSem, &semVal);
            TRACE(TRACE_DEBUG, threadIndex, "before take, gMqRespondSem value is %d\n", semVal);
                
            SEM_WAIT(gMqRespondSem, semRet);
            if(semRet == -1)
            {
                TRACE(TRACE_ERROR, threadIndex, "sem_wait() failed\n");
            }
            else
            {
                sem_getvalue(&gMqRespondSem, &semVal);
                TRACE(TRACE_DEBUG, threadIndex, "after take, gMqRespondSem value is %d\n", semVal);
        
                TRACE(TRACE_INFO, threadIndex, "mq respond message: %s\n", gMqRespMsg.msgResp);
                if(strcmp(gMqRespMsg.msgResp, "get a string") == 0)
                {
                    TRACE(TRACE_INFO, threadIndex, "mq respond message is wrong!!!\n");
                }
            }
        }

        SEM_POST(gMqRequestSem, semRet);
        if(semRet == -1)
        {
            TRACE(TRACE_ERROR, threadIndex, "sem_post() failed\n");
        }
        sem_getvalue(&gMqRequestSem, &semVal);
        TRACE(TRACE_DEBUG, threadIndex, "after release, gMqRequestSem value is %d\n", semVal);

        TimerAddAppUnit(threadIndex, MSG_EVENT_TIMER_EXPIRE, NULL, 150);
        timerHit++;
    }
}

/* Init mq global resource */
mq_ret mqInit()
{
    /* Init bitmap */
    gPtrBitmapPool = bitmapCreat(MQ_INDEX_MAX);
    if(gPtrBitmapPool == NULL)
    {
        return MQ_FAILURE;
    }

    /* Init message queue*/
    int oflag = O_RDWR | O_CREAT | O_EXCL;
    mode_t mode = 0777;
    struct mq_attr attr;
    attr.mq_flags = 0;
    attr.mq_maxmsg = MQ_MAX_NUM;
    attr.mq_msgsize = MQ_MAX_SIZE;
    
    gMqQid = mq_open(MQ_NAME, oflag, mode, &attr);
    if(gMqQid == (mqd_t)-1)
    {
        TRACE(TRACE_ERROR, TASK_INDEX_MQ_RCV, "cannot create message queue, errno: %d\n", errno);
        if(errno == EEXIST)
        {
            if(mq_unlink(MQ_NAME) == (mqd_t)-1)
            {
                TRACE(TRACE_ERROR, TASK_INDEX_MQ_RCV, "cannot remove message queue, errno: %d\n", errno);
                return MQ_FAILURE;
            }
            oflag |= O_EXCL;
            gMqQid = mq_open(MQ_NAME, oflag, mode, &attr);
            if(gMqQid == (mqd_t)-1)
            {
                TRACE(TRACE_ERROR, TASK_INDEX_MQ_RCV, "cannot create message queue, errno: %d\n", errno);
                return MQ_FAILURE;
            }
        }
        else
        {
            return MQ_FAILURE;
        }
    }

    /*init request and respond sem*/
    int retVal;
    retVal = sem_init(&gMqRequestSem, 0, 1);
    if(retVal == -1)
    {
       TRACE(TRACE_ERROR, TASK_INDEX_MQ_RCV, "cannot create request sem, errno: %d\n", errno);
       mq_close(gMqQid);
       mq_unlink(MQ_NAME);
       return MQ_FAILURE;
    }

    retVal = sem_init(&gMqRespondSem, 0, 0);
    if(retVal == -1)
    {
       TRACE(TRACE_ERROR, TASK_INDEX_MQ_RCV, "cannot create request sem, errno: %d\n", errno);
       sem_destroy(&gMqRequestSem);
       mq_close(gMqQid);
       mq_unlink(MQ_NAME);
       return MQ_FAILURE;
    }

    /* Init global mq respond message */
    memset(&gMqRespMsg, 0, sizeof(gMqRespMsg));

    return MQ_SUCCESS;
}

void* taskMqRcv(void* arg)
{
    sleep(1);

    thread_params* pThrParams = (thread_params*) arg;
    int ret;

    TRACE(TRACE_INFO, TASK_INDEX_MQ_RCV, "set thread name %s\n", pThrParams->threadName);
    if((ret=prctl(PR_SET_NAME, pThrParams->threadName)) != 0)
    {
        TRACE(TRACE_ERROR, TASK_INDEX_MQ_RCV, "cannot set thread name, %s\n", strerror(ret));
    }

    char mqReqMsgStr[MQ_MAX_SIZE];
    mq_req_msg* pMqReqMsg;
    unsigned int msgPrio;
    int mqRcvByteNum;
    int semRet;
    int semVal;
    int allocateIndex;
    
    bitmap_ret bmRet;
    while(1)
    {
        mqRcvByteNum = mq_receive(gMqQid, mqReqMsgStr, MQ_MAX_SIZE, &msgPrio);
        pMqReqMsg = (mq_req_msg*)mqReqMsgStr;

        TRACE(TRACE_INFO, TASK_INDEX_MQ_RCV, "receive message type: %d, %s, priority: %d\n", pMqReqMsg->msgType, pMqReqMsg->msg, msgPrio);

        if(pMqReqMsg->msgType == MQ_REQ_INDEX_ALLOCATE)
        {
            bmRet = bitmapAllocate(gPtrBitmapPool, &allocateIndex);
            if(bmRet == BITMAP_SUCCESS)
            {
                sprintf(gMqRespMsg.msgResp, "allocate index %d", allocateIndex);
            }
            else
            {
                sprintf(gMqRespMsg.msgResp, "cannot allocate index");
            }
        }
        else if(pMqReqMsg->msgType == MQ_REQ_INDEX_DEALLOCATE)
        {
            bmRet = bitmapDeallocate(gPtrBitmapPool, atoi(pMqReqMsg->msg));
            if(bmRet == BITMAP_SUCCESS)
            {
                sprintf(gMqRespMsg.msgResp, "deallocate index %d", atoi(pMqReqMsg->msg));
            }
            else
            {
                sprintf(gMqRespMsg.msgResp, "cannot deallocate index %d", atoi(pMqReqMsg->msg));
            }
        }
        else if(pMqReqMsg->msgType == MQ_REQ_STRING)
        {
            sprintf(gMqRespMsg.msgResp, "get a string");
        }
        else
        {
            sprintf(gMqRespMsg.msgResp, "wrong message type for index manipulation");
        }

        sem_getvalue(&gMqRespondSem, &semVal);
        TRACE(TRACE_DEBUG, TASK_INDEX_MQ_RCV, "mq response string: %s\n", gMqRespMsg.msgResp);
        TRACE(TRACE_DEBUG, TASK_INDEX_MQ_RCV, "before release, gMqRespondSem value is %d\n", semVal);
        
        SEM_POST(gMqRespondSem, semRet);
        if(semRet == -1)
        {
            TRACE(TRACE_ERROR, TASK_INDEX_MQ_RCV, "sem_post() failed\n");
        }

        sem_getvalue(&gMqRespondSem, &semVal);
        TRACE(TRACE_DEBUG, TASK_INDEX_MQ_RCV, "after release, gMqRespondSem value is %d\n", semVal);
    }
}


void* taskMqSnd(void* arg) 
{
    sleep(1);

    thread_params* pThrParams = (thread_params*) arg;
    int ret;

    TRACE(TRACE_INFO, TASK_INDEX_MQ_SND, "set thread name %s\n", pThrParams->threadName);
    if((ret=prctl(PR_SET_NAME, pThrParams->threadName)) != 0)
    {
        TRACE(TRACE_ERROR, TASK_INDEX_MQ_SND, "cannot set thread name, %s\n", strerror(ret));
    }

    TimerAddAppUnit(TASK_INDEX_MQ_SND, MQ_SND_EVENT_TIMER, NULL, 150);

    while(1)
    {
        threadManagerRcvEvent(TASK_INDEX_MQ_SND, MQ_SND_EVENT_TIMER, 0, THREAD_EVENT_NO_WAIT, taskMqSndEventHandler);
    }
}

void taskMqSndEventHandler(int threadIndex, int event, char* pMsg)
{
    int mqRetVal;
    int semVal;
    int semRet;
    mq_req_msg mqReqMsg;

    if(event & MQ_SND_EVENT_TIMER)
    {
        TRACE(TRACE_INFO, threadIndex, "mqsend timer hit\n");
        
        sem_getvalue(&gMqRequestSem, &semVal);
        TRACE(TRACE_DEBUG, threadIndex, "before take, gMqRequestSem value is %d\n", semVal);

        SEM_WAIT(gMqRequestSem, semRet);
        if(semRet == -1)
        {
            TRACE(TRACE_ERROR, threadIndex, "sem_wait() failed\n");
        }

        sem_getvalue(&gMqRequestSem, &semVal);
        TRACE(TRACE_DEBUG, threadIndex, "after take, gMqRequestSem value is %d\n", semVal);

        mqReqMsg.msgType = MQ_REQ_STRING;
        sprintf(mqReqMsg.msg, "%s", "Greeting from mqSnd");
        TRACE(TRACE_DEBUG, threadIndex, "request msg: %d, %s\n", mqReqMsg.msgType, mqReqMsg.msg);
        mqRetVal = mq_send(gMqQid, (char*)&mqReqMsg, sizeof(mqReqMsg), 0);
        if(mqRetVal != 0)
        {
            TRACE(TRACE_ERROR, threadIndex, "mq_send error: %d\n", errno);
        }
        else
        {
            sem_getvalue(&gMqRespondSem, &semVal);
            TRACE(TRACE_DEBUG, threadIndex, "before take, gMqRespondSem value is %d\n", semVal);
        
            SEM_WAIT(gMqRespondSem, semRet);
            if(semRet == -1)
            {
                TRACE(TRACE_ERROR, threadIndex, "sem_wait() failed\n");
            }
            else
            {
                sem_getvalue(&gMqRespondSem, &semVal);
                TRACE(TRACE_DEBUG, threadIndex, "after take, gMqRespondSem value is %d\n", semVal);
            
                TRACE(TRACE_INFO, threadIndex, "mq respond message: %s\n", gMqRespMsg.msgResp);
                if(strcmp(gMqRespMsg.msgResp, "get a string") != 0)
                {
                    TRACE(TRACE_INFO, threadIndex, "mq respond message is wrong!!!\n");
                }
            }
        }

        TimerAddAppUnit(threadIndex, MQ_SND_EVENT_TIMER, NULL, 150);

        SEM_POST(gMqRequestSem, semRet);
        if(semRet == -1)
        {
            TRACE(TRACE_ERROR, threadIndex, "sem_post() failed\n");
        }

        sem_getvalue(&gMqRequestSem, &semVal);
        TRACE(TRACE_DEBUG, threadIndex, "after release, gMqRequestSem value is %d\n", semVal);
    }
}


void* taskHeartBeat(void* arg)
{
    sleep(1);

    thread_params* pThrParams = (thread_params*) arg;
    int ret;
    msg_struct msgSt;
    union sigval sigVal;

    TRACE(TRACE_INFO, TASK_INDEX_HEARTBEAT, "set thread name %s\n", pThrParams->threadName);
    if((ret=prctl(PR_SET_NAME, pThrParams->threadName)) != 0)
    {
        TRACE(TRACE_ERROR, TASK_INDEX_HEARTBEAT, "cannot set thread name, %s", strerror(ret));
    }

    memset(&gHeartBeatData, 0, sizeof(gHeartBeatData));

    if(pthread_mutex_init(&gHeartBeatDataMutex, NULL) != 0)
    {
        TRACE(TRACE_ERROR, TASK_INDEX_HEARTBEAT, "cannot init gHeartBeatDataMutex, thread cannot be created\n");
        return NULL;
    }
    
    msgSt.msgParams = "this is from client, heartbeat!";
    msgSt.msgHead.msgLen = strlen(msgSt.msgParams);
    msgSt.msgHead.msgSrc = CLIENT;
    msgSt.msgHead.msgDest = SERVER;
    msgSt.msgHead.msgEnt = MSG_ENTITY_MAIN;
    msgSt.msgHead.msgAct = MSG_ACTION_MAIN_HEARTBEAT;
    msgSt.msgHead.frameCnt = 0;

    while(1)
    {
        msgUdpSocketSend(TASK_INDEX_HEARTBEAT, &msgSt);
        msgSt.msgHead.frameCnt++;

        /*Deal with heartbeat count to monitor connection*/
        if(gHeartBeatAttached == HEARTBEAT_ATTACHED)
        {
            MSG_MUTEX_LOCK(&gHeartBeatDataMutex, TASK_INDEX_HEARTBEAT);

            if(gHeartBeatData.rcvCnt != 0)
            {
                gHeartBeatData.missCnt = 0;
                gHeartBeatData.rcvCnt = 0;
            }
            else
            {
                gHeartBeatData.missCnt++;
            }

            if(gHeartBeatData.missCnt >= HEARTBEAT_MISS_CNT_MAX)
            {
                TRACE(TRACE_ERROR, TASK_INDEX_HEARTBEAT, "heartbeat is failed\n");
                sigVal.sival_int = 88;
                sigqueue(getpid(), SIGTERM, sigVal);
            }

            MSG_MUTEX_UNLOCK(&gHeartBeatDataMutex, TASK_INDEX_HEARTBEAT);
        }

        sleep(1);
    }
}

    
msg_ret msgRcvHandleHeartBeat(msg_struct* msgSt)
{
    if(gHeartBeatAttached == HEARTBEAT_NOT_ATTACHED)
    {
        gHeartBeatAttached = HEARTBEAT_ATTACHED;
        TRACE(TRACE_INFO, TASK_INDEX_HEARTBEAT, "client is attached!!!\n");
        threadManagerSendEvent(TASK_INDEX_MESSAGE, MSG_EVENT_SYNC_START);
    }

    static int preFrameCnt = -1;

    if(preFrameCnt != -1 && (msgSt->msgHead.frameCnt - preFrameCnt) != 1)
    {   
        TRACE(TRACE_ERROR, TASK_INDEX_HEARTBEAT, "frame loss, pre frame count:%d, frame count:%d!!!\n", preFrameCnt, msgSt->msgHead.frameCnt);
    }   
    preFrameCnt = msgSt->msgHead.frameCnt;

    TRACE(TRACE_INFO, TASK_INDEX_HEARTBEAT, "receive message:%s, frame count:%d!!!\n", msgSt->msgParams, msgSt->msgHead.frameCnt);
    
    MSG_MUTEX_LOCK(&gHeartBeatDataMutex, TASK_INDEX_HEARTBEAT);
    gHeartBeatData.rcvCnt++;
    MSG_MUTEX_UNLOCK(&gHeartBeatDataMutex, TASK_INDEX_HEARTBEAT);

    return MSG_SUCCESS;
}
