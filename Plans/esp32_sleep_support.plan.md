---
name: ESP32 Event Loop Sleep Support
overview: Add intelligent sleep mechanism to ESP32 event loop to prevent CPU overheating and reduce power consumption. The event loop should sleep when idle, waking up for tasks or timers.
todos:
  - id: next-timer-delay
    content: Implement event_loop_get_next_timer_delay_ms() function to calculate milliseconds until next timer
    status: pending
  - id: platform-sleep
    content: Add platform_sleep_ms() function to platform.h and implement for ESP32 (vTaskDelay) and other platforms (usleep)
    status: pending
  - id: esp32-main-loop
    content: Modify main_esp32.c to run continuous event loop with intelligent sleep when idle
    status: pending
  - id: test-sleep
    content: Test sleep behavior with timers and tasks to ensure responsiveness
    status: pending
---

# ESP32 Event Loop Sleep Support Plan

## Overview

Add intelligent sleep mechanism to ESP32 event loop to prevent CPU overheating and reduce power consumption. The event loop should sleep when idle, waking up for tasks or timers. This is critical for embedded systems where continuous CPU usage causes overheating and excessive power drain.

## Architecture

```
┌─────────────────────┐
│  main_esp32.c       │
│  Main Event Loop    │
└──────────┬──────────┘
           │
           ├─> event_loop_run_next()
           │   (process tasks)
           │
           ├─> event_loop_get_next_timer_delay_ms()
           │   (calculate sleep duration)
           │
           └─> platform_sleep_ms()
               (ESP32: vTaskDelay)
```

## Implementation Steps

### 1. Calculate Next Timer Delay

**File**: `src/event_loop.c`

Add function to calculate milliseconds until next timer:

```c
/**
 * @brief Get milliseconds until next timer is due
 * @return Milliseconds until next timer (0 if timer ready, -1 if no timers)
 */
int event_loop_get_next_timer_delay_ms(void) {
    CljVector *timer_vec = timer_queue_get();
    if (!timer_vec || vector_count(timer_vec) == 0) {
        return -1;  // No timers
    }
    
    // Get first timer (they're sorted by scheduled time)
    CljPersistentMap *task_map = (CljPersistentMap*)vector_nth(timer_vec, 0);
    if (!task_map) return -1;
    
    int scheduled_sec = task_get_scheduled_sec(task_map);
    int scheduled_msec = task_get_scheduled_msec(task_map);
    RELEASE(task_map);
    
    // Get current time
    struct timeval tv;
    gettimeofday(&tv, NULL);
    int now_sec = (int)tv.tv_sec;
    int now_msec = (int)(tv.tv_usec / 1000);
    
    // Calculate delay
    int delay_sec = scheduled_sec - now_sec;
    int delay_msec = scheduled_msec - now_msec;
    int total_delay_ms = delay_sec * 1000 + delay_msec;
    
    // Return 0 if timer is ready (or overdue)
    if (total_delay_ms <= 0) return 0;
    
    return total_delay_ms;
}
```

**Key Points**:
- Returns -1 if no timers (can sleep indefinitely)
- Returns 0 if timer is ready (don't sleep)
- Returns positive milliseconds until next timer
- Handles timer queue being empty

### 2. Platform Sleep Function

**File**: `src/platform.h`

Add platform sleep declaration:

```c
/**
 * @brief Sleep for specified milliseconds
 * @param milliseconds Sleep duration in milliseconds
 * @note Platform-specific implementation
 */
void platform_sleep_ms(int milliseconds);
```

**File**: `src/platform_esp32_embedded.c`

Implement for ESP32:

```c
#ifdef ESP32_BUILD
#include "FreeRTOS.h"
#include "task.h"

void platform_sleep_ms(int milliseconds) {
    if (milliseconds <= 0) return;
    // FreeRTOS vTaskDelay uses tick counts
    // pdMS_TO_TICKS converts milliseconds to ticks
    vTaskDelay(pdMS_TO_TICKS(milliseconds));
}
#else
// Fallback for non-ESP32 platforms
#include <unistd.h>

void platform_sleep_ms(int milliseconds) {
    if (milliseconds <= 0) return;
    usleep(milliseconds * 1000);  // usleep uses microseconds
}
#endif
```

**File**: `src/platform_macos.c` (or other platform files)

Add implementation for other platforms:

```c
#include <unistd.h>

void platform_sleep_ms(int milliseconds) {
    if (milliseconds <= 0) return;
    usleep(milliseconds * 1000);
}
```

**Key Points**:
- ESP32 uses FreeRTOS `vTaskDelay` with `pdMS_TO_TICKS`
- Other platforms use `usleep` (microseconds)
- Handles zero/negative values gracefully
- Platform-specific includes via compile guards

### 3. Main Event Loop with Sleep

**File**: `src/main_esp32.c`

Modify main to run continuous event loop:

```c
int main() {
    platform_init();
    event_loop_init();
    DEBUG_PRINT("Tiny-Clj ESP32 - Embedded Execution");
    
    // Initialize interpreter
    register_builtins();
    
    // Get evaluation state
    EvalState *state = get_global_eval_state();
    
    // Load and execute startup code
    DEBUG_PRINT("Loading startup code...");
    CljObject *result = eval_string(startup_code, state);
    if (!result) {
        DEBUG_PRINT("ERROR: Failed to load startup code");
        return 1;
    }
    RELEASE(result);
    DEBUG_PRINT("Startup code executed successfully");
    
    // Main event loop with intelligent sleep
    DEBUG_PRINT("Starting event loop...");
    while (true) {
        // Process all available tasks
        bool tasks_processed = false;
        while (event_loop_run_next(NULL, state)) {
            tasks_processed = true;
        }
        
        // Calculate sleep duration based on next timer
        int sleep_ms = event_loop_get_next_timer_delay_ms();
        
        if (sleep_ms > 0) {
            // Timer pending - sleep until timer (with max limit)
            int max_sleep = 1000;  // Max 1 second sleep chunks
            sleep_ms = (sleep_ms > max_sleep) ? max_sleep : sleep_ms;
            platform_sleep_ms(sleep_ms);
        } else if (sleep_ms == -1) {
            // No timers - sleep a short time to prevent busy-wait
            platform_sleep_ms(10);  // 10ms default sleep
        }
        // If sleep_ms == 0, timer is ready, continue immediately
    }
    
    // Never reached, but included for completeness
    autorelease_pool_cleanup_all();
    return 0;
}
```

**Sleep Strategy**:
1. **No tasks, no timers** (`sleep_ms == -1`): Sleep 10ms (prevent busy-wait)
2. **No tasks, timer pending** (`sleep_ms > 0`): Sleep until timer (max 1 second chunks)
3. **Timer ready** (`sleep_ms == 0`): Continue immediately (no sleep)
4. **Tasks available**: Process immediately (no sleep, handled by inner while loop)

**Key Points**:
- Processes all available tasks before sleeping
- Sleeps in 1-second chunks to maintain responsiveness
- Default 10ms sleep when idle prevents busy-wait
- Infinite loop (embedded system pattern)

### 4. Header Updates

**File**: `src/event_loop.h`

Add declaration:

```c
/**
 * @brief Get milliseconds until next timer is due
 * @return Milliseconds until next timer (0 if ready, -1 if no timers)
 */
int event_loop_get_next_timer_delay_ms(void);
```

## File Changes Summary

### Modified Files
1. `src/event_loop.c` - Add `event_loop_get_next_timer_delay_ms()` function
2. `src/event_loop.h` - Add function declaration
3. `src/platform.h` - Add `platform_sleep_ms()` declaration
4. `src/platform_esp32_embedded.c` - Implement ESP32 sleep (vTaskDelay)
5. `src/platform_macos.c` - Implement macOS sleep (usleep)
6. `src/main_esp32.c` - Add main event loop with sleep logic

## Benefits

- **Prevents CPU Overheating**: ESP32 doesn't run at 100% CPU when idle
- **Reduces Power Consumption**: Sleep reduces power draw significantly
- **Maintains Responsiveness**: Wakes up for tasks and timers
- **Timer Accuracy**: Sleeps until next timer, maintaining precision
- **Scalable**: Works with any number of timers and tasks

## Testing Considerations

- Test with no timers (should sleep 10ms repeatedly)
- Test with pending timer (should sleep until timer)
- Test with ready timer (should not sleep)
- Test with tasks queued (should process immediately)
- Test timer accuracy after sleep
- Test multiple timers (should wake for earliest)
- Test power consumption (should be lower with sleep)

## Dependencies

- FreeRTOS on ESP32 (for `vTaskDelay`)
- `gettimeofday` for time calculations
- Event loop timer queue (already implemented)

## Notes

- Sleep is in 1-second chunks to allow for new tasks/timers
- Default 10ms sleep prevents busy-wait when completely idle
- Sleep duration is calculated dynamically based on next timer
- Platform-specific implementation allows different sleep mechanisms

