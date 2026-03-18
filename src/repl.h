#ifndef REPL_H
#define REPL_H

#include <stdbool.h>
#include <stddef.h>
#include "namespace.h"  // For EvalState

#define REPL_ACC_CAP_DEFAULT 4096u

typedef enum ReplFormState {
    REPL_FORM_INVALID = -1,
    REPL_FORM_BALANCED = 0,
    REPL_FORM_INCOMPLETE = 1
} ReplFormState;

/**
 * @brief Evaluate Clojure code from a string with escape sequence support
 * @param raw_code The Clojure code as a string (may contain escape sequences like \n)
 * @param st The evaluation state
 * @return true on success, false on error
 */
bool repl_eval_arg(const char *raw_code, EvalState *st);

/**
 * @brief Evaluate one or more forms and print their results/exceptions.
 * @param code Source code containing one or more forms.
 * @param st Active evaluation state.
 * @return true when all forms evaluate successfully.
 */
bool eval_multiform_string(const char *code, EvalState *st);

/**
 * @brief Process a bounded number of pending event-loop tasks.
 * @param st Active evaluation state.
 */
void repl_process_event_loop(EvalState *st);

/**
 * @brief Compute delimiter/string balance for incremental REPL input.
 * @param source Source buffer to inspect.
 * @param error_pos Optional index of first unmatched closing delimiter.
 * @return >0 when more input is needed, 0 when balanced, <0 when invalid.
 */
int repl_form_balance(const char *source, int *error_pos);

/**
 * @brief Classify incremental REPL input into invalid/balanced/incomplete.
 * @param source Source buffer to inspect.
 * @param error_pos Optional index of first unmatched closing delimiter.
 * @return One of ReplFormState values.
 */
ReplFormState repl_form_state(const char *source, int *error_pos);

/**
 * @brief Append one submitted line to a multiline REPL accumulator.
 * @param acc Accumulator buffer (NUL-terminated).
 * @param acc_cap Capacity of @p acc in bytes.
 * @param line Submitted line to append.
 * @return true on success, false when the append would overflow.
 */
bool repl_acc_append_line(char *acc, size_t acc_cap, const char *line);

/**
 * @brief Format the REPL prompt text for the current namespace.
 * @param st Evaluation state containing current namespace.
 * @param balanced true for normal prompt (`=>`), false for continuation (`...`).
 * @param out Destination buffer for prompt text.
 * @param out_size Size of @p out in bytes.
 */
void repl_format_prompt(EvalState *st, bool balanced, char *out, size_t out_size);

/**
 * @brief Configuration for startup banner rendering.
 */
typedef struct ReplStartupBannerConfig {
    const char *title_line;
    bool center_title;
    unsigned center_width;
    bool include_ram_line;
    bool include_exit_hint;
} ReplStartupBannerConfig;

/**
 * @brief Inputs for deciding whether a macOS bundle launch should switch to the host app path.
 */
typedef struct ReplBundleLaunchDecisionInputs {
    bool tiny_fx_enabled;
    bool bundle_launch;
    bool stdin_is_tty;
    bool has_ns_arg;
    bool has_eval_args;
    bool has_file_arg;
    bool has_main_ns;
    bool start_repl;
    bool no_core;
} ReplBundleLaunchDecisionInputs;

/**
 * @brief Print build information lines using a caller-provided line emitter.
 * @param emit_line Callback that prints one line (without trailing newline handling constraints).
 */
void repl_print_build_info_with_emitter(void (*emit_line)(const char *line));

/**
 * @brief Print the startup banner using a caller-provided line emitter.
 * @param config Banner rendering configuration.
 * @param emit_line Callback that prints one line.
 */
void repl_print_startup_banner_with_emitter(const ReplStartupBannerConfig *config,
                                            void (*emit_line)(const char *line));

/**
 * @brief Decide whether `tiny-fx.app` should switch to the host app path instead of the CLI EOF path.
 * @param inputs Launch context summary.
 * @return true when the bundle launch should enter the host app path.
 */
bool repl_should_launch_tiny_fx_host_app(const ReplBundleLaunchDecisionInputs *inputs);

#endif // REPL_H
