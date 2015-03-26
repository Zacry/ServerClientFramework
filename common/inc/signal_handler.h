#ifndef _SIGNAL_HANDLER_H
#define _SIGNAL_HANDLER_H

#define TASK_INDEX_SIGNAL (gSignalTaskIndex)

typedef enum
{
    SIGNAL_SUCCESS = 0,
    SIGNAL_FAILURE
}signal_ret;

signal_ret SignalInit();
void* taskSingal(void* arg);

#endif
