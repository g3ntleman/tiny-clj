#include "embedded_sources.h"

#include <string.h>
#include <stddef.h>

#if defined(__has_include)
#  if !__has_include("clojure.core.clj.inc")
#    error "Missing generated embedded Clojure includes (clojure.core.clj.inc). Run CMake configure/build to generate build/generated/embedded_clojure/*.inc."
#  endif
#endif

#define EMBEDDED_SOURCE_ENTRY(path_literal, source_array) \
  {                                                       \
      (path_literal),                                     \
      (uint16_t)(sizeof(path_literal) - 1u),              \
      (const uint8_t *)(source_array),                    \
      (int)(sizeof(source_array) - 1u)}

static const char clojure_core_code[] =
#include "clojure.core.clj.inc"
    ;

static const char clojure_core_async_code[] =
#include "clojure.core.async.clj.inc"
    ;

static const char clojure_string_code[] =
#include "clojure.string.clj.inc"
    ;

static const char clojure_repl_code[] =
#include "clojure.repl.clj.inc"
    ;

static const char clojure_pprint_code[] =
#include "clojure.pprint.clj.inc"
    ;

static const char clojure_stacktrace_code[] =
#include "clojure.stacktrace.clj.inc"
    ;

static const char tiny_clj_runtime_code[] =
#include "tiny-clj.runtime.clj.inc"
    ;

static const char tiny_db_kv_code[] =
#include "tiny-db.kv.clj.inc"
    ;

static const char tiny_clj_datetime_code[] =
#include "tiny-clj.datetime.clj.inc"
    ;

static const char tiny_clj_fs_code[] =
#include "tiny-clj.fs.clj.inc"
    ;

static const char tiny_fx_trk1_code[] =
#include "tiny-fx.trk1.clj.inc"
    ;

static const char tiny_fx_sound_code[] =
#include "tiny-fx.sound.clj.inc"
    ;

static const char tiny_fx_assets_code[] =
#include "tiny-fx.assets.clj.inc"
    ;

#if TINY_FX_ENABLED
static const char tiny_fx_gfx_records_code[] =
#include "tiny-fx.gfx-records.clj.inc"
    ;
#endif

static const char tiny_fx_sound_demos_code[] =
#include "tiny-fx.sound-demos.clj.inc"
    ;

static const char tiny_fx_sound_demos_the_entertainer_edn[] =
#include "tiny-fx.sound-demos.the-entertainer.edn.inc"
    ;

static const char tiny_fx_sound_demos_minuet_in_g_edn[] =
#include "tiny-fx.sound-demos.minuet-in-g.edn.inc"
    ;

static const char tiny_fx_sound_demos_gymnopedie_no_1_edn[] =
#include "tiny-fx.sound-demos.gymnopedie-no-1.edn.inc"
    ;

static const char tiny_fx_sound_demos_rondo_alla_turca_edn[] =
#include "tiny-fx.sound-demos.rondo-alla-turca.edn.inc"
    ;

static const char tiny_fx_sound_demos_hall_of_the_mountain_king_edn[] =
#include "tiny-fx.sound-demos.hall-of-the-mountain-king.edn.inc"
    ;

static const char tiny_fx_sound_demos_can_can_edn[] =
#include "tiny-fx.sound-demos.can-can.edn.inc"
    ;

static const char tiny_fx_sound_demos_laser_sfx_edn[] =
#include "tiny-fx.sound-demos.laser-sfx.edn.inc"
    ;

static const char tiny_fx_sound_demos_rocket_launch_sfx_edn[] =
#include "tiny-fx.sound-demos.rocket-launch-sfx.edn.inc"
    ;

#if TINY_FX_ENABLED
static const char tiny_fx_sound_demos_william_code[] =
#include "tiny-fx.sound-demos-william.clj.inc"
    ;

#ifdef DEBUG
static const char tiny_fx_sound_debug_code[] =
#include "tiny-fx.sound-debug.clj.inc"
    ;
#endif
#endif

static const char tiny_clj_gpio_code[] =
#include "tiny-clj.gpio.clj.inc"
    ;

static const char tiny_clj_board_code[] =
#include "tiny-clj.board.clj.inc"
    ;

static const char tiny_clj_button_code[] =
#include "tiny-clj.button.clj.inc"
    ;

static const char tiny_clj_event_code[] =
#include "tiny-clj.event.clj.inc"
    ;

static const char tiny_clj_sensor_code[] =
#include "tiny-clj.sensor.clj.inc"
    ;

static const char tiny_clj_deployment_code[] =
#include "tiny-clj.deployment.clj.inc"
    ;

static const char tiny_clj_net_code[] =
#include "tiny-clj.net.clj.inc"
    ;

static const char tiny_clj_net_mdns_code[] =
#include "tiny-clj.net.mdns.clj.inc"
    ;

#if TINY_FX_ENABLED
static const char tiny_fx_gfx_scene_code[] =
#include "tiny-gfx.scene.clj.inc"
    ;

static const char tiny_fx_gfx_timeline_code[] =
#include "tiny-fx.gfx-timeline.clj.inc"
    ;

#ifdef DEBUG
static const char tiny_fx_gfx_collision_code[] =
#include "tiny-gfx.collision.clj.inc"
    ;
#endif

#ifdef DEBUG
static const char tiny_fx_gfx_bench_code[] =
#include "tiny-fx.gfx-bench.clj.inc"
    ;
#endif

static const char tiny_fx_gfx_code[] =
#include "tiny-gfx.runtime.clj.inc"
    ;

static const char tiny_fx_startup_code[] =
#include "tiny-fx.startup.clj.inc"
    ;

static const char tiny_fx_game_demo_code[] =
#include "tiny-fx.game-demo.clj.inc"
    ;

static const char tiny_breakout_core_code[] =
#include "tiny-breakout.core.clj.inc"
    ;

static const char tiny_breakout_runtime_code[] =
#include "tiny-breakout.runtime.clj.inc"
    ;

static const char tiny_breakout_scene_code[] =
#include "tiny-breakout.scene.clj.inc"
    ;

static const char tiny_breakout_audio_code[] =
#include "tiny-breakout.audio.clj.inc"
    ;

static const char tiny_breakout_audio_compiler_code[] =
#include "tiny-breakout.audio-compiler.clj.inc"
    ;

static const char tiny_breakout_levels_code[] =
#include "tiny-breakout.levels.clj.inc"
    ;
#endif

static const char tiny_db_rrd_code[] =
#include "tiny-db.rrd.clj.inc"
    ;

static const char tiny_db_rrd_classic_code[] =
#include "tiny-db.rrd-classic.clj.inc"
    ;

static const char tiny_db_rrd_spline_code[] =
#include "tiny-db.rrd-spline.clj.inc"
    ;

typedef struct {
  const char *path;
  uint16_t path_len;
  const uint8_t *data;
  int len;
} EmbeddedSourceEntry;

#define EMBEDDED_ASSET_BYTES(path_literal, arr) \
  { (path_literal), (uint16_t)(sizeof(path_literal) - 1u), (const uint8_t *)(arr), (int)sizeof(arr) }

static const uint8_t tiny_fx_the_entertainer_trk1[] = {
#include "the_entertainer_trk1.inc"
};

static const EmbeddedSourceEntry g_embedded_sources[] = {
    EMBEDDED_SOURCE_ENTRY("/libs/clojure/core.clj", clojure_core_code),
    EMBEDDED_SOURCE_ENTRY("/libs/clojure/core/async.clj", clojure_core_async_code),
    EMBEDDED_SOURCE_ENTRY("/libs/clojure/string.clj", clojure_string_code),
    EMBEDDED_SOURCE_ENTRY("/libs/clojure/repl.clj", clojure_repl_code),
    EMBEDDED_SOURCE_ENTRY("/libs/clojure/pprint.clj", clojure_pprint_code),
    EMBEDDED_SOURCE_ENTRY("/libs/clojure/stacktrace.clj", clojure_stacktrace_code),
    EMBEDDED_SOURCE_ENTRY("/libs/tiny-clj/runtime.clj", tiny_clj_runtime_code),
    EMBEDDED_SOURCE_ENTRY("/libs/tiny-clj/fs.clj", tiny_clj_fs_code),
    EMBEDDED_SOURCE_ENTRY("/libs/tiny-clj/board.clj", tiny_clj_board_code),
    EMBEDDED_SOURCE_ENTRY("/libs/tiny-clj/button.clj", tiny_clj_button_code),
    EMBEDDED_SOURCE_ENTRY("/libs/tiny-clj/event.clj", tiny_clj_event_code),
    EMBEDDED_SOURCE_ENTRY("/libs/tiny-clj/sensor.clj", tiny_clj_sensor_code),
    EMBEDDED_SOURCE_ENTRY("/libs/tiny-clj/deployment.clj", tiny_clj_deployment_code),
    EMBEDDED_SOURCE_ENTRY("/libs/tiny-fx/trk1.clj", tiny_fx_trk1_code),
    EMBEDDED_SOURCE_ENTRY("/libs/tiny-fx/sound.clj", tiny_fx_sound_code),
    EMBEDDED_SOURCE_ENTRY("/libs/tiny-fx/assets.clj", tiny_fx_assets_code),
    EMBEDDED_SOURCE_ENTRY("/libs/tiny-fx/sound-demos.clj", tiny_fx_sound_demos_code),
    EMBEDDED_SOURCE_ENTRY("/assets/tiny-fx/sound-demos/the-entertainer.edn",
                          tiny_fx_sound_demos_the_entertainer_edn),
    EMBEDDED_SOURCE_ENTRY("/assets/tiny-fx/sound-demos/minuet-in-g.edn",
                          tiny_fx_sound_demos_minuet_in_g_edn),
    EMBEDDED_SOURCE_ENTRY("/assets/tiny-fx/sound-demos/gymnopedie-no-1.edn",
                          tiny_fx_sound_demos_gymnopedie_no_1_edn),
    EMBEDDED_SOURCE_ENTRY("/assets/tiny-fx/sound-demos/rondo-alla-turca.edn",
                          tiny_fx_sound_demos_rondo_alla_turca_edn),
    EMBEDDED_SOURCE_ENTRY("/assets/tiny-fx/sound-demos/hall-of-the-mountain-king.edn",
                          tiny_fx_sound_demos_hall_of_the_mountain_king_edn),
    EMBEDDED_SOURCE_ENTRY("/assets/tiny-fx/sound-demos/can-can.edn",
                          tiny_fx_sound_demos_can_can_edn),
    EMBEDDED_SOURCE_ENTRY("/assets/tiny-fx/sound-demos/laser-sfx.edn",
                          tiny_fx_sound_demos_laser_sfx_edn),
    EMBEDDED_SOURCE_ENTRY("/assets/tiny-fx/sound-demos/rocket-launch-sfx.edn",
                          tiny_fx_sound_demos_rocket_launch_sfx_edn),
    EMBEDDED_ASSET_BYTES("/assets/tiny-fx/sound-demos/the-entertainer.trk1", tiny_fx_the_entertainer_trk1),
#if TINY_FX_ENABLED
    EMBEDDED_SOURCE_ENTRY("/libs/tiny-fx/gfx-records.clj", tiny_fx_gfx_records_code),
    EMBEDDED_SOURCE_ENTRY("/libs/tiny-fx/sound-demos-william.clj", tiny_fx_sound_demos_william_code),
#ifdef DEBUG
    EMBEDDED_SOURCE_ENTRY("/libs/tiny-fx/sound-debug.clj", tiny_fx_sound_debug_code),
#endif
#endif
    EMBEDDED_SOURCE_ENTRY("/libs/tiny-clj/gpio.clj", tiny_clj_gpio_code),
    EMBEDDED_SOURCE_ENTRY("/libs/tiny-clj/net.clj", tiny_clj_net_code),
    EMBEDDED_SOURCE_ENTRY("/libs/tiny-clj/net/mdns.clj", tiny_clj_net_mdns_code),
#if TINY_FX_ENABLED
    EMBEDDED_SOURCE_ENTRY("/libs/tiny-fx/gfx-scene.clj", tiny_fx_gfx_scene_code),
    EMBEDDED_SOURCE_ENTRY("/libs/tiny-fx/gfx-timeline.clj", tiny_fx_gfx_timeline_code),
#ifdef DEBUG
    EMBEDDED_SOURCE_ENTRY("/libs/tiny-fx/gfx-collision.clj", tiny_fx_gfx_collision_code),
#endif
#ifdef DEBUG
    EMBEDDED_SOURCE_ENTRY("/libs/tiny-fx/gfx-bench.clj", tiny_fx_gfx_bench_code),
#endif
    EMBEDDED_SOURCE_ENTRY("/libs/tiny-fx/gfx.clj", tiny_fx_gfx_code),
    EMBEDDED_SOURCE_ENTRY("/libs/tiny-fx/startup.clj", tiny_fx_startup_code),
    EMBEDDED_SOURCE_ENTRY("/libs/tiny-fx/game-demo.clj", tiny_fx_game_demo_code),
    EMBEDDED_SOURCE_ENTRY("/libs/tiny-breakout/core.clj", tiny_breakout_core_code),
    EMBEDDED_SOURCE_ENTRY("/libs/tiny-breakout/runtime.clj", tiny_breakout_runtime_code),
    EMBEDDED_SOURCE_ENTRY("/libs/tiny-breakout/scene.clj", tiny_breakout_scene_code),
    EMBEDDED_SOURCE_ENTRY("/libs/tiny-breakout/audio.clj", tiny_breakout_audio_code),
    EMBEDDED_SOURCE_ENTRY("/libs/tiny-breakout/audio-compiler.clj", tiny_breakout_audio_compiler_code),
    EMBEDDED_SOURCE_ENTRY("/libs/tiny-breakout/levels.clj", tiny_breakout_levels_code),
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
#undef EMBEDDED_ASSET_BYTES
