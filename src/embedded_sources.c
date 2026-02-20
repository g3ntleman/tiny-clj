#include "embedded_sources.h"

#include <string.h>
#include <stddef.h>

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

static const char tiny_snd_composer_code[] =
#include "tiny-clj.audio.clj"
    ;

static const char tiny_clj_net_code[] =
#include "tiny-clj.net.clj"
    ;

static const char tiny_clj_net_mdns_code[] =
#include "tiny-clj.net.mdns.clj"
    ;

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
    const uint8_t *data;
    int len;
} EmbeddedSourceEntry;

static const EmbeddedSourceEntry g_embedded_sources[] = {
    { "/libs/clojure/core.clj", (const uint8_t *)clojure_core_code, (int)(sizeof(clojure_core_code) - 1) },
    { "/libs/clojure/string.clj", (const uint8_t *)clojure_string_code, (int)(sizeof(clojure_string_code) - 1) },
    { "/libs/clojure/repl.clj", (const uint8_t *)clojure_repl_code, (int)(sizeof(clojure_repl_code) - 1) },
    { "/libs/clojure/pprint.clj", (const uint8_t *)clojure_pprint_code, (int)(sizeof(clojure_pprint_code) - 1) },
    { "/libs/clojure/stacktrace.clj", (const uint8_t *)clojure_stacktrace_code, (int)(sizeof(clojure_stacktrace_code) - 1) },
    { "/libs/tiny-clj/runtime.clj", (const uint8_t *)tiny_clj_runtime_code, (int)(sizeof(tiny_clj_runtime_code) - 1) },
    { "/libs/tiny-clj/fs.clj", (const uint8_t *)tiny_clj_fs_code, (int)(sizeof(tiny_clj_fs_code) - 1) },
    { "/libs/tiny-snd/composer.clj", (const uint8_t *)tiny_snd_composer_code, (int)(sizeof(tiny_snd_composer_code) - 1) },
    { "/libs/tiny-clj/net.clj", (const uint8_t *)tiny_clj_net_code, (int)(sizeof(tiny_clj_net_code) - 1) },
    { "/libs/tiny-clj/net/mdns.clj", (const uint8_t *)tiny_clj_net_mdns_code, (int)(sizeof(tiny_clj_net_mdns_code) - 1) },
    { "/libs/tiny-db/kv.clj", (const uint8_t *)tiny_db_kv_code, (int)(sizeof(tiny_db_kv_code) - 1) },
    { "/libs/tiny-db/rrd.clj", (const uint8_t *)tiny_db_rrd_code, (int)(sizeof(tiny_db_rrd_code) - 1) },
    { "/libs/tiny-db/rrd-classic.clj", (const uint8_t *)tiny_db_rrd_classic_code, (int)(sizeof(tiny_db_rrd_classic_code) - 1) },
    { "/libs/tiny-db/rrd-spline.clj", (const uint8_t *)tiny_db_rrd_spline_code, (int)(sizeof(tiny_db_rrd_spline_code) - 1) },
    { "/libs/tiny-clj/datetime.clj", (const uint8_t *)tiny_clj_datetime_code, (int)(sizeof(tiny_clj_datetime_code) - 1) },
};

void embedded_source_map_init(void) {
    // No-op: embedded sources are served via static lookup + on-demand byte-array views.
}

bool embedded_source_lookup(const char *path, const uint8_t **out_data, int *out_len) {
    if (!path || !out_data || !out_len) return false;

    for (size_t i = 0; i < sizeof(g_embedded_sources) / sizeof(g_embedded_sources[0]); i++) {
        const EmbeddedSourceEntry *e = &g_embedded_sources[i];
        if (strcmp(path, e->path) == 0) {
            *out_data = e->data;
            *out_len = e->len;
            return true;
        }
    }

    return false;
}
