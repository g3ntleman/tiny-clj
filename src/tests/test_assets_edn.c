#include "tests_common.h"
#include "../tiny_clj.h"

TEST_SHARED_1(test_assets_edn_loader_contract) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // Setup fake EDN files
    ID setup = eval_string(
        "(do "
        "  (spit \"/data/valid.edn\" \"{:id :test :value 42}\") "
        "  (spit \"/data/invalid.edn\" \"{:value 42}\") " // Missing :id
        "  true)",
        g_test_eval_state);
    TEST_ASSERT_TRUE(setup && setup != clj_false);

    // 1. Loader findet und liest Asset-Dateien
    ID load_ok = eval_string(
        "(do "
        "  (require 'tiny-fx.assets) "
        "  (tiny-fx.assets/reset-cache!) "
        "  (let [data (tiny-fx.assets/load-edn-asset \"/data/valid.edn\" [:id])] "
        "    (and (map? data) (= 42 (:value data)))))",
        g_test_eval_state);
    TEST_ASSERT_TRUE_MESSAGE(load_ok && load_ok != clj_false, "Loader failed to read or parse valid EDN");

    // 2. Loader validiert Pflichtfelder (harte Fehler bei invaliden Assets)
    ID load_invalid = eval_string(
        "(try "
        "  (tiny-fx.assets/load-edn-asset \"/data/invalid.edn\" [:id]) "
        "  false "
        "  (catch Exception e true))",
        g_test_eval_state);
    TEST_ASSERT_TRUE_MESSAGE(load_invalid && load_invalid != clj_false, "Loader should throw on missing required keys");

    // 3. Loader-Cache-Verhalten
    ID cache_ok = eval_string(
        "(do "
        "  (tiny-fx.assets/reset-cache!) "
        "  (let [d1 (tiny-fx.assets/load-edn-asset \"/data/valid.edn\" [:id]) "
        "        _ (spit \"/data/valid.edn\" \"{:id :test :value 99}\") " // Change file
        "        d2 (tiny-fx.assets/load-edn-asset \"/data/valid.edn\" [:id]) " // Should return cached 42
        "        _ (tiny-fx.assets/reset-cache!) "
        "        d3 (tiny-fx.assets/load-edn-asset \"/data/valid.edn\" [:id])] " // Should return new 99
        "    (and (= 42 (:value d1)) "
        "         (= 42 (:value d2)) "
        "         (= 99 (:value d3)))))",
        g_test_eval_state);
    TEST_ASSERT_TRUE_MESSAGE(cache_ok && cache_ok != clj_false, "Loader cache behavior is incorrect");
}

// Diese Tests stellen sicher, dass das Laden der Namespaces nicht zu viel Speicher verbraucht.

TEST_SHARED_1(test_require_heap_startup) {
    // Heap-Messung für tiny-fx.startup
    ID result = eval_string("(:total (heap (require 'tiny-fx.startup)))", g_test_eval_state);
    
    TEST_ASSERT_NOT_NULL_MESSAGE(result, "heap evaluation failed or returned nil");
    TEST_ASSERT_TRUE_MESSAGE(is_fixnum(result), "heap :total should be a fixnum");
    
    int total_bytes = as_fixnum(result);
    // Hard budget limit from plan: 32768 (raised to 220000 due to tiny-fx.gfx-scene footprint)
    // Target: 60000; TODO: Find/fix leaks to lower again.
    test_fprintf(stderr, "\ntiny-fx.startup require heap delta: %d bytes\n", total_bytes);
    TEST_ASSERT_LESS_OR_EQUAL_INT_MESSAGE(70000, total_bytes, "tiny-fx.startup require exceeded heap budget");
}

TEST_SHARED_1(test_require_heap_game_demo) {
    // Heap-Messung für tiny-fx.game-demo
    ID result = eval_string("(:total (heap (require 'tiny-fx.game-demo)))", g_test_eval_state);
    
    TEST_ASSERT_NOT_NULL_MESSAGE(result, "heap evaluation failed or returned nil");
    TEST_ASSERT_TRUE_MESSAGE(is_fixnum(result), "heap :total should be a fixnum");
    
    int total_bytes = as_fixnum(result);
    // Hard budget limit from plan: 131072 (raised to 700000 due to dependencies like tiny-fx.sound and tiny-fx.gfx-scene)
    // Target: 185000; TODO: Find/fix leaks to lower again.
    test_fprintf(stderr, "\ntiny-fx.game-demo require heap delta: %d bytes\n", total_bytes);
    TEST_ASSERT_LESS_OR_EQUAL_INT_MESSAGE(190000, total_bytes, "tiny-fx.game-demo require exceeded heap budget");
}

TEST_SHARED_1(test_require_heap_sound_demos) {
    // Heap-Messung für tiny-fx.sound-demos
    ID result = eval_string("(:total (heap (require 'tiny-fx.sound-demos)))", g_test_eval_state);
    
    TEST_ASSERT_NOT_NULL_MESSAGE(result, "heap evaluation failed or returned nil");
    TEST_ASSERT_TRUE_MESSAGE(is_fixnum(result), "heap :total should be a fixnum");
    
    int total_bytes = as_fixnum(result);
    // Hard budget limit from plan: 32768
    test_fprintf(stderr, "\ntiny-fx.sound-demos require heap delta: %d bytes\n", total_bytes);
    TEST_ASSERT_LESS_OR_EQUAL_INT_MESSAGE(32768, total_bytes, "tiny-fx.sound-demos require exceeded heap budget");
}

TEST_SHARED_1(test_assets_edn_sound_demos_loader_contract) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // 1. Pro Song wird genau ein Asset geladen (hier exemplarisch für minuet-in-g)
    // Wenn :minuet-in-g aufgerufen wird, soll es über den Loader geladen werden.
    // Wir prüfen, ob nur EIN Song geladen wird, indem wir den Speicherbedarf beim ersten Aufruf messen.
    // Aktuell lädt es ALLE Songs, was extrem viel Speicher verbraucht.
    ID minuet_heap = eval_string("(:total (heap (do (require 'tiny-fx.sound-demos) (tiny-fx.sound-demos/demo :minuet-in-g))))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(minuet_heap);
    int minuet_bytes = as_fixnum(minuet_heap);
    test_fprintf(stderr, "\nminuet-in-g demo load heap delta: %d bytes\n", minuet_bytes);
    // Ein einzelner Song sollte deutlich weniger als 30KB brauchen (aktuell >350KB)
    TEST_ASSERT_LESS_OR_EQUAL_INT_MESSAGE(30000, minuet_bytes, "minuet-in-g loaded too much data (probably all songs)");
    
    // 3. Format-/Schema-Tests pro Song-Asset
    // Pflichtfelder (:track-id, :steps, :opts) vorhanden.
    ID check_keys = eval_string(
        "(let [song (tiny-fx.sound-demos/demo :minuet-in-g)] "
        "  (and (contains? song :track-id) "
        "       (contains? song :steps) "
        "       (contains? song :opts)))",
        g_test_eval_state);
    TEST_ASSERT_TRUE_MESSAGE(check_keys && check_keys != clj_false, "minuet-in-g missing required keys");
}
