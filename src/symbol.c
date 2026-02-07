#include <stddef.h>
struct CljSymbol *SYM_KW_META = NULL;
#include "symbol.h"
#include "object.h"
#include "runtime.h"
#include "value.h"
#include "exception.h"
#include "namespace.h"
#include "types.h"  // For SINGLETON_RC
#include "memory.h" // For ASSIGN
#include "vector.h"  // For vector operations
#include "hashset.h" // For HashSet symbol table (O(1) lookup)
#include "symbol_token.h"  // For CljSymbolToken
#include "common.h"  // For CLJ_ASSERT
#include "eval.h"  // For SpecialFormEvalFn type
#include "eval_special_forms.h"  // For eval_special_* functions
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>  // For snprintf
#include <assert.h>

// Note: Symbols have SINGLETON_RC and are never released

bool is_earmuffed_dynamic_symbol(const CljSymbol *sym) {
    return sym && ((sym->base.flags & CLJ_FLAG_DYNAMIC) != 0);
}

// Globale Symbol-Pointer Definitionen
CljSymbol *SYM_TRY = NULL;
CljSymbol *SYM_CATCH = NULL;
CljSymbol *SYM_IF = NULL;
CljSymbol *SYM_COND = NULL;
CljSymbol *SYM_WHEN = NULL;
CljSymbol *SYM_WHILE = NULL;
CljSymbol *SYM_LET = NULL;
CljSymbol *SYM_FN = NULL;
CljSymbol *SYM_DEF = NULL;
CljSymbol *SYM_DEFN = NULL;
CljSymbol *SYM_DEFMACRO = NULL;
CljSymbol *SYM_QUOTE = NULL;
CljSymbol *SYM_QUASIQUOTE = NULL;
CljSymbol *SYM_UNQUOTE = NULL;
CljSymbol *SYM_UNQUOTE_SPLICE = NULL;
CljSymbol *SYM_SOURCE = NULL;
static CljSymbol *SYM_SOURCE_NATIVE = NULL;
static CljSymbol *SYM_DIR_NATIVE = NULL;
static CljSymbol *SYM_SQRT_NATIVE = NULL;
CljSymbol *SYM_DO = NULL;
CljSymbol *SYM_LOOP = NULL;
CljSymbol *SYM_RECUR = NULL;
CljSymbol *SYM_DESTRUCTURE = NULL;
CljSymbol *SYM_THROW = NULL;
CljSymbol *SYM_FINALLY = NULL;
CljSymbol *SYM_VAR = NULL;
CljSymbol *SYM_NS = NULL;
CljSymbol *SYM_BINDING = NULL;
CljSymbol *SYM_TIME = NULL;
#ifdef DEBUG
CljSymbol *SYM_HEAP = NULL;
#endif
CljSymbol *SYM_GO = NULL;
CljSymbol *SYM_DEREF = NULL;
CljSymbol *SYM_NIL = NULL;
CljSymbol *SYM_AMP = NULL;  // & for variadic parameters

// Builtin-Funktionen
CljSymbol *SYM_PLUS = NULL;
CljSymbol *SYM_MINUS = NULL;
CljSymbol *SYM_MULTIPLY = NULL;
CljSymbol *SYM_DIVIDE = NULL;
CljSymbol *SYM_EQUALS = NULL;
CljSymbol *SYM_LT = NULL;
CljSymbol *SYM_GT = NULL;
CljSymbol *SYM_LE = NULL;
CljSymbol *SYM_GE = NULL;
CljSymbol *SYM_PRINTLN = NULL;
CljSymbol *SYM_PRINT = NULL;
CljSymbol *SYM_STR = NULL;
CljSymbol *SYM_CONJ = NULL;
CljSymbol *SYM_NTH = NULL;
CljSymbol *SYM_TRIM = NULL;
CljSymbol *SYM_UPPER_CASE = NULL;
CljSymbol *SYM_LOWER_CASE = NULL;
CljSymbol *SYM_PAD_LEFT = NULL;
CljSymbol *SYM_LAST_INDEX_OF = NULL;
CljSymbol *SYM_STRING_REVERSE = NULL;
CljSymbol *SYM_FIRST = NULL;
CljSymbol *SYM_REST = NULL;
CljSymbol *SYM_COUNT = NULL;
CljSymbol *SYM_ALL_NS = NULL;

// Additional symbols for optimization
CljSymbol *SYM_CONS = NULL;
CljSymbol *SYM_SEQ = NULL;
CljSymbol *SYM_NEXT = NULL;
CljSymbol *SYM_LIST = NULL;
CljSymbol *SYM_AND = NULL;
CljSymbol *SYM_OR = NULL;
CljSymbol *SYM_DOSEQ = NULL;
CljSymbol *SYM_DOTIMES = NULL;

// Keywords
CljSymbol *SYM_KW_LINE = NULL;
CljSymbol *SYM_KW_FILE = NULL;
CljSymbol *SYM_KW_DOC = NULL;
CljSymbol *SYM_KW_ERROR = NULL;
CljSymbol *SYM_KW_STACK = NULL;
CljSymbol *SYM_KW_NS = NULL;
CljSymbol *SYM_KW_NAME = NULL;
CljSymbol *SYM_KW_NATIVE = NULL;
CljSymbol *SYM_KW_AS = NULL;
CljSymbol *SYM_KW_REFER = NULL;
CljSymbol *SYM_KW_ELSE = NULL;
CljSymbol *SYM_KW_VALUE = NULL;
CljSymbol *SYM_KW_CLOSED = NULL;
CljSymbol *SYM_KW_DATA = NULL;
CljSymbol *SYM_KW_FROM = NULL;
CljSymbol *SYM_KW_TO = NULL;
CljSymbol *SYM_KW_PORT = NULL;
CljSymbol *SYM_KW_HOST = NULL;
CljSymbol *SYM_KW_COLUMN = NULL;
CljSymbol *SYM_KW_FN = NULL;
CljSymbol *SYM_KW_PATH = NULL;
CljSymbol *SYM_KW_CALLBACK_FN = NULL;
CljSymbol *SYM_KW_WATCHER_ID = NULL;
CljSymbol *SYM_KW_HOST_OS = NULL;
CljSymbol *SYM_KW_MACRO = NULL;
CljSymbol *SYM_KW_TYPE = NULL;
CljSymbol *SYM_KW_SIZE = NULL;
CljSymbol *SYM_KW_CHUNKS = NULL;
CljSymbol *SYM_KW_ENTRIES = NULL;
CljSymbol *SYM_KW_LAST_KEY = NULL;

// Datetime keywords
CljSymbol *SYM_KW_YEAR = NULL;
CljSymbol *SYM_KW_MONTH = NULL;
CljSymbol *SYM_KW_DAY = NULL;
CljSymbol *SYM_KW_HOUR = NULL;
CljSymbol *SYM_KW_MINUTE = NULL;
CljSymbol *SYM_KW_SECOND = NULL;

// Runtime stats keywords
// Runtime stats keywords (always available)
CljSymbol *SYM_KW_OS = NULL;
CljSymbol *SYM_KW_VERSION = NULL;

#ifdef DEBUG
// Runtime stats keywords (DEBUG only)
CljSymbol *SYM_KW_SYMBOLS = NULL;
CljSymbol *SYM_KW_NAMESPACES = NULL;
CljSymbol *SYM_KW_BYTES_CURRENT = NULL;
CljSymbol *SYM_KW_BYTES_PEAK = NULL;
CljSymbol *SYM_KW_MEMORY_STATS = NULL;

#if defined(MEMORY_PROFILING_ENABLED) && MEMORY_PROFILING_ENABLED
// Extended memory profiling keywords (DEBUG + MEMORY_PROFILING_ENABLED only)
CljSymbol *SYM_KW_RAW_BYTES_CURRENT = NULL;
CljSymbol *SYM_KW_RAW_BYTES_PEAK = NULL;
CljSymbol *SYM_KW_RAW_BLOCKS_CURRENT = NULL;
CljSymbol *SYM_KW_RAW_BLOCKS_PEAK = NULL;
CljSymbol *SYM_KW_BYTES_BY_TYPE = NULL;
CljSymbol *SYM_KW_TOTAL_ALLOCATIONS = NULL;
CljSymbol *SYM_KW_TOTAL_DEALLOCATIONS = NULL;
CljSymbol *SYM_KW_MEMORY_LEAKS = NULL;
CljSymbol *SYM_KW_ALLOC_COUNT = NULL;
CljSymbol *SYM_KW_DEALLOC_COUNT = NULL;
#endif // MEMORY_PROFILING_ENABLED
#endif // DEBUG

// Global symbols for namespace names (for fast comparison)
CljSymbol *SYM_CLOJURE_CORE = NULL;
CljSymbol *SYM_CLOJURE_STRING = NULL;
CljSymbol *SYM_CLOJURE_REPL = NULL;
CljSymbol *SYM_CLOJURE_LANG = NULL;
CljSymbol *SYM_TINYCLJ = NULL;

// tinyclj namespace function symbols
CljSymbol *SYM_RETAIN_COUNT = NULL;
CljSymbol *SYM_LIST_BATCH = NULL;

// Additional symbols for hot path optimization
CljSymbol *SYM_NS_STAR = NULL;

// Internal pre-interned symbols for lazy seq thunk state (hot path)
CljSymbol *SYM_CONCAT_X = NULL;
CljSymbol *SYM_CONCAT_Y = NULL;
CljSymbol *SYM_CONCAT_THUNK_FN = NULL;
CljSymbol *SYM_MAP_FN = NULL;
CljSymbol *SYM_MAP_SEQS = NULL;
CljSymbol *SYM_MAP_THUNK_FN = NULL;
CljSymbol *SYM_MAPCAT_FN = NULL;
CljSymbol *SYM_MAPCAT_COLL = NULL;
CljSymbol *SYM_MAPCAT_INNER = NULL;
CljSymbol *SYM_MAPCAT_THUNK_FN = NULL;
CljSymbol *SYM_RANGE_CUR = NULL;
CljSymbol *SYM_RANGE_INF_THUNK_FN = NULL;

// Macro to reduce boilerplate for static symbol declarations (DRY principle)
// Note: For symbols that need to be extern (used in other files), use DEFINE_EXTERN_SYMBOL instead
#define DEFINE_STATIC_SYMBOL(var_name, symbol_name) \
    static struct { CljSymbol sym; } var_name = { \
        .sym = { .base = { .type = CLJ_SYMBOL, .rc = SINGLETON_RC }, .ns_name = NULL, .cname = symbol_name } \
    }

// Macro for Special Symbols (with space for function pointer)
#define DEFINE_STATIC_SPECIAL_SYMBOL(var_name, symbol_name) \
    static struct { CljSpecialSymbol sym; } var_name = { \
        .sym = { .base = { .base = { .type = CLJ_SYMBOL, .rc = SINGLETON_RC }, .ns_name = NULL, .cname = symbol_name }, .eval_fn = NULL } \
    }

// Macro for extern Special Symbols (used in other files)
#define DEFINE_EXTERN_SPECIAL_SYMBOL(var_name, symbol_name) \
    SpecialSymbolData var_name = { \
        .sym = { .base = { .base = { .type = CLJ_SYMBOL, .rc = SINGLETON_RC }, .ns_name = NULL, .cname = symbol_name }, .eval_fn = NULL } \
    }

// Macro for non-static (extern) symbols that are statically initialized (compile-time, not dynamically allocated)
// These are native/builtin functions, so they get CLJ_FLAG_NATIVE for fast macro-skip in ast_canon
#define DEFINE_EXTERN_SYMBOL(var_name, symbol_name) \
    extern StaticSymbolData var_name; \
    StaticSymbolData var_name = { \
        .sym = { .base = { .type = CLJ_SYMBOL, .rc = SINGLETON_RC, .flags = CLJ_FLAG_NATIVE }, .ns_name = NULL, .cname = symbol_name } \
    }

// Consolidated special symbols array - provides stable, contiguous addresses
// for pointer-range-based detection without consuming a symbol flag bit
static struct {
    CljSpecialSymbol sym;
} g_special_symbols[] = {
    { .sym = { .base = { .base = { .type = CLJ_SYMBOL, .rc = SINGLETON_RC }, .ns_name = NULL, .cname = "try" }, .eval_fn = NULL } },
    { .sym = { .base = { .base = { .type = CLJ_SYMBOL, .rc = SINGLETON_RC }, .ns_name = NULL, .cname = "catch" }, .eval_fn = NULL } },
    { .sym = { .base = { .base = { .type = CLJ_SYMBOL, .rc = SINGLETON_RC }, .ns_name = NULL, .cname = "if" }, .eval_fn = NULL } },
    { .sym = { .base = { .base = { .type = CLJ_SYMBOL, .rc = SINGLETON_RC }, .ns_name = NULL, .cname = "cond" }, .eval_fn = NULL } },
    { .sym = { .base = { .base = { .type = CLJ_SYMBOL, .rc = SINGLETON_RC }, .ns_name = NULL, .cname = "when" }, .eval_fn = NULL } },
    { .sym = { .base = { .base = { .type = CLJ_SYMBOL, .rc = SINGLETON_RC }, .ns_name = NULL, .cname = "while" }, .eval_fn = NULL } },
    { .sym = { .base = { .base = { .type = CLJ_SYMBOL, .rc = SINGLETON_RC }, .ns_name = NULL, .cname = "let" }, .eval_fn = NULL } },
    { .sym = { .base = { .base = { .type = CLJ_SYMBOL, .rc = SINGLETON_RC }, .ns_name = NULL, .cname = "fn" }, .eval_fn = NULL } },
    { .sym = { .base = { .base = { .type = CLJ_SYMBOL, .rc = SINGLETON_RC }, .ns_name = NULL, .cname = "def" }, .eval_fn = NULL } },
    { .sym = { .base = { .base = { .type = CLJ_SYMBOL, .rc = SINGLETON_RC }, .ns_name = NULL, .cname = "defmacro" }, .eval_fn = NULL } },
    { .sym = { .base = { .base = { .type = CLJ_SYMBOL, .rc = SINGLETON_RC }, .ns_name = NULL, .cname = "quote" }, .eval_fn = NULL } },
    { .sym = { .base = { .base = { .type = CLJ_SYMBOL, .rc = SINGLETON_RC }, .ns_name = NULL, .cname = "quasiquote" }, .eval_fn = NULL } },
    { .sym = { .base = { .base = { .type = CLJ_SYMBOL, .rc = SINGLETON_RC }, .ns_name = NULL, .cname = "unquote" }, .eval_fn = NULL } },
    { .sym = { .base = { .base = { .type = CLJ_SYMBOL, .rc = SINGLETON_RC }, .ns_name = NULL, .cname = "unquote-splice" }, .eval_fn = NULL } },
    { .sym = { .base = { .base = { .type = CLJ_SYMBOL, .rc = SINGLETON_RC }, .ns_name = NULL, .cname = "loop" }, .eval_fn = NULL } },
    { .sym = { .base = { .base = { .type = CLJ_SYMBOL, .rc = SINGLETON_RC }, .ns_name = NULL, .cname = "recur" }, .eval_fn = NULL } },
    { .sym = { .base = { .base = { .type = CLJ_SYMBOL, .rc = SINGLETON_RC }, .ns_name = NULL, .cname = "throw" }, .eval_fn = NULL } },
    { .sym = { .base = { .base = { .type = CLJ_SYMBOL, .rc = SINGLETON_RC }, .ns_name = NULL, .cname = "finally" }, .eval_fn = NULL } },
    { .sym = { .base = { .base = { .type = CLJ_SYMBOL, .rc = SINGLETON_RC }, .ns_name = NULL, .cname = "var" }, .eval_fn = NULL } },
    { .sym = { .base = { .base = { .type = CLJ_SYMBOL, .rc = SINGLETON_RC }, .ns_name = NULL, .cname = "ns" }, .eval_fn = NULL } },
    { .sym = { .base = { .base = { .type = CLJ_SYMBOL, .rc = SINGLETON_RC }, .ns_name = NULL, .cname = "binding" }, .eval_fn = NULL } },
    { .sym = { .base = { .base = { .type = CLJ_SYMBOL, .rc = SINGLETON_RC }, .ns_name = NULL, .cname = "time" }, .eval_fn = NULL } },
    { .sym = { .base = { .base = { .type = CLJ_SYMBOL, .rc = SINGLETON_RC }, .ns_name = NULL, .cname = "heap" }, .eval_fn = NULL } },
    { .sym = { .base = { .base = { .type = CLJ_SYMBOL, .rc = SINGLETON_RC }, .ns_name = NULL, .cname = "go" }, .eval_fn = NULL } },
    { .sym = { .base = { .base = { .type = CLJ_SYMBOL, .rc = SINGLETON_RC }, .ns_name = NULL, .cname = "and" }, .eval_fn = NULL } },
    { .sym = { .base = { .base = { .type = CLJ_SYMBOL, .rc = SINGLETON_RC }, .ns_name = NULL, .cname = "or" }, .eval_fn = NULL } },
};

// Namespace name symbols: store as a small static array (no heap allocation).
// These are used frequently for qualified symbol resolution and native lookup.
static struct {
    CljSymbol sym;
} g_namespace_name_symbols[] = {
    { .sym = { .base = { .type = CLJ_SYMBOL, .rc = SINGLETON_RC }, .ns_name = NULL, .unqualified = NULL, .cname = "clojure.string" } },
    { .sym = { .base = { .type = CLJ_SYMBOL, .rc = SINGLETON_RC }, .ns_name = NULL, .unqualified = NULL, .cname = "clojure.repl" } },
    { .sym = { .base = { .type = CLJ_SYMBOL, .rc = SINGLETON_RC }, .ns_name = NULL, .unqualified = NULL, .cname = "clojure.core" } },
    { .sym = { .base = { .type = CLJ_SYMBOL, .rc = SINGLETON_RC }, .ns_name = NULL, .unqualified = NULL, .cname = "clojure.lang" } },
    { .sym = { .base = { .type = CLJ_SYMBOL, .rc = SINGLETON_RC }, .ns_name = NULL, .unqualified = NULL, .cname = "tinyclj" } },
};

#define NS_NAME_CLOJURE_STRING_IDX 0
#define NS_NAME_CLOJURE_REPL_IDX   1
#define NS_NAME_CLOJURE_CORE_IDX   2
#define NS_NAME_CLOJURE_LANG_IDX   3
#define NS_NAME_TINYCLJ_IDX        4

// Array indices for special symbols
#define SYM_TRY_IDX 0
#define SYM_CATCH_IDX 1
#define SYM_IF_IDX 2
#define SYM_COND_IDX 3
#define SYM_WHEN_IDX 4
#define SYM_WHILE_IDX 5
#define SYM_LET_IDX 6
#define SYM_FN_IDX 7
#define SYM_DEF_IDX 8
#define SYM_DEFMACRO_IDX 9
#define SYM_QUOTE_IDX 10
#define SYM_QUASIQUOTE_IDX 11
#define SYM_UNQUOTE_IDX 12
#define SYM_UNQUOTE_SPLICE_IDX 13
#define SYM_LOOP_IDX 14
#define SYM_RECUR_IDX 15
#define SYM_THROW_IDX 16
#define SYM_FINALLY_IDX 17
#define SYM_VAR_IDX 18
#define SYM_NS_IDX 19
#define SYM_BINDING_IDX 20
#define SYM_TIME_IDX 21
#define SYM_HEAP_IDX 22
#define SYM_GO_IDX 23
#define SYM_AND_IDX 24
#define SYM_OR_IDX 25

#define G_SPECIAL_SYMBOLS_COUNT (sizeof(g_special_symbols) / sizeof(g_special_symbols[0]))

// Range-based detection helper for special symbols in the consolidated array
// This replaces flag-based detection for array-based symbols
static inline bool is_in_special_symbols_array(const CljSymbol *symbol) {
    if (!symbol) return false;
    const CljSymbol *array_start = (const CljSymbol*)&g_special_symbols[0].sym;
    const CljSymbol *array_end = (const CljSymbol*)&g_special_symbols[G_SPECIAL_SYMBOLS_COUNT].sym;
    return (symbol >= array_start && symbol < array_end);
}

// Public API: Check if a symbol is a special form
// Uses address range check for array-based symbols + explicit check for sym_do (extern symbol)
// This replaces the old flag-based detection
bool is_special_symbol(CljSymbol *symbol) {
    if (!symbol) return false;
    // Check if symbol is in the consolidated array
    if (is_in_special_symbols_array(symbol)) return true;
    // Check for extern special symbol (do) which is outside the array
    // Note: SYM_DO may be NULL during initialization, so check pointer validity
    return (SYM_DO != NULL && symbol == SYM_DO);
}

// Non-special symbol definitions (these remain separate as they're not special forms)
DEFINE_STATIC_SYMBOL(sym_defn_data, "defn");  // macro, not special form
DEFINE_EXTERN_SYMBOL(sym_deref_data, "deref");
DEFINE_STATIC_SYMBOL(sym_nil_data, "nil");
DEFINE_STATIC_SYMBOL(sym_amp_data, "&");  // variadic parameter marker
// Extern symbol structs for native functions (compile-time initialization, statically allocated)
// These are extern so they can be used in builtins.c's native function table
DEFINE_STATIC_SYMBOL(sym_source_special_data, "source");
DEFINE_EXTERN_SYMBOL(sym_source_data, "source");
DEFINE_EXTERN_SYMBOL(sym_dir_data, "dir");
DEFINE_EXTERN_SYMBOL(sym_retain_count_data, "retain-count");
DEFINE_EXTERN_SYMBOL(sym_meta_data, "meta");
DEFINE_EXTERN_SYMBOL(sym_with_meta_data, "with-meta");
DEFINE_EXTERN_SPECIAL_SYMBOL(sym_do_data, "do");
DEFINE_EXTERN_SYMBOL(sym_reduce_data, "reduce");

// Extern symbol structs for native functions (compile-time initialization, statically allocated)
// These are extern so they can be used in builtins.c's native function table
DEFINE_EXTERN_SYMBOL(sym_plus_data, "+");
DEFINE_EXTERN_SYMBOL(sym_minus_data, "-");
DEFINE_EXTERN_SYMBOL(sym_multiply_data, "*");
DEFINE_EXTERN_SYMBOL(sym_divide_data, "/");
DEFINE_EXTERN_SYMBOL(sym_equals_data, "=");
DEFINE_EXTERN_SYMBOL(sym_lt_data, "<");
DEFINE_EXTERN_SYMBOL(sym_gt_data, ">");
DEFINE_EXTERN_SYMBOL(sym_le_data, "<=");
DEFINE_EXTERN_SYMBOL(sym_ge_data, ">=");
DEFINE_EXTERN_SYMBOL(sym_println_data, "println");
DEFINE_EXTERN_SYMBOL(sym_print_data, "print");
DEFINE_EXTERN_SYMBOL(sym_str_data, "str");
DEFINE_EXTERN_SYMBOL(sym_conj_data, "conj");
DEFINE_EXTERN_SYMBOL(sym_disj_data, "disj");
DEFINE_EXTERN_SYMBOL(sym_hash_set_data, "hash-set");
DEFINE_EXTERN_SYMBOL(sym_nth_data, "nth");
DEFINE_EXTERN_SYMBOL(sym_first_data, "first");
DEFINE_EXTERN_SYMBOL(sym_rest_data, "rest");
DEFINE_EXTERN_SYMBOL(sym_concat_data, "concat");
DEFINE_EXTERN_SYMBOL(sym_concat2_data, "concat2");
DEFINE_EXTERN_SYMBOL(sym_count_data, "count");
DEFINE_EXTERN_SYMBOL(sym_map_data, "map");
DEFINE_EXTERN_SYMBOL(sym_mapcat_data, "mapcat");
DEFINE_EXTERN_SYMBOL(sym_filter_data, "filter");
DEFINE_EXTERN_SYMBOL(sym_group_by_data, "group-by");
DEFINE_EXTERN_SYMBOL(sym_last_data, "last");
DEFINE_EXTERN_SYMBOL(sym_ns_unload_data, "ns-unload");
DEFINE_EXTERN_SYMBOL(sym_get_thread_bindings_data, "get-thread-bindings");
DEFINE_EXTERN_SYMBOL(sym_gpio_watch_data, "gpio-watch");
DEFINE_EXTERN_SYMBOL(sym_gpio_unwatch_data, "gpio-unwatch");
DEFINE_EXTERN_SYMBOL(sym_gpio_simulate_data, "gpio-simulate!");

// Extern symbol structs for native functions (compile-time initialization, statically allocated)
// These are extern so they can be used in builtins.c's native function table
DEFINE_EXTERN_SYMBOL(sym_cons_data, "cons");
DEFINE_EXTERN_SYMBOL(sym_seq_data, "seq");
DEFINE_EXTERN_SYMBOL(sym_next_data, "next");
DEFINE_EXTERN_SYMBOL(sym_nnext_data, "nnext");
DEFINE_EXTERN_SYMBOL(sym_nthnext_data, "nthnext");
DEFINE_EXTERN_SYMBOL(sym_destructure_data, "destructure");
DEFINE_EXTERN_SYMBOL(sym_gensym_data, "gensym");
DEFINE_EXTERN_SYMBOL(sym_partition_data, "partition");
DEFINE_EXTERN_SYMBOL(sym_some_data, "some");
DEFINE_EXTERN_SYMBOL(sym_list_data, "list");
// Note: sym_and_data and sym_or_data are now in g_special_symbols[] array
DEFINE_STATIC_SYMBOL(sym_doseq_data, "doseq");
DEFINE_STATIC_SYMBOL(sym_dotimes_data, "dotimes");

// Internal state keys for lazy thunk executors (avoid interning in hot path)
DEFINE_STATIC_SYMBOL(sym_concat_x_key_data, "__concat_x__");
DEFINE_STATIC_SYMBOL(sym_concat_y_key_data, "__concat_y__");
DEFINE_STATIC_SYMBOL(sym_concat_thunk_fn_key_data, "__concat_thunk_fn__");
DEFINE_STATIC_SYMBOL(sym_map_fn_key_data, "__map_fn__");
DEFINE_STATIC_SYMBOL(sym_map_seqs_key_data, "__map_seqs__");
DEFINE_STATIC_SYMBOL(sym_map_thunk_fn_key_data, "__map_thunk_fn__");
DEFINE_STATIC_SYMBOL(sym_mapcat_fn_key_data, "__mapcat_fn__");
DEFINE_STATIC_SYMBOL(sym_mapcat_coll_key_data, "__mapcat_coll__");
DEFINE_STATIC_SYMBOL(sym_mapcat_inner_key_data, "__mapcat_inner__");
DEFINE_STATIC_SYMBOL(sym_mapcat_thunk_fn_key_data, "__mapcat_thunk_fn__");
DEFINE_STATIC_SYMBOL(sym_range_cur_key_data, "__range_cur__");
DEFINE_STATIC_SYMBOL(sym_range_inf_thunk_fn_key_data, "__range_inf_thunk_fn__");

// Extern symbol structs for clojure.string native functions (compile-time initialization, statically allocated)
// These are extern so they can be used in builtins.c's native function table
DEFINE_EXTERN_SYMBOL(sym_trim_data, "trim");
DEFINE_EXTERN_SYMBOL(sym_upper_case_data, "upper-case");
DEFINE_EXTERN_SYMBOL(sym_lower_case_data, "lower-case");
DEFINE_EXTERN_SYMBOL(sym_pad_left_data, "pad-left");
DEFINE_EXTERN_SYMBOL(sym_last_index_of_data, "last-index-of");
DEFINE_EXTERN_SYMBOL(sym_string_reverse_data, "reverse");

// Extern symbol structs for clojure.core native functions (compile-time initialization, statically allocated)
// These are extern so they can be used in builtins.c's native function table
#ifdef DEBUG
DEFINE_EXTERN_SYMBOL(sym_ast_string_data, "ast-string");
#endif
DEFINE_EXTERN_SYMBOL(sym_mod_data, "mod");
DEFINE_EXTERN_SYMBOL(sym_quot_data, "quot");
DEFINE_EXTERN_SYMBOL(sym_bit_shift_left_data, "bit-shift-left");
DEFINE_EXTERN_SYMBOL(sym_range_data, "range");
DEFINE_EXTERN_SYMBOL(sym_repeat_data, "repeat");
DEFINE_EXTERN_SYMBOL(sym_lazy_seq_star_data, "lazy-seq*");
DEFINE_EXTERN_SYMBOL(sym_math_sqrt_data, "Math/sqrt");
DEFINE_EXTERN_SYMBOL(sym_sqrt_data, "sqrt");
DEFINE_EXTERN_SYMBOL(sym_format_data, "format");
DEFINE_EXTERN_SYMBOL(sym_subs_data, "subs");
DEFINE_EXTERN_SYMBOL(sym_symbol_data, "symbol");
DEFINE_EXTERN_SYMBOL(sym_type_data, "type");
DEFINE_EXTERN_SYMBOL(sym_array_map_data, "array-map");
DEFINE_EXTERN_SYMBOL(sym_vector_data, "vector");
DEFINE_EXTERN_SYMBOL(sym_vec_data, "vec");
DEFINE_EXTERN_SYMBOL(sym_peek_data, "peek");
DEFINE_EXTERN_SYMBOL(sym_pop_data, "pop");
DEFINE_EXTERN_SYMBOL(sym_subvec_data, "subvec");
DEFINE_EXTERN_SYMBOL(sym_reverse_data, "reverse");
DEFINE_EXTERN_SYMBOL(sym_assoc_data, "assoc");
DEFINE_EXTERN_SYMBOL(sym_dissoc_data, "dissoc");
DEFINE_EXTERN_SYMBOL(sym_merge_data, "merge");
DEFINE_EXTERN_SYMBOL(sym_contains_p_data, "contains?");
DEFINE_EXTERN_SYMBOL(sym_update_data, "update");
DEFINE_EXTERN_SYMBOL(sym_into_data, "into");
DEFINE_EXTERN_SYMBOL(sym_select_keys_data, "select-keys");
DEFINE_EXTERN_SYMBOL(sym_find_data, "find");
DEFINE_EXTERN_SYMBOL(sym_transient_data, "transient");
DEFINE_EXTERN_SYMBOL(sym_persistent_bang_data, "persistent!");
DEFINE_EXTERN_SYMBOL(sym_conj_bang_data, "conj!");
DEFINE_EXTERN_SYMBOL(sym_get_data, "get");
DEFINE_EXTERN_SYMBOL(sym_keys_data, "keys");
DEFINE_EXTERN_SYMBOL(sym_vals_data, "vals");
DEFINE_EXTERN_SYMBOL(sym_nilp_data, "nil?");
DEFINE_EXTERN_SYMBOL(sym_not_data, "not");
DEFINE_EXTERN_SYMBOL(sym_not_eq_data, "not=");
DEFINE_EXTERN_SYMBOL(sym_identical_data, "identical?");
DEFINE_EXTERN_SYMBOL(sym_vector_p_data, "vector?");
DEFINE_EXTERN_SYMBOL(sym_map_p_data, "map?");
DEFINE_EXTERN_SYMBOL(sym_number_p_data, "number?");
DEFINE_EXTERN_SYMBOL(sym_integer_p_data, "integer?");
DEFINE_EXTERN_SYMBOL(sym_float_p_data, "float?");
DEFINE_EXTERN_SYMBOL(sym_string_p_data, "string?");
DEFINE_EXTERN_SYMBOL(sym_keyword_p_data, "keyword?");
DEFINE_EXTERN_SYMBOL(sym_keyword_data, "keyword");
DEFINE_EXTERN_SYMBOL(sym_name_data, "name");
DEFINE_EXTERN_SYMBOL(sym_symbol_p_data, "symbol?");
DEFINE_EXTERN_SYMBOL(sym_fn_p_data, "fn?");
DEFINE_EXTERN_SYMBOL(sym_atom_p_data, "atom?");
DEFINE_EXTERN_SYMBOL(sym_char_p_data, "char?");
DEFINE_EXTERN_SYMBOL(sym_list_p_data, "list?");
DEFINE_EXTERN_SYMBOL(sym_set_p_data, "set?");
DEFINE_EXTERN_SYMBOL(sym_yield_data, "yield");
DEFINE_EXTERN_SYMBOL(sym_current_time_ms_data, "current-time-ms");
DEFINE_EXTERN_SYMBOL(sym_ns_map_data, "ns-map");
DEFINE_EXTERN_SYMBOL(sym_find_ns_data, "find-ns");
DEFINE_EXTERN_SYMBOL(sym_all_ns_data, "all-ns");
DEFINE_EXTERN_SYMBOL(sym_pr_data, "pr");
DEFINE_EXTERN_SYMBOL(sym_prn_data, "prn");
DEFINE_EXTERN_SYMBOL(sym_byte_array_data, "byte-array");
DEFINE_EXTERN_SYMBOL(sym_aget_data, "aget");
DEFINE_EXTERN_SYMBOL(sym_aset_data, "aset");
DEFINE_EXTERN_SYMBOL(sym_alength_data, "alength");
DEFINE_EXTERN_SYMBOL(sym_aclone_data, "aclone");
DEFINE_EXTERN_SYMBOL(sym_run_next_task_data, "run-next-task");
DEFINE_EXTERN_SYMBOL(sym_schedule_data, "schedule");
DEFINE_EXTERN_SYMBOL(sym_schedule_periodic_data, "schedule-periodic");
DEFINE_EXTERN_SYMBOL(sym_cancel_timer_data, "cancel-timer");
DEFINE_EXTERN_SYMBOL(sym_atom_data, "atom");
DEFINE_EXTERN_SYMBOL(sym_reset_bang_data, "reset!");
DEFINE_EXTERN_SYMBOL(sym_swap_bang_data, "swap!");
DEFINE_EXTERN_SYMBOL(sym_list_batch_data, "list-batch");
#ifndef ESP32_BUILD
DEFINE_EXTERN_SYMBOL(sym_slurp_data, "slurp");
DEFINE_EXTERN_SYMBOL(sym_spit_data, "spit");
#endif

// Static symbol structs for keywords (compile-time initialization)
DEFINE_STATIC_SYMBOL(sym_kw_line_data, ":line");
DEFINE_STATIC_SYMBOL(sym_kw_file_data, ":file");
DEFINE_STATIC_SYMBOL(sym_kw_doc_data, ":doc");
DEFINE_STATIC_SYMBOL(sym_kw_error_data, ":error");
DEFINE_STATIC_SYMBOL(sym_kw_stack_data, ":stack");
DEFINE_STATIC_SYMBOL(sym_kw_ns_data, ":ns");
DEFINE_STATIC_SYMBOL(sym_kw_name_data, ":name");
DEFINE_STATIC_SYMBOL(sym_kw_native_data, ":native");
DEFINE_STATIC_SYMBOL(sym_kw_as_data, ":as");
DEFINE_STATIC_SYMBOL(sym_kw_refer_data, ":refer");
DEFINE_STATIC_SYMBOL(sym_kw_else_data, ":else");
DEFINE_STATIC_SYMBOL(sym_kw_value_data, ":value");
DEFINE_STATIC_SYMBOL(sym_kw_closed_data, ":closed");
DEFINE_STATIC_SYMBOL(sym_kw_data_data, ":data");
DEFINE_STATIC_SYMBOL(sym_kw_from_data, ":from");
DEFINE_STATIC_SYMBOL(sym_kw_to_data, ":to");
DEFINE_STATIC_SYMBOL(sym_kw_port_data, ":port");
DEFINE_STATIC_SYMBOL(sym_kw_host_data, ":host");
DEFINE_STATIC_SYMBOL(sym_kw_column_data, ":column");
DEFINE_STATIC_SYMBOL(sym_kw_fn_data, ":fn");
DEFINE_STATIC_SYMBOL(sym_kw_path_data, ":path");
/* GPIO watcher map keys (:callback-fn, :watcher-id) */
DEFINE_STATIC_SYMBOL(sym_kw_callback_fn_data, ":callback-fn");
DEFINE_STATIC_SYMBOL(sym_kw_watcher_id_data, ":watcher-id");
DEFINE_STATIC_SYMBOL(sym_kw_meta_data, ":meta");
DEFINE_STATIC_SYMBOL(sym_kw_host_os_data, ":host-os");
DEFINE_STATIC_SYMBOL(sym_kw_macro_data, ":macro");
DEFINE_STATIC_SYMBOL(sym_kw_type_data, ":type");
DEFINE_STATIC_SYMBOL(sym_kw_size_data, ":size");
DEFINE_STATIC_SYMBOL(sym_kw_chunks_data, ":chunks");
DEFINE_STATIC_SYMBOL(sym_kw_entries_data, ":entries");
DEFINE_STATIC_SYMBOL(sym_kw_last_key_data, ":last-key");

DEFINE_STATIC_SYMBOL(sym_kw_year_data, ":year");
DEFINE_STATIC_SYMBOL(sym_kw_month_data, ":month");
DEFINE_STATIC_SYMBOL(sym_kw_day_data, ":day");
DEFINE_STATIC_SYMBOL(sym_kw_hour_data, ":hour");
DEFINE_STATIC_SYMBOL(sym_kw_minute_data, ":minute");
DEFINE_STATIC_SYMBOL(sym_kw_second_data, ":second");

// Runtime stats keywords
// Runtime stats keywords (always available)
DEFINE_STATIC_SYMBOL(sym_kw_os_data, ":os");
DEFINE_STATIC_SYMBOL(sym_kw_version_data, ":version");

#ifdef DEBUG
// Runtime stats keywords (DEBUG only)
DEFINE_STATIC_SYMBOL(sym_kw_symbols_data, ":symbols");
DEFINE_STATIC_SYMBOL(sym_kw_namespaces_data, ":namespaces");
DEFINE_STATIC_SYMBOL(sym_kw_bytes_current_data, ":bytes-current");
DEFINE_STATIC_SYMBOL(sym_kw_bytes_peak_data, ":bytes-peak");
DEFINE_STATIC_SYMBOL(sym_kw_memory_stats_data, ":memory-stats");

#if defined(MEMORY_PROFILING_ENABLED) && MEMORY_PROFILING_ENABLED
// Extended memory profiling keywords (DEBUG + MEMORY_PROFILING_ENABLED only)
DEFINE_STATIC_SYMBOL(sym_kw_raw_bytes_current_data, ":raw-bytes-current");
DEFINE_STATIC_SYMBOL(sym_kw_raw_bytes_peak_data, ":raw-bytes-peak");
DEFINE_STATIC_SYMBOL(sym_kw_raw_blocks_current_data, ":raw-blocks-current");
DEFINE_STATIC_SYMBOL(sym_kw_raw_blocks_peak_data, ":raw-blocks-peak");
DEFINE_STATIC_SYMBOL(sym_kw_bytes_by_type_data, ":bytes-by-type");
DEFINE_STATIC_SYMBOL(sym_kw_total_allocations_data, ":total-allocations");
DEFINE_STATIC_SYMBOL(sym_kw_total_deallocations_data, ":total-deallocations");
DEFINE_STATIC_SYMBOL(sym_kw_memory_leaks_data, ":memory-leaks");
DEFINE_STATIC_SYMBOL(sym_kw_alloc_count_data, ":alloc-count");
DEFINE_STATIC_SYMBOL(sym_kw_dealloc_count_data, ":dealloc-count");
#endif // MEMORY_PROFILING_ENABLED
#endif // DEBUG

// Additional symbols for optimization (used in hot path)
DEFINE_STATIC_SYMBOL(sym_ns_star_data, "*ns*");

// All other clojure.core symbols (def/defn/defmacro names) - static to avoid heap on core load
static struct { CljSymbol sym; } g_core_symbols[] = {
    { .sym = { .base = { .type = CLJ_SYMBOL, .rc = SINGLETON_RC }, .ns_name = NULL, .unqualified = NULL, .cname = "inc" } },
    { .sym = { .base = { .type = CLJ_SYMBOL, .rc = SINGLETON_RC }, .ns_name = NULL, .unqualified = NULL, .cname = "dec" } },
    { .sym = { .base = { .type = CLJ_SYMBOL, .rc = SINGLETON_RC }, .ns_name = NULL, .unqualified = NULL, .cname = "second" } },
    { .sym = { .base = { .type = CLJ_SYMBOL, .rc = SINGLETON_RC }, .ns_name = NULL, .unqualified = NULL, .cname = "empty?" } },
    { .sym = { .base = { .type = CLJ_SYMBOL, .rc = SINGLETON_RC }, .ns_name = NULL, .unqualified = NULL, .cname = "identity" } },
    { .sym = { .base = { .type = CLJ_SYMBOL, .rc = SINGLETON_RC }, .ns_name = NULL, .unqualified = NULL, .cname = "constantly" } },
    { .sym = { .base = { .type = CLJ_SYMBOL, .rc = SINGLETON_RC }, .ns_name = NULL, .unqualified = NULL, .cname = "take" } },
    { .sym = { .base = { .type = CLJ_SYMBOL, .rc = SINGLETON_RC }, .ns_name = NULL, .unqualified = NULL, .cname = "drop" } },
    { .sym = { .base = { .type = CLJ_SYMBOL, .rc = SINGLETON_RC }, .ns_name = NULL, .unqualified = NULL, .cname = "zero?" } },
    { .sym = { .base = { .type = CLJ_SYMBOL, .rc = SINGLETON_RC }, .ns_name = NULL, .unqualified = NULL, .cname = "pos?" } },
    { .sym = { .base = { .type = CLJ_SYMBOL, .rc = SINGLETON_RC }, .ns_name = NULL, .unqualified = NULL, .cname = "neg?" } },
    { .sym = { .base = { .type = CLJ_SYMBOL, .rc = SINGLETON_RC }, .ns_name = NULL, .unqualified = NULL, .cname = "even?" } },
    { .sym = { .base = { .type = CLJ_SYMBOL, .rc = SINGLETON_RC }, .ns_name = NULL, .unqualified = NULL, .cname = "odd?" } },
    { .sym = { .base = { .type = CLJ_SYMBOL, .rc = SINGLETON_RC }, .ns_name = NULL, .unqualified = NULL, .cname = "max" } },
    { .sym = { .base = { .type = CLJ_SYMBOL, .rc = SINGLETON_RC }, .ns_name = NULL, .unqualified = NULL, .cname = "min" } },
    { .sym = { .base = { .type = CLJ_SYMBOL, .rc = SINGLETON_RC }, .ns_name = NULL, .unqualified = NULL, .cname = "butlast" } },
    { .sym = { .base = { .type = CLJ_SYMBOL, .rc = SINGLETON_RC }, .ns_name = NULL, .unqualified = NULL, .cname = "interleave" } },
    { .sym = { .base = { .type = CLJ_SYMBOL, .rc = SINGLETON_RC }, .ns_name = NULL, .unqualified = NULL, .cname = "interleave-repeat" } },
    { .sym = { .base = { .type = CLJ_SYMBOL, .rc = SINGLETON_RC }, .ns_name = NULL, .unqualified = NULL, .cname = "lazy-seq" } },
    { .sym = { .base = { .type = CLJ_SYMBOL, .rc = SINGLETON_RC }, .ns_name = NULL, .unqualified = NULL, .cname = "defn-" } },
    { .sym = { .base = { .type = CLJ_SYMBOL, .rc = SINGLETON_RC }, .ns_name = NULL, .unqualified = NULL, .cname = "mapv" } },
    { .sym = { .base = { .type = CLJ_SYMBOL, .rc = SINGLETON_RC }, .ns_name = NULL, .unqualified = NULL, .cname = "->" } },
    { .sym = { .base = { .type = CLJ_SYMBOL, .rc = SINGLETON_RC }, .ns_name = NULL, .unqualified = NULL, .cname = "->>" } },
    { .sym = { .base = { .type = CLJ_SYMBOL, .rc = SINGLETON_RC }, .ns_name = NULL, .unqualified = NULL, .cname = "as->" } },
    { .sym = { .base = { .type = CLJ_SYMBOL, .rc = SINGLETON_RC }, .ns_name = NULL, .unqualified = NULL, .cname = "some->" } },
    { .sym = { .base = { .type = CLJ_SYMBOL, .rc = SINGLETON_RC }, .ns_name = NULL, .unqualified = NULL, .cname = "some->>" } },
    { .sym = { .base = { .type = CLJ_SYMBOL, .rc = SINGLETON_RC }, .ns_name = NULL, .unqualified = NULL, .cname = "cond->" } },
    { .sym = { .base = { .type = CLJ_SYMBOL, .rc = SINGLETON_RC }, .ns_name = NULL, .unqualified = NULL, .cname = "cond->>" } },
    { .sym = { .base = { .type = CLJ_SYMBOL, .rc = SINGLETON_RC }, .ns_name = NULL, .unqualified = NULL, .cname = "every?" } },
    { .sym = { .base = { .type = CLJ_SYMBOL, .rc = SINGLETON_RC }, .ns_name = NULL, .unqualified = NULL, .cname = "not-every?" } },
    { .sym = { .base = { .type = CLJ_SYMBOL, .rc = SINGLETON_RC }, .ns_name = NULL, .unqualified = NULL, .cname = "not-any?" } },
    { .sym = { .base = { .type = CLJ_SYMBOL, .rc = SINGLETON_RC }, .ns_name = NULL, .unqualified = NULL, .cname = "take-while" } },
    { .sym = { .base = { .type = CLJ_SYMBOL, .rc = SINGLETON_RC }, .ns_name = NULL, .unqualified = NULL, .cname = "drop-while" } },
    { .sym = { .base = { .type = CLJ_SYMBOL, .rc = SINGLETON_RC }, .ns_name = NULL, .unqualified = NULL, .cname = "keep" } },
    { .sym = { .base = { .type = CLJ_SYMBOL, .rc = SINGLETON_RC }, .ns_name = NULL, .unqualified = NULL, .cname = "reductions" } },
    { .sym = { .base = { .type = CLJ_SYMBOL, .rc = SINGLETON_RC }, .ns_name = NULL, .unqualified = NULL, .cname = "frequencies" } },
    { .sym = { .base = { .type = CLJ_SYMBOL, .rc = SINGLETON_RC }, .ns_name = NULL, .unqualified = NULL, .cname = "distinct" } },
    { .sym = { .base = { .type = CLJ_SYMBOL, .rc = SINGLETON_RC }, .ns_name = NULL, .unqualified = NULL, .cname = "partition-all" } },
    { .sym = { .base = { .type = CLJ_SYMBOL, .rc = SINGLETON_RC }, .ns_name = NULL, .unqualified = NULL, .cname = "split-at" } },
    { .sym = { .base = { .type = CLJ_SYMBOL, .rc = SINGLETON_RC }, .ns_name = NULL, .unqualified = NULL, .cname = "split-with" } },
    { .sym = { .base = { .type = CLJ_SYMBOL, .rc = SINGLETON_RC }, .ns_name = NULL, .unqualified = NULL, .cname = "zipmap" } },
    { .sym = { .base = { .type = CLJ_SYMBOL, .rc = SINGLETON_RC }, .ns_name = NULL, .unqualified = NULL, .cname = "get-in" } },
    { .sym = { .base = { .type = CLJ_SYMBOL, .rc = SINGLETON_RC }, .ns_name = NULL, .unqualified = NULL, .cname = "partial" } },
    { .sym = { .base = { .type = CLJ_SYMBOL, .rc = SINGLETON_RC }, .ns_name = NULL, .unqualified = NULL, .cname = "comp" } },
    { .sym = { .base = { .type = CLJ_SYMBOL, .rc = SINGLETON_RC }, .ns_name = NULL, .unqualified = NULL, .cname = "juxt" } },
    { .sym = { .base = { .type = CLJ_SYMBOL, .rc = SINGLETON_RC }, .ns_name = NULL, .unqualified = NULL, .cname = "complement" } },
    { .sym = { .base = { .type = CLJ_SYMBOL, .rc = SINGLETON_RC }, .ns_name = NULL, .unqualified = NULL, .cname = "repeatedly" } },
    { .sym = { .base = { .type = CLJ_SYMBOL, .rc = SINGLETON_RC }, .ns_name = NULL, .unqualified = NULL, .cname = "reduce-kv" } },
    { .sym = { .base = { .type = CLJ_SYMBOL, .rc = SINGLETON_RC }, .ns_name = NULL, .unqualified = NULL, .cname = "abs" } },
    { .sym = { .base = { .type = CLJ_SYMBOL, .rc = SINGLETON_RC }, .ns_name = NULL, .unqualified = NULL, .cname = "rem" } },
    { .sym = { .base = { .type = CLJ_SYMBOL, .rc = SINGLETON_RC }, .ns_name = NULL, .unqualified = NULL, .cname = "normalize-for-bindings-helper" } },
    { .sym = { .base = { .type = CLJ_SYMBOL, .rc = SINGLETON_RC }, .ns_name = NULL, .unqualified = NULL, .cname = "normalize-for-bindings" } },
    { .sym = { .base = { .type = CLJ_SYMBOL, .rc = SINGLETON_RC }, .ns_name = NULL, .unqualified = NULL, .cname = "for-build" } },
    { .sym = { .base = { .type = CLJ_SYMBOL, .rc = SINGLETON_RC }, .ns_name = NULL, .unqualified = NULL, .cname = "for" } },
    { .sym = { .base = { .type = CLJ_SYMBOL, .rc = SINGLETON_RC }, .ns_name = NULL, .unqualified = NULL, .cname = "macroexpand-1" } },
    { .sym = { .base = { .type = CLJ_SYMBOL, .rc = SINGLETON_RC }, .ns_name = NULL, .unqualified = NULL, .cname = "macroexpand" } },
    { .sym = { .base = { .type = CLJ_SYMBOL, .rc = SINGLETON_RC }, .ns_name = NULL, .unqualified = NULL, .cname = "quasiquote-fn" } },
    { .sym = { .base = { .type = CLJ_SYMBOL, .rc = SINGLETON_RC }, .ns_name = NULL, .unqualified = NULL, .cname = "watcher-registry" } },
    { .sym = { .base = { .type = CLJ_SYMBOL, .rc = SINGLETON_RC }, .ns_name = NULL, .unqualified = NULL, .cname = "get-watcher-map" } },
    { .sym = { .base = { .type = CLJ_SYMBOL, .rc = SINGLETON_RC }, .ns_name = NULL, .unqualified = NULL, .cname = "update-watcher-map" } },
    { .sym = { .base = { .type = CLJ_SYMBOL, .rc = SINGLETON_RC }, .ns_name = NULL, .unqualified = NULL, .cname = "add-watch" } },
    { .sym = { .base = { .type = CLJ_SYMBOL, .rc = SINGLETON_RC }, .ns_name = NULL, .unqualified = NULL, .cname = "remove-watch" } },
    { .sym = { .base = { .type = CLJ_SYMBOL, .rc = SINGLETON_RC }, .ns_name = NULL, .unqualified = NULL, .cname = "notify-watchers" } },
    { .sym = { .base = { .type = CLJ_SYMBOL, .rc = SINGLETON_RC }, .ns_name = NULL, .unqualified = NULL, .cname = "sleep" } },
    { .sym = { .base = { .type = CLJ_SYMBOL, .rc = SINGLETON_RC }, .ns_name = NULL, .unqualified = NULL, .cname = "Math" } },
    { .sym = { .base = { .type = CLJ_SYMBOL, .rc = SINGLETON_RC }, .ns_name = NULL, .unqualified = NULL, .cname = "coll?" } },
    { .sym = { .base = { .type = CLJ_SYMBOL, .rc = SINGLETON_RC }, .ns_name = NULL, .unqualified = NULL, .cname = "seq?" } },
    { .sym = { .base = { .type = CLJ_SYMBOL, .rc = SINGLETON_RC }, .ns_name = NULL, .unqualified = NULL, .cname = "seqable?" } },
    { .sym = { .base = { .type = CLJ_SYMBOL, .rc = SINGLETON_RC }, .ns_name = NULL, .unqualified = NULL, .cname = "ifn?" } },
    { .sym = { .base = { .type = CLJ_SYMBOL, .rc = SINGLETON_RC }, .ns_name = NULL, .unqualified = NULL, .cname = "some?" } },
    { .sym = { .base = { .type = CLJ_SYMBOL, .rc = SINGLETON_RC }, .ns_name = NULL, .unqualified = NULL, .cname = "true?" } },
    { .sym = { .base = { .type = CLJ_SYMBOL, .rc = SINGLETON_RC }, .ns_name = NULL, .unqualified = NULL, .cname = "false?" } },
    { .sym = { .base = { .type = CLJ_SYMBOL, .rc = SINGLETON_RC }, .ns_name = NULL, .unqualified = NULL, .cname = "boolean?" } },
    { .sym = { .base = { .type = CLJ_SYMBOL, .rc = SINGLETON_RC }, .ns_name = NULL, .unqualified = NULL, .cname = "set?" } },
};

// Undef macro to avoid namespace pollution
#undef DEFINE_STATIC_SYMBOL

// Macro to reduce initialization boilerplate (DRY principle)
#define INIT_SYMBOL(sym_var, data_var) \
    do { \
        sym_var = &data_var.sym; \
        symbol_table_add(sym_var); \
    } while(0)

// Macro for symbols with namespace (sets ns_name at runtime)
#define INIT_SYMBOL_NS(sym_var, data_var, ns_sym) \
    do { \
        sym_var = &data_var.sym; \
        sym_var->ns_name = ns_sym; \
        symbol_table_add(sym_var); \
    } while(0)

// Macro for Special Symbols (CljSpecialSymbol)
#define INIT_SPECIAL_SYMBOL(sym_var, data_var) \
    do { \
        sym_var = (CljSymbol*)&data_var.sym; \
        symbol_table_add(sym_var); \
    } while(0)

// Initialisierung der globalen Symbole
void init_special_symbols() {
    // Initialize consolidated special symbols array - iterate and register all special forms
    // This replaces individual INIT_SPECIAL_SYMBOL calls for array-based symbols
    SYM_TRY = (CljSymbol*)&g_special_symbols[SYM_TRY_IDX].sym;
    SYM_CATCH = (CljSymbol*)&g_special_symbols[SYM_CATCH_IDX].sym;
    SYM_IF = (CljSymbol*)&g_special_symbols[SYM_IF_IDX].sym;
    SYM_COND = (CljSymbol*)&g_special_symbols[SYM_COND_IDX].sym;
    SYM_WHEN = (CljSymbol*)&g_special_symbols[SYM_WHEN_IDX].sym;
    SYM_WHILE = (CljSymbol*)&g_special_symbols[SYM_WHILE_IDX].sym;
    SYM_LET = (CljSymbol*)&g_special_symbols[SYM_LET_IDX].sym;
    SYM_FN = (CljSymbol*)&g_special_symbols[SYM_FN_IDX].sym;
    SYM_DEF = (CljSymbol*)&g_special_symbols[SYM_DEF_IDX].sym;
    SYM_DEFMACRO = (CljSymbol*)&g_special_symbols[SYM_DEFMACRO_IDX].sym;
    SYM_QUOTE = (CljSymbol*)&g_special_symbols[SYM_QUOTE_IDX].sym;
    SYM_QUASIQUOTE = (CljSymbol*)&g_special_symbols[SYM_QUASIQUOTE_IDX].sym;
    SYM_UNQUOTE = (CljSymbol*)&g_special_symbols[SYM_UNQUOTE_IDX].sym;
    SYM_UNQUOTE_SPLICE = (CljSymbol*)&g_special_symbols[SYM_UNQUOTE_SPLICE_IDX].sym;
    SYM_LOOP = (CljSymbol*)&g_special_symbols[SYM_LOOP_IDX].sym;
    SYM_RECUR = (CljSymbol*)&g_special_symbols[SYM_RECUR_IDX].sym;
    SYM_THROW = (CljSymbol*)&g_special_symbols[SYM_THROW_IDX].sym;
    SYM_FINALLY = (CljSymbol*)&g_special_symbols[SYM_FINALLY_IDX].sym;
    SYM_VAR = (CljSymbol*)&g_special_symbols[SYM_VAR_IDX].sym;
    SYM_NS = (CljSymbol*)&g_special_symbols[SYM_NS_IDX].sym;
    SYM_BINDING = (CljSymbol*)&g_special_symbols[SYM_BINDING_IDX].sym;
    SYM_TIME = (CljSymbol*)&g_special_symbols[SYM_TIME_IDX].sym;
#ifdef DEBUG
    SYM_HEAP = (CljSymbol*)&g_special_symbols[SYM_HEAP_IDX].sym;
#endif
    SYM_GO = (CljSymbol*)&g_special_symbols[SYM_GO_IDX].sym;
    SYM_AND = (CljSymbol*)&g_special_symbols[SYM_AND_IDX].sym;
    SYM_OR = (CljSymbol*)&g_special_symbols[SYM_OR_IDX].sym;
    
    // Register all special symbols in symbol table
    for (size_t i = 0; i < G_SPECIAL_SYMBOLS_COUNT; i++) {
        symbol_table_add((CljSymbol*)&g_special_symbols[i].sym);
    }
    
    // Special forms - remaining symbols not in array (extern symbol for builtins.c)
    // SYM_DO is separate because builtins.c needs its stable address via sym_do_data
    INIT_SPECIAL_SYMBOL(SYM_DO, sym_do_data);
    
    // Non-special symbols
    INIT_SYMBOL(SYM_DEFN, sym_defn_data);
    INIT_SYMBOL(SYM_DEREF, sym_deref_data);
    INIT_SYMBOL(SYM_NIL, sym_nil_data);
    INIT_SYMBOL(SYM_AMP, sym_amp_data);

    // Namespace name symbols (static, no heap allocation; must be in symbol table)
    SYM_CLOJURE_STRING = &g_namespace_name_symbols[NS_NAME_CLOJURE_STRING_IDX].sym;
    SYM_CLOJURE_REPL = &g_namespace_name_symbols[NS_NAME_CLOJURE_REPL_IDX].sym;
    SYM_CLOJURE_CORE = &g_namespace_name_symbols[NS_NAME_CLOJURE_CORE_IDX].sym;
    SYM_CLOJURE_LANG = &g_namespace_name_symbols[NS_NAME_CLOJURE_LANG_IDX].sym;
    SYM_TINYCLJ = &g_namespace_name_symbols[NS_NAME_TINYCLJ_IDX].sym;
    symbol_table_add(SYM_CLOJURE_STRING);
    symbol_table_add(SYM_CLOJURE_REPL);
    symbol_table_add(SYM_CLOJURE_CORE);
    symbol_table_add(SYM_CLOJURE_LANG);
    symbol_table_add(SYM_TINYCLJ);

    // Arithmetic symbols: store ArithOp index in upper bits (ARITH_ADD=0, SUB=1, MUL=2, DIV=3)
    INIT_SYMBOL(SYM_PLUS, sym_plus_data);
    SYM_PLUS->base.flags |= CLJ_FLAG_ARITHMETIC | (0 << CLJ_ARITH_OP_SHIFT);

    INIT_SYMBOL(SYM_MINUS, sym_minus_data);
    SYM_MINUS->base.flags |= CLJ_FLAG_ARITHMETIC | (1 << CLJ_ARITH_OP_SHIFT);

    INIT_SYMBOL(SYM_MULTIPLY, sym_multiply_data);
    SYM_MULTIPLY->base.flags |= CLJ_FLAG_ARITHMETIC | (2 << CLJ_ARITH_OP_SHIFT);

    INIT_SYMBOL(SYM_DIVIDE, sym_divide_data);
    SYM_DIVIDE->base.flags |= CLJ_FLAG_ARITHMETIC | (3 << CLJ_ARITH_OP_SHIFT);

    // Comparison symbols: store ComparisonOp index in bits 6-7 (LT=0, GT=1, LE=2, GE=3)
    // Note: = (EQUALS) has special generic equality handling, not encoded here
    INIT_SYMBOL(SYM_EQUALS, sym_equals_data);
    SYM_EQUALS->base.flags |= CLJ_FLAG_COMPARISON;  // No index - handled separately

    INIT_SYMBOL(SYM_LT, sym_lt_data);
    SYM_LT->base.flags |= CLJ_FLAG_COMPARISON | (0 << CLJ_COMP_OP_SHIFT);

    INIT_SYMBOL(SYM_GT, sym_gt_data);
    SYM_GT->base.flags |= CLJ_FLAG_COMPARISON | (1 << CLJ_COMP_OP_SHIFT);

    INIT_SYMBOL(SYM_LE, sym_le_data);
    SYM_LE->base.flags |= CLJ_FLAG_COMPARISON | (2 << CLJ_COMP_OP_SHIFT);

    INIT_SYMBOL(SYM_GE, sym_ge_data);
    SYM_GE->base.flags |= CLJ_FLAG_COMPARISON | (3 << CLJ_COMP_OP_SHIFT);

    INIT_SYMBOL(SYM_PRINTLN, sym_println_data);

    INIT_SYMBOL(SYM_PRINT, sym_print_data);

    INIT_SYMBOL(SYM_STR, sym_str_data);

    INIT_SYMBOL(SYM_CONJ, sym_conj_data);

    INIT_SYMBOL(SYM_NTH, sym_nth_data);

    INIT_SYMBOL(SYM_FIRST, sym_first_data);

    INIT_SYMBOL(SYM_REST, sym_rest_data);

    INIT_SYMBOL(SYM_COUNT, sym_count_data);

    INIT_SYMBOL(SYM_ALL_NS, sym_all_ns_data);

    // Namespace name symbols are pre-allocated in g_namespace_name_symbols[] above
    // and registered in the symbol table earlier in this function.
    
    // clojure.string native function symbols - with namespace
    INIT_SYMBOL_NS(SYM_TRIM, sym_trim_data, SYM_CLOJURE_STRING);

    INIT_SYMBOL_NS(SYM_UPPER_CASE, sym_upper_case_data, SYM_CLOJURE_STRING);

    INIT_SYMBOL_NS(SYM_LOWER_CASE, sym_lower_case_data, SYM_CLOJURE_STRING);

    INIT_SYMBOL_NS(SYM_PAD_LEFT, sym_pad_left_data, SYM_CLOJURE_STRING);

    INIT_SYMBOL_NS(SYM_LAST_INDEX_OF, sym_last_index_of_data, SYM_CLOJURE_STRING);

    INIT_SYMBOL_NS(SYM_STRING_REVERSE, sym_string_reverse_data, SYM_CLOJURE_STRING);
    
    // Special form symbol for `source` (unqualified)
    INIT_SYMBOL(SYM_SOURCE, sym_source_special_data);
    
    // clojure.repl native function symbol (namespaced) for source
    INIT_SYMBOL_NS(SYM_SOURCE_NATIVE, sym_source_data, SYM_CLOJURE_REPL);
    // clojure.repl native function symbol for dir
    INIT_SYMBOL_NS(SYM_DIR_NATIVE, sym_dir_data, SYM_CLOJURE_REPL);
    // tinyclj namespace name symbol is pre-allocated in g_namespace_name_symbols[] above.
    
    // tinyclj native function symbol for retain-count
    INIT_SYMBOL_NS(SYM_RETAIN_COUNT, sym_retain_count_data, SYM_TINYCLJ);
    
    // list-batch symbol (used in tinyclj.fs namespace)
    INIT_SYMBOL(SYM_LIST_BATCH, sym_list_batch_data);
    
    // clojure.core sqrt native function symbol
    INIT_SYMBOL_NS(SYM_SQRT_NATIVE, sym_sqrt_data, SYM_CLOJURE_CORE);

    // Additional symbols - static structs with symbol table registration
    INIT_SYMBOL(SYM_CONS, sym_cons_data);

    INIT_SYMBOL(SYM_SEQ, sym_seq_data);

    INIT_SYMBOL(SYM_NEXT, sym_next_data);

    INIT_SYMBOL(SYM_LIST, sym_list_data);

    INIT_SYMBOL(SYM_DESTRUCTURE, sym_destructure_data);

    // clojure.core native symbols without SYM_* global (register for intern_symbol_global lookup)
    symbol_table_add(&sym_reduce_data.sym);
    symbol_table_add(&sym_meta_data.sym);
    symbol_table_add(&sym_with_meta_data.sym);
    symbol_table_add(&sym_concat2_data.sym);
    symbol_table_add(&sym_map_data.sym);
    symbol_table_add(&sym_mapcat_data.sym);
    symbol_table_add(&sym_filter_data.sym);
    symbol_table_add(&sym_group_by_data.sym);
    symbol_table_add(&sym_last_data.sym);
    symbol_table_add(&sym_get_thread_bindings_data.sym);
    symbol_table_add(&sym_ns_unload_data.sym);
    symbol_table_add(&sym_gpio_watch_data.sym);
    symbol_table_add(&sym_gpio_unwatch_data.sym);
    symbol_table_add(&sym_gpio_simulate_data.sym);
    symbol_table_add(&sym_hash_set_data.sym);
    symbol_table_add(&sym_disj_data.sym);
    symbol_table_add(&sym_set_p_data.sym);

    for (size_t i = 0; i < sizeof(g_core_symbols) / sizeof(g_core_symbols[0]); i++)
        symbol_table_add(&g_core_symbols[i].sym);

    // Internal pre-interned symbols for lazy seq thunk state (hot path)
    INIT_SYMBOL(SYM_CONCAT_X, sym_concat_x_key_data);
    INIT_SYMBOL(SYM_CONCAT_Y, sym_concat_y_key_data);
    INIT_SYMBOL(SYM_CONCAT_THUNK_FN, sym_concat_thunk_fn_key_data);
    INIT_SYMBOL(SYM_MAP_FN, sym_map_fn_key_data);
    INIT_SYMBOL(SYM_MAP_SEQS, sym_map_seqs_key_data);
    INIT_SYMBOL(SYM_MAP_THUNK_FN, sym_map_thunk_fn_key_data);
    INIT_SYMBOL(SYM_MAPCAT_FN, sym_mapcat_fn_key_data);
    INIT_SYMBOL(SYM_MAPCAT_COLL, sym_mapcat_coll_key_data);
    INIT_SYMBOL(SYM_MAPCAT_INNER, sym_mapcat_inner_key_data);
    INIT_SYMBOL(SYM_MAPCAT_THUNK_FN, sym_mapcat_thunk_fn_key_data);
    INIT_SYMBOL(SYM_RANGE_CUR, sym_range_cur_key_data);
    INIT_SYMBOL(SYM_RANGE_INF_THUNK_FN, sym_range_inf_thunk_fn_key_data);

    // Note: SYM_AND and SYM_OR are now in g_special_symbols[] array - initialized above


    INIT_SYMBOL(SYM_DOSEQ, sym_doseq_data);

    INIT_SYMBOL(SYM_DOTIMES, sym_dotimes_data);

    // Keywords - static structs with symbol table registration
    INIT_SYMBOL(SYM_KW_LINE, sym_kw_line_data);

    INIT_SYMBOL(SYM_KW_FILE, sym_kw_file_data);

    INIT_SYMBOL(SYM_KW_DOC, sym_kw_doc_data);

    INIT_SYMBOL(SYM_KW_ERROR, sym_kw_error_data);

    INIT_SYMBOL(SYM_KW_STACK, sym_kw_stack_data);

    INIT_SYMBOL(SYM_KW_NS, sym_kw_ns_data);

    INIT_SYMBOL(SYM_KW_NAME, sym_kw_name_data);

    INIT_SYMBOL(SYM_KW_NATIVE, sym_kw_native_data);

    INIT_SYMBOL(SYM_KW_AS, sym_kw_as_data);

    INIT_SYMBOL(SYM_KW_REFER, sym_kw_refer_data);

    INIT_SYMBOL(SYM_KW_ELSE, sym_kw_else_data);

    INIT_SYMBOL(SYM_KW_VALUE, sym_kw_value_data);

    INIT_SYMBOL(SYM_KW_CLOSED, sym_kw_closed_data);

    INIT_SYMBOL(SYM_KW_DATA, sym_kw_data_data);

    INIT_SYMBOL(SYM_KW_FROM, sym_kw_from_data);

    INIT_SYMBOL(SYM_KW_TO, sym_kw_to_data);

    INIT_SYMBOL(SYM_KW_PORT, sym_kw_port_data);

    INIT_SYMBOL(SYM_KW_HOST, sym_kw_host_data);

    INIT_SYMBOL(SYM_KW_COLUMN, sym_kw_column_data);

    INIT_SYMBOL(SYM_KW_FN, sym_kw_fn_data);

    INIT_SYMBOL(SYM_KW_PATH, sym_kw_path_data);
    INIT_SYMBOL(SYM_KW_CALLBACK_FN, sym_kw_callback_fn_data);
    INIT_SYMBOL(SYM_KW_WATCHER_ID, sym_kw_watcher_id_data);

    INIT_SYMBOL(SYM_KW_META, sym_kw_meta_data);

    INIT_SYMBOL(SYM_KW_HOST_OS, sym_kw_host_os_data);

    INIT_SYMBOL(SYM_KW_MACRO, sym_kw_macro_data);

    INIT_SYMBOL(SYM_KW_TYPE, sym_kw_type_data);

    INIT_SYMBOL(SYM_KW_SIZE, sym_kw_size_data);
    INIT_SYMBOL(SYM_KW_CHUNKS, sym_kw_chunks_data);

    INIT_SYMBOL(SYM_KW_ENTRIES, sym_kw_entries_data);

    INIT_SYMBOL(SYM_KW_LAST_KEY, sym_kw_last_key_data);

    INIT_SYMBOL(SYM_KW_YEAR, sym_kw_year_data);

    INIT_SYMBOL(SYM_KW_MONTH, sym_kw_month_data);

    INIT_SYMBOL(SYM_KW_DAY, sym_kw_day_data);

    INIT_SYMBOL(SYM_KW_HOUR, sym_kw_hour_data);

    INIT_SYMBOL(SYM_KW_MINUTE, sym_kw_minute_data);

    INIT_SYMBOL(SYM_KW_SECOND, sym_kw_second_data);

    // Runtime stats keywords
    // Runtime stats keywords (always available)
    INIT_SYMBOL(SYM_KW_OS, sym_kw_os_data);
    INIT_SYMBOL(SYM_KW_VERSION, sym_kw_version_data);

#ifdef DEBUG
    // Runtime stats keywords (DEBUG only)
    INIT_SYMBOL(SYM_KW_SYMBOLS, sym_kw_symbols_data);
    INIT_SYMBOL(SYM_KW_NAMESPACES, sym_kw_namespaces_data);
    INIT_SYMBOL(SYM_KW_BYTES_CURRENT, sym_kw_bytes_current_data);
    INIT_SYMBOL(SYM_KW_BYTES_PEAK, sym_kw_bytes_peak_data);
    INIT_SYMBOL(SYM_KW_MEMORY_STATS, sym_kw_memory_stats_data);

#if defined(MEMORY_PROFILING_ENABLED) && MEMORY_PROFILING_ENABLED
    // Extended memory profiling keywords (DEBUG + MEMORY_PROFILING_ENABLED only)
    INIT_SYMBOL(SYM_KW_RAW_BYTES_CURRENT, sym_kw_raw_bytes_current_data);
    INIT_SYMBOL(SYM_KW_RAW_BYTES_PEAK, sym_kw_raw_bytes_peak_data);
    INIT_SYMBOL(SYM_KW_RAW_BLOCKS_CURRENT, sym_kw_raw_blocks_current_data);
    INIT_SYMBOL(SYM_KW_RAW_BLOCKS_PEAK, sym_kw_raw_blocks_peak_data);
    INIT_SYMBOL(SYM_KW_BYTES_BY_TYPE, sym_kw_bytes_by_type_data);
    INIT_SYMBOL(SYM_KW_TOTAL_ALLOCATIONS, sym_kw_total_allocations_data);
    INIT_SYMBOL(SYM_KW_TOTAL_DEALLOCATIONS, sym_kw_total_deallocations_data);
    INIT_SYMBOL(SYM_KW_MEMORY_LEAKS, sym_kw_memory_leaks_data);
    INIT_SYMBOL(SYM_KW_ALLOC_COUNT, sym_kw_alloc_count_data);
    INIT_SYMBOL(SYM_KW_DEALLOC_COUNT, sym_kw_dealloc_count_data);
#endif // MEMORY_PROFILING_ENABLED
#endif // DEBUG

    // Additional symbols for hot path optimization
    INIT_SYMBOL(SYM_NS_STAR, sym_ns_star_data);
    SYM_NS_STAR->base.flags |= CLJ_FLAG_DYNAMIC;

    // Clean up macros to avoid namespace pollution
    #undef INIT_SYMBOL
    #undef INIT_SYMBOL_NS
    #undef INIT_SPECIAL_SYMBOL

    // Set function pointers for Special Forms (O(1) dispatch optimization)
    // Cast to CljSpecialSymbol and set eval_fn pointer (with type cast for compatibility)
    // Note: Using void* cast to bridge between placeholder type in symbol.h and real type in eval.h
    if (is_special_symbol(SYM_IF)) {
        ((CljSpecialSymbol*)SYM_IF)->eval_fn = (SpecialFormEvalFn_Placeholder)(SpecialFormEvalFn)eval_special_if;
    }
    if (is_special_symbol(SYM_TRY)) {
        ((CljSpecialSymbol*)SYM_TRY)->eval_fn = (SpecialFormEvalFn_Placeholder)(SpecialFormEvalFn)eval_special_try;
    }
    if (is_special_symbol(SYM_WHEN)) {
        ((CljSpecialSymbol*)SYM_WHEN)->eval_fn = (SpecialFormEvalFn_Placeholder)(SpecialFormEvalFn)eval_special_when;
    }
    if (is_special_symbol(SYM_WHILE)) {
        ((CljSpecialSymbol*)SYM_WHILE)->eval_fn = (SpecialFormEvalFn_Placeholder)(SpecialFormEvalFn)eval_special_while;
    }
    if (is_special_symbol(SYM_COND)) {
        ((CljSpecialSymbol*)SYM_COND)->eval_fn = (SpecialFormEvalFn_Placeholder)(SpecialFormEvalFn)eval_special_cond;
    }
    if (is_special_symbol(SYM_DO)) {
        ((CljSpecialSymbol*)SYM_DO)->eval_fn = (SpecialFormEvalFn_Placeholder)(SpecialFormEvalFn)eval_special_do;
    }
    if (is_special_symbol(SYM_AND)) {
        ((CljSpecialSymbol*)SYM_AND)->eval_fn = (SpecialFormEvalFn_Placeholder)(SpecialFormEvalFn)eval_special_and;
    }
    if (is_special_symbol(SYM_OR)) {
        ((CljSpecialSymbol*)SYM_OR)->eval_fn = (SpecialFormEvalFn_Placeholder)(SpecialFormEvalFn)eval_special_or;
    }
    if (is_special_symbol(SYM_FN)) {
        ((CljSpecialSymbol*)SYM_FN)->eval_fn = (SpecialFormEvalFn_Placeholder)(SpecialFormEvalFn)eval_special_fn;
    }
    if (is_special_symbol(SYM_LET)) {
        ((CljSpecialSymbol*)SYM_LET)->eval_fn = (SpecialFormEvalFn_Placeholder)(SpecialFormEvalFn)eval_special_let;
    }
    if (is_special_symbol(SYM_VAR)) {
        ((CljSpecialSymbol*)SYM_VAR)->eval_fn = (SpecialFormEvalFn_Placeholder)(SpecialFormEvalFn)eval_special_var;
    }
    if (is_special_symbol(SYM_QUOTE)) {
        ((CljSpecialSymbol*)SYM_QUOTE)->eval_fn = (SpecialFormEvalFn_Placeholder)(SpecialFormEvalFn)eval_special_quote;
    }
    if (is_special_symbol(SYM_RECUR)) {
        ((CljSpecialSymbol*)SYM_RECUR)->eval_fn = (SpecialFormEvalFn_Placeholder)(SpecialFormEvalFn)eval_special_recur;
    }
    if (is_special_symbol(SYM_THROW)) {
        ((CljSpecialSymbol*)SYM_THROW)->eval_fn = (SpecialFormEvalFn_Placeholder)(SpecialFormEvalFn)eval_special_throw;
    }
    if (is_special_symbol(SYM_GO)) {
        ((CljSpecialSymbol*)SYM_GO)->eval_fn = (SpecialFormEvalFn_Placeholder)(SpecialFormEvalFn)eval_special_go;
    }
    if (is_special_symbol(SYM_TIME)) {
        ((CljSpecialSymbol*)SYM_TIME)->eval_fn = (SpecialFormEvalFn_Placeholder)(SpecialFormEvalFn)eval_special_time;
    }
#ifdef DEBUG
    if (is_special_symbol(SYM_HEAP)) {
        ((CljSpecialSymbol*)SYM_HEAP)->eval_fn = (SpecialFormEvalFn_Placeholder)(SpecialFormEvalFn)eval_special_heap;
    }
#endif
    if (is_special_symbol(SYM_BINDING)) {
        ((CljSpecialSymbol*)SYM_BINDING)->eval_fn = (SpecialFormEvalFn_Placeholder)(SpecialFormEvalFn)eval_special_binding;
    }
    // Note: SYM_DOTIMES is handled inline in eval.c, not as Special Form
    
    // Quasiquote Special Form - delegates to Clojure quasiquote-fn after bootstrap
    if (is_special_symbol(SYM_QUASIQUOTE)) {
        ((CljSpecialSymbol*)SYM_QUASIQUOTE)->eval_fn = (SpecialFormEvalFn_Placeholder)(SpecialFormEvalFn)eval_special_quasiquote;
    }
    
    // defmacro Special Form - defines macros in the current namespace
    if (is_special_symbol(SYM_DEFMACRO)) {
        ((CljSpecialSymbol*)SYM_DEFMACRO)->eval_fn = (SpecialFormEvalFn_Placeholder)(SpecialFormEvalFn)eval_special_defmacro;
    }
    
}

// -----------------------------------------------------------------------------
// Symbol-table lookup keys (CljSymbol)
// -----------------------------------------------------------------------------
// Build a lightweight, stack-allocated symbol key for hash lookups.
// This avoids heap allocation during intern_symbol() lookups.
static inline CljSymbol make_symbol_key(CljSymbol *ns_name, const char *cname) {
    CljSymbol key = {
        .base = { .type = CLJ_SYMBOL, .flags = 0, .rc = SINGLETON_RC },
        .ns_name = ns_name,
        .unqualified = NULL,
        .cname = cname,
    };
    return key;
}

// Find symbol in the table - O(1) HashSet lookup
static CljSymbol* symbol_table_find(CljSymbol *ns_name, const char *cname) {
    if (!cname || !g_runtime.symbol_table) return NULL;

    CljSymbol key = make_symbol_key(ns_name, cname);
    ID result = hashset_get(g_runtime.symbol_table, (ID)&key);
    return (result == NOT_FOUND) ? NULL : (CljSymbol*)result;
}

// Add symbol to the table - O(1) HashSet insert
void symbol_table_add(CljSymbol *symbol) {
    if (!symbol || !symbol->cname) return;

    // Extract namespace and name from symbol
    CljSymbol *ns_name = symbol->ns_name;
    const char *cname = symbol->cname;

    if (!g_runtime.symbol_table) {
        g_runtime.symbol_table = make_hashset(512);  // 512 = good initial capacity for Linear Probing
    }

    if (hashset_contains(g_runtime.symbol_table, symbol)) {
        return;  // Already exists
    }

    // Insert symbol into HashSet.
    hashset_add_inplace(&g_runtime.symbol_table, symbol);
}

/**
 * @brief Create a symbol value (internal use only - not interned)
 * @param cname Symbol name
 * @param ns_name Namespace name symbol (can be NULL)
 * @return CljSymbol symbol object
 * @note This function is internal. Use intern_symbol() instead for proper symbol interning.
 */
static CljSymbol* make_symbol(const char *cname, CljSymbol *ns_name) {
    // Assertion: name must not be NULL
    CLJ_ASSERT(cname != NULL && "make_symbol: name cannot be NULL");
    
    if (!cname) {
        throw_exception_formatted(EXCEPTION_ILLEGAL_ARGUMENT, __FILE__, __LINE__, 0,
                                  "make_symbol: name cannot be NULL");
        return NULL;
    }

    // Assertion: name must not be empty
    CLJ_ASSERT(cname[0] != '\0' && "make_symbol: name cannot be empty");

    size_t cname_len = strlen(cname);

    // Range check for name length (keep for safety)
    if (cname_len >= SYMBOL_NAME_MAX_LEN) {
        throw_exception_formatted(EXCEPTION_ILLEGAL_ARGUMENT, __FILE__, __LINE__, 0,
                                  "Symbol name '%s' exceeds maximum length of %d characters",
                                  cname, SYMBOL_NAME_MAX_LEN - 1);
        return NULL;
    }

    // Allocate as raw heap block (symbols are singletons and never released).
    // Use CLJ_MALLOC so the allocation is tracked by the memory profiler.
    CljSymbol *sym = (CljSymbol*)CLJ_MALLOC(sizeof(CljSymbol));
    if (!sym) {
        throw_exception_formatted(EXCEPTION_OUT_OF_MEMORY, __FILE__, __LINE__, 0,
                                  "Failed to allocate memory for symbol '%s'", cname);
        return NULL;
    }

    sym->base.type = CLJ_SYMBOL;
    sym->base.rc = SINGLETON_RC;  // Interned symbols are singletons - never freed
    sym->base.flags = 0;  // Initialize flags to 0 (no special flags by default)

    if (cname_len > 1 && cname[0] == '*' && cname[cname_len - 1] == '*') {
        sym->base.flags |= CLJ_FLAG_DYNAMIC;
    }

    // Use string directly if in data segment (immutable), otherwise duplicate
    if (is_pointer_in_data_segment(cname)) {
        sym->cname = cname;
    } else {
        sym->cname = clj_strdup(cname);
        if (!sym->cname) {
            CLJ_FREE(sym);
            throw_exception_formatted(EXCEPTION_OUT_OF_MEMORY, __FILE__, __LINE__, 0,
                                      "Failed to duplicate string for symbol '%s'", cname);
            return NULL;
        }
    }

    // Enforce invariant: symbols always have a name
    CLJ_ASSERT(sym->cname != NULL && "Symbol must have a name after creation");

    // Use ns_name directly (already a CljSymbol*)
    sym->ns_name = ns_name;
    sym->unqualified = NULL;

    return sym;
}

/**
 * @brief Intern a symbol in a namespace (setup only).
 * @param ns_name Namespace symbol (or NULL for global)
 * @param cname C string name
 * @return Interned symbol or NULL
 * @note Use only during setup/initialization. Never use in production hot path;
 *       use pre-interned static/global symbol pointers (e.g. SYM_IF, KW_FN) instead.
 */
CljSymbol* intern_symbol(CljSymbol *ns_name, const char *cname) {
    if (!cname) return NULL;

    CljSymbol *existing = symbol_table_find(ns_name, cname);
    if (existing) {
        return existing;
    }

    CljSymbol *symbol = make_symbol(cname, ns_name);
    if (!symbol) return NULL;

    symbol_table_add(symbol);

    return symbol;
}

/**
 * @brief Intern a global (namespace-less) symbol (setup only).
 * @param cname C string name
 * @return Interned symbol or NULL
 * @note Use only during setup/initialization. Never use in production hot path;
 *       store result in a static variable and use that in production (see event_loop.c, gpio_esp32.c).
 */
CljSymbol* intern_symbol_global(const char *cname) {
    return intern_symbol(NULL, cname);
}

/**
 * @brief Get unqualified version of a symbol (cached)
 * 
 * NOTE: This function is used as a fallback mechanism for backwards compatibility.
 * After canonicalization, symbols are already properly interned, but this function
 * is still used in some places to support both qualified and unqualified lookups
 * in namespace mappings (e.g., for :refer :all cases).
 * 
 * @param symbol Symbol to get unqualified version of
 * @return Unqualified symbol (cached in symbol->unqualified field)
 */
// Helper: Get namespace object from symbol's namespace name (DRY principle)
// Returns clojure.core namespace if symbol has no explicit namespace (ns_name == NULL)
// Returns NULL if namespace doesn't exist
struct CljNamespace* symbol_get_namespace(CljSymbol *sym) {
    if (!sym) return NULL;
    
    // NULL namespace means clojure.core (default for core symbols)
    if (!sym->ns_name) {
        return ns_find("clojure.core");
    }
    
    // Safety check: ensure ns_name is a valid symbol before accessing cname
    if (TAG(sym->ns_name) != CLJ_SYMBOL) return NULL;
    CljSymbol *ns_sym = as_symbol(sym->ns_name);
    if (!ns_sym || !ns_sym->cname) return NULL;
    // Use fast symbol-based lookup (avoids redundant intern_symbol call)
    return ns_find_by_symbol(sym->ns_name);
}

// Helper: Get namespace name string from symbol (DRY principle)
// Returns "clojure.core" if symbol has no explicit namespace (ns_name == NULL)
const char* symbol_get_namespace_name(CljSymbol *sym) {
    if (!sym) return NULL;
    
    // NULL namespace means clojure.core (default for core symbols)
    if (!sym->ns_name) {
        return "clojure.core";
    }
    
    // Safety check: ensure ns_name is a valid symbol before accessing cname
    if (TAG(sym->ns_name) != CLJ_SYMBOL) return NULL;
    CljSymbol *ns_sym = as_symbol(sym->ns_name);
    if (!ns_sym || !ns_sym->cname) return NULL;
    return ns_sym->cname;
}

// Clean up symbol table (ONLY for test cleanup, not regular symbols)
// This function will be eliminated by dead-code-elimination in production builds
// since it's only called from test files
void symbol_table_cleanup() {
    RELEASE(g_runtime.symbol_table);
    g_runtime.symbol_table = NULL;
}

// Special Form Management moved to to_string.c
