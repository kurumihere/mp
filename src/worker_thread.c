#include "worker_thread.h"

#include <stddef.h>

#ifdef _WIN32
#include <process.h>
#include <stdint.h>
#include <windows.h>

static unsigned __stdcall worker_thread_run(void *context)
{
    Worker_Thread *thread = context;
    return (unsigned)thread->function(thread->context);
}
#else
static void *worker_thread_run(void *context)
{
    Worker_Thread *thread = context;
    thread->function(thread->context);
    return NULL;
}
#endif

bool worker_thread_start(Worker_Thread *thread,
                         Worker_Thread_Function function, void *context)
{
    if (thread == NULL || function == NULL || thread->started) return false;

    thread->function = function;
    thread->context = context;

#ifdef _WIN32
    uintptr_t handle =
        _beginthreadex(NULL, 0, worker_thread_run, thread, 0, NULL);

    if (handle == 0) return false;

    thread->handle = (void *)handle;
#else
    if (pthread_create(&thread->handle, NULL, worker_thread_run, thread) != 0) {
        return false;
    }
#endif

    thread->started = true;
    return true;
}

void worker_thread_join(Worker_Thread *thread)
{
    if (thread == NULL || !thread->started) return;

#ifdef _WIN32
    HANDLE handle = (HANDLE)thread->handle;
    WaitForSingleObject(handle, INFINITE);
    CloseHandle(handle);
    thread->handle = NULL;
#else
    pthread_join(thread->handle, NULL);
#endif

    thread->function = NULL;
    thread->context = NULL;
    thread->started = false;
}
