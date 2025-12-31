#include "symbol.h"
#include "object.h"
#include "runtime.h"
#include "value.h"
#include "exception.h"
#include "namespace.h"
#include "types.h"  // For SINGLETON_RC
#include "memory.h" // For ASSIGN
#include "vector.h"  // For vector operations
#include <subjective-c/hashmap.h> // For HashMap symbol table (O(1) lookup)
#include "symbol_token.h"  // For CljSymbolToken
#include "common.h"  // For CLJ_ASSERT
#include "strings.h"  // For string_data()
#include "eval.h"  // For SpecialFormEvalFn type
#include "eval_special_forms.h"  // For eval_special_* functions
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>  // For snprintf
#include <assert.h>

// Note: Symbols have SINGLETON_RC and are never released

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
CljSymbol *SYM_FOR = NULL;
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

// Global symbols for namespace names (for fast comparison)
CljSymbol *SYM_CLOJURE_CORE = NULL;
CljSymbol *SYM_CLOJURE_STRING = NULL;
CljSymbol *SYM_CLOJURE_REPL = NULL;
CljSymbol *SYM_CLOJURE_LANG = NULL;
CljSymbol *SYM_TINYCLJ = NULL;

// tinyclj namespace function symbols
CljSymbol *SYM_RETAIN_COUNT = NULL;

// Additional symbols for hot path optimization
CljSymbol *SYM_NS_STAR = NULL;

// Macro to reduce boilerplate for static symbol declarations (DRY principle)
// Note: For symbols that need to be extern (used in other files), use DEFINE_EXTERN_SYMBOL instead
#define DEFINE_STATIC_SYMBOL(var_name, symbol_name) \
    static struct { CljSymbol sym; } var_name = { \
        .sym = { .base = { .type = CLJ_SYMBOL, .rc = SINGLETON_RC }, .ns_name = NULL, .cname = symbol_name } \
    }

// Macro for Special Symbols (with space for function pointer)
#define DEFINE_STATIC_SPECIAL_SYMBOL(var_name, symbol_name) \
    static struct { CljSpecialSymbol sym; } var_name = { \
        .sym = { .base = { .base = { .type = CLJ_SYMBOL, .rc = SINGLETON_RC, .flags = CLJ_FLAG_SPECIAL }, .ns_name = NULL, .cname = symbol_name }, .eval_fn = NULL } \
    }

// Macro for extern Special Symbols (used in other files)
#define DEFINE_EXTERN_SPECIAL_SYMBOL(var_name, symbol_name) \
    SpecialSymbolData var_name = { \
        .sym = { .base = { .base = { .type = CLJ_SYMBOL, .rc = SINGLETON_RC, .flags = CLJ_FLAG_SPECIAL }, .ns_name = NULL, .cname = symbol_name }, .eval_fn = NULL } \
    }

// Macro for non-static (extern) symbols that are statically initialized (compile-time, not dynamically allocated)
// These are native/builtin functions, so they get CLJ_FLAG_NATIVE for fast macro-skip in ast_canon
#define DEFINE_EXTERN_SYMBOL(var_name, symbol_name) \
    extern StaticSymbolData var_name; \
    StaticSymbolData var_name = { \
        .sym = { .base = { .type = CLJ_SYMBOL, .rc = SINGLETON_RC, .flags = CLJ_FLAG_NATIVE }, .ns_name = NULL, .cname = symbol_name } \
    }

// Static symbol structs for special forms (compile-time initialization)
// These symbols have rc = SINGLETON_RC and use string literals (no strdup needed)
// Special Forms use CljSpecialSymbol to store function pointer
DEFINE_STATIC_SPECIAL_SYMBOL(sym_try_data, "try");
DEFINE_STATIC_SPECIAL_SYMBOL(sym_catch_data, "catch");
DEFINE_STATIC_SPECIAL_SYMBOL(sym_if_data, "if");
DEFINE_STATIC_SPECIAL_SYMBOL(sym_cond_data, "cond");
DEFINE_STATIC_SPECIAL_SYMBOL(sym_when_data, "when");
DEFINE_STATIC_SPECIAL_SYMBOL(sym_while_data, "while");
DEFINE_STATIC_SPECIAL_SYMBOL(sym_let_data, "let");
DEFINE_STATIC_SPECIAL_SYMBOL(sym_fn_data, "fn");
DEFINE_STATIC_SPECIAL_SYMBOL(sym_def_data, "def");
DEFINE_STATIC_SYMBOL(sym_defn_data, "defn");  // macro, not special form
DEFINE_STATIC_SPECIAL_SYMBOL(sym_defmacro_data, "defmacro");
DEFINE_EXTERN_SYMBOL(sym_deref_data, "deref");
DEFINE_STATIC_SYMBOL(sym_nil_data, "nil");
DEFINE_STATIC_SYMBOL(sym_amp_data, "&");  // variadic parameter marker
DEFINE_STATIC_SPECIAL_SYMBOL(sym_quote_data, "quote");
DEFINE_STATIC_SPECIAL_SYMBOL(sym_quasiquote_data, "quasiquote");
DEFINE_STATIC_SPECIAL_SYMBOL(sym_unquote_data, "unquote");
DEFINE_STATIC_SPECIAL_SYMBOL(sym_unquote_splice_data, "unquote-splice");
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
DEFINE_STATIC_SPECIAL_SYMBOL(sym_loop_data, "loop");
DEFINE_STATIC_SPECIAL_SYMBOL(sym_recur_data, "recur");
DEFINE_STATIC_SPECIAL_SYMBOL(sym_throw_data, "throw");
DEFINE_STATIC_SPECIAL_SYMBOL(sym_finally_data, "finally");
DEFINE_STATIC_SPECIAL_SYMBOL(sym_var_data, "var");
DEFINE_STATIC_SPECIAL_SYMBOL(sym_ns_data, "ns");
DEFINE_STATIC_SPECIAL_SYMBOL(sym_binding_data, "binding");
DEFINE_STATIC_SPECIAL_SYMBOL(sym_time_data, "time");
DEFINE_STATIC_SPECIAL_SYMBOL(sym_go_data, "go");

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
DEFINE_EXTERN_SYMBOL(sym_nth_data, "nth");
DEFINE_EXTERN_SYMBOL(sym_first_data, "first");
DEFINE_EXTERN_SYMBOL(sym_rest_data, "rest");
DEFINE_EXTERN_SYMBOL(sym_concat_data, "concat");
DEFINE_EXTERN_SYMBOL(sym_concat2_data, "concat2");
DEFINE_EXTERN_SYMBOL(sym_count_data, "count");

// Extern symbol structs for native functions (compile-time initialization, statically allocated)
// These are extern so they can be used in builtins.c's native function table
DEFINE_EXTERN_SYMBOL(sym_cons_data, "cons");
DEFINE_EXTERN_SYMBOL(sym_seq_data, "seq");
DEFINE_EXTERN_SYMBOL(sym_next_data, "next");
DEFINE_EXTERN_SYMBOL(sym_nnext_data, "nnext");
DEFINE_EXTERN_SYMBOL(sym_nthnext_data, "nthnext");
DEFINE_EXTERN_SYMBOL(sym_gensym_data, "gensym");
DEFINE_EXTERN_SYMBOL(sym_partition_data, "partition");
DEFINE_EXTERN_SYMBOL(sym_some_data, "some");
DEFINE_EXTERN_SYMBOL(sym_list_data, "list");
DEFINE_STATIC_SPECIAL_SYMBOL(sym_and_data, "and");
DEFINE_STATIC_SPECIAL_SYMBOL(sym_or_data, "or");
DEFINE_STATIC_SYMBOL(sym_for_data, "for");
DEFINE_STATIC_SYMBOL(sym_doseq_data, "doseq");
DEFINE_STATIC_SYMBOL(sym_dotimes_data, "dotimes");

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
DEFINE_EXTERN_SYMBOL(sym_char_p_data, "char?");
DEFINE_EXTERN_SYMBOL(sym_list_p_data, "list?");
DEFINE_EXTERN_SYMBOL(sym_sleep_data, "sleep");
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

// Additional symbols for optimization (used in hot path)
DEFINE_STATIC_SYMBOL(sym_ns_star_data, "*ns*");

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
    // Special forms - static structs with symbol table registration
    // Names are already set to string literals in static initialization (no strdup needed)
    // Special Forms use CljSpecialSymbol (already has CLJ_FLAG_SPECIAL set in DEFINE_STATIC_SPECIAL_SYMBOL)
    INIT_SPECIAL_SYMBOL(SYM_TRY, sym_try_data);
    INIT_SPECIAL_SYMBOL(SYM_CATCH, sym_catch_data);
    INIT_SPECIAL_SYMBOL(SYM_IF, sym_if_data);
    INIT_SPECIAL_SYMBOL(SYM_COND, sym_cond_data);
    INIT_SPECIAL_SYMBOL(SYM_WHEN, sym_when_data);
    INIT_SPECIAL_SYMBOL(SYM_WHILE, sym_while_data);
    INIT_SPECIAL_SYMBOL(SYM_LET, sym_let_data);
    INIT_SPECIAL_SYMBOL(SYM_FN, sym_fn_data);
    INIT_SPECIAL_SYMBOL(SYM_QUOTE, sym_quote_data);
    INIT_SPECIAL_SYMBOL(SYM_QUASIQUOTE, sym_quasiquote_data);
    INIT_SPECIAL_SYMBOL(SYM_UNQUOTE, sym_unquote_data);
    INIT_SPECIAL_SYMBOL(SYM_UNQUOTE_SPLICE, sym_unquote_splice_data);
    INIT_SPECIAL_SYMBOL(SYM_DO, sym_do_data);
    INIT_SPECIAL_SYMBOL(SYM_LOOP, sym_loop_data);
    INIT_SPECIAL_SYMBOL(SYM_RECUR, sym_recur_data);
    INIT_SPECIAL_SYMBOL(SYM_THROW, sym_throw_data);
    INIT_SPECIAL_SYMBOL(SYM_FINALLY, sym_finally_data);
    INIT_SYMBOL(SYM_DEFN, sym_defn_data);
    INIT_SPECIAL_SYMBOL(SYM_DEFMACRO, sym_defmacro_data);
    INIT_SYMBOL(SYM_DEREF, sym_deref_data);
    INIT_SYMBOL(SYM_NIL, sym_nil_data);
    INIT_SYMBOL(SYM_AMP, sym_amp_data);
    INIT_SPECIAL_SYMBOL(SYM_VAR, sym_var_data);
    // Built-in functions - static structs with symbol table registration
    INIT_SPECIAL_SYMBOL(SYM_DEF, sym_def_data);
    INIT_SPECIAL_SYMBOL(SYM_NS, sym_ns_data);
    INIT_SPECIAL_SYMBOL(SYM_BINDING, sym_binding_data);
    INIT_SPECIAL_SYMBOL(SYM_TIME, sym_time_data);
    INIT_SPECIAL_SYMBOL(SYM_GO, sym_go_data);

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

    // Initialize clojure.string namespace symbol first (needed for clojure.string functions)
    SYM_CLOJURE_STRING = intern_symbol_global("clojure.string");
    
    // Initialize clojure.repl namespace symbol (needed for REPL helper functions)
    SYM_CLOJURE_REPL = intern_symbol_global("clojure.repl");

    // Global symbols for namespace names (for fast comparison)
    // Use intern_symbol_global to ensure same symbol is returned by intern_symbol
    SYM_CLOJURE_CORE = intern_symbol_global("clojure.core");
    SYM_CLOJURE_LANG = intern_symbol_global("clojure.lang");
    
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
    // Initialize tinyclj namespace symbol
    SYM_TINYCLJ = intern_symbol_global("tinyclj");
    
    // tinyclj native function symbol for retain-count
    INIT_SYMBOL_NS(SYM_RETAIN_COUNT, sym_retain_count_data, SYM_TINYCLJ);
    
    // clojure.core sqrt native function symbol
    INIT_SYMBOL_NS(SYM_SQRT_NATIVE, sym_sqrt_data, SYM_CLOJURE_CORE);

    // Additional symbols - static structs with symbol table registration
    INIT_SYMBOL(SYM_CONS, sym_cons_data);

    INIT_SYMBOL(SYM_SEQ, sym_seq_data);

    INIT_SYMBOL(SYM_NEXT, sym_next_data);

    INIT_SYMBOL(SYM_LIST, sym_list_data);

    // Note: SYM_AND and SYM_OR are Special Forms but use regular CljSymbol
    // They will be handled via eval_special_form_dispatch fallback
    INIT_SPECIAL_SYMBOL(SYM_AND, sym_and_data);
    INIT_SPECIAL_SYMBOL(SYM_OR, sym_or_data);

    INIT_SYMBOL(SYM_FOR, sym_for_data);

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

    // Additional symbols for hot path optimization
    INIT_SYMBOL(SYM_NS_STAR, sym_ns_star_data);

    // Clean up macros to avoid namespace pollution
    #undef INIT_SYMBOL
    #undef INIT_SYMBOL_NS
    #undef INIT_SPECIAL_SYMBOL

    // Set function pointers for Special Forms (O(1) dispatch optimization)
    // Cast to CljSpecialSymbol and set eval_fn pointer (with type cast for compatibility)
    // Note: Using void* cast to bridge between placeholder type in symbol.h and real type in eval.h
    if (SYM_IF && (SYM_IF->base.flags & CLJ_FLAG_SPECIAL)) {
        ((CljSpecialSymbol*)SYM_IF)->eval_fn = (SpecialFormEvalFn_Placeholder)(SpecialFormEvalFn)eval_special_if;
    }
    if (SYM_TRY && (SYM_TRY->base.flags & CLJ_FLAG_SPECIAL)) {
        ((CljSpecialSymbol*)SYM_TRY)->eval_fn = (SpecialFormEvalFn_Placeholder)(SpecialFormEvalFn)eval_special_try;
    }
    if (SYM_WHEN && (SYM_WHEN->base.flags & CLJ_FLAG_SPECIAL)) {
        ((CljSpecialSymbol*)SYM_WHEN)->eval_fn = (SpecialFormEvalFn_Placeholder)(SpecialFormEvalFn)eval_special_when;
    }
    if (SYM_WHILE && (SYM_WHILE->base.flags & CLJ_FLAG_SPECIAL)) {
        ((CljSpecialSymbol*)SYM_WHILE)->eval_fn = (SpecialFormEvalFn_Placeholder)(SpecialFormEvalFn)eval_special_while;
    }
    if (SYM_COND && (SYM_COND->base.flags & CLJ_FLAG_SPECIAL)) {
        ((CljSpecialSymbol*)SYM_COND)->eval_fn = (SpecialFormEvalFn_Placeholder)(SpecialFormEvalFn)eval_special_cond;
    }
    if (SYM_DO && (SYM_DO->base.flags & CLJ_FLAG_SPECIAL)) {
        ((CljSpecialSymbol*)SYM_DO)->eval_fn = (SpecialFormEvalFn_Placeholder)(SpecialFormEvalFn)eval_special_do;
    }
    if (SYM_AND && (SYM_AND->base.flags & CLJ_FLAG_SPECIAL)) {
        ((CljSpecialSymbol*)SYM_AND)->eval_fn = (SpecialFormEvalFn_Placeholder)(SpecialFormEvalFn)eval_special_and;
    }
    if (SYM_OR && (SYM_OR->base.flags & CLJ_FLAG_SPECIAL)) {
        ((CljSpecialSymbol*)SYM_OR)->eval_fn = (SpecialFormEvalFn_Placeholder)(SpecialFormEvalFn)eval_special_or;
    }
    if (SYM_FN && (SYM_FN->base.flags & CLJ_FLAG_SPECIAL)) {
        ((CljSpecialSymbol*)SYM_FN)->eval_fn = (SpecialFormEvalFn_Placeholder)(SpecialFormEvalFn)eval_special_fn;
    }
    if (SYM_LET && (SYM_LET->base.flags & CLJ_FLAG_SPECIAL)) {
        ((CljSpecialSymbol*)SYM_LET)->eval_fn = (SpecialFormEvalFn_Placeholder)(SpecialFormEvalFn)eval_special_let;
    }
    if (SYM_VAR && (SYM_VAR->base.flags & CLJ_FLAG_SPECIAL)) {
        ((CljSpecialSymbol*)SYM_VAR)->eval_fn = (SpecialFormEvalFn_Placeholder)(SpecialFormEvalFn)eval_special_var;
    }
    if (SYM_QUOTE && (SYM_QUOTE->base.flags & CLJ_FLAG_SPECIAL)) {
        ((CljSpecialSymbol*)SYM_QUOTE)->eval_fn = (SpecialFormEvalFn_Placeholder)(SpecialFormEvalFn)eval_special_quote;
    }
    if (SYM_RECUR && (SYM_RECUR->base.flags & CLJ_FLAG_SPECIAL)) {
        ((CljSpecialSymbol*)SYM_RECUR)->eval_fn = (SpecialFormEvalFn_Placeholder)(SpecialFormEvalFn)eval_special_recur;
    }
    if (SYM_GO && (SYM_GO->base.flags & CLJ_FLAG_SPECIAL)) {
        ((CljSpecialSymbol*)SYM_GO)->eval_fn = (SpecialFormEvalFn_Placeholder)(SpecialFormEvalFn)eval_special_go;
    }
    if (SYM_TIME && (SYM_TIME->base.flags & CLJ_FLAG_SPECIAL)) {
        ((CljSpecialSymbol*)SYM_TIME)->eval_fn = (SpecialFormEvalFn_Placeholder)(SpecialFormEvalFn)eval_special_time;
    }
    if (SYM_BINDING && (SYM_BINDING->base.flags & CLJ_FLAG_SPECIAL)) {
        ((CljSpecialSymbol*)SYM_BINDING)->eval_fn = (SpecialFormEvalFn_Placeholder)(SpecialFormEvalFn)eval_special_binding;
    }
    // Note: SYM_DOTIMES is handled inline in eval.c, not as Special Form
    
    // Quasiquote Special Form - delegates to Clojure quasiquote-fn after bootstrap
    if (SYM_QUASIQUOTE && (SYM_QUASIQUOTE->base.flags & CLJ_FLAG_SPECIAL)) {
        ((CljSpecialSymbol*)SYM_QUASIQUOTE)->eval_fn = (SpecialFormEvalFn_Placeholder)(SpecialFormEvalFn)eval_special_quasiquote;
    }
    
    // defmacro Special Form - defines macros in the current namespace
    if (SYM_DEFMACRO && (SYM_DEFMACRO->base.flags & CLJ_FLAG_SPECIAL)) {
        ((CljSpecialSymbol*)SYM_DEFMACRO)->eval_fn = (SpecialFormEvalFn_Placeholder)(SpecialFormEvalFn)eval_special_defmacro;
    }
    
    // destructure is a Clojure function, not a special form
    SYM_DESTRUCTURE = intern_symbol_global("destructure");
}

// Static CljString for temporary lookups (avoids allocations)
// NOTE: CljString uses flexible array member (char data[]), so we need to manually
// create a structure with inline buffer
static struct {
    CljObject base;
    uint16_t length;
    char data[512];
} g_lookup_string = {
    .base = { .type = CLJ_STRING, .rc = SINGLETON_RC },
    .length = 0
};

// Helper: Create symbol table key from namespace and name
// Format: "ns/name" or just "name" for global symbols
// Returns CljString* for HashMap lookup (uses static buffer)
static CljString* make_symbol_key(CljSymbol *ns_name, const char *cname) {
    if (ns_name && ns_name->cname) {
        snprintf(g_lookup_string.data, sizeof(g_lookup_string.data), "%s/%s", ns_name->cname, cname);
    } else {
        snprintf(g_lookup_string.data, sizeof(g_lookup_string.data), "%s", cname);
    }
    g_lookup_string.length = (uint16_t)strlen(g_lookup_string.data);
    return (CljString*)&g_lookup_string;
}

// Find symbol in the table - O(1) HashMap lookup
static CljSymbol* symbol_table_find(CljSymbol *ns_name, const char *cname) {
    if (!cname || !g_runtime.symbol_table) return NULL;
    
    CljString *key = make_symbol_key(ns_name, cname);
    return (CljSymbol*)hashmap_get(g_runtime.symbol_table, key, NULL);
}

// Add symbol to the table - O(1) HashMap insert
void symbol_table_add(CljSymbol *symbol) {
    if (!symbol || !symbol->cname) return;

    // Extract namespace and name from symbol
    CljSymbol *ns_name = symbol->ns_name;
    const char *cname = symbol->cname;

    if (!g_runtime.symbol_table) {
        g_runtime.symbol_table = make_hashmap(512);  // 512 = good initial capacity for Linear Probing
    }

    // Use static lookup string to check if already exists
    CljString *lookup_key = make_symbol_key(ns_name, cname);
    if (hashmap_contains(g_runtime.symbol_table, lookup_key)) {
        return;  // Already exists
    }

    // Note: RC may be > 1 due to autorelease pool, COW handles this correctly

    // Create a real CljString key for HashMap storage
    // We need to create a new string because the HashMap will retain it
    CljString *key = make_clj_string(string_data(lookup_key));
    
    // Insert symbol into HashMap with CljString key
    // NOTE: The HashMap will RETAIN the key, so we can RELEASE our reference
    hashmap_assoc_inplace(&g_runtime.symbol_table, key, symbol);
    RELEASE(key);
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
        return throw_exception_formatted(EXCEPTION_ILLEGAL_ARGUMENT, __FILE__, __LINE__, 0,
                "make_symbol: name cannot be NULL");
    }

    // Assertion: name must not be empty
    CLJ_ASSERT(cname[0] != '\0' && "make_symbol: name cannot be empty");

    // Range check for name length (keep for safety)
    if (strlen(cname) >= SYMBOL_NAME_MAX_LEN) {
        return throw_exception_formatted(EXCEPTION_ILLEGAL_ARGUMENT, __FILE__, __LINE__, 0,
                "Symbol name '%s' exceeds maximum length of %d characters",
                cname, SYMBOL_NAME_MAX_LEN - 1);
    }

    // Use malloc directly instead of ALLOC macro
    CljSymbol *sym = (CljSymbol*)malloc(sizeof(CljSymbol));
    if (!sym) {
        return throw_exception_formatted(EXCEPTION_OUT_OF_MEMORY, __FILE__, __LINE__, 0,
                "Failed to allocate memory for symbol '%s'", cname);
    }

    sym->base.type = CLJ_SYMBOL;
    sym->base.rc = SINGLETON_RC;  // Interned symbols are singletons - never freed
    sym->base.flags = 0;  // Initialize flags to 0 (no special flags by default)

    // Store strdup'd name for heap-allocated symbols
    sym->cname = strdup(cname);
    if (!sym->cname) {
        free(sym);
        return throw_exception_formatted(EXCEPTION_OUT_OF_MEMORY, __FILE__, __LINE__, 0,
                "Failed to duplicate string for symbol '%s'", cname);
    }

    // Enforce invariant: symbols always have a name
    CLJ_ASSERT(sym->cname != NULL && "Symbol must have a name after creation");

    // Use ns_name directly (already a CljSymbol*)
    sym->ns_name = ns_name;
    sym->unqualified = NULL;

    return sym;
}

// Actual symbol interning
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

// Global symbols (without namespace)
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
