#include "embedded_sources.h"

#include "runtime.h"
#include "map.h"
#include "strings.h"
#include "byte_array.h"
#include "tiny_clj.h"
#include "memory.h"

#include <string.h>
#include <limits.h>

static const char *clojure_core_code =
#include "clojure.core.clj"
    ;

static const char *clojure_string_code =
#include "clojure.string.clj"
    ;

static const char *clojure_repl_code =
#include "clojure.repl.clj"
    ;

static const char *clojure_pprint_code =
#include "clojure.pprint.clj"
    ;

static const char *clojure_stacktrace_code =
#include "clojure.stacktrace.clj"
    ;

static const char *tiny_clj_runtime_code =
#include "tiny-clj.runtime.clj"
    ;

static const char *tiny_db_kv_code =
#include "tiny-db.kv.clj"
    ;

static const char *tiny_clj_datetime_code =
#include "tiny-clj.datetime.clj"
    ;

static const char *tiny_clj_fs_code =
#include "tiny-clj.fs.clj"
    ;

static const char *tiny_snd_composer_code =
#include "tiny-clj.audio.clj"
    ;

static const char *tiny_clj_net_code =
#include "tiny-clj.net.clj"
    ;

static const char *tiny_clj_net_mdns_code =
#include "tiny-clj.net.mdns.clj"
    ;

static const char *tiny_db_rrd_code =
#include "tiny-db.rrd.clj"
    ;

static const char *tiny_db_rrd_classic_code =
#include "tiny-db.rrd-classic.clj"
    ;

static const char *tiny_db_rrd_spline_code =
#include "tiny-db.rrd-spline.clj"
    ;

static void embedded_source_map_add(CljPersistentMap **map, const char *path, const char *data) {
    if (!map || !*map || !path || !data) return;

    size_t len = strlen(data);
    if (len > (size_t)INT_MAX) return;

    CljString *key = make_string(path);
    CljByteArray *val = make_byte_array_view((uint8_t*)data, (int)len);
    if (key && val) {
        map_assoc_inplace(map, (ID)key, (ID)val);
    }
    RELEASE(key);
    RELEASE(val);
}

void embedded_source_map_init(void) {
    if (g_runtime.embedded_source_map) return;

    CljPersistentMap *m = map_empty();

    embedded_source_map_add(&m, "/libs/clojure/core.clj", clojure_core_code);
    embedded_source_map_add(&m, "/libs/clojure/string.clj", clojure_string_code);
    embedded_source_map_add(&m, "/libs/clojure/repl.clj", clojure_repl_code);
    embedded_source_map_add(&m, "/libs/clojure/pprint.clj", clojure_pprint_code);
    embedded_source_map_add(&m, "/libs/clojure/stacktrace.clj", clojure_stacktrace_code);
    embedded_source_map_add(&m, "/libs/tiny-clj/runtime.clj", tiny_clj_runtime_code);
    embedded_source_map_add(&m, "/libs/tiny-clj/fs.clj", tiny_clj_fs_code);
    embedded_source_map_add(&m, "/libs/tiny-snd/composer.clj", tiny_snd_composer_code);
    embedded_source_map_add(&m, "/libs/tiny-clj/net.clj", tiny_clj_net_code);
    embedded_source_map_add(&m, "/libs/tiny-clj/net/mdns.clj", tiny_clj_net_mdns_code);
    embedded_source_map_add(&m, "/libs/tiny-db/kv.clj", tiny_db_kv_code);
    embedded_source_map_add(&m, "/libs/tiny-db/rrd.clj", tiny_db_rrd_code);
    embedded_source_map_add(&m, "/libs/tiny-db/rrd-classic.clj", tiny_db_rrd_classic_code);
    embedded_source_map_add(&m, "/libs/tiny-db/rrd-spline.clj", tiny_db_rrd_spline_code);
    embedded_source_map_add(&m, "/libs/tiny-clj/datetime.clj", tiny_clj_datetime_code);

    ASSIGN(g_runtime.embedded_source_map, m);
    RELEASE(m);
}
