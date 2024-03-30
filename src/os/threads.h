#pragma once

#include <stdbool.h>

#ifdef __linux__
#error "TODO"
#elif defined(_WIN64)
#include <WinSock2.h>

typedef struct { HANDLE handle; } Mutex, Thread;
#define THREAD_NULL ((Thread) { NULL })
#endif

char *threads_get_error(void);

bool mutex_init(Mutex *mutex);
bool mutex_close(Mutex mutex);

bool mutex_lock(Mutex mutex);
bool mutex_unlock(Mutex mutex);

bool thread_is_null(Thread thread);
bool thread_spawn(Thread *thread, void function(void *constext), void *context);
bool thread_suspend(Thread thread);
bool thread_resume(Thread thread);
bool thread_kill(Thread thread);

