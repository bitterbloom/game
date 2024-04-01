#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __linux__
#include <pthread.h>

typedef struct { pthread_mutex_t handle; } Mutex;
typedef struct { pthread_t handle; } Thread;
#define THREAD_NULL ((Thread) { 0 })
#elif defined(_WIN64)
#include <WinSock2.h>

typedef struct { HANDLE handle; } Mutex, Thread;
#define THREAD_NULL ((Thread) { NULL })
#endif

char *threads_get_error(void);

bool mutex_init(Mutex *mutex);
bool mutex_close(Mutex *mutex);

bool mutex_lock(Mutex *mutex);
bool mutex_unlock(Mutex *mutex);

bool thread_is_null(Thread thread);
bool thread_spawn(Thread *thread, void function(void *constext), void *context);

bool thread_suspend(Thread thread);
bool thread_resume(Thread thread);
bool thread_kill(Thread thread);

bool thread_sleep_ms(long millis, long *remaining);
bool time_get_monotonic(long *millis);

