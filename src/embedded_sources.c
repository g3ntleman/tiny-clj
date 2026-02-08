#include "embedded_sources.h"

#include "runtime.h"
#include "map.h"
#include "strings.h"
#include "byte_array.h"
#include "tiny_clj.h"
#include "memory.h"

#include <string.h>
#include <limits.h>

static const char *clojure_string_code =
#include "clojure.string.clj"
    ;

static const char *clojure_repl_code =
#include "clojure.repl.clj"
    ;

static const char *tinyclj_runtime_code =
#include "tinyclj.runtime.clj"
    ;

static void embedded_source_map_add(CljPersistentMap **map, const char *path, const char *data) {
    if (!map || !*map || !path || !data) return;

    size_t len = strlen(data);
    if (len > (size_t)INT_MAX) return;

    CljString *key = make_string(path);
    CljByteArray *val = make_byte_array_view((uint8_t*)data, (int)len);
    if (!key || !val) {
        RELEASE(key);
        RELEASE(val);
        return;
    }

    map_assoc_inplace(map, (ID)key, (ID)val);
    RELEASE(key);
    RELEASE(val);
}

void embedded_source_map_init(void) {
    if (g_runtime.embedded_source_map) return;

    CljPersistentMap *m = map_empty();

    embedded_source_map_add(&m, "/libs/clojure/core.clj", clojure_core_code);
    embedded_source_map_add(&m, "/libs/clojure/string.clj", clojure_string_code);
    embedded_source_map_add(&m, "/libs/clojure/repl.clj", clojure_repl_code);
    embedded_source_map_add(&m, "/libs/tinyclj/runtime.clj", tinyclj_runtime_code);

    ASSIGN(g_runtime.embedded_source_map, m);
    RELEASE(m);
}
