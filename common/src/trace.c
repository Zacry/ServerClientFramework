#include "trace.h"
#include <stdarg.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <sys/prctl.h>
#include <time.h>
#include <sys/time.h>
#include <errno.h>

/* Store FILE* and file name for each thread*/
FILE* gLogFp[MAX_TASK_TRACE_NUM];  
char* gLogFile[MAX_TASK_TRACE_NUM];

/* Store FILE* and file name for main task*/
char* gMainFile="main.log";
FILE* gMainFp;

char gTimeStr[64];

sem_t gTraceSem;

/* Flag to decide whether time should be displayed for each log string*/
int gTraceFlagTime = 1;

/* Flag to decide whether info or debug level log should be displayed*/
int gTraceFlagInfo = 1;
int gTraceFlagDebug = 1;

/* Init trace environment*/
trace_ret TraceInit()
{
    int index;
    int retVal;
    trace_ret traceRet = TRACE_SUCCESS;
    for(index=0; index<MAX_TASK_TRACE_NUM; index++)
    {
        gLogFp[index] = NULL;
    }

    gMainFp = fopen(gMainFile, "w+");
    memset(gTimeStr, 0, sizeof(gTimeStr));

    /*init trace sem*/
    retVal = sem_init(&gTraceSem, 0, 1);
    if(retVal == -1)
    {
       TRACE(TRACE_ERROR, TASK_INDEX_MAIN, "cannot create trace sem, errno: %d\n", errno);
       traceRet = TRACE_FAILURE;
    }

    return traceRet;
}

/* Add corresponding trace file to each thread*/
void TraceAddForTask(int taskIndex, char* logFile)
{
    gLogFile[taskIndex] = logFile;
    if(gLogFp[taskIndex] != NULL)
    {
        fclose(gLogFp[taskIndex]);
    }

    gLogFp[taskIndex] = fopen(gLogFile[taskIndex], "w+");
    if(gLogFp[taskIndex] == NULL) 
    {   
        return;
    }   
}

/* Display variable trace information to thread-specific log file based on tracelevel*/
void TraceLog(trace_level traceLevel, int taskIndex, const char* func, int lineNum, const char * format, ...)
{
    char gMsg[MSG_LENGTH];
    FILE* fp;
    gMsg[0] = 0;

    if(!(traceLevel == TRACE_ERROR ||
        (traceLevel == TRACE_INFO && gTraceFlagInfo == 1) ||
        (traceLevel == TRACE_DEBUG && gTraceFlagDebug == 1)))
    {
        return;
    }

    if(taskIndex == TASK_INDEX_MAIN)
    {
        if(gMainFp == NULL)
        {   
            gMainFp = fopen(gMainFile, "w+");
            if(gMainFp == NULL) 
            {   
                return;
            }   
        }
        fp = gMainFp;
    }
    else
    {
        if(gLogFp[taskIndex] == NULL)
        {   
            gLogFp[taskIndex] = fopen(gLogFile[taskIndex], "w+");
            if(gLogFp[taskIndex] == NULL) 
            {   
                return;
            }   
        }
        fp = gLogFp[taskIndex];
    }

    if(format == NULL)
    {   
        return;
    }   

    /* Display time in detailed */
    if(gTraceFlagTime != 0)
    {
        time_t curTm;
        struct timeval curTmVal;
        struct tm* localTm;

        time(&curTm);
        gettimeofday(&curTmVal, NULL);
        localTm = localtime(&curTm);
        sprintf(gMsg, "%d-%d-%d %d:%d:%d  %dms, ", localTm->tm_year - 100 + 2000, localTm->tm_mon + 1, localTm->tm_mday,
                localTm->tm_hour, localTm->tm_min, localTm->tm_sec, (int)curTmVal.tv_usec/1000);
    }
    
    sprintf(gMsg + strlen(gMsg), "In %s:%d, ", func, lineNum);
    
    va_list varg;
    va_start(varg, format);
    vsprintf(gMsg + strlen(gMsg), format, varg);
    va_end(varg);

    TraceWriteToFile(gMsg, fp);
}

void TraceWriteToFile(char* gMsg, FILE* fp)
{
    ssize_t msgLength = 0;
    printf(gMsg);
    msgLength = fwrite(gMsg, sizeof(char), strlen(gMsg), fp);
    fflush(fp);
    if(fsync(fileno(fp)) == -1)
    {
        /*In case the log file is deleted*/
        fp = NULL;
        return;
    }
}



