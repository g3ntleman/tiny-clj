// clojure.core.c

#include "symbol.h" // Must be included before namespace.h for CljSymbol definition
#include "namespace.h"
#include "tiny_clj.h"
#include "map.h"             // For map_get
#include "embedded_sources.h"
#include "source_resolver.h" // For resolve_path_to_bytes (load_clojure_repl, override fallback)
#include "builtins.h"        // For load_namespace_from_bytes/load_namespace_from_buffer
#include <stdbool.h>
#include <string.h>

#include <signal.h>

// Crash diagnostics: last top-level form index being processed while loading clojure.core.
// Used by crash handlers and optional debug logging.
volatile sig_atomic_t g_clojure_core_last_form = 0;

static ID g_core_sym_inc = NULL;
static ID g_core_sym_math = NULL;
static const IdSymbolCacheEntry g_clojure_core_symbol_cache[] = {
    {&g_core_sym_inc, "inc"},
    {&g_core_sym_math, "Math"},
};

static inline bool clojure_core_symbols_ready(void) {
  return id_symbol_cache_init_global(
      g_clojure_core_symbol_cache,
      sizeof(g_clojure_core_symbol_cache) / sizeof(g_clojure_core_symbol_cache[0]));
}

// Keyword symbol definitions for fs_layer and others
extern CljSymbol *SYM_KW_SIZE;

// Optional override for core source (tests/custom); default from embedded_sources.c via resolve_path_to_bytes.
static const char *clojure_core_override = NULL;

int load_clojure_core(EvalState *st) {
  if (!st)
    return 0;
  if (!clojure_core_symbols_ready())
    return 0;
  CljNamespace *existing_core = ns_find("clojure.core");
  if (existing_core && existing_core->loaded && existing_core->mappings) {
    CljSymbol *inc_sym = g_core_sym_inc;
    if (!inc_sym)
      return 0;
    CljObject *inc_val =
        (CljObject *)map_get_sentinel((CljValue)existing_core->mappings, (CljValue)inc_sym, NULL);
    if (inc_val && (TAG(inc_val) == CLJ_FUNC || TAG(inc_val) == CLJ_CLOSURE)) {
      return 1;
    }
  }
  g_clojure_core_last_form = 0;
  bool loaded = false;
  const uint8_t *embedded_core = NULL;
  int embedded_core_len = 0;
  if (embedded_source_lookup("/libs/clojure/core.clj", &embedded_core, &embedded_core_len) &&
      embedded_core && embedded_core_len >= 0) {
    loaded = load_namespace_from_buffer(st,
                                        "clojure.core",
                                        (const char *)embedded_core,
                                        (size_t)embedded_core_len,
                                        "/libs/clojure/core.clj");
  } else {
    ID bytes = resolve_path_to_bytes("/libs/clojure/core.clj");
    if (bytes)
      loaded = load_namespace_from_bytes(st, "clojure.core", bytes, "/libs/clojure/core.clj");
  }
  if (!loaded && clojure_core_override) {
    loaded = load_namespace_from_buffer(st,
                                        "clojure.core",
                                        clojure_core_override,
                                        strlen(clojure_core_override),
                                        "clojure.core.clj");
  }
  if (!loaded)
    return 0;
  CljNamespace *core = ns_find("clojure.core");
  if (!core)
    return 0;
  CljSymbol *math_alias = g_core_sym_math;
  if (math_alias && SYM_CLOJURE_CORE)
    ns_set_alias(core, math_alias, SYM_CLOJURE_CORE);
  if (!core->mappings)
    return 0;
  CljSymbol *inc_sym = g_core_sym_inc;
  if (!inc_sym)
    return 0;
  CljObject *inc_val = (CljObject *)map_get_sentinel((CljValue)core->mappings, (CljValue)inc_sym, NULL);
  if (!inc_val || (TAG(inc_val) != CLJ_FUNC && TAG(inc_val) != CLJ_CLOSURE))
    return 0;
  // Startup commonly loads additional app namespaces immediately after core.
  // Keep some symbol-table headroom so the next require() wave does not trigger
  // an expensive hashset rehash under an already tight heap budget.
  symbol_table_fit_startup_reserve(50u);
  return 1;
}

void clojure_core_set_quiet(bool quiet) {
  (void)quiet;
}

void clojure_core_set_source(const char *src) { clojure_core_override = src; }

// Load clojure.repl namespace via unified require path/embedded resolution.
int load_clojure_repl(EvalState *st) {
  if (!st)
    return 0;
  return require_namespace_by_name(st, "clojure.repl") ? 1 : 0;
}
