#ifndef TINY_CLJ_SYMBOLS_H
#define TINY_CLJ_SYMBOLS_H

#include "object.h"
#include <stdbool.h>

// Forward declaration for CljSymbol (needed for self-reference)
typedef struct CljSymbol CljSymbol;

// CljSymbol struct definition
#define SYMBOL_NAME_MAX_LEN 64

struct CljSymbol {
    CljObject base;
    struct CljSymbol *ns_name;  // Namespace name symbol (Clojure-compatible: Symbol->ns_name is a Symbol, not Namespace object)
    const char *cname;
};

// Type-safe casting
static inline CljSymbol* as_symbol(ID obj) {
    return (CljSymbol*)assert_type((CljObject*)obj, CLJ_SYMBOL);
}

// Check if an object is a keyword (symbol starting with ':')
static inline bool is_keyword(ID obj) {
    if (!obj || TAG(obj) != CLJ_SYMBOL) return false;
    CljSymbol *sym = as_symbol(obj);
    return sym->cname && sym->cname[0] == ':';
}
#define IS_KEYWORD(obj) is_keyword(obj)

// Globale Symbol-Pointer für Spezialformen
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
extern CljSymbol *SYM_VAR;
extern CljSymbol *SYM_QUOTE;
extern CljSymbol *SYM_QUASIQUOTE;
extern CljSymbol *SYM_UNQUOTE;
extern CljSymbol *SYM_SPLICE_UNQUOTE;
extern CljSymbol *SYM_SOURCE;
extern CljSymbol *SYM_DO;
extern CljSymbol *SYM_LOOP;
extern CljSymbol *SYM_RECUR;
extern CljSymbol *SYM_THROW;
extern CljSymbol *SYM_FINALLY;
extern CljSymbol *SYM_NS;
extern CljSymbol *SYM_GO;
extern CljSymbol *SYM_TIME;
extern CljSymbol *SYM_DEREF;
extern CljSymbol *SYM_NIL;

// Globale Symbol-Pointer für Builtin-Funktionen
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
extern CljSymbol *SYM_FOR;
extern CljSymbol *SYM_DOSEQ;
extern CljSymbol *SYM_DOTIMES;

// Globale Symbol-Pointer für Keywords
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

// Global symbols for namespace names (for fast comparison)
extern CljSymbol *SYM_CLOJURE_CORE;
extern CljSymbol *SYM_CLOJURE_STRING;
extern CljSymbol *SYM_CLOJURE_REPL;
extern CljSymbol *SYM_CLOJURE_LANG;

// Additional symbols for hot path optimization
extern CljSymbol *SYM_NS_STAR;

// make_symbol is internal - use intern_symbol instead for proper symbol interning
CljSymbol* intern_symbol(CljSymbol *ns_name, const char *cname);
CljSymbol* intern_symbol_global(const char *cname);  // Without namespace
void symbol_table_add(CljSymbol *symbol);
void symbol_table_cleanup();

// Helper functions for namespace lookup (DRY principle)
// Forward declaration for CljNamespace
struct CljNamespace;
struct CljNamespace* symbol_get_namespace(CljSymbol *sym);  // Get namespace object from symbol's namespace name
const char* symbol_get_namespace_name(CljSymbol *sym);  // Get namespace name string from symbol

// Initialisierung der globalen Symbole
void init_special_symbols();

// Named struct type for static symbol data (for compile-time initialization)
typedef struct StaticSymbolData {
    CljSymbol sym;
} StaticSymbolData;

// Static symbol data structures for compile-time initialization
// These are exposed for use in native function lookup tables
extern StaticSymbolData sym_trim_data;
extern StaticSymbolData sym_upper_case_data;
extern StaticSymbolData sym_lower_case_data;
extern StaticSymbolData sym_last_index_of_data;
extern StaticSymbolData sym_string_reverse_data;
extern StaticSymbolData sym_source_data;
extern StaticSymbolData sym_dir_data;
extern StaticSymbolData sym_meta_data;
extern StaticSymbolData sym_reduce_data;
extern StaticSymbolData sym_do_data;
extern StaticSymbolData sym_plus_data;
extern StaticSymbolData sym_minus_data;
extern StaticSymbolData sym_multiply_data;
extern StaticSymbolData sym_divide_data;
extern StaticSymbolData sym_equals_data;
extern StaticSymbolData sym_lt_data;
extern StaticSymbolData sym_gt_data;
extern StaticSymbolData sym_le_data;
extern StaticSymbolData sym_ge_data;
extern StaticSymbolData sym_println_data;
extern StaticSymbolData sym_print_data;
extern StaticSymbolData sym_str_data;
extern StaticSymbolData sym_conj_data;
extern StaticSymbolData sym_nth_data;
extern StaticSymbolData sym_first_data;
extern StaticSymbolData sym_rest_data;
extern StaticSymbolData sym_count_data;
extern StaticSymbolData sym_cons_data;
extern StaticSymbolData sym_next_data;
extern StaticSymbolData sym_list_data;
extern StaticSymbolData sym_mod_data;
extern StaticSymbolData sym_quot_data;
extern StaticSymbolData sym_bit_shift_left_data;
extern StaticSymbolData sym_range_data;
extern StaticSymbolData sym_repeat_data;
extern StaticSymbolData sym_math_sqrt_data;
extern StaticSymbolData sym_sqrt_data;
extern StaticSymbolData sym_format_data;
extern StaticSymbolData sym_subs_data;
extern StaticSymbolData sym_symbol_data;
extern StaticSymbolData sym_type_data;
extern StaticSymbolData sym_array_map_data;
extern StaticSymbolData sym_vector_data;
extern StaticSymbolData sym_vec_data;
extern StaticSymbolData sym_peek_data;
extern StaticSymbolData sym_pop_data;
extern StaticSymbolData sym_subvec_data;
extern StaticSymbolData sym_reverse_data;
extern StaticSymbolData sym_assoc_data;
extern StaticSymbolData sym_dissoc_data;
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
extern StaticSymbolData sym_sleep_data;
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
extern StaticSymbolData sym_schedule_data;
extern StaticSymbolData sym_schedule_periodic_data;
extern StaticSymbolData sym_cancel_timer_data;
extern StaticSymbolData sym_atom_data;
extern StaticSymbolData sym_deref_data;
extern StaticSymbolData sym_reset_bang_data;
extern StaticSymbolData sym_swap_bang_data;
#ifndef ESP32_BUILD
extern StaticSymbolData sym_slurp_data;
extern StaticSymbolData sym_spit_data;
#endif

#endif
