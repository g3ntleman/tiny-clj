#ifndef TINY_CLJ_STARTUP_PIPELINE_H
#define TINY_CLJ_STARTUP_PIPELINE_H

#include <stdbool.h>
#include "namespace.h"

typedef struct {
    bool init_event_loop;
} TinycljRuntimeBootstrapOptions;

typedef struct {
    bool ensure_builtins;
    bool load_core;
    bool load_repl;
    bool refer_repl;
    bool core_quiet;
} TinycljLanguageBootstrapOptions;

bool tinyclj_startup_bootstrap_runtime(const TinycljRuntimeBootstrapOptions *opts,
                                       EvalState **out_state);

bool tinyclj_startup_bootstrap_language(EvalState *st,
                                        const TinycljLanguageBootstrapOptions *opts);

bool tinyclj_startup_prepare_repl(EvalState *st);

bool tinyclj_startup_invoke_main(EvalState *st,
                                 const char *ns_name,
                                 int argc,
                                 const char **argv,
                                 bool bind_command_line_args);

#endif // TINY_CLJ_STARTUP_PIPELINE_H
