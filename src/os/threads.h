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

// Waits for the thread to finish execution and closes it
bool thread_close(Thread thread);

// A detached thread will close itself automatically once it finishes execution
bool thread_detach(Thread thread);

bool thread_sleep_ms(long millis);
bool time_get_monotonic(long *millis);

