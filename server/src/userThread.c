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
        int mqPriority = 2;
        
        TRACE(TRACE_INFO, threadIndex, "...taskMessage timer expire: this will update status of entries in this thread and send mq\n");
        
        //sem_getvalue(&gMqRequestSem, &semVal);
        //TRACE(TRACE_DEBUG, threadIndex, "gMqRequestSem value is %d\n", semVal);

        SEM_WAIT(gMqRequestSem, semRet);
        if(semRet == -1)
        {
            TRACE(TRACE_ERROR, threadIndex, "sem_wait() failed\n");
            return;
        }
        //sem_getvalue(&gMqRequestSem, &semVal);
        //TRACE(TRACE_DEBUG, threadIndex, "gMqRequestSem value is %d\n", semVal);

        /* Fill the request message */
        if(timerHit%5 == 0)
        {
            mqReqMsg.msgType = MQ_REQ_INDEX_DEALLOCATE;
            sprintf(mqReqMsg.msg, "%d", rand()%MQ_INDEX_MAX);
        }
        else
        {
            mqReqMsg.msgType = MQ_REQ_INDEX_ALLOCATE;
        }
        
        mqRetVal = mq_send(gMqQid, (char*)&mqReqMsg, sizeof(mqReqMsg), mqPriority);
        if(mqRetVal != 0)
        {
            TRACE(TRACE_ERROR, threadIndex, "mq_send error: %d\n", errno);
        }
        else
        {
            SEM_WAIT(gMqRespondSem, semRet);
            if(semRet == -1)
            {
                TRACE(TRACE_ERROR, threadIndex, "sem_wait() failed\n");
            }
            else
            {
                TRACE(TRACE_INFO, threadIndex, "mq respond message: %s\n", gMqRespMsg.msgResp);
            }
        }

        SEM_POST(gMqRequestSem, semRet);
        if(semRet == -1)
        {
            TRACE(TRACE_ERROR, threadIndex, "sem_post() failed\n");
        }
        //sem_getvalue(&gMqRequestSem, &semVal);
        //TRACE(TRACE_DEBUG, threadIndex, "gMqRequestSem value is %d\n", semVal);

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
    int oflag = O_RDWR | O_CREAT;
    mode_t mode = 0777;
    struct mq_attr attr;
    attr.mq_flags = 0;
    attr.mq_maxmsg = MQ_MAX_NUM;
    attr.mq_msgsize = MQ_MAX_SIZE;
    
    gMqQid = mq_open(MQ_NAME, oflag, mode, &attr);
    if(gMqQid == (mqd_t)-1)
    {
       TRACE(TRACE_ERROR, TASK_INDEX_MQ_RCV, "cannot create message queue, errno: %d\n", errno);
       return MQ_FAILURE;
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
        
        SEM_POST(gMqRespondSem, semRet);
        if(semRet == -1)
        {
            TRACE(TRACE_ERROR, TASK_INDEX_MQ_RCV, "sem_post() failed\n");
        }
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
        
        //sem_getvalue(&gMqRequestSem, &semVal);
        //TRACE(TRACE_DEBUG, threadIndex, "gMqRequestSem value is %d\n", semVal);

        SEM_WAIT(gMqRequestSem, semRet);
        if(semRet == -1)
        {
            TRACE(TRACE_ERROR, threadIndex, "sem_wait() failed\n");
        }

        //sem_getvalue(&gMqRequestSem, &semVal);
        //TRACE(TRACE_DEBUG, threadIndex, "gMqRequestSem value is %d\n", semVal);

        mqReqMsg.msgType = MQ_REQ_STRING;
        strcpy(mqReqMsg.msg, "Greeting from mqSnd");
        mqRetVal = mq_send(gMqQid, (char*)&mqReqMsg, sizeof(mqReqMsg), 0);
        if(mqRetVal != 0)
        {
            TRACE(TRACE_ERROR, threadIndex, "mq_send error: %d\n", errno);
        }
        else
        {
            SEM_WAIT(gMqRespondSem, semRet);
            if(semRet == -1)
            {
                TRACE(TRACE_ERROR, threadIndex, "sem_wait() failed\n");
            }
            else
            {
                TRACE(TRACE_INFO, threadIndex, "mq respond message: %s\n", gMqRespMsg.msgResp);
            }
        }

        TimerAddAppUnit(threadIndex, MQ_SND_EVENT_TIMER, NULL, 150);

        SEM_POST(gMqRequestSem, semRet);
        if(semRet == -1)
        {
            TRACE(TRACE_ERROR, threadIndex, "sem_post() failed\n");
        }

        //sem_getvalue(&gMqRequestSem, &semVal);
        //TRACE(TRACE_DEBUG, threadIndex, "gMqRequestSem value is %d\n", semVal);
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
    
    msgSt.msgParams = "this is from server, heartbeat!";
    msgSt.msgHead.msgLen = strlen(msgSt.msgParams);
    msgSt.msgHead.msgSrc = SERVER;
    msgSt.msgHead.msgDest = CLIENT;
    msgSt.msgHead.msgEnt = MSG_ENTITY_MAIN;
    msgSt.msgHead.msgAct = MSG_ACTION_MAIN_HEARTBEAT;
    msgSt.msgHead.frameCnt = 0;

    while(1)
    {
        msgUdpSocketSend(TASK_INDEX_HEARTBEAT, &msgSt);
        msgSt.msgHead.frameCnt++;

        sleep(1);
    }
}

    
msg_ret msgRcvHandleHeartBeat(msg_struct* msgSt)
{
    TRACE(TRACE_INFO, TASK_INDEX_HEARTBEAT, "receive heartbeat msg: %s\n", msgSt->msgParams);

    return MSG_SUCCESS;
}
