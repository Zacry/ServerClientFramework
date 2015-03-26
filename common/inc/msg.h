#ifndef _MSG_H
#define _MSG_H

#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>

#define MSG_MAX_LENGTH 1024
#define TASK_INDEX_MSG_UDP_RCV (gMsgUdpRcvTaskIndex)

typedef enum{
    MSG_SUCCESS = 0,
    MSG_FAILURE
}msg_ret;

typedef enum{
    MSG_TRUE = 0,
    MSG_FALSE
}msg_bool;

typedef enum{
    CLIENT = 0,
    SERVER
}msg_node;

/* Define msg head structure */
typedef struct{
    msg_node msgSrc;
    msg_node msgDest;
    int msgEnt;
    int msgAct;
    int msgLen;
    int frameCnt;
}msg_head;

/* Define msg, head + body */
typedef struct{
    msg_head msgHead;
    char* msgParams;
}msg_struct;

typedef struct{
    int msgSock;
    struct sockaddr_in msgSockAddr;
}msg_socket;

#define ACTION_NUM_MAX 2
typedef msg_ret (*MSG_HANDLER)(msg_struct* msgSt);

typedef struct{
    int actionMax;
    MSG_HANDLER handler[ACTION_NUM_MAX];
}msg_rcv_handler;

#define MSG_MUTEX_LOCK(mutex, taskIndex) {int retMutex;\
                                          if((retMutex=pthread_mutex_lock(mutex))!=0)\
                                          {\
                                             TRACE(TRACE_ERROR, taskIndex, "cannot lock mutex, error: %s\n", strerror(retMutex));\
                                             return MSG_FAILURE;\
                                          }\
                                         }

#define MSG_MUTEX_UNLOCK(mutex, taskIndex) {int retMutex;\
                                            if((retMutex=pthread_mutex_unlock(mutex))!=0)\
                                            {\
                                                TRACE(TRACE_ERROR, taskIndex, "cannot unlock mutex, error: %s\n", strerror(retMutex));\
                                                return MSG_FAILURE;\
                                            }\
                                           }


msg_ret msgUdpInit(int sendPort, int rcvPort);
msg_ret msgUdpSocketSend(int taskIndex, msg_struct* msgSt);
void* MsgUdpRcvTask(void*);
MSG_HANDLER msgGetHandlerFunction(int msgEnt, int msgAct);

#endif
