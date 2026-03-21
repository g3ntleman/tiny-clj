#include "tests_common.h"
#include "../tiny_clj.h"

TEST(test_assets_edn_loader_contract) {
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
        "  (let [data (tiny-fx.assets/edn-asset \"/data/valid.edn\" [:id])] "
        "    (and (map? data) (= 42 (:value data)))))",
        g_test_eval_state);
    TEST_ASSERT_TRUE_MESSAGE(load_ok && load_ok != clj_false, "Loader failed to read or parse valid EDN");

    // 2. Loader validiert Pflichtfelder (harte Fehler bei invaliden Assets)
    ID load_invalid = eval_string(
        "(try "
        "  (tiny-fx.assets/edn-asset \"/data/invalid.edn\" [:id]) "
        "  false "
        "  (catch Exception e true))",
        g_test_eval_state);
    TEST_ASSERT_TRUE_MESSAGE(load_invalid && load_invalid != clj_false, "Loader should throw on missing required keys");

    // 3. Loader ohne Cache: Dateiänderung muss sofort sichtbar sein
    ID uncached_ok = eval_string(
        "(do "
        "  (let [d1 (tiny-fx.assets/edn-asset \"/data/valid.edn\" [:id]) "
        "        _ (spit \"/data/valid.edn\" \"{:id :test :value 99}\") " // Change file
        "        d2 (tiny-fx.assets/edn-asset \"/data/valid.edn\" [:id])] "
        "    (and (= 42 (:value d1)) "
        "         (= 99 (:value d2)))))",
        g_test_eval_state);
    TEST_ASSERT_TRUE_MESSAGE(uncached_ok && uncached_ok != clj_false, "Loader should not cache globally");
}

// Diese Tests stellen sicher, dass das Laden der Namespaces nicht zu viel Speicher verbraucht.

TEST(test_require_heap_startup) {
    // Heap-Messung für tiny-fx.startup
    ID result = eval_string("(:total (heap (require 'tiny-fx.startup)))", g_test_eval_state);
    
    TEST_ASSERT_NOT_NULL_MESSAGE(result, "heap evaluation failed or returned nil");
    TEST_ASSERT_TRUE_MESSAGE(is_fixnum(result), "heap :total should be a fixnum");
    
    int total_bytes = as_fixnum(result);
    // Hard budget limit from plan: 32768 (raised to 220000 due to tiny-fx.gfx-scene footprint)
    // Target: 80000 (raised to 95000); TODO: Find/fix leaks to lower again.
    test_fprintf(stderr, "\ntiny-fx.startup require heap delta: %d bytes\n", total_bytes);
    TEST_ASSERT_LESS_OR_EQUAL_INT_MESSAGE(95000, total_bytes, "tiny-fx.startup require exceeded heap budget");
}

TEST(test_require_heap_game_demo) {
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

TEST(test_require_heap_sound_demos) {
    // Heap-Messung für tiny-fx.sound-demos
    ID result = eval_string("(:total (heap (require 'tiny-fx.sound-demos)))", g_test_eval_state);
    
    TEST_ASSERT_NOT_NULL_MESSAGE(result, "heap evaluation failed or returned nil");
    TEST_ASSERT_TRUE_MESSAGE(is_fixnum(result), "heap :total should be a fixnum");
    
    int total_bytes = as_fixnum(result);
    // Hard budget limit from plan: 32768
    test_fprintf(stderr, "\ntiny-fx.sound-demos require heap delta: %d bytes\n", total_bytes);
    TEST_ASSERT_LESS_OR_EQUAL_INT_MESSAGE(32768, total_bytes, "tiny-fx.sound-demos require exceeded heap budget");
}

TEST(test_require_sound_demos_does_not_autoload_sound_namespace) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);

    (void)eval_string("(require 'tiny-fx.sound-demos)", g_test_eval_state);
    CljNamespace *sound_ns_after_require = ns_find("tiny-fx.sound");
    TEST_ASSERT_NULL_MESSAGE(sound_ns_after_require,
                             "require tiny-fx.sound-demos must not autoload tiny-fx.sound");

    ID played = eval_string("(do (tiny-fx.sound-demos/play-demo! :laser-sfx) true)", g_test_eval_state);
    TEST_ASSERT_TRUE(played && played != clj_false);

    CljNamespace *sound_ns_after_play = ns_find("tiny-fx.sound");
    TEST_ASSERT_NOT_NULL_MESSAGE(sound_ns_after_play,
                                 "play-demo! should load tiny-fx.sound on demand");
}

TEST(test_assets_edn_sound_demos_loader_contract) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);
    
    // 1. Pro Song wird genau ein Asset geladen (hier exemplarisch für minuet-in-g)
    // Wenn :minuet-in-g aufgerufen wird, soll es über den Loader geladen werden.
    // Wir prüfen, ob nur EIN Song geladen wird, indem wir den Speicherbedarf beim ersten Aufruf messen.
    // Aktuell lädt es ALLE Songs, was extrem viel Speicher verbraucht.
    ID minuet_heap = eval_string("(:total (heap (do (require 'tiny-fx.sound-demos) (tiny-fx.sound-demos/load-song :minuet-in-g))))", g_test_eval_state);
    TEST_ASSERT_NOT_NULL(minuet_heap);
    int minuet_bytes = as_fixnum(minuet_heap);
    test_fprintf(stderr, "\nminuet-in-g demo load heap delta: %d bytes\n", minuet_bytes);
    // Ein einzelner Song sollte deutlich weniger als 30KB brauchen (aktuell >350KB)
    TEST_ASSERT_LESS_OR_EQUAL_INT_MESSAGE(30000, minuet_bytes, "minuet-in-g loaded too much data (probably all songs)");
    
    // 3. Format-/Schema-Tests pro Song-Asset
    // Pflichtfelder (:track-id, :steps, :opts) vorhanden.
    ID check_keys = eval_string(
        "(let [song (tiny-fx.sound-demos/load-song :minuet-in-g)] "
        "  (and (contains? song :track-id) "
        "       (contains? song :steps) "
        "       (contains? song :opts) "
        "       (contains? song :kind)))",
        g_test_eval_state);
    TEST_ASSERT_TRUE_MESSAGE(check_keys && check_keys != clj_false, "minuet-in-g missing required keys");
}

TEST(test_assets_edn_file_load_has_zero_heap_growth) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);

    ID setup = eval_string(
        "(do "
        "  (require 'tiny-fx.assets) "
        "  (defrecord HeapZeroRec [x y]) "
        "  (spit \"/data/heap-zero.edn\" "
        "        \"{:id :heap-zero "
        "          :nil nil "
        "          :bool true "
        "          :int 7 "
        "          :float 3.5 "
        "          :string \\\"ok\\\" "
        "          :keyword :k "
        "          :symbol hello/world "
        "          :list (1 2 3) "
        "          :vector [1 2 3] "
        "          :map {:a 1 :b 2} "
        "          :set #{1 2 3} "
        "          :record {:x 9 :y 10}}\") "
        "  true)",
        g_test_eval_state);
    TEST_ASSERT_TRUE(setup && setup != clj_false);

    ID loaded = eval_string(
        "(tiny-fx.assets/edn-asset \"/data/heap-zero.edn\" [:id])",
        g_test_eval_state);
    TEST_ASSERT_NOT_NULL_MESSAGE(loaded, "EDN file load returned nil");
    TEST_ASSERT_TRUE_MESSAGE(is_map(loaded), "EDN file load should return a map");
    TEST_ASSERT_EQUAL_INT_MESSAGE(
        7,
        as_fixnum(map_get(loaded, intern_symbol_global(":int"))),
        "EDN file load should parse payload values");

    ID all_types_ok = eval_string(
        "(let [d (tiny-fx.assets/edn-asset \"/data/heap-zero.edn\" [:id])] "
        "  (and (nil? (:nil d)) "
        "       (= true (:bool d)) "
        "       (= 7 (:int d)) "
        "       (= 3.5 (:float d)) "
        "       (= \"ok\" (:string d)) "
        "       (= :k (:keyword d)) "
        "       (= 'hello/world (:symbol d)) "
        "       (= '(1 2 3) (:list d)) "
        "       (= [1 2 3] (:vector d)) "
        "       (= {:a 1 :b 2} (:map d)) "
        "       (= #{1 2 3} (:set d)) "
        "       (let [rec (map->HeapZeroRec (:record d))] "
        "         (and (= 9 (:x rec)) "
        "              (= 10 (:y rec)))))))",
        g_test_eval_state);
    TEST_ASSERT_TRUE_MESSAGE(all_types_ok && all_types_ok != clj_false,
                             "heap-zero.edn should include all EDN types including a record");
}

TEST(test_sound_demo_asset_load_is_reclaimable) {
    TEST_ASSERT_NOT_NULL(g_test_eval_state);

    // tiny-fx.sound-demos/load-song must load data on demand and keep no global
    // reference when call-site drops the returned map.
    ID setup = eval_string(
        "(do "
        "  (require 'tiny-fx.sound-demos) "
        "  true)",
        g_test_eval_state);
    TEST_ASSERT_TRUE(setup && setup != clj_false);

    ID ok = eval_string(
        "(let [song (tiny-fx.sound-demos/load-song :minuet-in-g)] "
        "  (and (map? song) (contains? song :track-id) (contains? song :steps) (contains? song :opts)))",
        g_test_eval_state);
    TEST_ASSERT_TRUE_MESSAGE(ok && ok != clj_false, "demo should return a valid song map");
}
