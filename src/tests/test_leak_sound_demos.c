#include "tests_common.h"
#include "builtins.h"
#include "source_resolver.h"

TEST(test_sound_demos_leak) {
    WITH_AUTORELEASE_POOL({
        ID bytes = resolve_path_to_bytes("/libs/tiny-fx/sound-demos.clj");
        load_namespace_from_bytes(g_test_eval_state, "tiny-fx.sound-demos", bytes, "/libs/tiny-fx/sound-demos.clj");
    });
    
    MemoryStats before = memory_profiler_get_stats();
    for (int i = 0; i < 5; i++) {
        WITH_AUTORELEASE_POOL({
            ID bytes = resolve_path_to_bytes("/libs/tiny-fx/sound-demos.clj");
            CljNamespace *ns = ns_find("tiny-fx.sound-demos");
            if (ns) ns->loaded = false;
            load_namespace_from_bytes(g_test_eval_state, "tiny-fx.sound-demos", bytes, "/libs/tiny-fx/sound-demos.clj");
        });
    }
    MemoryStats after = memory_profiler_get_stats();
    
    long long diff = (long long)after.current_memory_usage - (long long)before.current_memory_usage;
    fprintf(stderr, "\nLeak after 5 reloads: %lld bytes\n", diff);
    
    for (int i=0; i<CLJ_TYPE_COUNT; i++) {
        long long type_diff = (long long)after.bytes_current_by_type[i] - (long long)before.bytes_current_by_type[i];
        if (type_diff != 0) {
            fprintf(stderr, "  %s: %lld bytes\n", clj_type_name((CljType)i), type_diff);
        }
    }
}
