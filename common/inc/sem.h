#ifndef _SEM_H
#define _SEM_H

#include <semaphore.h>

/* Restart when interrupted by handler */
#define SEM_WAIT(sem, ret) {while((ret = sem_wait(&sem)) == -1 && errno == EINTR) continue;}
#define SEM_POST(sem, ret) {ret = sem_post(&sem);}

#endif
