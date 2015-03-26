#ifndef _USERTHREAD_H
#define _USERTHREAD_H

#include "msg.h"

#define TASK_INDEX_MESSAGE (gMessageTaskIndex)
#define TASK_INDEX_MQ_RCV (gMqRcvTaskIndex)
#define TASK_INDEX_MQ_SND (gMqSndTaskIndex)
#define TASK_INDEX_HEARTBEAT (gHeartBeatTaskIndex)

/* Mq name should be unique in the system */
#define MQ_NAME "/mqQue-Server"
#define MQ_MAX_NUM 10
#define MQ_MAX_SIZE 100

/* Message task, use to identify the message type */
typedef enum{
    MSG_EVENT_SYNC_START = 0x1,
    MSG_EVENT_SYNC_End = 0x2,
    MSG_EVENT_SYNC_INFO = 0x4,
    MSG_EVENT_TIMER_EXPIRE = 0x8
}message_task_event;

/* Mq send/rcv task, use to identify respond message*/
#define MQ_MSG_SIZE 32
typedef struct{
    char msgResp[MQ_MSG_SIZE];
}mq_resp_msg;

/* Mq send/rcv task, use to identify request message*/
#define MQ_INDEX_MAX 20
typedef enum{
    MQ_REQ_INDEX_ALLOCATE = 0x1,
    MQ_REQ_INDEX_DEALLOCATE = 0x2,
    MQ_REQ_STRING = 0x4
}mq_req_type;

typedef struct{
    mq_req_type msgType;
    char msg[MQ_MSG_SIZE];
}mq_req_msg;

/* Mq send task, use to identify mq send event */
typedef enum{
    MQ_SND_EVENT_TIMER = 0x1
}mq_snd_task_event;

typedef enum{
    MQ_SUCCESS = 0,
    MQ_FAILURE
}mq_ret;

/* Define msg handle item */
typedef enum{
    MSG_ENTITY_MAIN = 0,
    MSG_ENTITY_COMM 
}msg_entity;

typedef enum{
    MSG_ACTION_MAIN_HEARTBEAT = 0,
    MSG_ACTION_MAIN_MAX
}msg_action_main;

typedef enum{
    MSG_ACTION_COMM_TEST1 = 0,
    MSG_ACTION_COMM_TEST2,
    MSG_ACTION_COMM_MAX
}msg_action_comm;

/* Define heartbeat info */
#define UDP_RCV_PORT 8880
#define UDP_SEND_PORT 8881

#define HEARTBEAT_MISS_CNT_MAX 10

typedef enum{
    HEARTBEAT_ATTACHED = 0,
    HEARTBEAT_NOT_ATTACHED
}heartbeat_attach;

typedef struct{
    int rcvCnt;
    int missCnt;
}heartbeat_data;

/* Struct to define task parameters*/
typedef struct{
    void* (*taskEntry)(void* arg);
    void* taskParam;
    int* taskIndex;
}user_defined_task_info;

/*thread: message, print message after getting event*/
void* taskMessage(void* arg);
void taskMessageEventHandler(int threadIndex, int event, char* pMsg);

/*thread: MqRcv, show request from receiving message, message server*/
mq_ret mqInit();
void* taskMqRcv(void* arg);

/*thread: MqSnd, send message to server, message*/
void* taskMqSnd(void* arg);
void taskMqSndEventHandler(int threadIndex, int event, char* pMsg);

/*thread: HeartBeat, send heartbeat message to opposite side*/
void* taskHeartBeat(void* arg);
msg_ret msgRcvHandleHeartBeat(msg_struct* msgSt);
#endif
