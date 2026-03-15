#include "debug.h"
#include "exception.h"
#include "namespace.h"
#include "startup_pipeline.h"
#include "tiny_clj.h"

// Embedded startup code (like clojure_core.c pattern)
static const char *startup_code =
#include "startup-code.clj"
    ;

int main(void) {
  DEBUG_PRINT("Tiny-Clj ESP32 - Embedded Execution");

  EvalState *state = NULL;
  TinycljRuntimeBootstrapOptions runtime_opts = {
      .init_event_loop = true,
  };
  if (!tinyclj_startup_bootstrap_runtime(&runtime_opts, &state) || !state) {
    DEBUG_PRINT("ERROR: Runtime bootstrap failed");
    autorelease_pool_free();
    return 1;
  }

  TinycljLanguageBootstrapOptions language_opts = {
      .ensure_builtins = true,
      .load_core = true,
      .load_repl = false,
      .refer_repl = false,
      .core_quiet = true,
  };
  if (!tinyclj_startup_bootstrap_language(state, &language_opts)) {
    DEBUG_PRINT("ERROR: Language bootstrap failed");
    autorelease_pool_free();
    return 1;
  }

  // Load and execute startup code.
  DEBUG_PRINT("Loading startup code...");
  ID startup_result = NULL;
  TRY {
    startup_result = eval_string(startup_code, state);
  }
  CATCH(ex) {
    DEBUG_PRINT("ERROR: Failed to load startup code");
    if (ex) {
      print_exception((CLJException *)ex);
    }
    autorelease_pool_free();
    return 1;
  }
  END_TRY
  RELEASE(startup_result);

  // Invoke current namespace -main after startup code evaluation.
  if (!tinyclj_startup_invoke_main(state, NULL, 0, NULL, true)) {
    DEBUG_PRINT("ERROR: current-ns/-main invocation failed");
    autorelease_pool_free();
    return 1;
  }

  DEBUG_PRINT("Startup code and -main executed successfully");
  autorelease_pool_free();
  return 0;
}
