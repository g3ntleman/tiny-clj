extern struct CljSymbol *SYM_KW_META;
#ifndef TINY_CLJ_SYMBOLS_H
#define TINY_CLJ_SYMBOLS_H

#include "object.h"
#include <stdbool.h>

// Forward declaration for CljSymbol (needed for self-reference)
typedef struct CljSymbol CljSymbol;

// Forward declaration for SpecialFormEvalFn (properly defined in eval.h)
// Using void* to avoid circular dependencies - actual typed definition is in eval.h
typedef void* (*SpecialFormEvalFn_Placeholder)(void*, void*, void*, const void*);

// CljSymbol struct definition
#define SYMBOL_NAME_MAX_LEN 64

struct CljSymbol {
    CljObject base;
    struct CljSymbol *ns_name;  // Namespace name symbol (Clojure-compatible: Symbol->ns_name is a Symbol, not Namespace object)
    struct CljSymbol *unqualified;  // Cached unqualified symbol pointer (ns_name == NULL)
    const char *cname;
};

// Dynamic vars are "earmuffed" symbols: *foo*
// Implemented in symbol.c to avoid duplicating code in every translation unit.
bool is_earmuffed_dynamic_symbol(const CljSymbol *sym);

// Type predicate - O(1) check if object is a symbol
static inline bool is_symbol(ID obj) {
    return obj && TAG(obj) == CLJ_SYMBOL;
}

// Type-safe casting
static inline CljSymbol* as_symbol(ID obj) {
    return (CljSymbol*)assert_type((CljObject*)obj, CLJ_SYMBOL);
}

// Check if an object is a keyword (symbol starting with ':')
static inline bool is_keyword(ID obj) {
    if (!is_symbol(obj)) return false;
    CljSymbol *sym = as_symbol(obj);
    return sym->cname && sym->cname[0] == ':';
}
#define IS_KEYWORD(obj) is_keyword(obj)

// Check if a symbol is a special form (if, let, fn, def, do, quote, etc.)
// O(1) check using address range comparison instead of flags
// Note: This function is implemented in symbol.c to access the static g_special_symbols[] array
bool is_special_symbol(CljSymbol *symbol);

// Check if a symbol is a native/builtin function (first, rest, +, -, etc.)
// O(1) check using flags field in CljObject
static inline bool is_native_symbol(CljSymbol *symbol) {
    return symbol && (symbol->base.flags & CLJ_FLAG_NATIVE);
}

// Global Symbol pointers for special forms
extern CljSymbol *SYM_TRY;
extern CljSymbol *SYM_CATCH;
extern CljSymbol *SYM_IF;
extern CljSymbol *SYM_COND;
extern CljSymbol *SYM_WHEN;
extern CljSymbol *SYM_WHILE;
extern CljSymbol *SYM_LET;
extern CljSymbol *SYM_FN;
extern CljSymbol *SYM_DEF;
extern CljSymbol *SYM_DEFN;
extern CljSymbol *SYM_DEFMACRO;
extern CljSymbol *SYM_DEFRECORD;
extern CljSymbol *SYM_VAR;
extern CljSymbol *SYM_QUOTE;
extern CljSymbol *SYM_QUASIQUOTE;
extern CljSymbol *SYM_UNQUOTE;
extern CljSymbol *SYM_UNQUOTE_SPLICE;
extern CljSymbol *SYM_SOURCE;
extern CljSymbol *SYM_DO;
extern CljSymbol *SYM_LOOP;
extern CljSymbol *SYM_RECUR;
extern CljSymbol *SYM_DESTRUCTURE;
extern CljSymbol *SYM_THROW;
extern CljSymbol *SYM_FINALLY;
extern CljSymbol *SYM_NS;
extern CljSymbol *SYM_BINDING;
extern CljSymbol *SYM_GO;
extern CljSymbol *SYM_TIME;
extern CljSymbol *SYM_HEAP;
extern CljSymbol *SYM_DEREF;
extern CljSymbol *SYM_NIL;
extern CljSymbol *SYM_AMP;  // & for variadic parameters

// Global Symbol pointers for builtins
extern CljSymbol *SYM_PLUS;
extern CljSymbol *SYM_MINUS;
extern CljSymbol *SYM_MULTIPLY;
extern CljSymbol *SYM_DIVIDE;
extern CljSymbol *SYM_EQUALS;
extern CljSymbol *SYM_LT;
extern CljSymbol *SYM_GT;
extern CljSymbol *SYM_LE;
extern CljSymbol *SYM_GE;
extern CljSymbol *SYM_PRINTLN;
extern CljSymbol *SYM_PRINT;
extern CljSymbol *SYM_STR;
extern CljSymbol *SYM_CONJ;
extern CljSymbol *SYM_NTH;
extern CljSymbol *SYM_TRIM;
extern CljSymbol *SYM_UPPER_CASE;
extern CljSymbol *SYM_LOWER_CASE;
extern CljSymbol *SYM_PAD_LEFT;
extern CljSymbol *SYM_LAST_INDEX_OF;
extern CljSymbol *SYM_STRING_REVERSE;
extern CljSymbol *SYM_FIRST;
extern CljSymbol *SYM_REST;
extern CljSymbol *SYM_COUNT;
extern CljSymbol *SYM_ALL_NS;

// Additional symbols for optimization
extern CljSymbol *SYM_CONS;
extern CljSymbol *SYM_SEQ;
extern CljSymbol *SYM_NEXT;
extern CljSymbol *SYM_LIST;
extern CljSymbol *SYM_AND;
extern CljSymbol *SYM_OR;
extern CljSymbol *SYM_DOSEQ;
extern CljSymbol *SYM_DOTIMES;

// Global keyword symbols
extern CljSymbol *SYM_KW_LINE;
extern CljSymbol *SYM_KW_FILE;
extern CljSymbol *SYM_KW_DOC;
extern CljSymbol *SYM_KW_ERROR;
extern CljSymbol *SYM_KW_STACK;
extern CljSymbol *SYM_KW_NS;
extern CljSymbol *SYM_KW_NAME;
extern CljSymbol *SYM_KW_NATIVE;
extern CljSymbol *SYM_KW_AS;
extern CljSymbol *SYM_KW_REFER;
extern CljSymbol *SYM_KW_ELSE;
extern CljSymbol *SYM_KW_VALUE;
extern CljSymbol *SYM_KW_CLOSED;
extern CljSymbol *SYM_KW_DATA;
extern CljSymbol *SYM_KW_FROM;
extern CljSymbol *SYM_KW_TO;
extern CljSymbol *SYM_KW_PORT;
extern CljSymbol *SYM_KW_HOST;
extern CljSymbol *SYM_KW_COLUMN;
extern CljSymbol *SYM_KW_FN;
extern CljSymbol *SYM_KW_PATH;
extern CljSymbol *SYM_KW_CALLBACK_FN;
extern CljSymbol *SYM_KW_WATCHER_ID;
extern CljSymbol *SYM_KW_CHUNKS;
extern CljSymbol *SYM_KW_HOST_OS;
extern CljSymbol *SYM_KW_MACRO;
extern CljSymbol *SYM_KW_TYPE;
extern CljSymbol *SYM_KW_SIGNAL;
extern CljSymbol *SYM_KW_MODE;
extern CljSymbol *SYM_KW_FREQ;
extern CljSymbol *SYM_KW_DUTY;
extern CljSymbol *SYM_KW_DIGITAL;
extern CljSymbol *SYM_KW_ANALOG;
extern CljSymbol *SYM_KW_INPUT;
extern CljSymbol *SYM_KW_OUTPUT;
extern CljSymbol *SYM_KW_ADC;
extern CljSymbol *SYM_KW_DAC;
extern CljSymbol *SYM_KW_PWM;
extern CljSymbol *SYM_KW_SIZE;
extern CljSymbol *SYM_KW_ENTRIES;
extern CljSymbol *SYM_KW_LAST_KEY;

// Datetime keyword symbols
extern CljSymbol *SYM_KW_YEAR;
extern CljSymbol *SYM_KW_MONTH;
extern CljSymbol *SYM_KW_DAY;
extern CljSymbol *SYM_KW_HOUR;
extern CljSymbol *SYM_KW_MINUTE;
extern CljSymbol *SYM_KW_SECOND;

// Runtime stats keyword symbols
// Runtime stats keywords (always available)
extern CljSymbol *SYM_KW_OS;
extern CljSymbol *SYM_KW_OS_VERSION;
extern CljSymbol *SYM_KW_VERSION;
extern CljSymbol *SYM_KW_HARDWARE;
extern CljSymbol *SYM_KW_MODEL;
extern CljSymbol *SYM_KW_CORES;
extern CljSymbol *SYM_KW_REVISION;
extern CljSymbol *SYM_KW_GPIO_PIN_COUNT;
extern CljSymbol *SYM_KW_EXTERNAL_RAM_TOTAL;
extern CljSymbol *SYM_KW_PSRAM_BYTES;
extern CljSymbol *SYM_KW_WIFI;
extern CljSymbol *SYM_KW_BLE;
extern CljSymbol *SYM_KW_BT;
extern CljSymbol *SYM_KW_EMB_FLASH;
extern CljSymbol *SYM_KW_EMB_PSRAM;
extern CljSymbol *SYM_KW_IEEE802154;

#ifdef DEBUG
// Runtime stats keywords (DEBUG only)
extern CljSymbol *SYM_KW_SYMBOLS;
extern CljSymbol *SYM_KW_NAMESPACES;
extern CljSymbol *SYM_KW_BYTES_CURRENT;
extern CljSymbol *SYM_KW_BYTES_PEAK;
extern CljSymbol *SYM_KW_MEMORY_STATS;

#if defined(MEMORY_PROFILING_ENABLED) && MEMORY_PROFILING_ENABLED
// Extended memory profiling keywords (DEBUG + MEMORY_PROFILING_ENABLED only)
extern CljSymbol *SYM_KW_RAW_BYTES_CURRENT;
extern CljSymbol *SYM_KW_RAW_BYTES_PEAK;
extern CljSymbol *SYM_KW_RAW_BLOCKS_CURRENT;
extern CljSymbol *SYM_KW_RAW_BLOCKS_PEAK;
extern CljSymbol *SYM_KW_BYTES_BY_TYPE;
extern CljSymbol *SYM_KW_TOTAL_ALLOCATIONS;
extern CljSymbol *SYM_KW_TOTAL_DEALLOCATIONS;
extern CljSymbol *SYM_KW_MEMORY_LEAKS;
extern CljSymbol *SYM_KW_ALLOC_COUNT;
extern CljSymbol *SYM_KW_DEALLOC_COUNT;
#endif // MEMORY_PROFILING_ENABLED
#endif // DEBUG

// Namespace name symbols
extern CljSymbol *SYM_CLOJURE_CORE;
extern CljSymbol *SYM_CLOJURE_STRING;
extern CljSymbol *SYM_CLOJURE_REPL;
extern CljSymbol *SYM_CLOJURE_LANG;
extern CljSymbol *SYM_TINYCLJ;
extern CljSymbol *SYM_NS_STAR;

// Internal pre-interned symbols for lazy seq thunk state (hot path)
extern CljSymbol *SYM_CONCAT_X;
extern CljSymbol *SYM_CONCAT_Y;
extern CljSymbol *SYM_CONCAT_THUNK_FN;
extern CljSymbol *SYM_THUNK_STATE;
extern CljSymbol *SYM_MAP_FN;
extern CljSymbol *SYM_MAP_SEQS;
extern CljSymbol *SYM_MAP_THUNK_FN;
extern CljSymbol *SYM_MAPCAT_FN;
extern CljSymbol *SYM_MAPCAT_COLL;
extern CljSymbol *SYM_MAPCAT_INNER;
extern CljSymbol *SYM_MAPCAT_THUNK_FN;
extern CljSymbol *SYM_RANGE_CUR;
extern CljSymbol *SYM_RANGE_INF_THUNK_FN;

// tiny-clj namespace function symbols
extern CljSymbol *SYM_RETAIN_COUNT;
extern CljSymbol *SYM_LIST_BATCH;

#if defined(CLJ_HOT_PATH)
    #if defined(__clang__) || defined(__GNUC__)
        // Compile-time enforcement: hot-path code may not intern symbols
        CljSymbol* intern_symbol(CljSymbol *ns_name, const char *cname)
            __attribute__((error("Symbol interning not allowed in hot path. Symbols must be pre-interned by the parser/setup.")));
        CljSymbol* intern_symbol_global(const char *cname)
            __attribute__((error("Symbol interning not allowed in hot path. Symbols must be pre-interned by the parser/setup.")));
    #else
        #error "CLJ_HOT_PATH enforcement requires Clang/GCC support for error attributes."
    #endif
#else
    // Symbol API (allowed outside hot path)
    CljSymbol* intern_symbol(CljSymbol *ns_name, const char *cname);
    CljSymbol* intern_symbol_global(const char *cname);
#endif // CLJ_HOT_PATH

// Helpers
void symbol_table_add(CljSymbol *symbol);
CljSymbol* symbol_table_lookup(CljSymbol *ns_name, const char *cname);
void symbol_table_cleanup(void);
void symbol_table_fit_startup_reserve(unsigned int reserve_percent);

struct CljNamespace;
struct CljNamespace* symbol_get_namespace(CljSymbol *sym);
const char* symbol_get_namespace_name(CljSymbol *sym);

void init_special_symbols(void);

// Static symbol data for builtin registration
typedef struct StaticSymbolData {
    CljSymbol sym;
} StaticSymbolData;

extern StaticSymbolData sym_trim_data;
extern StaticSymbolData sym_upper_case_data;
extern StaticSymbolData sym_lower_case_data;
extern StaticSymbolData sym_pad_left_data;
extern StaticSymbolData sym_last_index_of_data;
extern StaticSymbolData sym_string_reverse_data;
extern StaticSymbolData sym_source_data;
extern StaticSymbolData sym_dir_data;
extern StaticSymbolData sym_retain_count_data;
extern StaticSymbolData sym_meta_data;
extern StaticSymbolData sym_with_meta_data;
extern StaticSymbolData sym_reduce_data;
extern StaticSymbolData sym_plus_data;
extern StaticSymbolData sym_minus_data;
extern StaticSymbolData sym_multiply_data;
extern StaticSymbolData sym_divide_data;
extern StaticSymbolData sym_max_data;
extern StaticSymbolData sym_min_data;
extern StaticSymbolData sym_abs_data;
extern StaticSymbolData sym_equals_data;
extern StaticSymbolData sym_lt_data;
extern StaticSymbolData sym_gt_data;
extern StaticSymbolData sym_le_data;
extern StaticSymbolData sym_ge_data;
extern StaticSymbolData sym_println_data;
extern StaticSymbolData sym_print_data;
extern StaticSymbolData sym_str_data;
extern StaticSymbolData sym_conj_data;
extern StaticSymbolData sym_disj_data;
extern StaticSymbolData sym_hash_set_data;
extern StaticSymbolData sym_nth_data;
extern StaticSymbolData sym_first_data;
extern StaticSymbolData sym_rest_data;
extern StaticSymbolData sym_concat_data;
extern StaticSymbolData sym_concat2_data;
extern StaticSymbolData sym_map_data;
extern StaticSymbolData sym_mapcat_data;
extern StaticSymbolData sym_filter_data;
extern StaticSymbolData sym_group_by_data;
extern StaticSymbolData sym_last_data;
extern StaticSymbolData sym_ns_unload_data;
extern StaticSymbolData sym_get_thread_bindings_data;
extern StaticSymbolData sym_seq_data;
extern StaticSymbolData sym_not_data;
extern StaticSymbolData sym_count_data;
extern StaticSymbolData sym_cons_data;
extern StaticSymbolData sym_next_data;
extern StaticSymbolData sym_nnext_data;
extern StaticSymbolData sym_nthnext_data;
extern StaticSymbolData sym_destructure_data;
extern StaticSymbolData sym_gensym_data;
extern StaticSymbolData sym_partition_data;
extern StaticSymbolData sym_some_data;
extern StaticSymbolData sym_list_data;
extern StaticSymbolData sym_mod_data;
extern StaticSymbolData sym_quot_data;
extern StaticSymbolData sym_bit_shift_left_data;
extern StaticSymbolData sym_bit_shift_right_data;
extern StaticSymbolData sym_bit_and_data;
extern StaticSymbolData sym_bit_or_data;
extern StaticSymbolData sym_range_data;
extern StaticSymbolData sym_repeat_data;
extern StaticSymbolData sym_lazy_seq_star_data;
extern StaticSymbolData sym_math_sqrt_data;
extern StaticSymbolData sym_sqrt_data;
extern StaticSymbolData sym_format_data;
extern StaticSymbolData sym_subs_data;
extern StaticSymbolData sym_symbol_data;
extern StaticSymbolData sym_type_data;
extern StaticSymbolData sym_array_map_data;
extern StaticSymbolData sym_hash_map_data;
extern StaticSymbolData sym_vector_data;
extern StaticSymbolData sym_vec_data;
extern StaticSymbolData sym_peek_data;
extern StaticSymbolData sym_pop_data;
extern StaticSymbolData sym_subvec_data;
extern StaticSymbolData sym_reverse_data;
extern StaticSymbolData sym_assoc_data;
extern StaticSymbolData sym_dissoc_data;
extern StaticSymbolData sym_merge_data;
extern StaticSymbolData sym_contains_p_data;
extern StaticSymbolData sym_update_data;
extern StaticSymbolData sym_into_data;
extern StaticSymbolData sym_select_keys_data;
extern StaticSymbolData sym_find_data;
extern StaticSymbolData sym_transient_data;
extern StaticSymbolData sym_persistent_bang_data;
extern StaticSymbolData sym_conj_bang_data;
extern StaticSymbolData sym_get_data;
extern StaticSymbolData sym_keys_data;
extern StaticSymbolData sym_vals_data;
extern StaticSymbolData sym_nilp_data;
extern StaticSymbolData sym_not_eq_data;
extern StaticSymbolData sym_identical_data;
extern StaticSymbolData sym_vector_p_data;
extern StaticSymbolData sym_map_p_data;
extern StaticSymbolData sym_number_p_data;
extern StaticSymbolData sym_integer_p_data;
extern StaticSymbolData sym_float_p_data;
extern StaticSymbolData sym_string_p_data;
#ifdef DEBUG
extern StaticSymbolData sym_ast_string_data;
#endif
extern StaticSymbolData sym_keyword_p_data;
extern StaticSymbolData sym_keyword_data;
extern StaticSymbolData sym_name_data;
extern StaticSymbolData sym_symbol_p_data;
extern StaticSymbolData sym_fn_p_data;
extern StaticSymbolData sym_atom_p_data;
extern StaticSymbolData sym_char_p_data;
extern StaticSymbolData sym_list_p_data;
extern StaticSymbolData sym_set_p_data;
extern StaticSymbolData sym_yield_data;
extern StaticSymbolData sym_current_time_ms_data;
extern StaticSymbolData sym_ns_map_data;
extern StaticSymbolData sym_find_ns_data;
extern StaticSymbolData sym_all_ns_data;
extern StaticSymbolData sym_pr_data;
extern StaticSymbolData sym_prn_data;
extern StaticSymbolData sym_byte_array_data;
extern StaticSymbolData sym_aget_data;
extern StaticSymbolData sym_aset_data;
extern StaticSymbolData sym_alength_data;
extern StaticSymbolData sym_aclone_data;
extern StaticSymbolData sym_run_next_task_data;
extern StaticSymbolData sym_with_pool_data;
extern StaticSymbolData sym_schedule_data;
extern StaticSymbolData sym_schedule_periodic_data;
extern StaticSymbolData sym_cancel_timer_data;
extern StaticSymbolData sym_atom_data;
extern StaticSymbolData sym_deref_data;
extern StaticSymbolData sym_reset_bang_data;
extern StaticSymbolData sym_swap_bang_data;
extern StaticSymbolData sym_slurp_data;
extern StaticSymbolData sym_spit_data;

// Audio symbols
extern StaticSymbolData sym_sound_load_track_data;
extern StaticSymbolData sym_sound_unload_track_data;
extern StaticSymbolData sym_sound_play_music_data;
extern StaticSymbolData sym_sound_stop_track_data;
extern StaticSymbolData sym_sound_stop_music_data;
extern StaticSymbolData sym_sound_play_sfx_data;
extern StaticSymbolData sym_sound_stop_all_data;
extern StaticSymbolData sym_sound_set_track_volume_data;
extern StaticSymbolData sym_sound_set_music_volume_data;
extern StaticSymbolData sym_sound_on_finished_data;

// Extended Symbol for Special Forms with embedded evaluation function
typedef struct CljSpecialSymbol {
    CljSymbol base;                          // Inherits from CljSymbol (must be first member)
    SpecialFormEvalFn_Placeholder eval_fn;   // Direct function pointer for O(1) dispatch
} CljSpecialSymbol;

// Type-safe casting to CljSpecialSymbol
static inline CljSpecialSymbol* as_special_symbol(ID obj) {
    if (!obj || TAG(obj) != CLJ_SYMBOL) return NULL;
    CljSymbol *sym = (CljSymbol*)obj;
    if (!is_special_symbol(sym)) return NULL;
    return (CljSpecialSymbol*)sym;
}

// Extern declarations for Special Symbols (used in other files)
typedef struct { CljSpecialSymbol sym; } SpecialSymbolData;
extern SpecialSymbolData sym_do_data;

#endif // TINY_CLJ_SYMBOLS_H
