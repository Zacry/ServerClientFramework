#include "threadManager.h"
#include "trace.h"

/* Protect thread resources*/
pthread_mutex_t gThreadManagerMutex;
/* Global thread table stores all the threads*/
thread_manager_task_entry gTask[MAX_TASK_NUM];

/* Init thread resources*/
thread_ret_value threadManagerInit()
{
    int index;

    if(pthread_mutex_init(&gThreadManagerMutex, NULL) != 0)
    {
        TRACE(TRACE_ERROR, TASK_INDEX_MAIN, "cannot init gThreadManagerMutex\n");
    }
    
    MUTEX_LOCK(&gThreadManagerMutex);
    for(index=0; index<MAX_TASK_NUM; index++)
    {
        memset(gTask[index].tskName, 0, sizeof(gTask[index].tskName));
        gTask[index].free = THREAD_ENTRY_FREE;
        gTask[index].event = 0;
    }
    MUTEX_UNLOCK(&gThreadManagerMutex);
    
    return THREAD_SUCCESS;
}

/* Create thread and thread entry in the global thread table*/
thread_ret_value threadManagerCreateThread(thread_task_pointer threadStartAddr, thread_params* pThrParams, int* pThreadIndex)
{
    thread_ret_value retVal = THREAD_FAILURE;
    *pThreadIndex = TASK_INDEX_INVALID;

    if(pThrParams == NULL)
    {
        TRACE(TRACE_ERROR, TASK_INDEX_MAIN, "pThrParams is NULL\n");
    }
    else
    {
        /* Provide unique name for each thread */
        if(threadManagerFindEntry(pThrParams->threadName, pThreadIndex) == THREAD_FAILURE)
        {
            TRACE(TRACE_INFO, TASK_INDEX_MAIN, "Thread Parameters: \n\
                      \tstack size:%d\n\
                      \tpriority:%d\n\
                      \tthread name:%s\n\
                      \targs:%s\n\
                      \tmsg flag:%d\n\
                      \tmsg length:%d\n\
                      \tmsg num:%d\n", pThrParams->stackSize, pThrParams->priority, pThrParams->threadName, pThrParams->args,
                                       pThrParams->msgFlag, pThrParams->msgLength, pThrParams->msgMaxNum);

            if(threadManagerAddEntry(pThrParams, pThreadIndex) == THREAD_SUCCESS)
            {
                /*Add trace log for the thread*/
                TraceAddForTask(*pThreadIndex, pThrParams->logFile);

                pthread_t thrId;
                pthread_attr_t thrAttr;
                struct sched_param schParam, oSchParam;
                int ret;
                int maxPri;
                int minPri;

                schParam.sched_priority = pThrParams->priority;

                pthread_attr_init(&thrAttr);
                pthread_attr_setstacksize(&thrAttr, pThrParams->stackSize);
                pthread_attr_setdetachstate(&thrAttr, PTHREAD_CREATE_DETACHED);
                pthread_attr_setinheritsched(&thrAttr, PTHREAD_EXPLICIT_SCHED);
                
                if(pThrParams->priority == 0)
                {
                    /*priority of 0: non-realtime (timesliced)*/
                    pthread_attr_setschedpolicy(&thrAttr, SCHED_OTHER);
                    maxPri = sched_get_priority_max(SCHED_OTHER);
                    minPri = sched_get_priority_min(SCHED_OTHER);
                }
                else
                {
                    pthread_attr_setschedpolicy(&thrAttr, SCHED_RR);
                    maxPri = sched_get_priority_max(SCHED_RR);
                    minPri = sched_get_priority_min(SCHED_RR);
                }
                TRACE(TRACE_INFO, TASK_INDEX_MAIN, "max priority is %d\n", maxPri);
                TRACE(TRACE_INFO, TASK_INDEX_MAIN, "min priority is %d\n", minPri);
                pthread_attr_getschedparam(&thrAttr, &oSchParam);
                TRACE(TRACE_INFO, TASK_INDEX_MAIN, "cur priority is %d\n", oSchParam.sched_priority);

                pthread_attr_setschedparam(&thrAttr, &schParam);

                if(pthread_create(&thrId, &thrAttr, threadStartAddr, pThrParams) != 0)
                {
                    TRACE(TRACE_ERROR, TASK_INDEX_MAIN, "pthread_create return error %d: \n", ret, strerror(ret));
                    threadManagerDelEntry(pThrParams->threadName, NULL);
                    retVal = THREAD_FAILURE;
                }
                else
                {
                    retVal = THREAD_SUCCESS;
                }
            }
            else
            {
                TRACE(TRACE_ERROR, TASK_INDEX_MAIN, "cannot add entry in thread manager\n");
            }
        }
    }

    return retVal;
}


/* Manipulate global thread: find*/
thread_ret_value threadManagerFindEntry(char* uniqueName, void* entryIndex)
{
    MUTEX_LOCK(&gThreadManagerMutex);

    thread_ret_value retVal = THREAD_FAILURE;
    int index;
    int* retEntryIndex = (int*)entryIndex;
    
    if(uniqueName != NULL)
    {
        for(index=0; index<MAX_TASK_NUM; index++)
        {
            if(strcmp(uniqueName, gTask[index].tskName) == 0)
            {
                *retEntryIndex = index;
                retVal = THREAD_SUCCESS;
            }
        }
    }

    MUTEX_UNLOCK(&gThreadManagerMutex);

    return retVal;
}

/* Manipulate global thread: add*/
thread_ret_value threadManagerAddEntry(thread_params* pThrParams, void* entryIndex)
{
    MUTEX_LOCK(&gThreadManagerMutex);

    thread_ret_value retVal = THREAD_FAILURE;
    int retErr = 0;
    int index;
    int* retEntryIndex = (int*)entryIndex;
    
    if(pThrParams != NULL)
    {
        for(index=0; index<MAX_TASK_NUM; index++)
        {
            if(gTask[index].free == THREAD_ENTRY_FREE)
            {
                if((retErr = pthread_mutex_init(&gTask[index].tskMutex, NULL)) != 0)
                {
                    TRACE(TRACE_ERROR, TASK_INDEX_MAIN, "cannot init tskMutex errno %d: %s\n", retErr, strerror(retErr));
                    break;
                }
                
                if((retErr = pthread_cond_init(&gTask[index].eventCond, NULL)) != 0)
                {
                    TRACE(TRACE_ERROR, TASK_INDEX_MAIN, "cannot init eventCond errno %d: %s\n", retErr, strerror(retErr));
                    break;
                }

                if((retErr = pthread_mutex_init(&gTask[index].eventMutex, NULL)) != 0)
                {
                    TRACE(TRACE_ERROR, TASK_INDEX_MAIN, "cannot init eventMutex errno %d: %s\n", retErr, strerror(retErr));
                    break;
                }

                if(pThrParams->msgFlag == THREAD_MQ_USED)
                {
                    if((retErr = pthread_cond_init(&gTask[index].msgCond, NULL)) != 0)
                    {
                        TRACE(TRACE_ERROR, TASK_INDEX_MAIN, "cannot init msgCond errno %d: %s\n", retErr, strerror(retErr));
                        break;
                    }

                    gTask[index].msgLength = pThrParams->msgLength;
                    gTask[index].msgMaxNum = pThrParams->msgMaxNum;
                    gTask[index].pBase = (char*)malloc(gTask[index].msgLength * (gTask[index].msgMaxNum + 1));
                    gTask[index].pEnd = gTask[index].pBase + gTask[index].msgLength * (gTask[index].msgMaxNum + 1);
                    gTask[index].pRead = gTask[index].pBase;
                    gTask[index].pWrite = gTask[index].pBase;
                    gTask[index].overflow = 0;
                    gTask[index].msgNum = 0;
                }

                strcpy(gTask[index].tskName, pThrParams->threadName);
                gTask[index].free = THREAD_ENTRY_NOT_FREE;
                gTask[index].msgFlag = pThrParams->msgFlag;
                *retEntryIndex = index;
                retVal = THREAD_SUCCESS;
                break;
            }
        }
    }

    MUTEX_UNLOCK(&gThreadManagerMutex);

    return retVal;
}

/* Manipulate global thread: delete*/
thread_ret_value threadManagerDelEntry(char* uniqueName, void* entryIndex)
{
    MUTEX_LOCK(&gThreadManagerMutex);

    thread_ret_value retVal = THREAD_FAILURE;
    int index;
    
    if(uniqueName != NULL)
    {
        for(index=0; index<MAX_TASK_NUM; index++)
        {
            if(strcmp(uniqueName, gTask[index].tskName) == 0)
            {
                if(gTask[index].msgFlag == THREAD_MQ_USED)
                {
                    free(gTask[index].pBase);
                }
                
                memset(gTask[index].tskName, 0, sizeof(gTask[index].tskName));
                gTask[index].free = THREAD_ENTRY_FREE;
                gTask[index].event = 0;
                retVal = THREAD_SUCCESS;
            }
        }
    }

    MUTEX_UNLOCK(&gThreadManagerMutex);

    return retVal;
}

/* Send pure event to target thread */
thread_ret_value threadManagerSendEvent(int threadIndex, int event)
{
    MUTEX_LOCK(&gTask[threadIndex].eventMutex);
    gTask[threadIndex].event |= event;
    pthread_cond_signal(&gTask[threadIndex].eventCond);
    MUTEX_UNLOCK(&gTask[threadIndex].eventMutex);

    return THREAD_SUCCESS;
}

/* Send event and mq to target thread */
thread_ret_value threadManagerSendEventAndMsg(int threadIndex, int event, char* pMsg, thread_wait_flag waitFlag)
{
    MUTEX_LOCK(&gTask[threadIndex].eventMutex);

    if(gTask[threadIndex].msgNum == gTask[threadIndex].msgMaxNum)
    {
        if(waitFlag == THREAD_EVENT_NO_WAIT)
        {
            gTask[threadIndex].overflow++;
            MUTEX_UNLOCK(&gTask[threadIndex].eventMutex);
            return THREAD_FAILURE;
        }
        else
        {
            while(1)
            {
                if(gTask[threadIndex].msgNum < gTask[threadIndex].msgMaxNum)
                {
                    memcpy(gTask[threadIndex].pWrite, &pMsg, gTask[threadIndex].msgLength);

                    gTask[threadIndex].pWrite += gTask[threadIndex].msgLength;
                    if(gTask[threadIndex].pWrite == gTask[threadIndex].pEnd)
                    {
                        gTask[threadIndex].pWrite = gTask[threadIndex].pBase;
                    }
                    gTask[threadIndex].msgNum++;

                    gTask[threadIndex].event |= event;
                    break;
                }

                pthread_cond_wait(&gTask[threadIndex].msgCond, &gTask[threadIndex].eventMutex);
            }
        }
    }
    else
    {
        memcpy(gTask[threadIndex].pWrite, &pMsg, gTask[threadIndex].msgLength);

        gTask[threadIndex].pWrite += gTask[threadIndex].msgLength;
        if(gTask[threadIndex].pWrite == gTask[threadIndex].pEnd)
        {
            gTask[threadIndex].pWrite = gTask[threadIndex].pBase;
        }
        gTask[threadIndex].msgNum++;
        
        gTask[threadIndex].event |= event;
    }

    pthread_cond_signal(&gTask[threadIndex].eventCond);
    MUTEX_UNLOCK(&gTask[threadIndex].eventMutex);

    return THREAD_SUCCESS;
}

/* Threads wait for events. Pure event and event with msg are distinguished*/
thread_ret_value threadManagerRcvEvent(int threadIndex, int eventPure, int eventMsg, thread_wait_flag waitFlag, EVENT_HANDLER eventHandler)
{
    thread_ret_value retVal = THREAD_FAILURE;
    int event = eventPure|eventMsg;
    int rcvEvent;

    MUTEX_LOCK(&gTask[threadIndex].eventMutex);

    if(waitFlag == THREAD_EVENT_NO_WAIT)
    {
        if((gTask[threadIndex].event & event) != 0)
        {
            rcvEvent = gTask[threadIndex].event & event;
            gTask[threadIndex].event &= ~rcvEvent;
            threadManagerHandleRcvEvent(threadIndex, eventPure, eventMsg, eventHandler, rcvEvent);

            retVal = THREAD_SUCCESS;
        }
        else
        {
            retVal = THREAD_FAILURE;
        }
        MUTEX_UNLOCK(&gTask[threadIndex].eventMutex);
    }
    else
    {
        while(1)
        {
            if((gTask[threadIndex].event & event) != 0)
            {
                rcvEvent = gTask[threadIndex].event & event;
                gTask[threadIndex].event &= ~rcvEvent;
                threadManagerHandleRcvEvent(threadIndex, eventPure, eventMsg, eventHandler, rcvEvent);

                MUTEX_UNLOCK(&gTask[threadIndex].eventMutex);
                retVal = THREAD_SUCCESS;
                break;
            }

            pthread_cond_wait(&gTask[threadIndex].eventCond, &gTask[threadIndex].eventMutex);
        }
    }

    return retVal;
}

/* Distinguishly deal with pure event and event with msg*/
void threadManagerHandleRcvEvent(int threadIndex, int eventPure, int eventMsg, EVENT_HANDLER eventHandler, int rcvEvent)
{
    char* pMsg;

    if(rcvEvent & eventPure)
    {
        eventHandler(threadIndex, rcvEvent & eventPure, NULL);
    }

    if(rcvEvent & eventMsg)
    {
        while(gTask[threadIndex].msgNum != 0)
        {
            memcpy(&pMsg, gTask[threadIndex].pRead, gTask[threadIndex].msgLength);
            eventHandler(threadIndex, rcvEvent & eventMsg, pMsg);
            
            gTask[threadIndex].msgNum--;
            gTask[threadIndex].pRead += gTask[threadIndex].msgLength;
            if(gTask[threadIndex].pRead == gTask[threadIndex].pEnd)
            {
                gTask[threadIndex].pRead = gTask[threadIndex].pBase;
            }
        }

        pthread_cond_signal(&gTask[threadIndex].msgCond);
    }
}
