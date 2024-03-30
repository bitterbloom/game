#ifdef __linux__
#error "TODO"
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
    snprintf(error_buffer, 1024, string "(code: %lu, '%s')", GetLastError(), last_error); \
    LocalFree(last_error); \
    return false; \
}

char *threads_get_error() {
    return error_buffer;
}

bool mutex_init(Mutex *const m) {
    m->handle = CreateMutex(NULL, false, NULL);
    if (m->handle == NULL)
        FAIL_AND_GET_LAST_ERROR("Failed toinitialize mutex");
    return true;
}

bool mutex_close(Mutex m) {
    if (CloseHandle(m.handle) == 0)
        FAIL_AND_GET_LAST_ERROR("Failed to close mutex");
    return true;
}

// This function blocks execution.
bool mutex_lock(Mutex m) {
    switch (WaitForSingleObject(m.handle, INFINITE)) {
        case WAIT_OBJECT_0:  return true;
        case WAIT_TIMEOUT:   FAIL("Failed to lock mutex (timed out)");
        case WAIT_ABANDONED: FAIL("Failed to lock mutex (owning thread was terminated)");
        case WAIT_FAILED:    FAIL_AND_GET_LAST_ERROR("Failed to lock mutex");
        default:             FAIL("Failed to lock mutex (unknown cause)");
    }
}

bool mutex_unlock(Mutex m) {
    if (ReleaseMutex(m.handle) == 0)
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

bool thread_kill(Thread t) {
    if (TerminateThread(t.handle, 0) == 0)
        FAIL_AND_GET_LAST_ERROR("Failed to terminate thread");
    if (CloseHandle(t.handle) == 0)
        FAIL_AND_GET_LAST_ERROR("Failed to close thread");
    return true;
}
#endif
