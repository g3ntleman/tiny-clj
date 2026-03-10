#include "embedded_sources.h"

#include <string.h>
#include <stddef.h>

#define EMBEDDED_SOURCE_ENTRY(path_literal, source_array) \
  {                                                       \
      (path_literal),                                     \
      (uint16_t)(sizeof(path_literal) - 1u),              \
      (const uint8_t *)(source_array),                    \
      (int)(sizeof(source_array) - 1u)}

#ifndef TINYCLJ_WITH_TINY_FX
#define TINYCLJ_WITH_TINY_FX 1
#endif

static const char clojure_core_code[] =
#include "clojure.core.clj"
    ;

static const char clojure_string_code[] =
#include "clojure.string.clj"
    ;

static const char clojure_repl_code[] =
#include "clojure.repl.clj"
    ;

static const char clojure_pprint_code[] =
#include "clojure.pprint.clj"
    ;

static const char clojure_stacktrace_code[] =
#include "clojure.stacktrace.clj"
    ;

static const char tiny_clj_runtime_code[] =
#include "tiny-clj.runtime.clj"
    ;

static const char tiny_db_kv_code[] =
#include "tiny-db.kv.clj"
    ;

static const char tiny_clj_datetime_code[] =
#include "tiny-clj.datetime.clj"
    ;

static const char tiny_clj_fs_code[] =
#include "tiny-clj.fs.clj"
    ;

#if TINYCLJ_WITH_TINY_FX
static const char tiny_fx_sound_code[] =
#include "tiny-fx.sound.clj"
    ;

static const char tiny_fx_sound_native_code[] =
#include "tiny-fx.sound-native.clj"
    ;

#ifdef DEBUG
static const char tiny_fx_sound_debug_code[] =
#include "tiny-fx.sound-debug.clj"
    ;
#endif
#endif

static const char tiny_clj_gpio_code[] =
#include "tiny-clj.gpio.clj"
    ;

static const char tiny_clj_net_code[] =
#include "tiny-clj.net.clj"
    ;

static const char tiny_clj_net_mdns_code[] =
#include "tiny-clj.net.mdns.clj"
    ;

#if TINYCLJ_WITH_TINY_FX
static const char tiny_fx_gfx_scene_code[] =
#include "tiny-gfx.scene.clj"
    ;

#ifdef DEBUG
static const char tiny_fx_gfx_collision_code[] =
#include "tiny-gfx.collision.clj"
    ;
#endif

#ifdef DEBUG
static const char tiny_fx_gfx_bench_code[] =
#include "tiny-fx.gfx-bench.clj"
    ;
#endif

static const char tiny_fx_gfx_code[] =
#include "tiny-gfx.runtime.clj"
    ;

static const char tiny_fx_startup_code[] =
#include "tiny-fx.startup.clj"
    ;

static const char tiny_fx_game_demo_code[] =
#include "tiny-fx.game-demo.clj"
    ;
#endif

static const char tiny_db_rrd_code[] =
#include "tiny-db.rrd.clj"
    ;

static const char tiny_db_rrd_classic_code[] =
#include "tiny-db.rrd-classic.clj"
    ;

static const char tiny_db_rrd_spline_code[] =
#include "tiny-db.rrd-spline.clj"
    ;

typedef struct {
  const char *path;
  uint16_t path_len;
  const uint8_t *data;
  int len;
} EmbeddedSourceEntry;

static const EmbeddedSourceEntry g_embedded_sources[] = {
    EMBEDDED_SOURCE_ENTRY("/libs/clojure/core.clj", clojure_core_code),
    EMBEDDED_SOURCE_ENTRY("/libs/clojure/string.clj", clojure_string_code),
    EMBEDDED_SOURCE_ENTRY("/libs/clojure/repl.clj", clojure_repl_code),
    EMBEDDED_SOURCE_ENTRY("/libs/clojure/pprint.clj", clojure_pprint_code),
    EMBEDDED_SOURCE_ENTRY("/libs/clojure/stacktrace.clj", clojure_stacktrace_code),
    EMBEDDED_SOURCE_ENTRY("/libs/tiny-clj/runtime.clj", tiny_clj_runtime_code),
    EMBEDDED_SOURCE_ENTRY("/libs/tiny-clj/fs.clj", tiny_clj_fs_code),
#if TINYCLJ_WITH_TINY_FX
    EMBEDDED_SOURCE_ENTRY("/libs/tiny-fx/sound.clj", tiny_fx_sound_code),
    EMBEDDED_SOURCE_ENTRY("/libs/tiny-fx/sound-native.clj", tiny_fx_sound_native_code),
#ifdef DEBUG
    EMBEDDED_SOURCE_ENTRY("/libs/tiny-fx/sound-debug.clj", tiny_fx_sound_debug_code),
#endif
#endif
    EMBEDDED_SOURCE_ENTRY("/libs/tiny-clj/gpio.clj", tiny_clj_gpio_code),
    EMBEDDED_SOURCE_ENTRY("/libs/tiny-clj/net.clj", tiny_clj_net_code),
    EMBEDDED_SOURCE_ENTRY("/libs/tiny-clj/net/mdns.clj", tiny_clj_net_mdns_code),
#if TINYCLJ_WITH_TINY_FX
    EMBEDDED_SOURCE_ENTRY("/libs/tiny-fx/gfx-scene.clj", tiny_fx_gfx_scene_code),
#ifdef DEBUG
    EMBEDDED_SOURCE_ENTRY("/libs/tiny-fx/gfx-collision.clj", tiny_fx_gfx_collision_code),
#endif
#ifdef DEBUG
    EMBEDDED_SOURCE_ENTRY("/libs/tiny-fx/gfx-bench.clj", tiny_fx_gfx_bench_code),
#endif
    EMBEDDED_SOURCE_ENTRY("/libs/tiny-fx/gfx.clj", tiny_fx_gfx_code),
    EMBEDDED_SOURCE_ENTRY("/libs/tiny-fx/startup.clj", tiny_fx_startup_code),
    EMBEDDED_SOURCE_ENTRY("/libs/tiny-fx/game-demo.clj", tiny_fx_game_demo_code),
#endif
    EMBEDDED_SOURCE_ENTRY("/libs/tiny-db/kv.clj", tiny_db_kv_code),
    EMBEDDED_SOURCE_ENTRY("/libs/tiny-db/rrd.clj", tiny_db_rrd_code),
    EMBEDDED_SOURCE_ENTRY("/libs/tiny-db/rrd-classic.clj", tiny_db_rrd_classic_code),
    EMBEDDED_SOURCE_ENTRY("/libs/tiny-db/rrd-spline.clj", tiny_db_rrd_spline_code),
    EMBEDDED_SOURCE_ENTRY("/libs/tiny-clj/datetime.clj", tiny_clj_datetime_code),
};

/**
 * @brief Returns true when @p path matches an embedded source table entry.
 *
 * Uses a single caller-computed path length and length-first filtering to
 * reduce repeated scans over long `/libs/...` prefixes on embedded targets.
 *
 * @param e Table entry.
 * @param path Lookup path.
 * @param path_len Precomputed `strlen(path)`.
 * @return true when the path matches exactly.
 */
static bool embedded_source_path_matches(const EmbeddedSourceEntry *e, const char *path, size_t path_len) {
  if (!e || !path) {
    return false;
  }
  return (size_t)e->path_len == path_len && memcmp(e->path, path, path_len) == 0;
}

/**
 * @brief Initializes the embedded source registry.
 *
 * Embedded sources are served from a static table in this build, so this is a
 * no-op kept for API symmetry with other source backends.
 */
void embedded_source_map_init(void) {
  // No-op: embedded sources are served via static lookup + on-demand byte-array views.
}

/**
 * @brief Looks up embedded source bytes by virtual path.
 *
 * @param path Virtual path (e.g. `/libs/clojure/core.clj`).
 * @param out_data Receives pointer to immutable embedded bytes.
 * @param out_len Receives byte length.
 * @return true when an embedded source exists for @p path.
 */
bool embedded_source_lookup(const char *path, const uint8_t **out_data, int *out_len) {
  if (!path || !out_data || !out_len)
    return false;

  size_t path_len = strlen(path);

  for (size_t i = 0; i < sizeof(g_embedded_sources) / sizeof(g_embedded_sources[0]); i++) {
    const EmbeddedSourceEntry *e = &g_embedded_sources[i];
    if (embedded_source_path_matches(e, path, path_len)) {
      *out_data = e->data;
      *out_len = e->len;
      return true;
    }
  }

  return false;
}

#undef EMBEDDED_SOURCE_ENTRY
