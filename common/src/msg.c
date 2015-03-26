#include "msg.h"
#include "trace.h"
#include "threadManager.h"
#include <sys/prctl.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>

int gMsgUdpRcvTaskIndex;
thread_params gMsgUdpRcvThrParams = {24*1024, 0, "Msg", NULL, "msg.log", THREAD_MQ_NOT_USED, 0, 0};

/* Define send and rcv socket for the process */
msg_socket gMsgUdpSendSocket;
msg_socket gMsgUdpRcvSocket;
pthread_mutex_t gMsgUdpSendMutex;

/* Define global msg pdu */
msg_struct gMsgUdpRcvPdu;
char gMsgUdpRcvStr[sizeof(msg_head) + MSG_MAX_LENGTH];
char gMsgUdpSendStr[sizeof(msg_head) + MSG_MAX_LENGTH];

extern msg_rcv_handler gMsgRcvHandler[];
extern int gMsgRcvHandlerNum;

msg_ret msgUdpInit(int sendPort, int rcvPort)
{
    /*Set receive message buffer. Caculate: num of messages per process * num of max bytes per message * num of process*/
    int rcvBufferSize = 10*1024;

    if(pthread_mutex_init(&gMsgUdpSendMutex, NULL) != 0)
    {
        TRACE(TRACE_ERROR, TASK_INDEX_MAIN, "cannot init gMsgUdpSendMutex\n");
        return MSG_FAILURE;
    }

    gMsgUdpRcvPdu.msgParams = (char*)malloc(MSG_MAX_LENGTH);
    
    /*Init send and receive socket, connectionless*/
    if((gMsgUdpSendSocket.msgSock = socket(AF_INET, SOCK_DGRAM, 0)) == -1)
    {
        TRACE(TRACE_ERROR, TASK_INDEX_MAIN, "cannot create socket, errno: %d, %s\n", errno, strerror(errno));
        return MSG_FAILURE;
    }
    gMsgUdpSendSocket.msgSockAddr.sin_family = AF_INET;
    gMsgUdpSendSocket.msgSockAddr.sin_port = htons(sendPort);
    gMsgUdpSendSocket.msgSockAddr.sin_addr.s_addr = htonl(0x7f000001);

    if((gMsgUdpRcvSocket.msgSock = socket(AF_INET, SOCK_DGRAM, 0)) == -1)
    {
        TRACE(TRACE_ERROR, TASK_INDEX_MAIN, "cannot create socket, errno: %d, %s\n", errno, strerror(errno));
        return MSG_FAILURE;
    }
    gMsgUdpRcvSocket.msgSockAddr.sin_family = AF_INET;
    gMsgUdpRcvSocket.msgSockAddr.sin_port = htons(rcvPort);
    gMsgUdpRcvSocket.msgSockAddr.sin_addr.s_addr = htonl(0x7f000001);

    /*Set the receive socket buffer to accommodate the max message size*/
    if(setsockopt(gMsgUdpRcvSocket.msgSock, SOL_SOCKET, SO_RCVBUF, &rcvBufferSize , sizeof(rcvBufferSize)) == -1)
    {
        TRACE(TRACE_ERROR, TASK_INDEX_MAIN, "cannot set socket receive buffer, errno: %d, %s\n", errno, strerror(errno));
        return MSG_FAILURE;
    }

    /*Bind the receive socket to the specific address*/
    if(bind(gMsgUdpRcvSocket.msgSock, (struct sockaddr*)&gMsgUdpRcvSocket.msgSockAddr, sizeof(gMsgUdpRcvSocket.msgSockAddr)) == -1)
    {
        TRACE(TRACE_ERROR, TASK_INDEX_MAIN, "cannot bind socket address, errno: %d, %s\n", errno, strerror(errno));
        return MSG_FAILURE;
    }
    
    if(threadManagerCreateThread(MsgUdpRcvTask, &gMsgUdpRcvThrParams, &gMsgUdpRcvTaskIndex) == THREAD_FAILURE)
    {   
        TRACE(TRACE_ERROR, TASK_INDEX_MAIN, "cannot create thread, exit!");
        return MSG_FAILURE; 
    }   
    TRACE(TRACE_INFO, TASK_INDEX_MSG_UDP_RCV, "msg task index: %d\n", TASK_INDEX_MSG_UDP_RCV);
    
    return MSG_SUCCESS;
}


msg_ret msgUdpSocketSend(int taskIndex, msg_struct* msgSt)
{
    MSG_MUTEX_LOCK(&gMsgUdpSendMutex, taskIndex);

    if(msgSt == NULL)
    {
        TRACE(TRACE_ERROR, taskIndex, "the message is null\n");
        MSG_MUTEX_UNLOCK(&gMsgUdpSendMutex, taskIndex);
        return MSG_FAILURE;
    }

    memcpy(gMsgUdpSendStr, &msgSt->msgHead, sizeof(msg_head));
    memcpy(gMsgUdpSendStr + sizeof(msg_head), msgSt->msgParams, strlen(msgSt->msgParams));

    if(sendto(gMsgUdpSendSocket.msgSock, (char*)&gMsgUdpSendStr, sizeof(msg_head)+msgSt->msgHead.msgLen, 0, (struct sockaddr*)&gMsgUdpSendSocket.msgSockAddr, sizeof(gMsgUdpSendSocket.msgSockAddr)) == -1)
    {
        TRACE(TRACE_ERROR, taskIndex, "send message error, errno: %d, %s\n", errno, strerror(errno));
        MSG_MUTEX_UNLOCK(&gMsgUdpSendMutex, taskIndex);
        return MSG_FAILURE;
    }

    MSG_MUTEX_UNLOCK(&gMsgUdpSendMutex, taskIndex);
    return MSG_SUCCESS;
}


void* MsgUdpRcvTask(void* arg)
{
    sleep(3);

    thread_params* pThrParams = (thread_params*) arg;
    int ret;
    int rcvLen = 0;
    struct sockaddr_in fromSockAddr;
    int fromSockAddrLen;
    MSG_HANDLER handleFunc;
    

    TRACE(TRACE_INFO, TASK_INDEX_MSG_UDP_RCV, "set thread name %s\n", pThrParams->threadName);
    if((ret=prctl(PR_SET_NAME, pThrParams->threadName)) != 0)
    {   
        TRACE(TRACE_ERROR, TASK_INDEX_MSG_UDP_RCV, "cannot set thread name, %s", strerror(ret));
    }

    for(;;)
    {
        rcvLen = recvfrom(gMsgUdpRcvSocket.msgSock, gMsgUdpRcvStr, sizeof(msg_head) + MSG_MAX_LENGTH, 0, (struct sockaddr*)&fromSockAddr, (socklen_t*)&fromSockAddrLen);
        if(rcvLen <= 0)
        {
            TRACE(TRACE_ERROR, TASK_INDEX_MSG_UDP_RCV, "cannot receive mssage, close the receive socket\n");
            close(gMsgUdpRcvSocket.msgSock);
            return NULL;
        }

        if(rcvLen < sizeof(msg_head))
        {
            TRACE(TRACE_ERROR, TASK_INDEX_MSG_UDP_RCV, "the received length is less than the header size\n");
            continue;
        }

        memcpy(&gMsgUdpRcvPdu.msgHead, gMsgUdpRcvStr, sizeof(msg_head));
        memcpy(gMsgUdpRcvPdu.msgParams, gMsgUdpRcvStr+sizeof(msg_head), gMsgUdpRcvPdu.msgHead.msgLen);
        if(rcvLen != (gMsgUdpRcvPdu.msgHead.msgLen + sizeof(msg_head)))
        {
            TRACE(TRACE_ERROR, TASK_INDEX_MSG_UDP_RCV, "the received length is not equal to the pdu size\n");
            continue;
        }

        TRACE(TRACE_INFO, TASK_INDEX_MSG_UDP_RCV, "receive message from: %s:%d\n", inet_ntoa(fromSockAddr.sin_addr), ntohs(fromSockAddr.sin_port));
        handleFunc = msgGetHandlerFunction(gMsgUdpRcvPdu.msgHead.msgEnt, gMsgUdpRcvPdu.msgHead.msgAct);
        if(handleFunc == NULL)
        {
            TRACE(TRACE_ERROR, TASK_INDEX_MSG_UDP_RCV, "no handler function for the received message: %d, dest: %d, entity: %d, action: %d, len: %d, framecnt: %d\n", 
                                        gMsgUdpRcvPdu.msgHead.msgSrc,
                                        gMsgUdpRcvPdu.msgHead.msgDest,
                                        gMsgUdpRcvPdu.msgHead.msgEnt,
                                        gMsgUdpRcvPdu.msgHead.msgAct,
                                        gMsgUdpRcvPdu.msgHead.msgLen,
                                        gMsgUdpRcvPdu.msgHead.frameCnt);
        }
        else
        {
            handleFunc(&gMsgUdpRcvPdu);
        }
    }
}

/* Handle rcv pdu according to entity and action */
MSG_HANDLER msgGetHandlerFunction(int msgEnt, int msgAct)
{
    MSG_HANDLER handleFunc = NULL;
    
    if(msgEnt >= 0&& msgEnt < gMsgRcvHandlerNum)
    {
        if(msgAct >= 0 && msgAct < gMsgRcvHandler[msgEnt].actionMax)
        {
            handleFunc = gMsgRcvHandler[msgEnt].handler[msgAct];
        }
    }

    return handleFunc;
}

