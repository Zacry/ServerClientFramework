#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <sys/types.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include "threadManager.h"
#include "trace.h"
#include "userThread.h"   
#include "timer.h"
#include "signal_handler.h"
#include "errno.h"
#include "msg.h"

extern char gSwversionTime[];
extern char gSwversionBuilder[];

extern user_defined_task_info gUserDefinedTask[];
extern int gUserDefinedTaskNum;

int main()
{
    int* pTaskAssignedIndex;
    int localIndex;

    if(TraceInit() == TRACE_FAILURE)
    {
        TRACE(TRACE_ERROR, TASK_INDEX_MAIN, "cannot init trace, exit!");
        return -1;
    }

    threadManagerInit();
    
    TRACE(TRACE_INFO, TASK_INDEX_MAIN, "------------------------------------------------------\n");
    TRACE(TRACE_INFO, TASK_INDEX_MAIN, "              swversion information\n");
    TRACE(TRACE_INFO, TASK_INDEX_MAIN, "Build Time: %s\n", gSwversionTime);
    TRACE(TRACE_INFO, TASK_INDEX_MAIN, "Build User: %s\n", gSwversionBuilder);
    TRACE(TRACE_INFO, TASK_INDEX_MAIN, "Task Numbe: %d\n", gUserDefinedTaskNum);
    TRACE(TRACE_INFO, TASK_INDEX_MAIN, "------------------------------------------------------\n");

    if(SignalInit() == SIGNAL_FAILURE)
    {
        TRACE(TRACE_ERROR, TASK_INDEX_MAIN, "cannot init signal handler\n");
        return -1;
    }

    if(TimerInit() == TIMER_FAILURE)
    {
        TRACE(TRACE_ERROR, TASK_INDEX_MAIN, "cannot init timer, exit!");
        return -1;
    }

    if(msgUdpInit(UDP_SEND_PORT, UDP_RCV_PORT) == MSG_FAILURE)
    {
        TRACE(TRACE_ERROR, TASK_INDEX_MAIN, "cannot init msg, exit!");
        return -1;
    }

    if(mqInit() == MQ_FAILURE)
    {
        TRACE(TRACE_ERROR, TASK_INDEX_MAIN, "cannot init mq, exit!");
        return -1;
    }    

    /*Start task in loop*/
    for(localIndex=0; localIndex<gUserDefinedTaskNum; localIndex++)
    {
        pTaskAssignedIndex = gUserDefinedTask[localIndex].taskIndex;
        if(threadManagerCreateThread(gUserDefinedTask[localIndex].taskEntry, (thread_params*)gUserDefinedTask[localIndex].taskParam, pTaskAssignedIndex) == THREAD_FAILURE)
        {
            TRACE(TRACE_ERROR, TASK_INDEX_MAIN, "cannot create thread, exit!");
            return -1;
        }
        TRACE(TRACE_INFO, *pTaskAssignedIndex, "%s task index: %d\n", ((thread_params*)(gUserDefinedTask[localIndex].taskParam))->threadName, *pTaskAssignedIndex);
    }

    while(1)
    {
        sleep(1);
    }

    return 0;
}

