#include "event_loop.h"
#include "function_call.h"
#include "symbol.h"
#include "memory.h"
#include "exception.h"
#include <stdbool.h>

typedef struct GoTask {
    CljObject *fn;            // zero-arity function to execute
    CljObject *result_chan;   // result channel to put value and close
} GoTask;

static GoTask *g_tasks = NULL;
static int g_task_count = 0;
static int g_task_capacity = 0;

void event_loop_init(void) {
    if (g_tasks == NULL) {
        g_task_capacity = 8;
        g_tasks = (GoTask*)malloc(sizeof(GoTask) * (size_t)g_task_capacity);
        g_task_count = 0;
    }
}

void event_loop_enqueue(CljObject *fn_zero_arity, CljObject *result_channel) {
    event_loop_init();
    if (!fn_zero_arity) return;
    if (g_task_count >= g_task_capacity) {
        int newcap = g_task_capacity * 2;
        void *newmem = realloc(g_tasks, sizeof(GoTask) * (size_t)newcap);
        if (!newmem) return;
        g_tasks = (GoTask*)newmem;
        g_task_capacity = newcap;
    }
    g_tasks[g_task_count].fn = RETAIN(fn_zero_arity);
    g_tasks[g_task_count].result_chan = result_channel ? RETAIN(result_channel) : NULL;
    g_task_count++;
}


/**
 * @brief Execute the next enqueued go-block task
 * @param env Environment map (currently unused, may be NULL)
 * @param st Evaluation state for exception handling
 * @return true if a task was executed, false if queue is empty
 * 
 * This function processes one go-block task from the queue (FIFO order):
 * 1. Executes the zero-arity function with exception safety
 * 2. Puts the result (or NULL on error) into the result channel
 * 3. Closes the channel by setting :closed to true
 * 4. Releases all task resources
 */
bool event_loop_run_next(CljMap *env, EvalState *st) {
    (void)env;  // Currently unused, kept for future use
    (void)st;   // Currently unused, kept for future use
    if (g_task_count <= 0) return false;
    // Pop front (order is FIFO)
    GoTask task = g_tasks[0];
    for (int i = 1; i < g_task_count; ++i) g_tasks[i - 1] = g_tasks[i];
    g_task_count--;

    // Execute task with exception safety
    CljObject *result = NULL;
    bool ok = true;
    TRY {
        // zero-arity call
        result = eval_function_call(task.fn, NULL, 0, env, st);
    } CATCH(ex) {
        // On error: do not deliver a value, just close the channel
        ok = false;
    } END_TRY
    
    // Channel handling: reduce RC to 1 for in-place mutation
    // The channel is referenced by both the caller (via return value) and the queue.
    // To ensure map_assoc mutates in-place (RC=1), we release the queue's reference
    // before calling map_assoc. This ensures the caller's reference sees the updates.
    
    bool released_queue_ref = false;
    
    if (task.result_chan) {
        CljMap *map_data = as_map(task.result_chan);
        // Reduce RC to 1 by releasing the queue's reference
        // The caller's reference remains, so the channel won't be freed
        if (map_data && map_data->base.rc > 1) {
            RELEASE(task.result_chan);
            released_queue_ref = true;
        }
        
        CljObject *kw_value = (CljObject*)intern_symbol(NULL, ":value");
        CljObject *kw_closed = (CljObject*)intern_symbol(NULL, ":closed");
        
        ID current_chan = (ID)task.result_chan;
        
        if (ok && result) {
            // Put value into channel (map_assoc always returns a new map)
            ID new_chan = map_assoc(current_chan, (CljValue)kw_value, (CljValue)result);
            if (!released_queue_ref && current_chan == (ID)task.result_chan) {
                RELEASE((CljObject*)current_chan);
            } else if (current_chan != (ID)task.result_chan) {
                RELEASE((CljObject*)current_chan);
            }
            current_chan = new_chan;
        }
        
        // Close channel (map_assoc always returns a new map)
        ID new_chan = map_assoc(current_chan, (ID)kw_closed, (ID)clj_true);
        if (current_chan != (ID)task.result_chan) {
            RELEASE((CljObject*)current_chan);
        } else if (!released_queue_ref) {
            RELEASE((CljObject*)task.result_chan);
        }
        task.result_chan = (CljObject*)new_chan;
    }

    if (!IS_IMMEDIATE(result)) RELEASE(result);
    RELEASE(task.fn);
    // Only release queue's reference if we didn't already release it above
    if (!released_queue_ref) {
        RELEASE(task.result_chan);
    }
    return true;
}


