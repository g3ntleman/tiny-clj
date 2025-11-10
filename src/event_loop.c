#include "event_loop.h"
#include "function_call.h"
#include "symbol.h"
#include "memory.h"
#include "exception.h"
#include "channel.h"
#include "types.h"  // For ZOMBIE_RC
#include <stdbool.h>

typedef struct GoTask {
    CljObject *fn;            // zero-arity function to execute
    CljMap *result_chan;      // result channel (transient map) to put value and close
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

void event_loop_clear(void) {
    // Release all tasks in queue
    for (int i = 0; i < g_task_count; i++) {
        if (g_tasks[i].fn) {
            // Skip zombies - they were already freed
#ifdef DEBUG
            if (g_tasks[i].fn->rc != ZOMBIE_RC) {
                RELEASE(g_tasks[i].fn);
            }
#else
            RELEASE(g_tasks[i].fn);
#endif
        }
        if (g_tasks[i].result_chan) {
            // Skip zombies - they were already freed
#ifdef DEBUG
            CljObject *chan_obj = (CljObject*)g_tasks[i].result_chan;
            if (chan_obj->rc != ZOMBIE_RC) {
                RELEASE((CljObject*)g_tasks[i].result_chan);
            }
#else
            RELEASE((CljObject*)g_tasks[i].result_chan);
#endif
        }
    }
    g_task_count = 0;
}

void event_loop_enqueue(CljObject *fn_zero_arity, CljMap *result_channel) {
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
    // st is used by eval_function_call for namespace resolution
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
    
    // Channel handling: use map_conj (mutates transient map in-place)
    // Channels are transient maps, so we can mutate them directly
    if (task.result_chan) {
        CljMap *chan = task.result_chan;
        
        // Assertion: Channel must not be NULL
        CLJ_ASSERT(chan != NULL);
        
        // Assertion: Channel must be a transient map (or persistent map with RC=1 for COW)
        CljObject *obj = (CljObject*)chan;
        CLJ_ASSERT(obj != NULL);
        CLJ_ASSERT(obj->type == CLJ_TRANSIENT_MAP || obj->type == CLJ_MAP);
        
        // In COW cases, persistent maps with RC=1 can be mutated, but we use transient maps for channels
        if (obj->type == CLJ_MAP) {
            CLJ_ASSERT(obj->rc == 1);
        }
        
        // Store original pointer for verification
        void *chan_ptr_before = (void*)chan;
        
        // Put value into channel if result is available (mutates in-place)
        if (ok && result) {
            result_channel_put(chan, (ID)result);
            // Assertion: Channel pointer should not change after result_channel_put
            CLJ_ASSERT((void*)chan == chan_ptr_before);
        }
        
        // Close channel (mutates in-place)
        result_channel_close(chan);
        // Assertion: Channel pointer should not change after result_channel_close
        CLJ_ASSERT((void*)chan == chan_ptr_before);
        
        // Verify channel was actually mutated
        CljObject *kw_closed = (CljObject*)intern_symbol(NULL, ":closed");
        CljValue closed_val = map_get(chan, (CljValue)kw_closed);
        CLJ_ASSERT(closed_val != NULL);
        CLJ_ASSERT(is_special(closed_val));
        CLJ_ASSERT(as_special(closed_val) == SPECIAL_TRUE);
    } else {
        // Assertion: result_chan should not be NULL for go-blocks
        CLJ_ASSERT(0 && "event_loop_run_next: task.result_chan is NULL");
    }

    if (!IS_IMMEDIATE(result)) RELEASE(result);
    RELEASE(task.fn);
    RELEASE(task.result_chan);
    return true;
}


