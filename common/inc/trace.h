#ifndef _TRACE_H
#define _TRACE_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include "sem.h"

#define MSG_LENGTH 1024
#define MAX_TASK_TRACE_NUM 100
#define TASK_INDEX_MAIN -1

typedef enum{
    TRACE_SUCCESS = 0,
    TRACE_FAILURE
}trace_ret;

typedef enum{
    TRACE_ERROR = 0,
    TRACE_INFO,
    TRACE_DEBUG
}trace_level;

#define TRACE(traceLevel, taskIndex, format, ...) {TraceLog(traceLevel, taskIndex, __FUNCTION__, __LINE__, format, ##__VA_ARGS__);}

/*API for other module*/
trace_ret TraceInit();
void TraceAddForTask(int taskIndex, char* logFile);
void TraceLog(trace_level traceLevel, int taskIndex, const char* func, int lineNum, const char * format, ...);

/*Function for internal usage*/
void TraceWriteToFile(char* gMsg, FILE* fp);
#endif
