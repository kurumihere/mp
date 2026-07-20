#ifndef MP_WORKER_THREAD_H
#define MP_WORKER_THREAD_H

#include <stdbool.h>

#ifndef _WIN32
#include <pthread.h>
#endif

typedef int (*Worker_Thread_Function)(void *context);

typedef struct {
    Worker_Thread_Function function;
    void *context;
#ifdef _WIN32
    void *handle;
#else
    pthread_t handle;
#endif
    bool started;
} Worker_Thread;

bool worker_thread_start(Worker_Thread *thread,
                         Worker_Thread_Function function, void *context);
void worker_thread_join(Worker_Thread *thread);

#endif
