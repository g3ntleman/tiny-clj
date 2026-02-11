// clojure.core.c

#include "exception.h"
#include "symbol.h"  // Must be included before namespace.h for CljSymbol definition
#include "namespace.h"
#include "tiny_clj.h"
#include "reader.h"
#include "value.h"  // For IS_IMMEDIATE macro
#include "runtime.h" // For g_runtime
#include "list.h"    // For LIST_FIRST
#include "eval.h"  // For SYM_DEF, SYM_NS, SYM_DEFMACRO
#include "map.h"     // For map_get
#include "parser.h"  // For eval_parsed
#include "to_string.h" // For pr_str debug printing
#include "strings.h" // For string_data
#include "source_resolver.h" // For resolve_path_to_bytes (load_clojure_repl, override fallback)
#include "builtins.h"        // For load_namespace_from_bytes
#include "types.h"  // For clj_type_name
#include "memory_profiler.h"
#include "mini_format.h"
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

#if defined(DEBUG) && defined(MEMORY_PROFILING_ENABLED) && MEMORY_PROFILING_ENABLED
static void core_mem_print_top_types(const char *label, int top_n) {
  if (top_n <= 0) return;
  // Track top-N by bytes_current.
  size_t top_bytes[8];
  int top_types[8];
  const int max_n = (top_n > 8) ? 8 : top_n;
  for (int i = 0; i < max_n; i++) {
    top_bytes[i] = 0;
    top_types[i] = -1;
  }

  for (int ti = 0; ti < CLJ_TYPE_COUNT; ti++) {
    size_t bc = g_memory_stats.bytes_current_by_type[ti];
    if (bc == 0) continue;
    for (int slot = 0; slot < max_n; slot++) {
      if (bc > top_bytes[slot]) {
        // Shift down.
        for (int k = max_n - 1; k > slot; k--) {
          top_bytes[k] = top_bytes[k - 1];
          top_types[k] = top_types[k - 1];
        }
        top_bytes[slot] = bc;
        top_types[slot] = ti;
        break;
      }
    }
  }

  fprintf(stderr, "[core-mem] %s total=%zu peak=%zu\n",
          label ? label : "<core>",
          g_memory_stats.current_memory_usage,
          g_memory_stats.peak_memory_usage);
  for (int i = 0; i < max_n; i++) {
    if (top_types[i] < 0 || top_bytes[i] == 0) continue;
    int ti = top_types[i];
    fprintf(stderr, "  %s: bytes=%zu alloc=%zu dealloc=%zu\n",
            clj_type_name((CljType)ti),
            g_memory_stats.bytes_current_by_type[ti],
            g_memory_stats.allocations_by_type[ti],
            g_memory_stats.deallocations_by_type[ti]);
  }
}

typedef struct {
  long bytes_delta;
  long alloc_delta;
  long dealloc_delta;
  int type;
} CoreMemDeltaSlot;

static inline long core_mem_abs_long(long v) {
  return (v < 0) ? -v : v;
}

static void core_mem_print_delta(const char *label,
                                 const MemoryStats *before,
                                 const MemoryStats *after,
                                 int top_n) {
  if (!before || !after || top_n <= 0) return;

  const int max_n = (top_n > 8) ? 8 : top_n;
  CoreMemDeltaSlot slots[8];
  for (int i = 0; i < max_n; i++) {
    slots[i].bytes_delta = 0;
    slots[i].alloc_delta = 0;
    slots[i].dealloc_delta = 0;
    slots[i].type = -1;
  }

  for (int ti = 0; ti < CLJ_TYPE_COUNT; ti++) {
    long bytes_delta = (long)after->bytes_current_by_type[ti]
                     - (long)before->bytes_current_by_type[ti];
    if (bytes_delta == 0) continue;
    long abs_bytes = core_mem_abs_long(bytes_delta);

    for (int slot = 0; slot < max_n; slot++) {
      long slot_abs = core_mem_abs_long(slots[slot].bytes_delta);
      if (slots[slot].type < 0 || abs_bytes > slot_abs) {
        for (int k = max_n - 1; k > slot; k--) {
          slots[k] = slots[k - 1];
        }
        slots[slot].bytes_delta = bytes_delta;
        slots[slot].alloc_delta =
            (long)after->allocations_by_type[ti] - (long)before->allocations_by_type[ti];
        slots[slot].dealloc_delta =
            (long)after->deallocations_by_type[ti] - (long)before->deallocations_by_type[ti];
        slots[slot].type = ti;
        break;
      }
    }
  }

  long current_delta = (long)after->current_memory_usage - (long)before->current_memory_usage;
  long peak_delta = (long)after->peak_memory_usage - (long)before->peak_memory_usage;
  fprintf(stderr, "[core-mem] %s current=%+ld peak=%+ld\n",
          label ? label : "<core>", current_delta, peak_delta);

  for (int i = 0; i < max_n; i++) {
    if (slots[i].type < 0 || slots[i].bytes_delta == 0) continue;
    int ti = slots[i].type;
    fprintf(stderr, "  %s: bytes=%+ld alloc=%+ld dealloc=%+ld\n",
            clj_type_name((CljType)ti),
            slots[i].bytes_delta,
            slots[i].alloc_delta,
            slots[i].dealloc_delta);
  }
}
#endif



// Keyword symbol definitions for fs_layer and others
extern CljSymbol *SYM_KW_SIZE;



// Optional override for core source (tests/custom); default from embedded_sources.c via resolve_path_to_bytes.
static const char *clojure_core_override = NULL;

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
  char *buffer = (char*)CLJ_MALLOC((size_t)sz + 1);
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

static bool eval_core_source(const char *src, size_t src_len, const char *source_name, EvalState *st) {
  if (!src || !st)
    return false;
  if (src_len == 0) {
    src_len = strlen(src);
  }
  
  // Caller has set st->current_ns to the target namespace; use it for all defs.
  CljNamespace *target_ns = st->current_ns;

  // Use Reader to parse multiple expressions
  Reader reader;
  reader_init_with_length(&reader, src, src_len);
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
  // Memory diagnostics: print top types every N forms and/or a final summary.
#if defined(DEBUG) && defined(MEMORY_PROFILING_ENABLED) && MEMORY_PROFILING_ENABLED
  const int debug_mem_every = getenv_int("TINYCLJ_DEBUG_CORE_MEM_EVERY", 0);
  const int debug_mem_summary = getenv_int("TINYCLJ_DEBUG_CORE_MEM_SUMMARY", 0);
  const int debug_mem_delta = getenv_int("TINYCLJ_DEBUG_CORE_MEM_DELTA", 0);
#else
  const int debug_mem_every = 0;
  const int debug_mem_summary = 0;
  const int debug_mem_delta = 0;
  (void)debug_mem_every;
  (void)debug_mem_summary;
  (void)debug_mem_delta;
#endif
  // Autorelease diagnostics (compile-time gated):
  // Build with -DTINYCLJ_AUTORELEASE_DIAGNOSTICS=1 to enable.
#if defined(DEBUG) && defined(TINYCLJ_AUTORELEASE_DIAGNOSTICS) && TINYCLJ_AUTORELEASE_DIAGNOSTICS
  // - TINYCLJ_DEBUG_AUTORELEASE_THRESHOLD=N prints a line when a single top-level form
  //   causes the autorelease pool peak to exceed N.
  const int ar_threshold = getenv_int("TINYCLJ_DEBUG_AUTORELEASE_THRESHOLD", 0);
#else
  const int ar_threshold = 0;
  (void)ar_threshold;
#endif
  
  // Wrap entire parsing loop in TRY/CATCH to catch any unhandled ParseErrors
  TRY {
    // Parse and evaluate all expressions in the source with TRY/CATCH
    while (!reader_is_eof(&reader)) {
      reader_skip_all(&reader);
      if (reader_is_eof(&reader)) break;

      // Update crash diagnostics as early as possible for this iteration.
      g_clojure_core_last_form = expr_count + 1;

#if defined(DEBUG) && defined(MEMORY_PROFILING_ENABLED) && MEMORY_PROFILING_ENABLED
      MemoryStats mem_before = {0};
      bool mem_before_valid = false;
      if (debug_mem_delta > 0 && g_memory_profiling_enabled) {
        mem_before = g_memory_stats;
        mem_before_valid = true;
      }
#endif

      // Keep autorelease pool tracking bounded per top-level form.
      // Split parsing and evaluation into separate pools to reduce peak usage.
      CljValue form = NULL;
      bool parse_ok = true;

      WITH_AUTORELEASE_POOL({
#ifdef PROFILE_STARTUP
        clock_t parse_start = clock();
#endif
        size_t parse_offset_before = reader_offset(&reader);
        TRY {
          form = value_by_parsing_expr(&reader, st);
          if (form && !IS_IMMEDIATE((CljValue)form)) {
            RETAIN((CljValue)form); // keep form alive across pool boundary
          }
        } CATCH(ex) {
          // ParseError during parsing - log and continue to next expression
          if (ex) {
            if (!g_core_quiet) {
              fprintf(stderr, "[%s] ParseError at form #%d: %s - %s\n",
                      label, expr_count + 1, ex->type, ex->message);
            }
          }
          // IMPORTANT: Ensure forward progress on parse errors.
          if (!reader_is_eof(&reader) && reader_offset(&reader) == parse_offset_before) {
            reader_advance(&reader);
          }
          parse_ok = false;
        } END_TRY
#ifdef PROFILE_STARTUP
        g_parse_time_ms += (double)(clock() - parse_start) * 1000.0 / CLOCKS_PER_SEC;
#endif
      });

      if (parse_ok && form) {
        WITH_AUTORELEASE_POOL({
#if defined(DEBUG) && defined(TINYCLJ_AUTORELEASE_DIAGNOSTICS) && TINYCLJ_AUTORELEASE_DIAGNOSTICS
          if (ar_threshold > 0) {
            autorelease_pool_peak_reset();
          }
#endif

          if (debug_form > 0 && (expr_count + 1) == debug_form) {
            CljString *s = pr_str((ID)form);
            const char *printed = (s) ? string_data((ID)s) : "<unprintable>";
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
            if (result) {
              success_count++;
            } else {
              if (form && is_list_type(TAG((CljValue)form))) {
                CljList *list = as_list((CljValue)form);
                CljObject *first = LIST_FIRST(list);
                if (first && TAG(first) == CLJ_SYMBOL) {
                  CljSymbol *first_sym = as_symbol(first);
                  if (first_sym == SYM_DEF) { success_count++; }
                  else if (first_sym == SYM_NS) { success_count++; }
                }
              }
            }
          } CATCH(ex) {
            bool is_def_expr = false;
            if (form && is_list_type(TAG((CljValue)form))) {
              CljList *list = as_list((CljValue)form);
              CljObject *first = LIST_FIRST(list);
              if (first && TAG(first) == CLJ_SYMBOL) {
                CljSymbol *first_sym = as_symbol(first);
                if (first_sym == SYM_DEF || first_sym == SYM_DEFMACRO) { is_def_expr = true; }
              }
            }
            const char *error_type = (ex && ex->type[0]) ? ex->type : "Exception";
            const char *error_msg = (ex && ex->message[0]) ? ex->message : "Unknown error";
            const char *error_file = (ex && ex->file[0]) ? ex->file : "<unknown>";
            int error_line = ex ? ex->line : 0;
            const char *ns_name = target_ns && target_ns->name && target_ns->name->cname
                                  ? target_ns->name->cname : "clojure.core";
            bool is_clojure_repl = target_ns && target_ns->name && target_ns->name->cname
                                  && strcmp(target_ns->name->cname, "clojure.repl") == 0;
            if (!g_core_quiet || is_clojure_repl || is_def_expr) {
              fprintf(stderr, "[%s] Failed to eval form #%d%s: %s (%s:%d) [%s]\n",
                      ns_name, expr_count + 1, is_def_expr ? " (def)" : "",
                      error_msg, error_file, error_line, error_type);
#ifdef DEBUG
              if (is_def_expr && ex) { print_exception(ex); }
#endif
            }
          } END_TRY

#if defined(DEBUG) && defined(TINYCLJ_AUTORELEASE_DIAGNOSTICS) && TINYCLJ_AUTORELEASE_DIAGNOSTICS
          if (ar_threshold > 0) {
            uint32_t peak = autorelease_pool_peak_count();
            if ((int)peak > ar_threshold) {
              fprintf(stderr, "[%s] Autorelease peak in core form #%d: %u (threshold=%d)\n",
                      label, expr_count + 1, peak, ar_threshold);
            }
          }
#endif
        });
      }

      if (form && !IS_IMMEDIATE((CljValue)form)) {
        RELEASE((CljValue)form);
      }
    expr_count++;

#if defined(DEBUG) && defined(MEMORY_PROFILING_ENABLED) && MEMORY_PROFILING_ENABLED
    if (debug_mem_delta > 0 && mem_before_valid && g_memory_profiling_enabled) {
      char label_buf[64];
      mini_snprintf(label_buf, sizeof(label_buf), "delta form %d", expr_count);
      core_mem_print_delta(label_buf, &mem_before, &g_memory_stats, debug_mem_delta);
    }
#endif

#if defined(DEBUG) && defined(MEMORY_PROFILING_ENABLED) && MEMORY_PROFILING_ENABLED
    if (debug_mem_every > 0 && (expr_count % debug_mem_every) == 0) {
      char label_buf[64];
      mini_snprintf(label_buf, sizeof(label_buf), "after form %d", expr_count);
      core_mem_print_top_types(label_buf, 5);
    }
#endif

    if (stop_after_form > 0 && expr_count >= stop_after_form) {
      if (!g_core_quiet) {
        fprintf(stderr, "[%s] DEBUG stopping after core form #%d (TINYCLJ_DEBUG_CORE_STOP_AFTER)\n",
                label, expr_count);
      }
      return true;
    }
    }
  } CATCH(ex) {
    if (ex && !g_core_quiet) {
      fprintf(stderr, "Warning: Exception during namespace loading [%s]: %s - %s\n",
              label, ex->type, ex->message);
    }
    // Continue - return success if at least some expressions loaded
  } END_TRY
  
  // CRITICAL: Keep st->current_ns pointing to target_ns until after verification
  // Don't restore original_ns here - let load_clojure_core handle it after verification
  // This ensures the verification can check the correct namespace
  // target_ns is already registered in ns_registry, no cache needed
  
  if (!g_core_quiet) {
    fprintf(stderr, "[%s] Evaluated %d form(s), %d succeeded.\n",
            label, expr_count, success_count);
  }
#ifdef PROFILE_STARTUP
  fprintf(stderr, "[PROFILE] Parse: %.2f ms, Canon: %.2f ms, Eval: %.2f ms, Forms: %d\n",
          g_parse_time_ms, g_canon_time_ms, g_eval_time_ms, g_form_count);
#endif
#if defined(DEBUG) && defined(MEMORY_PROFILING_ENABLED) && MEMORY_PROFILING_ENABLED
  if (debug_mem_summary > 0) {
    core_mem_print_top_types("core load summary", 8);
  }
#endif
  return success_count > 0;
}

int load_clojure_core(EvalState *st) {
  if (!st) return 0;
  bool loaded = false;
  ID bytes = resolve_path_to_bytes("/libs/clojure/core.clj");
  if (bytes)
    loaded = load_namespace_from_bytes(st, "clojure.core", bytes, "/libs/clojure/core.clj");
  if (!loaded && clojure_core_override) {
    CljNamespace *orig_ns = st->current_ns;
    evalstate_set_ns(st, "clojure.core");
    CljNamespace *core = ns_find("clojure.core");
    if (!core && st->current_ns->name && st->current_ns->name->cname
        && strcmp(st->current_ns->name->cname, "clojure.core") == 0)
      core = st->current_ns;
    if (core) {
      st->current_ns = core;
      bool ok = eval_core_source(clojure_core_override, strlen(clojure_core_override), "clojure.core.clj", st);
      if (ok) core->loaded = true;
      if (orig_ns) st->current_ns = orig_ns;
      if (ok) loaded = true;
    }
  }
  if (!loaded) return 0;
  CljNamespace *core = ns_find("clojure.core");
  if (!core) return 0;
  CljSymbol *math_alias = intern_symbol_global("Math");
  if (math_alias && SYM_CLOJURE_CORE) ns_set_alias(core, math_alias, SYM_CLOJURE_CORE);
  runtime_ensure_resolve_cache(&g_runtime);
  if (!core->mappings) return 0;
  CljSymbol *inc_sym = intern_symbol_global("inc");
  if (!inc_sym) return 0;
  CljObject *inc_val = (CljObject *)map_get_sentinel((CljValue)core->mappings, (CljValue)inc_sym, NULL);
  if (!inc_val || (TAG(inc_val) != CLJ_FUNC && TAG(inc_val) != CLJ_CLOSURE)) return 0;
  return 1;
}

void clojure_core_set_quiet(bool quiet) {
  g_core_quiet = quiet;
  // Note: load_clojure_core() now requires EvalState parameter
  // Called from REPL main() instead
}

void clojure_core_set_source(const char *src) { clojure_core_override = src; }

// Load clojure.repl namespace from file
int load_clojure_repl(EvalState *st) {
  if (!st) return 0;
  
  // Convert namespace to relative path (clojure.repl -> clojure/repl.clj)
  const char *ns_name = "clojure.repl";
  size_t len = strlen(ns_name);
  char *rel = (char*)CLJ_MALLOC(len + 5); // +5 for ".clj" and potential slashes
  if (!rel) return 0;
  
  // Replace dots with slashes
  for (size_t i = 0; i < len; i++) {
    rel[i] = (ns_name[i] == '.') ? '/' : ns_name[i];
  }
  rel[len] = '\0';
  strcat(rel, ".clj");
  
  // Search order: libs/<rel>, <rel>, ../libs/<rel>, ../<rel>
  char libs_path[512];
  mini_snprintf(libs_path, sizeof(libs_path), "libs/%s", rel);
  char parent_libs_path[512];
  // Avoid -Werror=format-truncation: "../" + libs_path must fit.
  // Keep this robust on toolchains that treat warnings as errors (ESP-IDF).
  const size_t max_copy = sizeof(parent_libs_path) - 4; // "../" + '\0'
  if (strlen(libs_path) > max_copy) {
    CLJ_FREE(rel);
    return 0;
  }
  mini_snprintf(parent_libs_path, sizeof(parent_libs_path), "../%.*s", (int)max_copy, libs_path);
  char parent_rel_path[512];
  mini_snprintf(parent_rel_path, sizeof(parent_rel_path), "../%s", rel);
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
    CLJ_FREE(rel);
    return 0;
  }
  
  // Save original namespace state
  CljNamespace *orig_ns = st->current_ns;
  CljNamespace *orig_resolve_ns = st->resolve_ns;
  
  // Ensure target namespace exists
  CljNamespace *target_ns = ns_get_or_create(ns_name, NULL);
  if (!target_ns) {
    CLJ_FREE(source);
    CLJ_FREE(rel);
    return 0;
  }
  
  // Temporarily switch to target namespace
  st->current_ns = target_ns;
  st->resolve_ns = target_ns;
  
  // Evaluate source using same approach as eval_core_source
  size_t source_len = strlen(source);
  bool ok = eval_core_source(source, source_len, source_label, st);
  
  // Restore original namespace state
  if (orig_ns) {
    st->current_ns = orig_ns;
  }
  st->resolve_ns = orig_resolve_ns;
  
  CLJ_FREE(source);
  CLJ_FREE(rel);
  
  return ok ? 1 : 0;
}
