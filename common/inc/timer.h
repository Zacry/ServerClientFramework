#ifndef _TIMER_H
#define _TIMER_H

#include <pthread.h>
#include "trace.h"

#define TASK_INDEX_TIMER (gTimerTaskIndex)
/*periodic cycle in one sec*/
#define PERIODS_PER_SEC 100

typedef enum
{
    TIMER_SUCCESS = 0,
    TIMER_FAILURE,
    TIMER_FAILURE_INVALID_REMAIN_TIME
}timer_ret;

/* Indicate whether the timer list is free or not */
typedef enum
{
    TIMER_FREE = 0,
    TIMER_USED
}timer_list_node_status;

/* Define timer list node */
typedef struct str_timer_list_node
{
    struct str_timer_list_node* prev;
    struct str_timer_list_node* next;
    /*timer app information*/
    int taskId;
    timer_list_node_status status;
    int event;
    void (*CallBackFunction)(void*);
    int remainTime;
}timer_list_node;

/* Define timer list structure */
typedef struct
{
    timer_list_node* head;
    int count;
}timer_list;

/* Define timer wheel for each time granularity */
typedef struct
{
    int curStep;
    int maxStep;
    int stepSize;
    timer_list* wheelList;
}timer_wheel;

#define TIMER_MUTEX_LOCK(mutex) {int retMutex;\
                                 if((retMutex=pthread_mutex_lock(mutex))!=0)\
                                 {\
                                     TRACE(TRACE_ERROR, TASK_INDEX_TIMER, "cannot lock mutex, error: %s\n", strerror(retMutex));\
                                     return TIMER_FAILURE;\
                                 }\
                                }

#define TIMER_MUTEX_UNLOCK(mutex) {int retMutex;\
                                   if((retMutex=pthread_mutex_unlock(mutex))!=0)\
                                   {\
                                       TRACE(TRACE_ERROR, TASK_INDEX_TIMER, "cannot unlock mutex, error: %s\n", strerror(retMutex));\
                                       return TIMER_FAILURE;\
                                   }\
                                  }

/*Timer task initialization*/
timer_ret TimerInit();
void* TimerTask(void* arg);
timer_ret TimerProcessPeriod();

/*For external usage, API for modules to use timer*/
timer_ret TimerAddAppUnit(int taskId, int event, void (*CallBackFunction)(void*), int remainTime);

/*For internal usage, no need to use mutex*/
void TimerGetPositionForRemainTime(int remainTime, int* pWheelIndex, int* pStepToExpire, int* pRemainTimeLeft);
timer_ret TimerAddNodeToHead(timer_list_node* timerNode, timer_list* timerList);
timer_ret TimerDelNodeFromHead(timer_list_node** timerNode, timer_list* timerList);

#endif
