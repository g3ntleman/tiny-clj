// clojure.core.c

#include <subjective-c/exception.h>
#include "symbol.h"  // Must be included before namespace.h for CljSymbol definition
#include "namespace.h"
#include "tiny_clj.h"
#include "reader.h"
#include <subjective-c/value.h>  // For IS_IMMEDIATE macro
#include "runtime.h" // For g_runtime
#include "list.h"    // For LIST_FIRST
#include "eval.h"  // For SYM_DEF, SYM_NS
#include <subjective-c/map.h>     // For map_get
#include "parser.h"  // For eval_parsed
#include "to_string.h" // For pr_str debug printing
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <signal.h>

// Crash diagnostics: last top-level form index being processed while loading clojure.core.
// Used by crash handlers and optional debug logging.
volatile sig_atomic_t g_clojure_core_last_form = 0;

static bool g_core_quiet = false;

static int getenv_int(const char *name, int default_value) {
  const char *v = getenv(name);
  if (!v || !v[0]) return default_value;
  char *end = NULL;
  long n = strtol(v, &end, 10);
  if (end == v) return default_value;
  return (int)n;
}


// Clojure core code for Tiny-Clj interpreter
const char *clojure_core_code =

#include "clojure.core.clj"

    ;

// Forward declaration for value_by_parsing_expr
extern CljValue value_by_parsing_expr(Reader *reader, EvalState *st);

// Helper: Read entire text file into memory (caller frees)
static char* read_file_cstr_local(const char *path) {
  if (!path) return NULL;
  FILE *fp = fopen(path, "r");
  if (!fp) return NULL;
  if (fseek(fp, 0, SEEK_END) != 0) {
    fclose(fp);
    return NULL;
  }
  long sz = ftell(fp);
  if (sz < 0) {
    fclose(fp);
    return NULL;
  }
  if (fseek(fp, 0, SEEK_SET) != 0) {
    fclose(fp);
    return NULL;
  }
  char *buffer = (char*)malloc((size_t)sz + 1);
  if (!buffer) {
    fclose(fp);
    return NULL;
  }
  size_t read_sz = fread(buffer, 1, (size_t)sz, fp);
  buffer[read_sz] = '\0';
  fclose(fp);
  return buffer;
}

#ifdef PROFILE_STARTUP
#include <time.h>
static double g_parse_time_ms = 0;
static double g_eval_time_ms = 0;
static int g_form_count = 0;
double g_canon_time_ms = 0;  // extern in parser.c
#endif

static bool eval_core_source(const char *src, const char *source_name, EvalState *st) {
  if (!src || !st)
    return false;
  
  // CRITICAL: Save the original namespace object (not just the name)
  // This ensures we use the same namespace object that was cached
  (void)st;  // original_ns is saved but not used in this function
  
  // CRITICAL: Use the namespace from registry if it exists, otherwise use current_ns
  // This ensures we use the same namespace object that register_builtins() may have created
  // Note: For clojure.repl, we use st->current_ns which is already set to clojure.repl
  CljNamespace *target_ns = NULL;
  // Only use clojure.core from registry if current_ns is clojure.core
  // Otherwise, use current_ns (which is already set correctly for clojure.repl)
  if (st->current_ns && st->current_ns->name == SYM_CLOJURE_CORE) {
    target_ns = ns_find_by_symbol(SYM_CLOJURE_CORE);
    if (target_ns) {
      st->current_ns = target_ns;
    } else {
      target_ns = st->current_ns;
    }
  } else {
    // For other namespaces (like clojure.repl), use current_ns directly
    target_ns = st->current_ns;
  }
  
  // Use Reader to parse multiple expressions
  Reader reader;
  reader_init(&reader, src);
  const char *label = source_name;
  if (!label || !label[0]) {
    if (st && st->current_ns && st->current_ns->name && st->current_ns->name->cname) {
      label = st->current_ns->name->cname;
    } else {
      label = "<core>";
    }
  }
  reader_set_source_name(&reader, label);
  
  int expr_count = 0;
  int success_count = 0;

  // Debug controls for pinpointing bad core forms.
  // - TINYCLJ_DEBUG_CORE_FORM=N prints pr_str(form) for form N.
  // - TINYCLJ_DEBUG_CORE_STOP_AFTER=N returns after completing form N (useful to avoid crashes).
  const int debug_form = getenv_int("TINYCLJ_DEBUG_CORE_FORM", 0);
  const int stop_after_form = getenv_int("TINYCLJ_DEBUG_CORE_STOP_AFTER", 0);
  
  // Wrap entire parsing loop in TRY/CATCH to catch any unhandled ParseErrors
  TRY {
    // Parse and evaluate all expressions in the source with TRY/CATCH
    while (!reader_is_eof(&reader)) {
      reader_skip_all(&reader);
      if (reader_is_eof(&reader)) break;

      // Update crash diagnostics as early as possible for this iteration.
      g_clojure_core_last_form = expr_count + 1;

      // Keep autorelease pool tracking bounded per top-level form.
      // NOTE: We intentionally use AUTORELEASE_POOL_BEGIN/END (not WITH_AUTORELEASE_POOL)
      // so loop control flow stays obvious and we don't accidentally "continue" a do/while(0).
      AUTORELEASE_POOL_BEGIN();
      
#ifdef PROFILE_STARTUP
      clock_t parse_start = clock();
#endif
      CljValue form = NULL;
      bool parse_ok = true;
      TRY {
        form = value_by_parsing_expr(&reader, st);
      } CATCH(ex) {
        // ParseError during parsing - log and continue to next expression
        if (ex) {
          if (!g_core_quiet) {
            fprintf(stderr, "[%s] ParseError at form #%d: %s - %s\n",
                    label, expr_count + 1, ex->type, ex->message);
          }
        }
        parse_ok = false;
      } END_TRY
#ifdef PROFILE_STARTUP
    g_parse_time_ms += (double)(clock() - parse_start) * 1000.0 / CLOCKS_PER_SEC;
#endif
    if (parse_ok && form) {

    if (debug_form > 0 && (expr_count + 1) == debug_form) {
      CljString *s = pr_str((ID)form);
      const char *printed = (s) ? s->data : "<unprintable>";
      fprintf(stderr, "[%s] DEBUG core form #%d: %s\n", label, expr_count + 1, printed);
      fflush(stderr);
    }
    
    // Evaluate with exception handling using TRY/CATCH
    TRY {
#ifdef PROFILE_STARTUP
      clock_t eval_start = clock();
#endif
      ID result = eval_parsed((CljObject*)form, st, NULL);
#ifdef PROFILE_STARTUP
      g_eval_time_ms += (double)(clock() - eval_start) * 1000.0 / CLOCKS_PER_SEC;
      g_form_count++;
#endif
      // Don't RELEASE result - eval_parsed already returns AUTORELEASE
      // result can be NULL if nil was evaluated (legitimate case)
      // eval_parsed should throw exceptions for errors, not return NULL
      if (result) {
        success_count++;
      } else {
        // NULL result could be nil (legitimate) or evaluation failure
        // For def expressions, the symbol should be stored even if result is NULL
        // For ns expressions, nil is a valid return value
        // Check if this was a def or ns expression that might have stored something
        if (form && list_type_matches(TAG(form))) {
          CljList *list = as_list(form);
          CljObject *first = LIST_FIRST(list);
          if (first && TAG(first) == CLJ_SYMBOL) {
            CljSymbol *first_sym = as_symbol(first);
            if (first_sym == SYM_DEF) {
              // def returns the symbol, not the value
              // Even if value evaluation failed, def might have stored nil
              success_count++;
            } else if (first_sym == SYM_NS) {
              // ns returns nil, which is a valid result
              success_count++;
            }
          }
        }
      }
    } CATCH(ex) {
      // Exception occurred during evaluation
      // Log the exception for debugging (always log for def expressions to catch silent failures)
      bool is_def_expr = false;
        if (form && list_type_matches(TAG(form))) {
          CljList *list = as_list(form);
          CljObject *first = LIST_FIRST(list);
          if (first && TAG(first) == CLJ_SYMBOL && as_symbol(first) == SYM_DEF) {
          is_def_expr = true;
        }
      }
      const char *error_type = (ex && ex->type[0]) ? ex->type : "Exception";
      const char *error_msg = (ex && ex->message[0]) ? ex->message : "Unknown error";
      const char *error_file = (ex && ex->file[0]) ? ex->file : "<unknown>";
      int error_line = ex ? ex->line : 0;
      const char *ns_name = target_ns && target_ns->name && target_ns->name->cname 
                            ? target_ns->name->cname 
                            : "clojure.core";
      // Always show errors for clojure.repl (not clojure.core), or if not in quiet mode.
      // Additionally, always show errors for (def ...) forms even in quiet mode,
      // since silent def failures can leave clojure.core partially loaded.
      bool is_clojure_repl = target_ns && target_ns->name && 
                             target_ns->name->cname && 
                             strcmp(target_ns->name->cname, "clojure.repl") == 0;
      if (!g_core_quiet || is_clojure_repl || is_def_expr) {
        fprintf(stderr, "[%s] Failed to eval form #%d%s: %s (%s:%d) [%s]\n",
                ns_name,
                expr_count + 1,
                is_def_expr ? " (def)" : "",
                error_msg,
                error_file,
                error_line,
                error_type);
#ifdef DEBUG
        if (is_def_expr && ex) {
          print_exception(ex);
        }
#endif
      }
      
    } END_TRY
    }
    
    // value_by_parsing_expr returns AUTORELEASE object
    
    AUTORELEASE_POOL_END();
    expr_count++;

    if (stop_after_form > 0 && expr_count >= stop_after_form) {
      if (!g_core_quiet) {
        fprintf(stderr, "[%s] DEBUG stopping after core form #%d (TINYCLJ_DEBUG_CORE_STOP_AFTER)\n",
                label, expr_count);
      }
      return true;
    }
    }
  } CATCH(ex) {
    // ParseError or other exception during clojure.core loading
    // Log but don't fail - some expressions may have loaded successfully
    if (ex) {
      if (!g_core_quiet) {
        fprintf(stderr, "Warning: Exception during clojure.core loading: %s - %s\n", 
                ex->type, ex->message);
      }
    }
    // Continue - return success if at least some expressions loaded
  } END_TRY
  
  // CRITICAL: Keep st->current_ns pointing to target_ns until after verification
  // Don't restore original_ns here - let load_clojure_core handle it after verification
  // This ensures the verification can check the correct namespace
  // target_ns is already registered in ns_registry, no cache needed
  
  if (!g_core_quiet) {
    const char *ns_name = target_ns && target_ns->name && target_ns->name->cname 
                          ? target_ns->name->cname 
                          : "clojure.core";
    fprintf(stderr, "[%s] Evaluated %d form(s), %d succeeded.\n",
            ns_name, expr_count, success_count);
  }
  
#ifdef PROFILE_STARTUP
  fprintf(stderr, "[PROFILE] Parse: %.2f ms, Canon: %.2f ms, Eval: %.2f ms, Forms: %d\n",
          g_parse_time_ms, g_canon_time_ms, g_eval_time_ms, g_form_count);
#endif

  // Ensure Math alias points to clojure.core so Math/sqrt style symbols resolve
  if (target_ns && target_ns->name == SYM_CLOJURE_CORE) {
    CljSymbol *math_alias = intern_symbol_global("Math");
    if (math_alias && SYM_CLOJURE_CORE) {
      ns_set_alias(target_ns, math_alias, SYM_CLOJURE_CORE);
    }
  }

  return success_count > 0;
}

int load_clojure_core(EvalState *st) {
  if (!st) return 0;
  
  if (!clojure_core_code) {
    return 0;
  }

  // Preserve caller namespace; loading core should not permanently change it.
  CljNamespace *original_ns = st->current_ns;

  // CRITICAL: Ensure clojure.core namespace exists
  // evalstate_set_ns will create the namespace if it doesn't exist
  evalstate_set_ns(st, "clojure.core");
  
  // CRITICAL: Always use the namespace from registry for loading
  // This ensures we load into the same namespace that will be used for lookups
  CljNamespace *clojure_core = ns_find_by_symbol(SYM_CLOJURE_CORE);
  if (!clojure_core) {
    // Namespace should have been created by evalstate_set_ns, but if not, fail
    if (st->current_ns && st->current_ns->name == SYM_CLOJURE_CORE) {
      clojure_core = st->current_ns;
    } else {
      return 0;
    }
  }
  
  // IDEMPOTENCY: Check if clojure.core is already loaded
  // If 'inc' is already in mappings, skip loading to avoid double-loading
  if (clojure_core && clojure_core->mappings) {
    CljSymbol *inc_sym = intern_symbol_global("inc");
    if (inc_sym) {
      CljObject *inc_value = (CljObject*)map_get_sentinel((CljValue)clojure_core->mappings, (CljValue)inc_sym, NULL);
      if (inc_value && (TAG(inc_value) == CLJ_FUNC || TAG(inc_value) == CLJ_CLOSURE)) {
        // clojure.core is already loaded - return success without reloading
        clojure_core->loaded = true;
        if (original_ns) {
          st->current_ns = original_ns;
        }
        return 1;
      }
    }
  }
  
  // CRITICAL: Ensure st->current_ns points to the namespace from registry
  // This ensures all def operations store in the correct namespace
  st->current_ns = clojure_core;
  
  bool ok = eval_core_source(clojure_core_code, "clojure.core.clj", st);

  if (ok) {
    clojure_core->loaded = true;
  }
  
  // Restore original namespace after loading
  if (original_ns) {
    st->current_ns = original_ns;
  }

  (void)ok; // Result is checked below

  // CRITICAL ASSERTION: Verify that 'inc' was loaded successfully
  // This ensures that the loading process actually stored the function
  // NOTE: Use namespace from registry directly, not st->current_ns,
  // because st->current_ns may have been restored to original_ns after eval_core_source
  // clojure_core is already defined above
  if (clojure_core && clojure_core->mappings) {
    CljSymbol *inc_sym = intern_symbol_global("inc");
    if (inc_sym) {
      CljObject *inc_value = (CljObject*)map_get_sentinel((CljValue)clojure_core->mappings, (CljValue)inc_sym, NULL);
      if (!inc_value) {
        return 0;
      }
      // Verify it's a function
      if (!inc_value || (TAG(inc_value) != CLJ_FUNC && TAG(inc_value) != CLJ_CLOSURE)) {
        return 0;
      }
    }
  } else {
    return 0;
  }

  return ok ? 1 : 0;
}

void clojure_core_set_quiet(bool quiet) {
  g_core_quiet = quiet;
  // Note: load_clojure_core() now requires EvalState parameter
  // Called from REPL main() instead
}

void clojure_core_set_source(const char *src) { clojure_core_code = src; }

// Load clojure.repl namespace from file
int load_clojure_repl(EvalState *st) {
  if (!st) return 0;
  
  // Convert namespace to relative path (clojure.repl -> clojure/repl.clj)
  const char *ns_name = "clojure.repl";
  size_t len = strlen(ns_name);
  char *rel = (char*)malloc(len + 5); // +5 for ".clj" and potential slashes
  if (!rel) return 0;
  
  // Replace dots with slashes
  for (size_t i = 0; i < len; i++) {
    rel[i] = (ns_name[i] == '.') ? '/' : ns_name[i];
  }
  rel[len] = '\0';
  strcat(rel, ".clj");
  
  // Search order: libs/<rel>, <rel>, ../libs/<rel>, ../<rel>
  char libs_path[512];
  snprintf(libs_path, sizeof(libs_path), "libs/%s", rel);
  char parent_libs_path[512];
  snprintf(parent_libs_path, sizeof(parent_libs_path), "../%s", libs_path);
  char parent_rel_path[512];
  snprintf(parent_rel_path, sizeof(parent_rel_path), "../%s", rel);
  const char *candidates[] = {
    libs_path,
    rel,
    parent_libs_path,
    parent_rel_path
  };
  
  char *source = NULL;
  const char *source_label = NULL;
  for (size_t i = 0; i < sizeof(candidates)/sizeof(candidates[0]); i++) {
    source = read_file_cstr_local(candidates[i]);
    if (source) {
      source_label = candidates[i];
      break;
    }
  }
  
  if (!source) {
    free(rel);
    return 0;
  }
  
  // Save original namespace
  CljNamespace *orig_ns = st->current_ns;
  
  // Ensure target namespace exists
  CljNamespace *target_ns = ns_get_or_create(ns_name, NULL);
  if (!target_ns) {
    free(source);
    free(rel);
    return 0;
  }
  
  // Temporarily switch to target namespace
  st->current_ns = target_ns;
  
  // Evaluate source using same approach as eval_core_source
  bool ok = eval_core_source(source, source_label, st);
  
  // Restore original namespace
  if (orig_ns) {
    st->current_ns = orig_ns;
  }
  
  free(source);
  free(rel);
  
  return ok ? 1 : 0;
}
