#include "signal_handler.h"
#include <signal.h>
#include "trace.h"
#include <string.h>
#include "threadManager.h"
#include <sys/prctl.h>
#include <errno.h>

thread_params gSignalThrParams = {24*1024, 0, "Sig", NULL, "signal.log", THREAD_MQ_NOT_USED, 0, 0};
int gSignalTaskIndex;

/*signal initilization, provide signal mask for all thread in the process*/
signal_ret SignalInit()
{
    int ret;
    sigset_t sigSet;

    /*init the signal mask*/        
    sigemptyset(&sigSet);
    sigaddset(&sigSet, SIGTERM);
    sigaddset(&sigSet, SIGUSR1);
    sigaddset(&sigSet, SIGUSR2);

    if((ret = pthread_sigmask(SIG_BLOCK, &sigSet, NULL) != 0))
    {
        TRACE(TRACE_ERROR, TASK_INDEX_MAIN, "cannot set signal maks, %s\n", strerror(ret));
        return SIGNAL_FAILURE;
    }

    if(threadManagerCreateThread(taskSingal, &gSignalThrParams, &gSignalTaskIndex) == THREAD_FAILURE)
    {   
        TRACE(TRACE_ERROR, TASK_INDEX_MAIN, "cannot create thread, exit!");
        return SIGNAL_FAILURE; 
    }
    TRACE(TRACE_INFO, TASK_INDEX_SIGNAL, "signal task index: %d\n", TASK_INDEX_SIGNAL);

    return SIGNAL_SUCCESS;
}


/*only taskSingal thread is able to deal with signal: SIGTERM, SIGUSR2,SIGUSR1*/
void* taskSingal(void* arg)
{
    sleep(1);

    thread_params* pThrParams = (thread_params*) arg;
    int ret;
    sigset_t sigSet;
    siginfo_t sigInfo;
    int sigCaught;
    char threadName[32];

    if((ret=prctl(PR_SET_NAME, pThrParams->threadName)) != 0)
    {   
        TRACE(TRACE_ERROR, TASK_INDEX_SIGNAL, "cannot set thread name, %s\n", strerror(ret));
    }   
    prctl(PR_GET_NAME, threadName);
    TRACE(TRACE_INFO, TASK_INDEX_SIGNAL, "set thread name %s\n", threadName);

    /*init the signal mask*/            
    sigemptyset(&sigSet);
    sigaddset(&sigSet, SIGTERM);
    sigaddset(&sigSet, SIGUSR1);
    sigaddset(&sigSet, SIGUSR2);

    while(1)
    {   
        sigCaught = sigwaitinfo(&sigSet, &sigInfo);
        if(sigCaught == -1) 
        {   
            TRACE(TRACE_ERROR, TASK_INDEX_SIGNAL, "sigwaitinfo return error: %d\n", errno);
        }   
        else
        {   
            TRACE(TRACE_INFO, TASK_INDEX_SIGNAL, "receive signal %d val %d from process %d\n", 
                 sigInfo.si_signo, sigInfo.si_value.sival_int, sigInfo.si_pid);
        }   
    }   
}
