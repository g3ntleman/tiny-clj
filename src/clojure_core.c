#include "tiny_clj.h"
#include "clj_parser.h"
#include "namespace.h"
#include "map.h"
#include "function_call.h"
#include <stdio.h>
#include <stdlib.h>

// Helper function for count
CljObject* eval_count_simple(CljObject *arg) {
    if (!arg) return make_int(0);
    
    if (arg->type == CLJ_NIL) {
        return make_int(0); // nil has count 0
    }
    
    if (arg->type == CLJ_VECTOR) {
    CljPersistentVector *vec = as_vector(arg);
        return vec ? make_int(vec->count) : make_int(0);
    }
    
    if (arg->type == CLJ_LIST) {
        CljList *list_data = as_list(arg);
        if (!list_data) return make_int(0);
        
        int count = 0;
        CljObject *current = list_data->head;
        while (current) {
            count++;
            // For now, just count the head, don't traverse the list structure
            break;
        }
        return make_int(count);
    }
    
    if (arg->type == CLJ_MAP) {
        CljMap *map = as_map(arg);
        return map ? make_int(map->count) : make_int(0);
    }
    
    if (arg->type == CLJ_STRING) {
        return make_int(strlen((char*)arg->as.data));
    }
    
    return make_int(1); // Single value (int, bool, symbol, etc.)
}

const char* clojure_core_code =
R"CLOJURE(
;; Simple arithmetic functions
(def inc (fn [x] (+ x 1)))
(def dec (fn [x] (- x 1)))
(def add (fn [a b] (+ a b)))
(def sub (fn [a b] (- a b)))
(def mul (fn [a b] (* a b)))
(def div (fn [a b] (/ a b)))
(def square (fn [x] (* x x)))

;; Basic predicates
(def nil? (fn [x] (= x nil)))
(def true? (fn [x] (= x true)))
(def false? (fn [x] (= x false)))

;; Function helpers
(def identity (fn [x] x))
)CLOJURE";

// Global variable for the loaded namespace
static CljNamespace *clojure_core_ns = NULL;
static int g_core_quiet = 0; // ASCII-only banner; quiet when set

void clojure_core_set_quiet(int quiet) {
    g_core_quiet = quiet ? 1 : 0;
}

// Load and evaluate Clojure core functions
int load_clojure_core() {
    if (!g_core_quiet) printf("=== Loading Clojure Core Functions ===\n");
    
    // Create EvalState
    EvalState *st = evalstate_new();
    if (!st) {
        if (!g_core_quiet) printf("Error: Failed to create EvalState\n");
        return 0;
    }
    
    // Create clojure.core namespace
    clojure_core_ns = ns_get_or_create("clojure.core", "clojure_core.c");
    // Map capacity is now sufficient (64) from ns_get_or_create
    if (!clojure_core_ns) {
        if (!g_core_quiet) printf("Error: Failed to create clojure.core namespace\n");
        evalstate_free(st);
        return 0;
    }
    
    if (!g_core_quiet) printf("[OK] clojure.core namespace created\n");
    
    // Set namespace as current
    evalstate_set_ns(st, "clojure.core");
    if (!g_core_quiet) printf("[OK] Namespace set to clojure.core\n");
    
    // For now, manual implementation of functions in the namespace
    // Later we can use the parser
    
    if (!g_core_quiet) printf("Loading functions manually into namespace...\n");
    
    // Arithmetic functions
    CljObject *inc_sym = make_symbol("inc", NULL);
    CljObject *inc_func = make_function(NULL, 0, NULL, NULL, "inc");
    if (inc_sym && inc_func) {
        map_assoc(clojure_core_ns->mappings, inc_sym, inc_func);
        if (!g_core_quiet) printf("[OK] Added inc function\n");
    }
    
    CljObject *dec_sym = make_symbol("dec", NULL);
    CljObject *dec_func = make_function(NULL, 0, NULL, NULL, "dec");
    if (dec_sym && dec_func) {
        map_assoc(clojure_core_ns->mappings, dec_sym, dec_func);
        if (!g_core_quiet) printf("[OK] Added dec function\n");
    }
    
    CljObject *add_sym = make_symbol("add", NULL);
    CljObject *add_func = make_function(NULL, 0, NULL, NULL, "add");
    if (add_sym && add_func) {
        map_assoc(clojure_core_ns->mappings, add_sym, add_func);
        if (!g_core_quiet) printf("[OK] Added add function\n");
    }
    
    CljObject *sub_sym = make_symbol("sub", NULL);
    CljObject *sub_func = make_function(NULL, 0, NULL, NULL, "sub");
    if (sub_sym && sub_func) {
        map_assoc(clojure_core_ns->mappings, sub_sym, sub_func);
        if (!g_core_quiet) printf("[OK] Added sub function\n");
    }
    
    CljObject *mul_sym = make_symbol("mul", NULL);
    CljObject *mul_func = make_function(NULL, 0, NULL, NULL, "mul");
    if (mul_sym && mul_func) {
        map_assoc(clojure_core_ns->mappings, mul_sym, mul_func);
        if (!g_core_quiet) printf("[OK] Added mul function\n");
    }
    
    CljObject *div_sym = make_symbol("div", NULL);
    CljObject *div_func = make_function(NULL, 0, NULL, NULL, "div");
    if (div_sym && div_func) {
        map_assoc(clojure_core_ns->mappings, div_sym, div_func);
        if (!g_core_quiet) printf("[OK] Added div function\n");
    }
    
    CljObject *square_sym = make_symbol("square", NULL);
    CljObject *square_func = make_function(NULL, 0, NULL, NULL, "square");
    if (square_sym && square_func) {
        map_assoc(clojure_core_ns->mappings, square_sym, square_func);
        if (!g_core_quiet) printf("[OK] Added square function\n");
    }
    
    // Predicates
    CljObject *nil_sym = make_symbol("nil?", NULL);
    CljObject *nil_func = make_function(NULL, 0, NULL, NULL, "nil?");
    if (nil_sym && nil_func) {
        map_assoc(clojure_core_ns->mappings, nil_sym, nil_func);
        if (!g_core_quiet) printf("[OK] Added nil? function\n");
    }
    
    CljObject *true_sym = make_symbol("true?", NULL);
    CljObject *true_func = make_function(NULL, 0, NULL, NULL, "true?");
    if (true_sym && true_func) {
        map_assoc(clojure_core_ns->mappings, true_sym, true_func);
        if (!g_core_quiet) printf("[OK] Added true? function\n");
    }
    
    CljObject *false_sym = make_symbol("false?", NULL);
    CljObject *false_func = make_function(NULL, 0, NULL, NULL, "false?");
    if (false_sym && false_func) {
        map_assoc(clojure_core_ns->mappings, false_sym, false_func);
        if (!g_core_quiet) printf("[OK] Added false? function\n");
    }
    
    // Identity
    CljObject *identity_sym = make_symbol("identity", NULL);
    CljObject *identity_func = make_function(NULL, 0, NULL, NULL, "identity");
    if (identity_sym && identity_func) {
        map_assoc(clojure_core_ns->mappings, identity_sym, identity_func);
        if (!g_core_quiet) printf("[OK] Added identity function\n");
    }
    
    // Count
    CljObject *count_sym = make_symbol("count", NULL);
    CljObject *count_func = make_function(NULL, 0, NULL, NULL, "count");
    if (count_sym && count_func) {
        map_assoc(clojure_core_ns->mappings, count_sym, count_func);
        if (!g_core_quiet) printf("[OK] Added count function\n");
    }
    
    if (!g_core_quiet) printf("Successfully loaded Clojure Core functions into namespace\n");
    
    // Free EvalState
    evalstate_free(st);
    
    return 1;
}

// Call a Clojure core function
CljObject* call_clojure_core_function(const char *name, int argc, CljObject **argv) {
    if (!clojure_core_ns) {
        return make_error("Clojure Core not loaded", NULL, 0, 0);
    }
    
    // Direct implementation of Clojure core functions
    // Functions are stored in the namespace, but we use direct implementations
    
    if (strcmp(name, "inc") == 0) {
        if (argc != 1 || !argv[0] || argv[0]->type != CLJ_INT) {
            return make_error("inc requires one integer argument", NULL, 0, 0);
        }
        return make_int(argv[0]->as.i + 1);
    }
    
    if (strcmp(name, "dec") == 0) {
        if (argc != 1 || !argv[0] || argv[0]->type != CLJ_INT) {
            return make_error("dec requires one integer argument", NULL, 0, 0);
        }
        return make_int(argv[0]->as.i - 1);
    }
    
    if (strcmp(name, "add") == 0) {
        if (argc != 2 || !argv[0] || !argv[1] || 
            argv[0]->type != CLJ_INT || argv[1]->type != CLJ_INT) {
            return make_error("add requires two integer arguments", NULL, 0, 0);
        }
        return make_int(argv[0]->as.i + argv[1]->as.i);
    }
    
    if (strcmp(name, "sub") == 0) {
        if (argc != 2 || !argv[0] || !argv[1] || 
            argv[0]->type != CLJ_INT || argv[1]->type != CLJ_INT) {
            return make_error("sub requires two integer arguments", NULL, 0, 0);
        }
        return make_int(argv[0]->as.i - argv[1]->as.i);
    }
    
    if (strcmp(name, "mul") == 0) {
        if (argc != 2 || !argv[0] || !argv[1] || 
            argv[0]->type != CLJ_INT || argv[1]->type != CLJ_INT) {
            return make_error("mul requires two integer arguments", NULL, 0, 0);
        }
        return make_int(argv[0]->as.i * argv[1]->as.i);
    }
    
    if (strcmp(name, "div") == 0) {
        if (argc != 2 || !argv[0] || !argv[1] || 
            argv[0]->type != CLJ_INT || argv[1]->type != CLJ_INT) {
            return make_error("div requires two integer arguments", NULL, 0, 0);
        }
        if (argv[1]->as.i == 0) {
            return make_error("Division by zero", NULL, 0, 0);
        }
        return make_int(argv[0]->as.i / argv[1]->as.i);
    }
    
    if (strcmp(name, "square") == 0) {
        if (argc != 1 || !argv[0] || argv[0]->type != CLJ_INT) {
            return make_error("square requires one integer argument", NULL, 0, 0);
        }
        return make_int(argv[0]->as.i * argv[0]->as.i);
    }
    
    if (strcmp(name, "nil?") == 0) {
        if (argc != 1) {
            return make_error("nil? requires one argument", NULL, 0, 0);
        }
        return argv[0] == clj_nil() ? clj_true() : clj_false();
    }
    
    if (strcmp(name, "true?") == 0) {
        if (argc != 1) {
            return make_error("true? requires one argument", NULL, 0, 0);
        }
        return argv[0] == clj_true() ? clj_true() : clj_false();
    }
    
    if (strcmp(name, "false?") == 0) {
        if (argc != 1) {
            return make_error("false? requires one argument", NULL, 0, 0);
        }
        return argv[0] == clj_false() ? clj_true() : clj_false();
    }
    
    if (strcmp(name, "identity") == 0) {
        if (argc != 1) {
            return make_error("identity requires one argument", NULL, 0, 0);
        }
        return argv[0];
    }
    
    if (strcmp(name, "count") == 0) {
        if (argc != 1) {
            return make_error("count requires one argument", NULL, 0, 0);
        }
        return eval_count_simple(argv[0]);
    }
    
    return make_error("Function not found in clojure.core", NULL, 0, 0);
}

// Get clojure.core namespace
CljNamespace* get_clojure_core_namespace() {
    return clojure_core_ns;
}

// Clojure core cleanup
void cleanup_clojure_core() {
    if (clojure_core_ns) {
        // Namespace will be freed automatically by ns_cleanup()
        clojure_core_ns = NULL;
    }
}
