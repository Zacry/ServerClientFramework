#include "timer.h"
#include <stdlib.h>
#include "threadManager.h"
#include <sys/prctl.h>
#include <sys/times.h>
#include <unistd.h>

pthread_mutex_t gTimerMutex;

int gTimerTaskIndex;
thread_params gTimerThrParams = {24*1024, 0, "Timer", NULL, "timer.log", THREAD_MQ_NOT_USED, 0, 0};

/* Define timer stage size: 1s, 60s, 60min, 24hour, 7day */
int gTimerStepSize[] = {PERIODS_PER_SEC, 60, 60, 24, 7};
int gTimerWheelNumMax = sizeof(gTimerStepSize)/sizeof(int);

/* Define timer list for each timer step in the timer wheel */
int gTimerListForAllTimerStepNum;

/* Define max timer node to allocate for all threads */
int gTimerListNodeNumMax = 100; /*can be set to other values*/

timer_wheel* gTimerWheel = NULL;
timer_list* gTimerListForAllTimerStep = NULL;
timer_list_node* gTimerListNodeAvailableForAllThreads = NULL;

/* Initlize timer resources */
timer_ret TimerInit()
{
    int* pTaskIndex = &gTimerTaskIndex;
    int wheelIndex = 0;
    int stepSum = 0;
    int localIndex;

    if(pthread_mutex_init(&gTimerMutex, NULL) != 0)
    {   
        TRACE(TRACE_ERROR, TASK_INDEX_MAIN, "cannot init gTimerMutex\n");
        return TIMER_FAILURE;
    }       

    TIMER_MUTEX_LOCK(&gTimerMutex);

    gTimerWheel = (timer_wheel*)malloc(gTimerWheelNumMax * sizeof(timer_wheel));
    if(gTimerWheel == NULL)
    {
        return TIMER_FAILURE;
    }

    for(wheelIndex=0; wheelIndex<gTimerWheelNumMax; wheelIndex++)
    {
        gTimerWheel[wheelIndex].curStep = 0;
        gTimerWheel[wheelIndex].maxStep = gTimerStepSize[wheelIndex];
        if(wheelIndex == 0)
        {
            gTimerWheel[wheelIndex].stepSize = 1;
        }
        else
        {
            gTimerWheel[wheelIndex].stepSize = gTimerWheel[wheelIndex-1].maxStep * gTimerWheel[wheelIndex-1].stepSize;
        }

        stepSum += gTimerWheel[wheelIndex].maxStep;
    }

    gTimerListForAllTimerStepNum = stepSum;
    gTimerListForAllTimerStep = (timer_list*)malloc(gTimerListForAllTimerStepNum * sizeof(timer_list));
    if(gTimerListForAllTimerStep == NULL)
    {
        free(gTimerWheel);
        gTimerWheel = NULL;
        TIMER_MUTEX_UNLOCK(&gTimerMutex);
        return TIMER_FAILURE;
    }

    for(localIndex=0; localIndex<gTimerListForAllTimerStepNum; localIndex++)
    {
        gTimerListForAllTimerStep[localIndex].head = NULL;
        gTimerListForAllTimerStep[localIndex].count = 0;
    }

    gTimerListNodeAvailableForAllThreads = (timer_list_node*)malloc(gTimerListNodeNumMax * sizeof(timer_list_node));
    if(gTimerListNodeAvailableForAllThreads == NULL)
    {
        free(gTimerListForAllTimerStep);
        gTimerListForAllTimerStep = NULL;
        free(gTimerWheel);
        gTimerWheel = NULL;
        TIMER_MUTEX_UNLOCK(&gTimerMutex);
        return TIMER_FAILURE;
    }

    for(localIndex=0; localIndex<gTimerListNodeNumMax; localIndex++)
    {
        gTimerListNodeAvailableForAllThreads[localIndex].prev = NULL;
        gTimerListNodeAvailableForAllThreads[localIndex].next = NULL;
        gTimerListNodeAvailableForAllThreads[localIndex].taskId = 0;
        gTimerListNodeAvailableForAllThreads[localIndex].status = TIMER_FREE;
        gTimerListNodeAvailableForAllThreads[localIndex].event = 0;
        gTimerListNodeAvailableForAllThreads[localIndex].CallBackFunction = NULL;
        gTimerListNodeAvailableForAllThreads[localIndex].remainTime = 0;
    }

    stepSum = 0;
    for(wheelIndex=0; wheelIndex<gTimerWheelNumMax; wheelIndex++)
    {
        gTimerWheel[wheelIndex].wheelList = &gTimerListForAllTimerStep[stepSum];
        stepSum += gTimerWheel[wheelIndex].maxStep;
    }

    if(threadManagerCreateThread(TimerTask, &gTimerThrParams, pTaskIndex) == THREAD_FAILURE)
    {   
        TRACE(TRACE_ERROR, TASK_INDEX_MAIN, "cannot create thread, exit!");
        TIMER_MUTEX_UNLOCK(&gTimerMutex);
        return TIMER_FAILURE; 
    }   
    TRACE(TRACE_INFO, TASK_INDEX_TIMER, "timer task index: %d\n", TASK_INDEX_TIMER);

    TIMER_MUTEX_UNLOCK(&gTimerMutex);
    return TIMER_SUCCESS;
}

/* TimerTask: clock marches every timer tick */
void* TimerTask(void* arg)
{
    sleep(1);

    thread_params* pThrParams = (thread_params*) arg;
    int ret;
    long ticksPerPeriod;
    long msPerPeriod;
    struct timespec sleepTime;
    clock_t startTick;
    clock_t endTick;
    int periodNum;

    TRACE(TRACE_INFO, TASK_INDEX_TIMER, "set thread name %s\n", pThrParams->threadName);
    if((ret=prctl(PR_SET_NAME, pThrParams->threadName)) != 0)
    {   
        TRACE(TRACE_ERROR, TASK_INDEX_TIMER, "cannot set thread name, %s", strerror(ret));
    }
    
    ticksPerPeriod = sysconf(_SC_CLK_TCK)/PERIODS_PER_SEC;
    TRACE(TRACE_INFO, TASK_INDEX_TIMER, "ticksPerPeriod is %d\n", ticksPerPeriod);

    msPerPeriod = 1000000 / PERIODS_PER_SEC;
    sleepTime.tv_sec = (time_t)msPerPeriod / 100000;
    sleepTime.tv_nsec = (msPerPeriod % 1000000) * 1000;

    while(1)
    {
        startTick = times(NULL);
        nanosleep(&sleepTime, NULL);
        endTick = times(NULL);

        if(startTick < endTick)
        {
            periodNum = (endTick - startTick) / ticksPerPeriod;
        }
        else
        {
            periodNum = 1;
        }

        while(periodNum != 0)
        {
            TimerProcessPeriod();
            periodNum--;
        }
    }
}

/* Process timer tick */
timer_ret TimerProcessPeriod()
{
    TIMER_MUTEX_LOCK(&gTimerMutex);

    int wheelIndex;
    int innerWheelIndex;
    int timerListCount;
    int nextStage = 1;
    int remainTime;
    int stepToExpire;
    int stepExpirePoint;
    int remainTimeLeft;
    timer_list_node* tempNode;
    timer_ret retVal;
    char str[100];
    char* strBase = str;
    int localIndex;

    for(wheelIndex=0; wheelIndex<gTimerWheelNumMax; wheelIndex++)
    {
        if(nextStage == 1)
        {
            nextStage = 0;
        }
        else
        {
            continue;
        }

        gTimerWheel[wheelIndex].curStep++;
        if(gTimerWheel[wheelIndex].curStep == gTimerWheel[wheelIndex].maxStep)
        {
            nextStage = 1;
            gTimerWheel[wheelIndex].curStep = 0;
        }

        timerListCount = gTimerWheel[wheelIndex].wheelList[gTimerWheel[wheelIndex].curStep].count;
        while(timerListCount != 0)
        {
            if(TimerDelNodeFromHead(&tempNode, &gTimerWheel[wheelIndex].wheelList[gTimerWheel[wheelIndex].curStep]) == TIMER_FAILURE)
            {
                break;
            }

            remainTime = tempNode->remainTime;

            if(remainTime == 0)
            {
                sprintf(str, "timer hit ");
                for(localIndex=gTimerWheelNumMax-1; localIndex>=0; localIndex--)
                {
                    sprintf(str + strlen(str), "%3d :", gTimerWheel[localIndex].curStep);
                }
                sprintf(str + strlen(str) - 1, "\n");
                TRACE(TRACE_DEBUG, tempNode->taskId, "%s", strBase);
                
                if(tempNode->CallBackFunction != NULL)
                {
                    tempNode->CallBackFunction(tempNode);
                }

                if(tempNode->event != 0 && tempNode->taskId != TASK_INDEX_INVALID)
                {
                    threadManagerSendEvent(tempNode->taskId, tempNode->event);
                }

                tempNode->prev = NULL;
                tempNode->next = NULL;
                tempNode->taskId = 0;
                tempNode->status = TIMER_FREE;
                tempNode->event = 0;
                tempNode->CallBackFunction = NULL;
                tempNode->remainTime = 0;
            }
            else
            {
                TimerGetPositionForRemainTime(remainTime, &innerWheelIndex, &stepToExpire, &remainTimeLeft);
                tempNode->remainTime = remainTimeLeft;
                
                stepExpirePoint = (gTimerWheel[innerWheelIndex].curStep + stepToExpire) % gTimerWheel[innerWheelIndex].maxStep;
                retVal = TimerAddNodeToHead(tempNode, &gTimerWheel[innerWheelIndex].wheelList[stepExpirePoint]); 

                if(retVal == TIMER_FAILURE)
                {
                    tempNode->prev = NULL;
                    tempNode->next = NULL;
                    tempNode->taskId = 0;
                    tempNode->status = TIMER_FREE;
                    tempNode->event = 0;
                    tempNode->CallBackFunction = NULL;
                    tempNode->remainTime = 0;
                }
            }

            timerListCount--;
        }
   }

    TIMER_MUTEX_UNLOCK(&gTimerMutex);
    return TIMER_SUCCESS;
}

/* Thread adds timer node to timer wheel */
timer_ret TimerAddAppUnit(int taskId, int event, void (*CallBackFunction)(void*), int remainTime)
{
    TIMER_MUTEX_LOCK(&gTimerMutex);

    char str[100];
    char* strBase = str;
    int localIndex;

    sprintf(str, "add timer(%d) ", remainTime);
    for(localIndex=gTimerWheelNumMax-1; localIndex>=0; localIndex--)
    {
        sprintf(str + strlen(str), "%3d :", gTimerWheel[localIndex].curStep);
    }
    sprintf(str + strlen(str) - 1, "\n");
    TRACE(TRACE_DEBUG, taskId, "%s", strBase);

    if(remainTime >= gTimerWheel[gTimerWheelNumMax-1].maxStep*gTimerWheel[gTimerWheelNumMax-1].stepSize ||
       remainTime <= 0)
    {
        TIMER_MUTEX_UNLOCK(&gTimerMutex);
        return TIMER_FAILURE_INVALID_REMAIN_TIME;
    }
    else
    {
        int wheelIndex;
        int stepToExpire;
        int stepExpirePoint;
        int remainTimeLeft;
        timer_ret retVal;
        int nodeIndex;

        for(nodeIndex=0; nodeIndex<gTimerListNodeNumMax; nodeIndex++)
        {
            if(gTimerListNodeAvailableForAllThreads[nodeIndex].status == TIMER_FREE)
            {
                gTimerListNodeAvailableForAllThreads[nodeIndex].prev = NULL;
                gTimerListNodeAvailableForAllThreads[nodeIndex].next = NULL;
                gTimerListNodeAvailableForAllThreads[nodeIndex].taskId = taskId;
                gTimerListNodeAvailableForAllThreads[nodeIndex].status = TIMER_USED;
                gTimerListNodeAvailableForAllThreads[nodeIndex].event = event;
                gTimerListNodeAvailableForAllThreads[nodeIndex].CallBackFunction = CallBackFunction;
                gTimerListNodeAvailableForAllThreads[nodeIndex].remainTime = remainTime;
                break;
            }
        }

        if(nodeIndex == gTimerListNodeNumMax)
        {
            TIMER_MUTEX_UNLOCK(&gTimerMutex);
            return TIMER_FAILURE;
        }

        TimerGetPositionForRemainTime(remainTime, &wheelIndex, &stepToExpire, &remainTimeLeft);
        gTimerListNodeAvailableForAllThreads[nodeIndex].remainTime = remainTimeLeft;
        
        stepExpirePoint = (gTimerWheel[wheelIndex].curStep + stepToExpire) % gTimerWheel[wheelIndex].maxStep;
        retVal = TimerAddNodeToHead(&gTimerListNodeAvailableForAllThreads[nodeIndex], &gTimerWheel[wheelIndex].wheelList[stepExpirePoint]); 
        
        TIMER_MUTEX_UNLOCK(&gTimerMutex);
        return retVal;
    }
}

/* Identify position for timer node */
void TimerGetPositionForRemainTime(int remainTime, int* pWheelIndex, int* pStepToExpire, int* pRemainTimeLeft)
{
    int wheelLeftTime;
    int wheelIndex;
    int stepToExpire;

    for(wheelIndex=0; wheelIndex<gTimerWheelNumMax; wheelIndex++)
    {
        wheelLeftTime = gTimerWheel[wheelIndex].stepSize*(gTimerWheel[wheelIndex].maxStep-gTimerWheel[wheelIndex].curStep);
        if(remainTime < gTimerWheel[wheelIndex].stepSize*gTimerWheel[wheelIndex].maxStep)
        {
            stepToExpire = remainTime / gTimerWheel[wheelIndex].stepSize;
            if(wheelIndex != 0)
            {
                stepToExpire += 1;
            }

            remainTime = remainTime % gTimerWheel[wheelIndex].stepSize;
            break;
        }
        else
        {
            remainTime -= wheelLeftTime;
        }
    }

    *pWheelIndex = wheelIndex;
    *pStepToExpire = stepToExpire;
    *pRemainTimeLeft = remainTime;
}

/* Add timer node to corresponding timer list */
timer_ret TimerAddNodeToHead(timer_list_node* timerNode, timer_list* timerList)
{
    if(timerList == NULL || timerNode == NULL)
    {
        return TIMER_FAILURE;
    }
    else
    {
        timer_list_node* tempNode;
        if(timerList->head == NULL)
        {
            timerList->head = timerNode;
        }
        else
        {
            tempNode = timerList->head;
            timerList->head = timerNode;
            timerList->head->next = tempNode;
            tempNode->prev = timerList->head;
        }

        timerList->count++;

        return TIMER_SUCCESS;
    }
}


/* Delete timer node from corresponding timer list, make sure timerNode is in timerList before this function is called*/
timer_ret TimerDelNodeFromHead(timer_list_node** timerNode, timer_list* timerList)
{
    if(timerList == NULL)
    {
        return TIMER_FAILURE;
    }
    else
    {
        *timerNode = timerList->head;
        timerList->head = timerList->head->next;
        if(timerList->head != NULL)
        {
            timerList->head->prev = NULL;
        }
        timerList->count--;
        (*timerNode)->prev = NULL;
        (*timerNode)->next = NULL;

        return TIMER_SUCCESS;
    }
}
