#ifdef __linux__
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <threads.h>

#include "./threads.h"
#include "../util.h"

static char error_buffer[1024];

#define FAIL(string) { \
    if (sizeof string > 1024) \
        EXIT_PRINT("Error message too long for buffer"); \
    memcpy(error_buffer, string, sizeof string); \
    return false; \
}

#define FAIL_WITH_ERROR(string, error) { \
    snprintf(error_buffer, 1024, string " (code: %d, '%s')", error, strerror(error)); \
    return false; \
}

#define FAIL_AND_GET_ERROR(string) { \
    snprintf(error_buffer, 1024, string " (code: %d, '%s')", errno, strerror(errno)); \
    return false; \
}

char *threads_get_error() {
    return error_buffer;
}

bool mutex_init(Mutex *const m) {
    int const error = pthread_mutex_init(&m->handle, NULL);
    if (error != 0)
        FAIL_WITH_ERROR("Failed to initialize mutex", error);
    return true;
}

bool mutex_close(Mutex *const m) {
    int const error = pthread_mutex_destroy(&m->handle);
    if (error != 0)
        FAIL_WITH_ERROR("Failed to close mutex", error);
    return true;
}

bool mutex_lock(Mutex *const m) {
    pthread_mutex_lock(&m->handle);
    // TODO: Error check
    return true;
}

bool mutex_unlock(Mutex *const m) {
    pthread_mutex_unlock(&m->handle);
    // TODO: Error check
    return true;
}

typedef struct {
    void (*function)(void *);
    void *context;
} WrapperContext;
static void *wrapper(WrapperContext *const c) {
    c->function(c->context);
    free(c);
    return NULL;
}

bool thread_is_null(Thread const thread) {
    return thread.handle == THREAD_NULL.handle;
}
bool thread_spawn(Thread *const t, void (*const function)(void *context), void *const context) {
    WrapperContext *const wrapper_context = malloc(sizeof (WrapperContext));
    *wrapper_context = (WrapperContext) {function, context};

    pthread_create(&t->handle, NULL, (void *(*)(void *)) wrapper, wrapper_context);
    // TODO: Error check
    return true;
}

bool thread_suspend(Thread t);
bool thread_resume(Thread t);

bool thread_kill(Thread t) {
    pthread_cancel(t.handle);
    // TODO: Error check
    return true;
}

bool thread_sleep_ms(long const ms, long *const remaining) {
    struct timespec const ts = {.tv_sec = 0, .tv_nsec = ms * 1000000};
    struct timespec rem;
    if (thrd_sleep(&ts, remaining != NULL ? &rem : NULL) != 0)
        FAIL("Failed to sleep");
    if (remaining != NULL)
        *remaining = rem.tv_sec * 1000 + rem.tv_nsec / 1000000;
    return true;
}

bool time_get_monotonic(long *const millis) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
        FAIL_AND_GET_ERROR("Failed to get time");
    *millis = ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
    return true;
}
#endif

#ifdef _WIN64
#include <Ws2tcpip.h>
#include <process.h>

#include "./threads.h"
#include "../util.h"

static char error_buffer[1024];

#define FAIL(string) { \
    if (sizeof string > 1024) \
        EXIT_PRINT("Error message too long for buffer"); \
    memcpy(error_buffer, string, sizeof string); \
    return false; \
}

#define FAIL_AND_GET_LAST_ERROR(string) { \
    LPSTR last_error = NULL; \
    TCHAR nstored = FormatMessage( \
        FORMAT_MESSAGE_FROM_SYSTEM \
        | FORMAT_MESSAGE_IGNORE_INSERTS \
        | FORMAT_MESSAGE_ALLOCATE_BUFFER, \
        NULL, GetLastError(), \
        MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US), \
        (LPSTR) &last_error, \
        0, NULL \
    ); \
    if (nstored == 0) \
        EXIT_PRINT("Failed to format error message"); \
    snprintf(error_buffer, 1024, string " (code: %lu, '%s')", GetLastError(), last_error); \
    LocalFree(last_error); \
    return false; \
}

char *threads_get_error() {
    return error_buffer;
}

bool mutex_init(Mutex *const m) {
    m->handle = CreateMutex(NULL, false, NULL);
    if (m->handle == NULL)
        FAIL_AND_GET_LAST_ERROR("Failed to initialize mutex");
    return true;
}

bool mutex_close(Mutex *const m) {
    if (CloseHandle(m->handle) == 0)
        FAIL_AND_GET_LAST_ERROR("Failed to close mutex");
    return true;
}

// This function blocks execution.
bool mutex_lock(Mutex *const m) {
    switch (WaitForSingleObject(m->handle, INFINITE)) {
        case WAIT_OBJECT_0:  return true;
        case WAIT_TIMEOUT:   FAIL("Failed to lock mutex (timed out)");
        case WAIT_ABANDONED: FAIL("Failed to lock mutex (owning thread was terminated)");
        case WAIT_FAILED:    FAIL_AND_GET_LAST_ERROR("Failed to lock mutex");
        default:             FAIL("Failed to lock mutex (unknown cause)");
    }
}

bool mutex_unlock(Mutex *const m) {
    if (ReleaseMutex(m->handle) == 0)
        FAIL_AND_GET_LAST_ERROR("Failed to unlock mutex");
    return true;
}

typedef struct {
    void (*function)(void *);
    void *context;
} WrapperContext;
static unsigned int __stdcall wrapper(WrapperContext *const c) {
    c->function(c->context);
    free(c);
    _endthreadex(0);
    return 0;
}

bool thread_is_null(Thread const t) {
    return t.handle == THREAD_NULL.handle;
}

bool thread_spawn(Thread *const t, void (*const function)(void *), void *const context) {
    WrapperContext *wrapper_context = malloc(sizeof (WrapperContext));
    *wrapper_context = (WrapperContext) {function, context};

    t->handle = (HANDLE) _beginthreadex(
        NULL, 0,
        (unsigned int __stdcall (*)(void *)) wrapper,
        wrapper_context,
        0, NULL
    );
    if (t->handle == NULL)
        FAIL("Failed to initialize thread");
    return true;
}

bool thread_join(Thread t) {
    if (WaitForSingleObject(t.handle, INFINITE) != WAIT_OBJECT_0)
        FAIL_AND_GET_LAST_ERROR("Failed to join thread");
    return true;
}

bool thread_suspend(Thread t) {
    if (SuspendThread(t.handle) == (DWORD) -1)
        FAIL_AND_GET_LAST_ERROR("Failed to suspend thread");
    return true;
}

bool thread_resume(Thread t) {
    if (ResumeThread(t.handle) == (DWORD) -1)
        FAIL_AND_GET_LAST_ERROR("Failed to resume thread");
    return true;
}

bool thread_sleep_ms(long const ms) {
    Sleep((DWORD) ms);
    return true;
}

bool time_get_monotonic(long *const millis) {
    *millis = GetTickCount();
    return true;
}
#endif
