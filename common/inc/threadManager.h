#ifndef _THREADMANAGER_H
#define _THREADMANAGER_H

#include <pthread.h>
#include <stdio.h>
#include <string.h>

#define MAX_TASK_NUM 100
#define MAX_MSG_NUM 100
#define MAX_NAME_LENGTH 32
#define TASK_INDEX_INVALID (-1)

typedef void*(*thread_task_pointer (void*));
typedef void (*EVENT_HANDLER)(int threadIndex, int event, char* pMsg);

typedef enum{
    THREAD_SUCCESS = 0,
    THREAD_FAILURE = 1
}thread_ret_value;

/* Indicate whether the task entry is free*/
typedef enum{
    THREAD_ENTRY_FREE = 0,
    THREAD_ENTRY_NOT_FREE = 1
}thread_entry_is_free;

/* Indicate whether the thread will use mq and other thread could send mq to this one*/
typedef enum{
    THREAD_MQ_USED = 0,
    THREAD_MQ_NOT_USED
}thread_msg_flag;

/* Indicate whether the thread needs to block waiting for event */
typedef enum{
    THREAD_EVENT_NO_WAIT = 0,
    THREAD_EVENT_WAIT = 1
}thread_wait_flag;


typedef struct{
    char tskName[MAX_NAME_LENGTH];
    thread_entry_is_free free;
    /* Protect resource in corresponding thread*/		
    pthread_mutex_t tskMutex;
    /* For msg queue usage*/
    thread_msg_flag msgFlag;
    char* pRead;
    char* pWrite;
    char* pBase;
    char* pEnd;
    int msgLength;
    int msgMaxNum;
    int overflow;
    int msgNum;
    pthread_cond_t msgCond;
    /* For event usage*/
    int event;
    pthread_cond_t eventCond;
    pthread_mutex_t eventMutex;
}thread_manager_task_entry;

/* Each thread needs to provide the parameters to start*/
typedef struct
{
    int stackSize;
    int priority;
    char* threadName;
    char* args;
    char* logFile;
    thread_msg_flag msgFlag;
    int msgLength;
    int msgMaxNum;
}thread_params;

#define MUTEX_LOCK(mutex) {int retMutex;\
                           if((retMutex=pthread_mutex_lock(mutex))!=0)\
                           {\
                               TRACE(TRACE_ERROR, TASK_INDEX_MAIN, "cannot lock mutex, error: %s\n", strerror(retMutex));\
                               return THREAD_FAILURE;\
                           }\
                          }

#define MUTEX_UNLOCK(mutex) {int retMutex;\
                             if((retMutex=pthread_mutex_unlock(mutex))!=0)\
                             {\
                                 TRACE(TRACE_ERROR, TASK_INDEX_MAIN, "cannot unlock mutex, error: %s\n", strerror(retMutex));\
                                 return THREAD_FAILURE;\
                             }\
                            }

thread_ret_value threadManagerInit();

/*For external usage, create thread*/
thread_ret_value threadManagerCreateThread(thread_task_pointer threadStartAddr, thread_params* pThrParams, int* threadIndex);

/*For external usage, send and recv event, msg*/
thread_ret_value threadManagerSendEvent(int threadIndex, int event);
thread_ret_value threadManagerSendEventAndMsg(int threadIndex, int event, char* pMsg, thread_wait_flag waitFlag);
thread_ret_value threadManagerRcvEvent(int threadIndex, int eventPure, int eventMsg, thread_wait_flag waitFlag, EVENT_HANDLER eventHandler);

/*For internal usage, thread handler recv event and msg*/
void threadManagerHandleRcvEvent(int threadIndex, int eventPure, int eventMsg, EVENT_HANDLER eventHandler, int rcvEvent);

/*For internal usage, deal with task entry*/
thread_ret_value threadManagerFindEntry(char* uniqueName, void* entryIndex);
thread_ret_value threadManagerAddEntry(thread_params* pThrParams, void* entryIndex);
thread_ret_value threadManagerDelEntry(char* uniqueName, void* entryIndex);
#endif
