#ifndef SUBJECTIVE_C_THREAD_H
#define SUBJECTIVE_C_THREAD_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct SubjectiveCThread SubjectiveCThread;
typedef struct SubjectiveCMutex SubjectiveCMutex;
typedef struct SubjectiveCCondVar SubjectiveCCondVar;

typedef void (*SubjectiveCThreadFn)(void *arg);

typedef struct {
    const char *name;
    size_t stack_bytes;
    int priority;
} SubjectiveCThreadConfig;

SubjectiveCThread *subjective_c_thread_create(SubjectiveCThreadFn fn,
                                              void *arg,
                                              const SubjectiveCThreadConfig *config);
bool subjective_c_thread_join(SubjectiveCThread *thread);
void subjective_c_thread_destroy(SubjectiveCThread *thread);
size_t subjective_c_thread_stack_high_water_mark_bytes(const SubjectiveCThread *thread);

void subjective_c_thread_sleep_ms(uint32_t sleep_ms);
void subjective_c_thread_yield(void);

SubjectiveCMutex *subjective_c_mutex_create(void);
void subjective_c_mutex_destroy(SubjectiveCMutex *mutex);
void subjective_c_mutex_lock(SubjectiveCMutex *mutex);
void subjective_c_mutex_unlock(SubjectiveCMutex *mutex);
bool subjective_c_mutex_trylock(SubjectiveCMutex *mutex);

SubjectiveCCondVar *subjective_c_condvar_create(void);
void subjective_c_condvar_destroy(SubjectiveCCondVar *condvar);
void subjective_c_condvar_signal(SubjectiveCCondVar *condvar);
void subjective_c_condvar_broadcast(SubjectiveCCondVar *condvar);
bool subjective_c_condvar_wait(SubjectiveCCondVar *condvar,
                               SubjectiveCMutex *mutex,
                               uint32_t timeout_ms);

#endif /* SUBJECTIVE_C_THREAD_H */
