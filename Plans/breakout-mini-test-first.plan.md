---
name: breakout mini one-screen demo
overview: Build a one-screen Breakout demo for the tiny-handheld using tiny-fx scene/state patterns, explicitly using the MDN pure JavaScript Breakout tutorial as behavioral reference and mapping it to tiny-fx architecture in test-first increments.
todos:
  - id: define-scope-and-contracts
    content: Define one-screen scope, game states, controls, and deterministic update contracts before implementation
    status: pending
  - id: consolidate-deployment-tools
    content: Konsolidiere alle build-tools unter dem Namespace "tiny-clj.deployment"
    status: pending
  - id: phase-1-red-core-motion-tests
    content: Add failing tests for paddle movement, serve state, and basic ball motion/reflection
    status: pending
  - id: phase-2-green-core-loop
    content: Implement minimal playable loop to satisfy phase-1 tests without adding polish features
    status: pending
  - id: phase-3-red-collision-and-scoring-tests
    content: Add failing tests for brick collisions, score updates, life loss, and deterministic resets
    status: pending
  - id: phase-4-green-collision-and-scoring
    content: Implement brick field, collision handling, score/lives state, and level-clear transitions
    status: pending
  - id: phase-5-red-ui-and-state-machine-tests
    content: Add failing tests for title, pause, game-over, victory, and restart state transitions
    status: pending
  - id: phase-6-green-ui-and-flow
    content: Implement one-screen UI and full state-machine flow with stable scene snapshots
    status: pending
  - id: phase-7-red-audio-and-feel-tests
    content: Add failing tests for sound trigger semantics and bounded gameplay pacing assumptions
    status: pending
  - id: phase-8-green-audio-and-polish
    content: Implement hit/lose/win audio hooks and minimal visual feedback without changing gameplay contracts
    status: pending
  - id: phase-9-regression-and-budgets
    content: Run focused and full regression tests and lock acceptance criteria for the demo
    status: pending
  - id: cleanup
    content: Sourcecode aufräumen – Debug-Code, temporäre Workarounds, tote Codepfade, überflüssige Kommentare und nicht mehr benötigte Hilfsfunktionen entfernen. Optimiere für esp32 (Codesize, Heap-Use, Speed).
    status: pending
isProject: false
---

# Breakout Mini (One-Screen) Test-First Plan

## Reference template

Behavioral reference for game progression and mechanics:

- MDN tutorial: `https://developer.mozilla.org/en-US/docs/Games/Tutorials/2D_Breakout_game_pure_JavaScript`
- Companion repo: `https://github.com/end3r/Gamedev-Canvas-workshop`

Important adaptation rule:

- Use MDN only as gameplay template (order of mechanics and UX flow).
- Do not copy browser/canvas runtime architecture.
- Implement with tiny-fx scene records, slot atoms, and tiny-clj host/runtime contracts.

## Goal

Create a simple and robust Breakout demo for the tiny-handheld:

- no world scrolling
- one fixed playfield
- short session length (2-4 minutes)
- deterministic behavior suitable for host/unit testing

## Constraints

- Test-first: every behavior change starts with a failing test.
- Incremental delivery: each phase should leave the game in a runnable state.
- No compatibility wrappers if API contracts are intentionally updated.
- Keep runtime memory and startup behavior stable (no large load-time spikes).
- Reuse established tiny-fx patterns (`bundle`, slot atoms, scene snapshot updates).
- Keep the mechanic rollout aligned with MDN chapter order where practical.
- Paddle input must support two interchangeable sources: GPIO left/right simulation and rotary encoder delta.
- Breakout module must not depend on `tiny-fx.startup` or `tiny-fx.game-demo`.
- Build/Test wiring should be centralized in namespace `tiny-clj.deployment`.

## Existing patterns to reuse

- Scene records and EDN conversion utilities in `libs/tiny-fx/gfx-scene.clj`.
- Existing tiny-fx input and audio integration patterns (without namespace dependency on startup/game-demo).
- C-side behavior and integration tests under `src/tests/`.

## Target gameplay contract (v1)

- States: `:title`, `:serve`, `:play`, `:pause`, `:level-clear`, `:game-over`, `:victory`.
- Controls:
  - paddle movement via either GPIO left/right simulation or rotary encoder input
  - A: launch ball (from `:serve`) and start a new game from terminal states
  - B: pause/resume
- Core rules:
  - ball reflects at walls and paddle
  - ball destroys bricks and grants score
  - ball leaving bottom costs one life and returns to `:serve`
  - clear all bricks to advance level; clear final level to `:victory`
  - terminal overlays use exact texts: `Game Over` or `You win!`

## Step-by-step test-first phases (MDN-aligned)

### Phase 0a - Deployment Tools Consolidation

1. Identifiziere bestehende Build-Tools (z.B. Scripte, `tinyclj-cp`).
2. Erstelle/Erweitere den Namespace `tiny-clj.deployment`.
3. Migriere die Deployment- und Build-Logik aus separaten Skripten in diesen Namespace.
4. Passe Aufrufe in der Build-Pipeline (z.B. CMake) an, um `tiny-clj.deployment` zu nutzen.

Exit criterion:
- Alle Deployment-Routinen sind im Namespace `tiny-clj.deployment` zusammengefasst und die Build-Pipeline funktioniert wie zuvor.

### Phase 0 - Contracts and fixtures

1. Define a minimal pure update contract (input + dt + current state -> next state + events).
2. Add fixed fixtures for level layouts (at least 3 one-screen brick patterns).
3. Decide and document deterministic constants (playfield size, paddle speed, ball speed, lives).
4. Define an input adapter contract that normalizes GPIO and rotary events into one paddle-intent format.
5. Add a phase-note matrix that maps each implementation step to a corresponding MDN lesson.

Exit criterion:
- contract document and fixture vectors/maps are committed as baseline for tests.

### Phase 1 - Red tests: core motion

Add failing tests first:

1. Paddle moves left/right within bounds and clamps correctly.
2. In `:serve`, ball stays attached to paddle before launch.
3. Launch sets expected initial velocity.
4. Wall reflection keeps ball inside top/side bounds.
5. GPIO left/right simulation updates paddle identically to normalized paddle-intent inputs.
6. Rotary encoder delta updates paddle identically to normalized paddle-intent inputs.

Then implement the smallest code to make them green.

MDN mapping:
- Create Canvas and draw on it
- Move the ball
- Bounce off the walls
- Paddle and keyboard controls

### Phase 2 - Red tests: bricks, score, lives

Add failing tests first:

1. Ball-brick collision removes/decrements exactly one brick hit target.
2. Score increments by brick points exactly once per collision.
3. Ball crossing bottom decrements lives once and enters `:serve`.
4. When lives reach zero, state becomes `:game-over`.

Then implement minimal collision and scoring logic.

MDN mapping:
- Build the brick field
- Collision detection
- Game over
- Track the score and win

### Phase 3 - Red tests: state machine flow

Add failing tests first:

1. `:title` -> `:serve` on start input.
2. `:play` <-> `:pause` on B toggle.
3. Empty brick field triggers `:level-clear`.
4. Last level clear triggers `:victory`.
5. In `:game-over`, overlay text equals exactly `Game Over`.
6. In `:victory`, overlay text equals exactly `You win!`.
7. Pressing A from `:game-over` or `:victory` starts a new game (canonical initial state).

Then implement the state machine transitions.

MDN mapping:
- Game over
- Track the score and win
- Finishing up (state flow consistency)

### Phase 4 - Red tests: rendering/data wiring

Add failing tests first:

1. Scene snapshot contains paddle, ball, bricks, and HUD nodes with stable IDs.
2. HUD reflects score/lives/state text from runtime state.
3. Scene build/update functions preserve expected shape for host-side contracts.

Then wire rendering output to the tested game state.

MDN mapping:
- Translating draw/update cadence from browser frame loop into tiny-fx snapshot pipeline

### Phase 5 - Red tests: audio/event semantics

Add failing tests first:

1. Distinct events emitted for paddle-hit, brick-hit, life-lost, level-clear, game-over, victory.
2. No duplicate event emission for a single simulation tick/collision.
3. Pause state suppresses gameplay events.

Then implement sound/event triggers against existing sound APIs.

MDN mapping:
- Finishing up (feedback/polish), adapted to tiny-fx sound/event model

### Phase 6 - Regression and acceptance

1. Run focused tests for the new Breakout module/tests.
2. Run broader relevant suites for gfx/collision/sound regressions.
3. Run full unit-test suite before marking the plan complete.
4. Execute build/test orchestration through `tiny-clj.deployment` entry points (no scattered ad-hoc wiring).
5. Capture final acceptance checklist and known limitations.

## File plan (expected)

- New:
  - `libs/tiny-fx/breakout.clj` (game state/update/render contract, MDN-mechanics port)
  - `libs/tiny-fx/assets/breakout.edn` (optional declarative layout/HUD presets)
  - `libs/tiny-clj/deployment.clj` (centralizes breakout build/test wiring and deployment helpers)
  - `src/tests/test_breakout_contract.c` (primary behavior contract tests)
- Possible updates:
  - `CMakeLists.txt` and related build files, only to hook into `tiny-clj.deployment`-driven workflow

## Acceptance criteria

1. One-screen gameplay works end-to-end without camera/world scrolling.
2. Terminal states show exact texts: `Game Over` and `You win!`.
3. Pressing A in either terminal state starts a fresh new game.
4. Paddle control works via both input modes: GPIO left/right simulation and rotary encoder.
5. All planned state transitions are covered by tests and green.
6. Collision, score, and life-loss behavior are deterministic in tests.
7. No regression in existing relevant tiny-fx test groups.
8. Full unit-test run is green.
9. ESP32 Optimization: Code is optimized for the ESP32 platform, considering code size, heap usage, and execution speed.
