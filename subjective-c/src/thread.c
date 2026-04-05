#include "thread.h"

#include "exception.h"
#include "memory.h"

#include <limits.h>
#include <string.h>

#if defined(ESP_PLATFORM)
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#else
#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <time.h>
#endif

#if defined(ESP_PLATFORM)

typedef struct {
    SubjectiveCThreadFn fn;
    void *arg;
    SemaphoreHandle_t done_sem;
    char name[THREAD_NAME_MAX];
} SubjectiveCThreadStartArgs;

struct SubjectiveCThread {
    TaskHandle_t handle;
    SemaphoreHandle_t done_sem;
    bool joined;
};

struct SubjectiveCMutex {
    SemaphoreHandle_t handle;
};

struct SubjectiveCCondVar {
    SemaphoreHandle_t handle;
};

static UBaseType_t thread_stack_words_from_bytes(size_t stack_bytes) {
    if (stack_bytes == 0u) {
        return configMINIMAL_STACK_SIZE;
    }
    size_t word_bytes = sizeof(StackType_t);
    size_t words = (stack_bytes + word_bytes - 1u) / word_bytes;
    if (words > (size_t)UINT_MAX) {
        words = (size_t)UINT_MAX;
    }
    if (words == 0u) {
        words = 1u;
    }
    return (UBaseType_t)words;
}

static void subjective_c_thread_entry(void *raw_args) {
    SubjectiveCThreadStartArgs args = *(SubjectiveCThreadStartArgs *)raw_args;
    CLJ_FREE(raw_args);

    if (args.name[0] != '\0') {
        subjective_c_set_thread_name(args.name);
    }
    if (args.fn) {
        args.fn(args.arg);
    }
    if (args.done_sem) {
        (void)xSemaphoreGive(args.done_sem);
    }
    vTaskDelete(NULL);
}

/**
 * @brief Creates one OS thread/task using Subjective-C's cross-platform API.
 *
 * The returned handle is heap-owned by the caller and must be released with
 * subjective_c_thread_destroy after subjective_c_thread_join succeeds.
 */
SubjectiveCThread *subjective_c_thread_create(SubjectiveCThreadFn fn,
                                              void *arg,
                                              const SubjectiveCThreadConfig *config) {
    if (!fn) {
        return NULL;
    }

    SubjectiveCThread *thread = (SubjectiveCThread *)CLJ_MALLOC(sizeof(*thread));
    if (!thread) {
        return NULL;
    }
    memset(thread, 0, sizeof(*thread));

    thread->done_sem = xSemaphoreCreateBinary();
    if (!thread->done_sem) {
        CLJ_FREE(thread);
        return NULL;
    }

    SubjectiveCThreadStartArgs *args =
        (SubjectiveCThreadStartArgs *)CLJ_MALLOC(sizeof(*args));
    if (!args) {
        vSemaphoreDelete(thread->done_sem);
        CLJ_FREE(thread);
        return NULL;
    }
    memset(args, 0, sizeof(*args));
    args->fn = fn;
    args->arg = arg;
    args->done_sem = thread->done_sem;

    const char *name = (config && config->name && config->name[0] != '\0')
                           ? config->name
                           : "tiny-thread";
    strncpy(args->name, name, sizeof(args->name) - 1u);
    args->name[sizeof(args->name) - 1u] = '\0';

    UBaseType_t stack_words = thread_stack_words_from_bytes(config ? config->stack_bytes : 0u);
    UBaseType_t priority = (config && config->priority > 0)
                               ? (UBaseType_t)config->priority
                               : (tskIDLE_PRIORITY + 1u);

    BaseType_t rc = xTaskCreate(subjective_c_thread_entry,
                                args->name,
                                stack_words,
                                args,
                                priority,
                                &thread->handle);
    if (rc != pdPASS) {
        CLJ_FREE(args);
        vSemaphoreDelete(thread->done_sem);
        CLJ_FREE(thread);
        return NULL;
    }

    return thread;
}

/**
 * @brief Joins one previously created thread/task.
 *
 * This blocks until the thread function returns.
 */
bool subjective_c_thread_join(SubjectiveCThread *thread) {
    if (!thread) {
        return false;
    }
    if (thread->joined) {
        return true;
    }
    if (!thread->done_sem) {
        return false;
    }
    if (xSemaphoreTake(thread->done_sem, portMAX_DELAY) != pdTRUE) {
        return false;
    }
    thread->joined = true;
    return true;
}

/**
 * @brief Destroys one thread handle and its synchronization resources.
 */
void subjective_c_thread_destroy(SubjectiveCThread *thread) {
    if (!thread) {
        return;
    }
    if (!thread->joined) {
        (void)subjective_c_thread_join(thread);
    }
    if (thread->done_sem) {
        vSemaphoreDelete(thread->done_sem);
    }
    CLJ_FREE(thread);
}

size_t subjective_c_thread_stack_high_water_mark_bytes(const SubjectiveCThread *thread) {
    if (!thread || !thread->handle) {
        return 0u;
    }
    UBaseType_t words = uxTaskGetStackHighWaterMark(thread->handle);
    if (words == 0u) {
        return 0u;
    }
    return (size_t)words * sizeof(StackType_t);
}

void subjective_c_thread_sleep_ms(uint32_t sleep_ms) {
    if (sleep_ms == 0u) {
        taskYIELD();
        return;
    }
    TickType_t ticks = pdMS_TO_TICKS(sleep_ms);
    if (ticks == 0) {
        ticks = 1;
    }
    vTaskDelay(ticks);
}

void subjective_c_thread_yield(void) {
    taskYIELD();
}

/**
 * @brief Allocates one mutex abstraction object.
 */
SubjectiveCMutex *subjective_c_mutex_create(void) {
    SubjectiveCMutex *mutex = (SubjectiveCMutex *)CLJ_MALLOC(sizeof(*mutex));
    if (!mutex) {
        return NULL;
    }
    mutex->handle = xSemaphoreCreateMutex();
    if (!mutex->handle) {
        CLJ_FREE(mutex);
        return NULL;
    }
    return mutex;
}

/**
 * @brief Releases one mutex abstraction object.
 */
void subjective_c_mutex_destroy(SubjectiveCMutex *mutex) {
    if (!mutex) {
        return;
    }
    if (mutex->handle) {
        vSemaphoreDelete(mutex->handle);
    }
    CLJ_FREE(mutex);
}

void subjective_c_mutex_lock(SubjectiveCMutex *mutex) {
    if (!mutex || !mutex->handle) {
        return;
    }
    (void)xSemaphoreTake(mutex->handle, portMAX_DELAY);
}

void subjective_c_mutex_unlock(SubjectiveCMutex *mutex) {
    if (!mutex || !mutex->handle) {
        return;
    }
    (void)xSemaphoreGive(mutex->handle);
}

bool subjective_c_mutex_trylock(SubjectiveCMutex *mutex) {
    if (!mutex || !mutex->handle) {
        return false;
    }
    return xSemaphoreTake(mutex->handle, 0) == pdTRUE;
}

/**
 * @brief Allocates one condition-variable abstraction object.
 */
SubjectiveCCondVar *subjective_c_condvar_create(void) {
    SubjectiveCCondVar *condvar = (SubjectiveCCondVar *)CLJ_MALLOC(sizeof(*condvar));
    if (!condvar) {
        return NULL;
    }
    condvar->handle = xSemaphoreCreateBinary();
    if (!condvar->handle) {
        CLJ_FREE(condvar);
        return NULL;
    }
    return condvar;
}

/**
 * @brief Releases one condition-variable abstraction object.
 */
void subjective_c_condvar_destroy(SubjectiveCCondVar *condvar) {
    if (!condvar) {
        return;
    }
    if (condvar->handle) {
        vSemaphoreDelete(condvar->handle);
    }
    CLJ_FREE(condvar);
}

void subjective_c_condvar_signal(SubjectiveCCondVar *condvar) {
    if (!condvar || !condvar->handle) {
        return;
    }
    (void)xSemaphoreGive(condvar->handle);
}

void subjective_c_condvar_broadcast(SubjectiveCCondVar *condvar) {
    if (!condvar || !condvar->handle) {
        return;
    }
    (void)xSemaphoreGive(condvar->handle);
}

bool subjective_c_condvar_wait(SubjectiveCCondVar *condvar,
                               SubjectiveCMutex *mutex,
                               uint32_t timeout_ms) {
    if (!condvar || !condvar->handle || !mutex || !mutex->handle || timeout_ms == 0u) {
        return false;
    }

    subjective_c_mutex_unlock(mutex);

    TickType_t ticks = (timeout_ms == UINT32_MAX) ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
    if (ticks == 0 && timeout_ms != UINT32_MAX) {
        ticks = 1;
    }
    bool signaled = (xSemaphoreTake(condvar->handle, ticks) == pdTRUE);

    subjective_c_mutex_lock(mutex);
    return signaled;
}

#else

typedef struct {
    SubjectiveCThreadFn fn;
    void *arg;
} SubjectiveCThreadStartArgs;

struct SubjectiveCThread {
    pthread_t handle;
    bool joined;
};

struct SubjectiveCMutex {
    pthread_mutex_t handle;
};

struct SubjectiveCCondVar {
    pthread_cond_t handle;
};

static void condvar_deadline_from_now(uint32_t timeout_ms, struct timespec *out_deadline) {
    if (!out_deadline) {
        return;
    }
    (void)clock_gettime(CLOCK_REALTIME, out_deadline);
    out_deadline->tv_sec += (time_t)(timeout_ms / 1000u);
    long nsec = out_deadline->tv_nsec + (long)(timeout_ms % 1000u) * 1000000L;
    if (nsec >= 1000000000L) {
        out_deadline->tv_sec += 1;
        nsec -= 1000000000L;
    }
    out_deadline->tv_nsec = nsec;
}

static void *subjective_c_thread_entry(void *raw_args) {
    SubjectiveCThreadStartArgs args = *(SubjectiveCThreadStartArgs *)raw_args;
    CLJ_FREE(raw_args);
    if (args.fn) {
        args.fn(args.arg);
    }
    return NULL;
}

/**
 * @brief Creates one OS thread/task using Subjective-C's cross-platform API.
 *
 * The returned handle is heap-owned by the caller and must be released with
 * subjective_c_thread_destroy after subjective_c_thread_join succeeds.
 */
SubjectiveCThread *subjective_c_thread_create(SubjectiveCThreadFn fn,
                                              void *arg,
                                              const SubjectiveCThreadConfig *config) {
    if (!fn) {
        return NULL;
    }

    SubjectiveCThread *thread = (SubjectiveCThread *)CLJ_MALLOC(sizeof(*thread));
    if (!thread) {
        return NULL;
    }
    memset(thread, 0, sizeof(*thread));

    SubjectiveCThreadStartArgs *args =
        (SubjectiveCThreadStartArgs *)CLJ_MALLOC(sizeof(*args));
    if (!args) {
        CLJ_FREE(thread);
        return NULL;
    }
    args->fn = fn;
    args->arg = arg;

    pthread_attr_t attr;
    pthread_attr_t *attr_ptr = NULL;
    bool attr_initialized = false;
    if (config && config->stack_bytes > 0u) {
        if (pthread_attr_init(&attr) == 0) {
            attr_initialized = true;
            (void)pthread_attr_setstacksize(&attr, config->stack_bytes);
            attr_ptr = &attr;
        }
    }

    int rc = subjective_c_pthread_create_named(&thread->handle,
                                               attr_ptr,
                                               subjective_c_thread_entry,
                                               args,
                                               config ? config->name : NULL);

    if (attr_initialized) {
        (void)pthread_attr_destroy(&attr);
    }

    if (rc != 0) {
        CLJ_FREE(args);
        CLJ_FREE(thread);
        return NULL;
    }
    return thread;
}

/**
 * @brief Joins one previously created thread/task.
 *
 * This blocks until the thread function returns.
 */
bool subjective_c_thread_join(SubjectiveCThread *thread) {
    if (!thread) {
        return false;
    }
    if (thread->joined) {
        return true;
    }
    if (pthread_join(thread->handle, NULL) != 0) {
        return false;
    }
    thread->joined = true;
    return true;
}

/**
 * @brief Destroys one thread handle and its synchronization resources.
 */
void subjective_c_thread_destroy(SubjectiveCThread *thread) {
    if (!thread) {
        return;
    }
    if (!thread->joined) {
        (void)subjective_c_thread_join(thread);
    }
    CLJ_FREE(thread);
}

size_t subjective_c_thread_stack_high_water_mark_bytes(const SubjectiveCThread *thread) {
    (void)thread;
    return 0u;
}

void subjective_c_thread_sleep_ms(uint32_t sleep_ms) {
    if (sleep_ms == 0u) {
        (void)sched_yield();
        return;
    }
    struct timespec ts = {
        .tv_sec = (time_t)(sleep_ms / 1000u),
        .tv_nsec = (long)(sleep_ms % 1000u) * 1000000L,
    };
    (void)nanosleep(&ts, NULL);
}

void subjective_c_thread_yield(void) {
    (void)sched_yield();
}

/**
 * @brief Allocates one mutex abstraction object.
 */
SubjectiveCMutex *subjective_c_mutex_create(void) {
    SubjectiveCMutex *mutex = (SubjectiveCMutex *)CLJ_MALLOC(sizeof(*mutex));
    if (!mutex) {
        return NULL;
    }
    if (pthread_mutex_init(&mutex->handle, NULL) != 0) {
        CLJ_FREE(mutex);
        return NULL;
    }
    return mutex;
}

/**
 * @brief Releases one mutex abstraction object.
 */
void subjective_c_mutex_destroy(SubjectiveCMutex *mutex) {
    if (!mutex) {
        return;
    }
    (void)pthread_mutex_destroy(&mutex->handle);
    CLJ_FREE(mutex);
}

void subjective_c_mutex_lock(SubjectiveCMutex *mutex) {
    if (!mutex) {
        return;
    }
    (void)pthread_mutex_lock(&mutex->handle);
}

void subjective_c_mutex_unlock(SubjectiveCMutex *mutex) {
    if (!mutex) {
        return;
    }
    (void)pthread_mutex_unlock(&mutex->handle);
}

bool subjective_c_mutex_trylock(SubjectiveCMutex *mutex) {
    if (!mutex) {
        return false;
    }
    return pthread_mutex_trylock(&mutex->handle) == 0;
}

/**
 * @brief Allocates one condition-variable abstraction object.
 */
SubjectiveCCondVar *subjective_c_condvar_create(void) {
    SubjectiveCCondVar *condvar = (SubjectiveCCondVar *)CLJ_MALLOC(sizeof(*condvar));
    if (!condvar) {
        return NULL;
    }
    if (pthread_cond_init(&condvar->handle, NULL) != 0) {
        CLJ_FREE(condvar);
        return NULL;
    }
    return condvar;
}

/**
 * @brief Releases one condition-variable abstraction object.
 */
void subjective_c_condvar_destroy(SubjectiveCCondVar *condvar) {
    if (!condvar) {
        return;
    }
    (void)pthread_cond_destroy(&condvar->handle);
    CLJ_FREE(condvar);
}

void subjective_c_condvar_signal(SubjectiveCCondVar *condvar) {
    if (!condvar) {
        return;
    }
    (void)pthread_cond_signal(&condvar->handle);
}

void subjective_c_condvar_broadcast(SubjectiveCCondVar *condvar) {
    if (!condvar) {
        return;
    }
    (void)pthread_cond_broadcast(&condvar->handle);
}

bool subjective_c_condvar_wait(SubjectiveCCondVar *condvar,
                               SubjectiveCMutex *mutex,
                               uint32_t timeout_ms) {
    if (!condvar || !mutex || timeout_ms == 0u) {
        return false;
    }

    if (timeout_ms == UINT32_MAX) {
        return pthread_cond_wait(&condvar->handle, &mutex->handle) == 0;
    }

    struct timespec deadline;
    condvar_deadline_from_now(timeout_ms, &deadline);
    int rc = pthread_cond_timedwait(&condvar->handle, &mutex->handle, &deadline);
    if (rc == 0) {
        return true;
    }
    if (rc == ETIMEDOUT) {
        return false;
    }
    return false;
}

#endif
