#include "event_loop.h"
#include "function_call.h"
#include "symbol.h"
#include "memory.h"

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

// Helper: put result into simple map-based result channel {:value v :closed true}
static void channel_put_and_close(CljObject *chan, CljObject *value) {
    if (!chan) return;
    CljObject *kw_value = intern_symbol(NULL, ":value");
    CljObject *kw_closed = intern_symbol(NULL, ":closed");
    if (value) {
        map_assoc(chan, kw_value, value);
    }
    map_assoc(chan, kw_closed, make_special(SPECIAL_TRUE));
}

int event_loop_run_next(CljMap *env, EvalState *st) {
    if (g_task_count <= 0) return 0;
    // Pop front (order is FIFO)
    GoTask task = g_tasks[0];
    for (int i = 1; i < g_task_count; ++i) g_tasks[i - 1] = g_tasks[i];
    g_task_count--;

    // Execute task
    CljObject *result = NULL;
    // zero-arity call
    result = eval_function_call(task.fn, NULL, 0, env);
    // Deliver to result channel and close
    channel_put_and_close(task.result_chan, result);

    if (result && !IS_IMMEDIATE(result)) RELEASE(result);
    if (task.fn) RELEASE(task.fn);
    if (task.result_chan) RELEASE(task.result_chan);
    return 1;
}


