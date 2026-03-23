#include "startup_pipeline.h"

#include "builtins.h"
#include "eval.h"
#include "event_loop.h"
#include "exception.h"
#include "function.h"
#include "list.h"
#include "map.h"
#include "memory.h"
#include "platform.h"
#include "runtime.h"
#include "strings.h"
#include "symbol.h"
#include "tiny_clj.h"
#include "vector.h"

static ID g_startup_sym_command_line_args = NULL;
static const IdSymbolCacheEntry g_startup_symbol_cache[] = {
    {&g_startup_sym_command_line_args, "*command-line-args*"},
};

static inline bool startup_symbols_ready(void) {
  return id_symbol_cache_init_global(g_startup_symbol_cache,
                                     sizeof(g_startup_symbol_cache) / sizeof(g_startup_symbol_cache[0]));
}

/**
 * @brief Release argument array built for eval_function_call.
 *
 * @param args Array of string IDs.
 * @param argc Number of entries in @p args.
 */
static void startup_release_main_args(ID *args, int argc) {
  if (!args) {
    return;
  }
  for (int i = 0; i < argc; i++) {
    RELEASE(args[i]);
  }
  CLJ_FREE(args);
}

/**
 * @brief Build string arguments for invoking a Clojure function from C.
 *
 * @param argc Number of argv entries.
 * @param argv Raw argument strings.
 * @return Heap array of CLJ_STRING IDs or NULL when argc is 0.
 */
static ID *startup_build_main_args(int argc, const char **argv) {
  if (argc <= 0) {
    return NULL;
  }

  ID *args = (ID *)CLJ_MALLOC(sizeof(ID) * (size_t)argc);
  if (!args) {
    throw_exception(EXCEPTION_RUNTIME, "Failed to allocate -main argument array", __FILE__, __LINE__, 0);
    return NULL;
  }

  for (int i = 0; i < argc; i++) {
    args[i] = NULL;
  }

  for (int i = 0; i < argc; i++) {
    const char *raw = (argv && argv[i]) ? argv[i] : "";
    ID value = (ID)make_string(raw);
    if (!value) {
      startup_release_main_args(args, argc);
      throw_exception(EXCEPTION_RUNTIME, "Failed to allocate -main argument string", __FILE__, __LINE__, 0);
      return NULL;
    }
    args[i] = value;
  }

  return args;
}

/**
 * @brief Convert argv values into a list for *command-line-args*.
 *
 * @param args String IDs to include.
 * @param argc Number of values in @p args.
 * @return List object or NULL for empty input.
 */
static ID startup_make_command_line_args_seq(ID *args, int argc) {
  CljList *seq = NULL;
  for (int i = argc - 1; i >= 0; i--) {
    CljList *next = make_list(args[i], seq);
    RELEASE(seq);
    seq = next;
  }
  return (ID)seq;
}

/**
 * @brief Resolve target namespace name for invoking -main.
 *
 * @param st Active eval state.
 * @param explicit_ns Optional namespace name from caller.
 * @return Namespace name string or NULL when no namespace can be determined.
 */
static const char *startup_target_ns_name(EvalState *st, const char *explicit_ns) {
  if (explicit_ns && explicit_ns[0] != '\0') {
    return explicit_ns;
  }
  if (st && st->current_ns && st->current_ns->name && st->current_ns->name->cname &&
      st->current_ns->name->cname[0] != '\0') {
    return st->current_ns->name->cname;
  }
  return NULL;
}

/**
 * @brief Build and push a dynamic binding frame for *command-line-args*.
 *
 * @param st Active eval state.
 * @param command_line_args Sequence/nil value to bind.
 * @return true on success.
 */
static bool startup_push_command_line_args_binding(EvalState *st, ID command_line_args) {
  if (!st || !st->dynamic_bindings) {
    throw_exception(EXCEPTION_RUNTIME, "Dynamic bindings are not initialized", __FILE__, __LINE__, 0);
    return false;
  }

  CljPersistentMap *frame = make_map(2);
  if (!frame) {
    throw_exception(EXCEPTION_RUNTIME, "Failed to allocate dynamic binding frame", __FILE__, __LINE__, 0);
    return false;
  }

  if (!startup_symbols_ready()) {
    RELEASE(frame);
    throw_exception(EXCEPTION_RUNTIME, "Failed to initialize startup symbols", __FILE__, __LINE__, 0);
    return false;
  }
  CljSymbol *cli_sym = g_startup_sym_command_line_args;
  if (!cli_sym) {
    RELEASE(frame);
    throw_exception(EXCEPTION_RUNTIME, "Failed to intern *command-line-args*", __FILE__, __LINE__, 0);
    return false;
  }
  map_assoc_inplace(&frame, (ID)cli_sym, command_line_args);

  if (SYM_CLOJURE_CORE) {
    CljSymbol *qualified_cli_sym = intern_symbol(SYM_CLOJURE_CORE, "*command-line-args*");
    if (qualified_cli_sym && qualified_cli_sym != cli_sym) {
      map_assoc_inplace(&frame, (ID)qualified_cli_sym, command_line_args);
    }
  }

  vector_push(st->dynamic_bindings, (ID)frame);
  RELEASE(frame);
  return true;
}

/**
 * @brief Print caught startup exception once in shared startup paths.
 *
 * @param ex Exception object captured by TRY/CATCH.
 */
static void startup_print_caught_exception(CLJException *ex) {
  if (ex) {
    print_exception(ex);
  }
}

static bool startup_namespace_loaded(const char *ns_name) {
  if (!ns_name || ns_name[0] == '\0') {
    return false;
  }

  CljNamespace *ns = ns_find(ns_name);
  return ns && ns->loaded;
}

bool tinyclj_startup_bootstrap_runtime(const TinycljRuntimeBootstrapOptions *opts,
                                       EvalState **out_state) {
  bool init_event_loop = true;
  if (opts) {
    init_event_loop = opts->init_event_loop;
  }
  if (out_state) {
    *out_state = NULL;
  }

  EvalState *st = NULL;
  bool success = true;

  TRY {
#if defined(DEBUG) && defined(TINYCLJ_HOST_HEAP_LIMIT_BYTES) && !defined(ESP32_BUILD)
    memory_set_heap_limit_bytes((size_t)TINYCLJ_HOST_HEAP_LIMIT_BYTES);
#endif
    platform_init();
    runtime_init(&g_runtime);
    if (init_event_loop) {
      event_loop_init();
    }

    st = get_global_eval_state();
    if (!st) {
      throw_exception(EXCEPTION_RUNTIME, "Failed to create EvalState", __FILE__, __LINE__, 0);
    }
    evalstate_set_ns(st, "user");
  }
  CATCH(ex) {
    startup_print_caught_exception((CLJException *)ex);
    success = false;
    st = NULL;
  }
  END_TRY

  if (out_state) {
    *out_state = st;
  }
  return success && st != NULL;
}

bool tinyclj_startup_bootstrap_language(EvalState *st,
                                        const TinycljLanguageBootstrapOptions *opts) {
  if (!st) {
    return false;
  }

  TinycljLanguageBootstrapOptions effective = {
      .ensure_builtins = true,
      .load_core = true,
      .load_repl = false,
      .refer_repl = false,
      .core_quiet = false,
  };
  if (opts) {
    effective = *opts;
  }

  bool success = true;
  TRY {
    clojure_core_set_quiet(effective.core_quiet);

    bool need_builtins = effective.ensure_builtins || effective.load_core ||
                         effective.load_repl || effective.refer_repl;
    if (need_builtins) {
      evalstate_ensure_builtins_ready();
    }

    if (effective.load_core && !load_clojure_core(st)) {
      throw_exception(EXCEPTION_RUNTIME, "Failed to load clojure.core", __FILE__, __LINE__, 0);
    }

    if (effective.load_repl && !load_clojure_repl(st)) {
      throw_exception(EXCEPTION_RUNTIME, "Failed to load clojure.repl", __FILE__, __LINE__, 0);
    }

    if (effective.refer_repl) {
      evalstate_set_ns(st, "user");
      (void)eval_string("(require '[clojure.repl :refer :all])", st);
    }

    evalstate_set_ns(st, "user");
  }
  CATCH(ex) {
    startup_print_caught_exception((CLJException *)ex);
    success = false;
    evalstate_set_ns(st, "user");
  }
  END_TRY

  return success;
}

bool tinyclj_startup_prepare_repl(EvalState *st) {
  TinycljLanguageBootstrapOptions repl_opts = {
      .ensure_builtins = true,
      .load_core = !startup_namespace_loaded("clojure.core"),
      .load_repl = !startup_namespace_loaded("clojure.repl"),
      .refer_repl = true,
      .core_quiet = true,
  };
  return tinyclj_startup_bootstrap_language(st, &repl_opts);
}

bool tinyclj_startup_invoke_main(EvalState *st,
                                 const char *ns_name,
                                 int argc,
                                 const char **argv,
                                 bool bind_command_line_args) {
  ID *main_args = NULL;
  ID command_line_args = NULL;
  unsigned int base_depth = 0u;
  bool pushed_bindings = false;
  bool success = false;

  TRY {
    if (!st) {
      throw_exception(EXCEPTION_RUNTIME, "EvalState is required for -main invocation", __FILE__, __LINE__, 0);
    }
    if (argc < 0) {
      throw_exception(EXCEPTION_ILLEGAL_ARGUMENT, "-main argument count must be >= 0", __FILE__, __LINE__, 0);
    }

    const char *target_ns_name = startup_target_ns_name(st, ns_name);
    if (!target_ns_name || target_ns_name[0] == '\0') {
      throw_exception(EXCEPTION_RUNTIME, "Unable to determine namespace for -main", __FILE__, __LINE__, 0);
    }

    if (ns_name && ns_name[0] != '\0' && !require_namespace_by_name(st, target_ns_name)) {
      throw_exception_formatted(EXCEPTION_RUNTIME, __FILE__, __LINE__, 0,
                                "Failed to load namespace '%s' for -main",
                                target_ns_name);
    }

    CljNamespace *target_ns = ns_find(target_ns_name);
    CljSymbol *target_ns_sym = (target_ns && target_ns->name)
                                   ? target_ns->name
                                   : intern_symbol_global(target_ns_name);
    if (!target_ns_sym) {
      throw_exception_formatted(EXCEPTION_RUNTIME, __FILE__, __LINE__, 0,
                                "Failed to resolve namespace symbol '%s'",
                                target_ns_name);
    }

    CljSymbol *main_sym = intern_symbol(target_ns_sym, "-main");
    if (!main_sym) {
      throw_exception(EXCEPTION_RUNTIME, "Failed to intern -main symbol", __FILE__, __LINE__, 0);
    }

    ID main_fn = ns_resolve(st, main_sym);
    if (main_fn == NOT_FOUND || !main_fn) {
      throw_exception_formatted(EXCEPTION_RUNTIME, __FILE__, __LINE__, 0,
                                "Namespace '%s' does not define -main",
                                target_ns_name);
    }
    if (!is_callable(main_fn)) {
      throw_exception_formatted(EXCEPTION_RUNTIME, __FILE__, __LINE__, 0,
                                "'%s/-main' is not callable",
                                target_ns_name);
    }

    main_args = startup_build_main_args(argc, argv);
    if (argc > 0 && !main_args) {
      throw_exception(EXCEPTION_RUNTIME, "Failed to build argument list for -main", __FILE__, __LINE__, 0);
    }

    if (bind_command_line_args) {
      command_line_args = startup_make_command_line_args_seq(main_args, argc);
      if (!st->dynamic_bindings || !st->dynamic_bindings->backing) {
        throw_exception(EXCEPTION_RUNTIME, "Dynamic bindings are not initialized", __FILE__, __LINE__, 0);
      }
      base_depth = vector_count(st->dynamic_bindings->backing);
      if (!startup_push_command_line_args_binding(st, command_line_args)) {
        throw_exception(EXCEPTION_RUNTIME, "Failed to bind *command-line-args*", __FILE__, __LINE__, 0);
      }
      pushed_bindings = true;
    }

    (void)eval_function_call(main_fn, main_args, (unsigned int)argc, NULL, st);
    success = true;
  }
  CATCH(ex) {
    startup_print_caught_exception((CLJException *)ex);
    success = false;
  }
  END_TRY

  if (pushed_bindings) {
    evalstate_pop_dynamic_bindings_to(st, base_depth);
  }
  RELEASE(command_line_args);
  startup_release_main_args(main_args, argc);
  return success;
}
