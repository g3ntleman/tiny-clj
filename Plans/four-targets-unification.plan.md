---
name: Four Targets Unification
overview: Unify host and ESP32 startup paths around exactly four runtime targets (`tiny-clj` and `tiny-fx` on each platform), with a shared native `-m` launcher followed by REPL entry. The startup menu is intentionally out of scope for this step; this plan only establishes the foundation for it.
todos:
  - id: unify-build-targets
    content: Reduce runtime products to four targets and separate product and platform concerns cleanly in CMake
    status: pending
  - id: add-build-run-tasks
    content: Add four symmetric incremental Cursor build and run tasks that match the final product matrix
    status: pending
  - id: extract-shared-launcher
    content: Derive a shared native launcher from the current REPL and ESP32 startup paths and define the new `-m` then REPL semantics
    status: pending
  - id: align-esp32-startup
    content: Move ESP32 IDF and embedded startup paths onto the same startup pipeline and launcher contract
    status: pending
  - id: define-bundle-resource-loading
    content: Define how macOS app bundle resources are packaged and resolved for runtime file loading
    status: pending
  - id: update-cli-tests
    content: Update CLI, `-main`, and REPL tests for the new semantics and add regressions for a still-alive REPL and runloop after `-m`
    status: pending
  - id: cleanup
    content: Sourcecode aufräumen – Debug-Code, temporäre Workarounds, tote Codepfade, überflüssige Kommentare und nicht mehr benötigte Hilfsfunktionen entfernen
    status: pending
isProject: false
---

# Four Targets Unification

## Target State

- There are exactly four relevant runtime targets: `tiny-clj` and `tiny-fx`, each for host/macOS and ESP32.
- `tiny-clj` replaces the current REPL binaries.
- `tiny-fx` replaces the current viewer and host-viewer binaries.
- With no arguments, both binaries start the REPL.
- With `-m <ns> [args...]`, both binaries invoke the shared namespace launcher, call `-main`, and then return to the REPL.
- A startup menu will come later; this step only prepares a unified launcher and boot foundation for it.

## Key Decisions

- The first launcher lives in native C, not inside an already running REPL.
- The shared startup pipeline in [`src/startup_pipeline.c`](/Users/theisen/Projects/tiny-clj/src/startup_pipeline.c) and [`src/startup_pipeline.h`](/Users/theisen/Projects/tiny-clj/src/startup_pipeline.h) remains the central foundation.
- The current split between [`src/repl.c`](/Users/theisen/Projects/tiny-clj/src/repl.c), [`src/main_esp32.c`](/Users/theisen/Projects/tiny-clj/src/main_esp32.c), [`src/game_demo_minifb.c`](/Users/theisen/Projects/tiny-clj/src/game_demo_minifb.c), and [`esp32-idf/main/tinyclj_idf_run.c`](/Users/theisen/Projects/tiny-clj/esp32-idf/main/tinyclj_idf_run.c) should converge on one shared launcher contract.

## Implementation Plan

1. Unify build targets in [`CMakeLists.txt`](/Users/theisen/Projects/tiny-clj/CMakeLists.txt) and [`esp32-idf/main/CMakeLists.txt`](/Users/theisen/Projects/tiny-clj/esp32-idf/main/CMakeLists.txt).

- Remove the old product split between `tiny-clj-repl`, `tiny-clj-esp32-repl`, `game-demo`, and the current special-case role of `tiny-clj`.
- Replace it with explicit product axes: `tiny-clj` = core plus REPL, `tiny-fx` = core plus REPL plus tiny-fx native code.
- Keep platform differences only in platform-specific source and link sets, not in product semantics.
- Use separate build directories per platform family so host and ESP32 configurations do not overwrite each other.
  Target direction: one incremental macOS build directory and one incremental ESP32 build directory, with no shared build directory reused across platform boundaries.

1. Add exactly four symmetric Cursor `build&run` tasks in [`.vscode/tasks.json`](/Users/theisen/Projects/tiny-clj/.vscode/tasks.json) for the new product matrix.

- Replace the current asymmetric mix of `run-repl`, `run-game-demo`, and ESP32 flash and monitor tasks with four clearly named run tasks.
- Target labels:
  `build&run: tiny-clj (macos)`,
  `build&run: tiny-fx.app`,
  `build&run: tiny-clj (esp32)`,
  `build&run: tiny-fx (esp32)`.
- All four tasks must use incremental builds and reuse existing build directories instead of forcing clean rebuilds or full reconfiguration.
- The macOS tasks should build the matching CMake target incrementally and then launch the resulting product.
  For `tiny-fx.app`, the canonical launch command is `open -n ./build/tiny-fx.app`, not a direct `Contents/MacOS/...` executable invocation.
- The ESP32 tasks should build the matching firmware mode incrementally, flash it, and enter the monitor.
- The task implementation must reflect the separate platform build directories so macOS and ESP32 tasks remain independently incremental.
- Task names should match the final product names so Cursor tasks, build targets, and CLI documentation stay consistent.

1. Extract a shared native launcher flow from [`src/repl.c`](/Users/theisen/Projects/tiny-clj/src/repl.c).

- Redefine CLI semantics as: default = REPL, `-m` runs `-main` and then returns to the REPL.
- Remove or replace the current restriction that `-m/--main` cannot be combined with `--repl`, because REPL continuation becomes the default launcher behavior.
- Review file-based and eval-based entry points (`-e`, `-f`) against the new product model and either keep them explicitly or simplify them intentionally.
- Structure the launcher so a future terminal menu or graphical menu can call the same start contract.

1. Extend the shared startup pipeline in [`src/startup_pipeline.c`](/Users/theisen/Projects/tiny-clj/src/startup_pipeline.c).

- Add an explicit flow for `bootstrap runtime -> bootstrap language -> optional -main -> load or refer clojure.repl -> continue in interactive REPL`.
- Move the ESP32 IDF path in [`esp32-idf/main/tinyclj_idf_run.c`](/Users/theisen/Projects/tiny-clj/esp32-idf/main/tinyclj_idf_run.c) onto the same contract instead of maintaining an independent REPL bootstrap sequence.
- Evaluate whether [`src/main_esp32.c`](/Users/theisen/Projects/tiny-clj/src/main_esp32.c) can disappear as a separate special case or become only a thin wrapper around the shared launcher.

1. Define the `tiny-fx` product layer as “the same as `tiny-clj`, but with additional tiny-fx native code”.

- Reuse the existing tiny-fx source and link logic in [`CMakeLists.txt`](/Users/theisen/Projects/tiny-clj/CMakeLists.txt), but make the product entry points match the `tiny-clj` family.
- On macOS, build `tiny-fx` as an app bundle instead of only as a loose CLI binary. The existing `MACOSX_BUNDLE` approach for `breakout-app` in [`CMakeLists.txt`](/Users/theisen/Projects/tiny-clj/CMakeLists.txt) is the natural starting point.
- The normal host launch for the bundle is via `open -n`, so developer docs and tasks should treat `tiny-fx.app` as a macOS app bundle first and only use `Contents/MacOS/...` for low-level debugging.
- The current viewer-specific path in [`src/game_demo_minifb.c`](/Users/theisen/Projects/tiny-clj/src/game_demo_minifb.c) should stop being treated as a separate product; relevant startup and runloop mechanics should move into reusable components or a later `tiny-fx` menu and launcher layer.
- Official application entry points such as Breakout should be launched via explicit namespaces, for example through existing deployment entry points in [`libs/tiny-clj/deployment.clj`](/Users/theisen/Projects/tiny-clj/libs/tiny-clj/deployment.clj).

1. Define and unify runtime file loading for host CLI, macOS app bundles, and ESP32.

- The current resolver contract in [`src/source_resolver.c`](/Users/theisen/Projects/tiny-clj/src/source_resolver.c) is: KV store overrides first, then `embedded_sources`, then filesystem fallback for `/libs/`, `/assets/`, and `.clj` paths.
- For the macOS CLI, the current filesystem fallback may continue to load directly from the repository or relative paths.
- For the macOS `tiny-fx` app bundle, add an explicit bundle resource path so `/libs/...`, `/assets/...`, and optionally `/boot/root.edn` resolve from the bundle instead of implicitly relying on the current working directory.
- For this step, the `tiny-fx` app bundle must carry all required runtime-loadable resources entirely inside `Contents/Resources`; there is no primary external override directory for the app bundle.
- Preserve the virtual path structure exactly inside the bundle, in particular:
  `Contents/Resources/libs/...`,
  `Contents/Resources/assets/...`,
  and optionally `Contents/Resources/boot/...`.
- Add bundle resource lookup in the macOS host platform or resolver layer. At the moment, there is no app-bundle-specific lookup path in [`src/platform_macos.c`](/Users/theisen/Projects/tiny-clj/src/platform_macos.c) or [`src/source_resolver.c`](/Users/theisen/Projects/tiny-clj/src/source_resolver.c).
- On ESP32, runtime loading should continue to rely primarily on the KV or flash store plus `embedded_sources`; host-style filesystem fallback is not the primary runtime strategy there.

1. Update tests and CLI contracts.

- Update the existing CLI and `-main` tests in [`src/tests/test_main_entry.c`](/Users/theisen/Projects/tiny-clj/src/tests/test_main_entry.c) and the REPL tests in [`src/tests/test_repl.c`](/Users/theisen/Projects/tiny-clj/src/tests/test_repl.c) for the new semantics of `-m` followed by REPL continuation.
- Add focused regressions for the shared startup pipeline so `-m` sets `*command-line-args*` correctly and the REPL and runloop remain alive afterward.
- Recheck the existing Breakout and viewer-related build and runtime tests so removing `game-demo` as a standalone product does not silently drop behavior.

## Constraints

- Do not preserve backward compatibility for old binary names or old CLI semantics.
- The startup menu is explicitly out of scope for this step, but the launcher API should be shaped to support it later.
- `tiny-fx` remains a product variant of `tiny-clj` with extra native code, not a separate special-purpose startup path.
- Runtime file loading must be defined per platform intentionally; host CLI, macOS app bundle, and ESP32 must not share implicit path assumptions.
