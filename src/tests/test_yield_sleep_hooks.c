#include "tests_common.h"
#include "../platform.h"

// Test-only overrides for yield/current-time-ms.
//
// We override the weak hooks defined in src/builtins.c:
// - tinyclj_runloop_once_for_yield
// - tinyclj_current_time_ms_for_sleep
//
// This allows deterministic tests without replacing platform_* symbols (which would
// conflict with platform_macos.c in host builds).

static bool g_override_enabled = false;
static uint32_t g_fake_now_ms = 0;
static unsigned int g_yield_calls = 0;
static unsigned int g_last_yield_timeout_ms = 0;

void test_yield_sleep_hooks_enable(uint32_t start_ms) {
    g_override_enabled = true;
    g_fake_now_ms = start_ms % 86400000u;
    g_yield_calls = 0;
    g_last_yield_timeout_ms = 0;
}

void test_yield_sleep_hooks_disable(void) {
    g_override_enabled = false;
}

unsigned int test_yield_sleep_yield_calls(void) { return g_yield_calls; }
unsigned int test_yield_sleep_last_timeout_ms(void) { return g_last_yield_timeout_ms; }
uint32_t test_yield_sleep_now_ms(void) { return g_fake_now_ms; }

void tinyclj_runloop_once_for_yield(unsigned int timeout_ms) {
    if (!g_override_enabled) {
        uint32_t start_ms = platform_current_time_ms();
        platform_runloop_run_once(timeout_ms);
        if (timeout_ms == 0u) {
            return;
        }

        uint32_t end_ms = platform_current_time_ms();
        uint32_t elapsed_ms = (end_ms >= start_ms) ? (end_ms - start_ms)
                                                   : ((86400000u - start_ms) + end_ms);
        if (elapsed_ms < timeout_ms) {
            platform_sleep_ms(timeout_ms - elapsed_ms);
        }
        return;
    }
    g_yield_calls++;
    g_last_yield_timeout_ms = timeout_ms;
    g_fake_now_ms = (g_fake_now_ms + (timeout_ms % 86400000u)) % 86400000u;
}

uint32_t tinyclj_current_time_ms_for_sleep(void) {
    if (!g_override_enabled) {
        return platform_current_time_ms();
    }
    return g_fake_now_ms;
}
