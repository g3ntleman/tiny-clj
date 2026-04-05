---
name: ESP32 SPI-Display Render-Task
overview: >
  Generische Thread-Abstraktion ist in subjective-c konsolidiert
  (subjective-c/src/subjective-c/thread.h + subjective-c/src/thread.c).
  Shared Render-Driver in src/render_driver.h/.c ist aktiv.
  ESP32-spezifisch bleibt Config + Lifecycle-Wiring + Display-Bootstrap.
  Slot-Change-Tracker nutzt SubjectiveCMutex/SubjectiveCCondVar;
  der bisherige pthread/busy-poll-Fallback ist entfernt.
todos:
  - id: step-1-red-thread-abstraction
    content: "RED: Unit-Tests fuer Thread-Abstraktion (create/join, mutex trylock, condvar wait/signal)"
    status: done
  - id: step-2-green-thread-abstraction
    content: "GREEN: subjective-c/src/subjective-c/thread.h + subjective-c/src/thread.c"
    status: done
  - id: step-3-refactor-slot-tracker
    content: "REFACTOR: Slot-Change-Tracker auf SubjectiveCMutex/SubjectiveCCondVar umstellen"
    status: done
  - id: step-4-red-render-driver
    content: "RED: Unit-Tests fuer Render-Driver (Stripe-Push, Render-Loop-Step, Start/Stop)"
    status: done
  - id: step-5-green-render-driver
    content: "GREEN: src/render_driver.h/.c — Stripe-Push + Loop + Thread-Lifecycle"
    status: done
  - id: step-6-red-arch-contract
    content: "RED: Architektur-Contract-Tests fuer ESP32-Renderer-Wiring"
    status: done
  - id: step-7-green-esp32-wiring
    content: "GREEN: esp32-idf/main/tinyclj_idf_renderer.c (Config + Lifecycle, ~20 Zeilen)"
    status: done
  - id: step-8-green-orientation
    content: "GREEN: Display-Orientierung in tinyclj_idf_display.c"
    status: done
  - id: step-9-green-cmake
    content: "GREEN: CMake-Wiring (subjective-c thread + render_driver in Component + Host)"
    status: done
  - id: step-10-boot-screen
    content: "Boot-Screen mit Vektor-Grafik (erster sichtbarer Display-Output)"
    status: done
  - id: step-11-cleanup
    content: "CLEANUP & OPT: Baselines fuer Stripe/Stack/Priority + DMA-Alignment; optionale Host-Refactors separat"
    status: done
isProject: false
---

# ESP32 SPI-Display Render-Task (Test-First)

## Architektur

### Naming-Update

- `thread_local.c` wird in diesem Plan als `thread.c` gefuehrt.
- `thread_local.h` bleibt der TLS-Headername.
- Die portable Thread-API ist konsolidiert unter
  `subjective-c/thread.h` + `subjective-c/thread.c`.

### Status 2026-04-04

- Threading ist konsolidiert in `subjective-c`:
  `subjective-c/src/subjective-c/thread.h`, `subjective-c/src/thread.c`.
- Thread-API wurde um Stack-Messung erweitert:
  `subjective_c_thread_stack_high_water_mark_bytes(...)`.
- Slot-Tracker nutzt `SubjectiveCMutex *` / `SubjectiveCCondVar *` in `src/scene.h/.c`.
- Shared Render-Driver ist umgesetzt: `src/render_driver.h/.c`.
- ESP32-Wiring ist umgesetzt:
  `esp32-idf/main/tinyclj_idf_renderer.h/.c`, Integration in `tinyclj_idf_run.c`.
- Display-Orientierung ist gesetzt in `tinyclj_idf_display.c` via `vg_panel_set_orientation(...)`.
- Step-11 Baselines sind umgesetzt:
  `TINYCLJ_RENDER_STRIPE_ROWS` (default `30`), DMA-Alignment
  (`__attribute__((aligned(4)))`), konfigurierbare
  `TINYCLJ_RENDER_THREAD_STACK_BYTES`/`TINYCLJ_RENDER_THREAD_PRIORITY`,
  plus Stack-High-Water-Report beim Renderer-Stop.
- CMake-Wiring aktualisiert:
  `CMakeLists.txt`, `esp32-idf/components/tinyclj/CMakeLists.txt`,
  `subjective-c/CMakeLists.txt`, `esp32-idf/components/subjective-c/CMakeLists.txt`,
  `esp32-idf/main/CMakeLists.txt`.
- Architektur-Contract-Tests wurden erweitert, neue Unit-Tests hinzugefuegt:
  `src/tests/test_subjective_c_thread.c`, `src/tests/test_render_driver.c`.
- Verifiziert: `./build/unit-tests --quiet` → `2149 Tests, 0 Failures, 8 Ignored`.

### Schicht-Modell

```
┌─────────────────────────────────────────────────┐
│  Clojure:  (start-renderer!) / (stop-renderer!) │
├─────────────────────────────────────────────────┤
│  renderer_lifecycle.h  (existiert)              │
├─────────────────────────────────────────────────┤
│  render_driver.h       (NEU, shared)            │
│    - stripe_push (Stripe-Rendering)             │
│    - step        (Slot-Change → Render → Push)  │
│    - start/stop  (Thread-Lifecycle)             │
├─────────────────────────────────────────────────┤
│  subjective-c/thread.h (NEU, shared API)        │
│    - SubjectiveCThread / SubjectiveCMutex /     │
│      SubjectiveCCondVar                          │
├─────────────────────────────────────────────────┤
│  subjective-c/thread.c (plattform branch intern)│
│    - host: pthread, esp32: FreeRTOS             │
└─────────────────────────────────────────────────┘
```

### Was wird shared, was bleibt plattform-spezifisch?

| Baustein | Shared? | Datei |
|---|---|---|
| Thread-Abstraktion API | ja | `subjective-c/src/subjective-c/thread.h` |
| Thread Backend (host + esp32) | ja (branch-intern) | `subjective-c/src/thread.c` |
| Slot-Change-Tracker (refactored) | ja | `src/scene.c` |
| Render-Driver (Stripe + Loop + Lifecycle) | ja | `src/render_driver.h/.c` |
| ESP32 Config + Lifecycle-Reg | esp32-only | `esp32-idf/main/tinyclj_idf_renderer.c` |
| Display-Bootstrap (SPI, ST7789) | esp32-only | `esp32-idf/main/tinyclj_idf_display.c` |

### Warum Thread-Abstraktion?

Der Slot-Change-Tracker (`scene.c`) hatte zuvor:

```c
#if VG_SLOT_CHANGE_TRACKER_USE_PTHREAD
    pthread_cond_timedwait(...)    // effizient, blockiert
#else
    platform_sleep_ms(1u);         // busy-poll, 1ms Latenz, CPU-Verschwendung
#endif
```

Mit `SubjectiveCCondVar` verschwindet dieses `#if` und ESP32 nutzt native
FreeRTOS-Semaphoren statt busy-poll. Das allein rechtfertigt die Abstraktion.

---

> Hinweis: Die folgenden Step-Bloecke sind die urspruengliche Soll-Beschreibung.
> Der aktuelle Ist-Stand ist oben unter **Status 2026-04-04** und in den
> `todos`-Statusfeldern dokumentiert.

## Step 1 (RED) — Unit-Tests fuer Thread-Abstraktion

**Neue Datei:** `src/tests/test_subjective_c_thread.c`

### Thread-Tests

```
TEST(test_subjective_c_thread_create_join)
  Thread erstellen, Funktion setzt Flag, join → Flag gesetzt

TEST(test_subjective_c_thread_create_with_name)
  Thread mit Name erstellen → kein Crash (Name ist best-effort)
```

### Mutex-Tests

```
TEST(test_subjective_c_mutex_lock_unlock)
  Lock → Unlock → kein Deadlock

TEST(test_subjective_c_mutex_trylock_succeeds_when_free)
  Trylock auf freiem Mutex → true

TEST(test_subjective_c_mutex_trylock_fails_when_held)
  Thread A lockt, Thread B trylock → false
```

### CondVar-Tests

```
TEST(test_subjective_c_condvar_signal_wakes_waiter)
  Thread A wartet, Thread B signalisiert → A wacht auf

TEST(test_subjective_c_condvar_wait_timeout)
  Warten mit 10ms Timeout, kein Signal → kehrt nach ~10ms zurueck

TEST(test_subjective_c_condvar_signal_before_wait)
  Signal vor Wait → naechster Wait kehrt sofort zurueck
  (oder nicht — je nach Semantik. Dokumentieren.)
```

**Erwartetes Ergebnis:** Kompiliert nicht.

---

## Step 2 (GREEN) — Thread-Abstraktion implementieren

**Neue Dateien (Ist-Stand, ein shared Backend):**
- `subjective-c/src/subjective-c/thread.h`
- `subjective-c/src/thread.c`

### subjective-c/thread.h (Entwurfs-Skizze; final im Repo)

```c
#ifndef SUBJECTIVE_C_THREAD_H
#define SUBJECTIVE_C_THREAD_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

/* --- Thread --- */

typedef struct SubjectiveCThread SubjectiveCThread;

typedef struct {
    const char *name;           // best-effort, kann NULL sein
    size_t stack_bytes;         // 0 = plattform-default
    int priority;               // plattform-spezifisch; 0 = default
} SubjectiveCThreadConfig;

SubjectiveCThread *subjective_c_thread_create(void (*fn)(void *arg), void *arg,
                               const SubjectiveCThreadConfig *config);
bool subjective_c_thread_join(SubjectiveCThread *t);    // blockiert bis Thread endet
void subjective_c_thread_destroy(SubjectiveCThread *t); // nach join: Ressourcen freigeben

/* --- Mutex --- */

typedef struct SubjectiveCMutex SubjectiveCMutex;

SubjectiveCMutex *subjective_c_mutex_create(void);
void subjective_c_mutex_destroy(SubjectiveCMutex *m);
void subjective_c_mutex_lock(SubjectiveCMutex *m);
void subjective_c_mutex_unlock(SubjectiveCMutex *m);
bool subjective_c_mutex_trylock(SubjectiveCMutex *m);   // true = lock erhalten

/* --- CondVar --- */

typedef struct SubjectiveCCondVar SubjectiveCCondVar;

SubjectiveCCondVar *subjective_c_condvar_create(void);
void subjective_c_condvar_destroy(SubjectiveCCondVar *cv);
void subjective_c_condvar_signal(SubjectiveCCondVar *cv);
void subjective_c_condvar_broadcast(SubjectiveCCondVar *cv);

// Wartet bis signal/broadcast oder timeout_ms abgelaufen.
// UINT32_MAX = unendlich. Returniert true bei Signal, false bei Timeout.
bool subjective_c_condvar_wait(SubjectiveCCondVar *cv, SubjectiveCMutex *m, uint32_t timeout_ms);

#endif
```

### Posix-Backend (Skizze)

```c
// thread.c (host branch)
struct SubjectiveCThread {
    pthread_t handle;
    void (*fn)(void *);
    void *arg;
    bool joined;
};

struct SubjectiveCMutex {
    pthread_mutex_t handle;
};

struct SubjectiveCCondVar {
    pthread_cond_t handle;
};
```

Direkte 1:1-Abbildung auf pthreads. `subjective_c_condvar_wait` mit
`pthread_cond_timedwait` + Deadline-Berechnung.

### FreeRTOS-Backend (Skizze)

```c
// thread.c (esp32 branch)
struct SubjectiveCThread {
    TaskHandle_t handle;
    void (*fn)(void *);
    void *arg;
    SemaphoreHandle_t done_sem;  // signalisiert Thread-Ende fuer join
};

struct SubjectiveCMutex {
    SemaphoreHandle_t handle;    // xSemaphoreCreateMutex()
};

struct SubjectiveCCondVar {
    SemaphoreHandle_t handle;    // xSemaphoreCreateBinary()
};
```

`subjective_c_thread_join`: wartet auf `done_sem`.
`subjective_c_condvar_wait`: `xSemaphoreTake(cv->handle, pdMS_TO_TICKS(timeout_ms))`.
`subjective_c_condvar_signal`: `xSemaphoreGive(cv->handle)`.

**Design-Entscheidung:** Heap-Allokation (`CLJ_MALLOC`) fuer die opaken Structs.
Alternativ: Inline-Structs mit `#ifdef`. Heap ist einfacher und die Objekte
sind langlebig (einmal pro Render-Thread-Lebenszyklus). Kann spaeter auf
statische Allokation umgestellt werden wenn noetig.

**Akzeptanzkriterium:** Step-1-Tests gruen.

---

## Step 3 (REFACTOR) — Slot-Change-Tracker umstellen

**Datei:** `src/scene.h`, `src/scene.c`

### Vorher

```c
typedef struct {
    // ...
#if VG_SLOT_CHANGE_TRACKER_USE_PTHREAD
    pthread_mutex_t mutex;
    pthread_cond_t cond;
#endif
} VgSlotChangeTracker;
```

### Nachher

```c
typedef struct {
    // ...
    SubjectiveCMutex *mutex;
    SubjectiveCCondVar *cond;
} VgSlotChangeTracker;
```

- `#if VG_SLOT_CHANGE_TRACKER_USE_PTHREAD` entfaellt komplett
- `#include <pthread.h>` in `scene.h` entfaellt
- `vg_slot_change_tracker_init()` ruft `subjective_c_mutex_create()` + `subjective_c_condvar_create()`
- `vg_slot_change_tracker_wait_for_changes()` wird zu einer einzigen Implementierung:

```c
uint32_t vg_slot_change_tracker_wait_for_changes(...) {
    subjective_c_mutex_lock(tracker->mutex);
    uint32_t changed = snapshot_mask(...);
    if (changed == 0u && timeout_ms > 0u) {
        (void)subjective_c_condvar_wait(tracker->cond, tracker->mutex, timeout_ms);
        changed = snapshot_mask(...);
    }
    subjective_c_mutex_unlock(tracker->mutex);
    return changed;
}
```

**Kein busy-poll-Fallback mehr.** Auf ESP32 nutzt `subjective_c_condvar_wait` native
FreeRTOS-Semaphoren mit echtem Blocking.

**Akzeptanzkriterium:**
- Bestehende Slot-Change-Tracker-Tests gruen
- Gesamte Test-Suite gruen (keine Regression)

---

## Step 4 (RED) — Unit-Tests fuer Render-Driver

**Neue Datei:** `src/tests/test_render_driver.c`

### Stripe-Push-Tests (Mock-Panel)

```
TEST(test_render_driver_stripe_push_single_stripe)
TEST(test_render_driver_stripe_push_full_display)
TEST(test_render_driver_stripe_push_partial_rect)
TEST(test_render_driver_stripe_push_unaligned)
TEST(test_render_driver_stripe_push_full_height_no_striping)
TEST(test_render_driver_stripe_push_pixels_within_buffer)
```

(Details wie in voriger Plan-Version, hier nicht wiederholt.)

### Start/Stop-Tests

```
TEST(test_render_driver_start_creates_thread)
  → start() → Thread laeuft (Flag gesetzt) → stop() → Thread beendet

TEST(test_render_driver_stop_idempotent)
  → stop() ohne start → kein Crash

TEST(test_render_driver_start_stop_cycle)
  → start → stop → start → stop → keine Leaks
```

**Erwartetes Ergebnis:** Kompiliert nicht.

---

## Step 5 (GREEN) — Render-Driver implementieren

**Neue Dateien:** `src/render_driver.h`, `src/render_driver.c`

### render_driver.h

```c
#ifndef TINY_CLJ_RENDER_DRIVER_H
#define TINY_CLJ_RENDER_DRIVER_H

#include "panel.h"
#include "vector_scene_graph.h"
#include "scene.h"
#include "thread.h"

typedef struct {
    // Konfiguration (vom Caller gesetzt vor start)
    VgPanel *panel;
    uint16_t *stripe_pixels;       // Caller-owned Buffer
    int16_t display_width;
    int16_t display_height;
    int16_t stripe_rows;           // Buffer-Hoehe (≤ display_height)
    uint8_t slot_count;
    const char *thread_name;       // optional, z.B. "render"
    size_t thread_stack_bytes;     // 0 = default
    int thread_priority;           // 0 = default

    // Interner State (vom Driver verwaltet)
    VgRenderSlotState *slot_states;
    uint32_t *seen_generations;
    uint32_t animated_slots_mask;
    SubjectiveCThread *thread;
    volatile bool running;
} VgRenderDriver;

// Stripe-aware push: rendert dirty in Stripes der Hoehe stripe_rows.
bool vg_render_driver_stripe_push(VgRenderDriver *driver,
                                  const VgNode *scene_root,
                                  VgClipRect dirty,
                                  uint32_t now_ms,
                                  uint16_t bg_color);

// Eine Iteration der Render-Loop.
int vg_render_driver_step(VgRenderDriver *driver,
                          VgSlotChangeTracker *tracker,
                          const ID *scene_snapshots);

// Thread starten/stoppen (nutzt subjective-c/thread intern).
bool vg_render_driver_start(VgRenderDriver *driver);
bool vg_render_driver_stop(VgRenderDriver *driver);

#endif
```

### Render-Driver-Thread-Main (shared)

```c
static void render_driver_thread_main(void *arg) {
    VgRenderDriver *d = (VgRenderDriver *)arg;
    while (d->running) {
        (void)vg_render_driver_step(d, /* global tracker */, /* snapshots */);
    }
}

bool vg_render_driver_start(VgRenderDriver *driver) {
    if (!driver || driver->thread) return false;
    driver->running = true;
    SubjectiveCThreadConfig cfg = {
        .name = driver->thread_name,
        .stack_bytes = driver->thread_stack_bytes,
        .priority = driver->thread_priority,
    };
    driver->thread = subjective_c_thread_create(render_driver_thread_main, driver, &cfg);
    return driver->thread != NULL;
}

bool vg_render_driver_stop(VgRenderDriver *driver) {
    if (!driver || !driver->thread) return true;
    driver->running = false;
    // Slot-Tracker wecken, damit der Thread aufwacht
    // ...
    subjective_c_thread_join(driver->thread);
    subjective_c_thread_destroy(driver->thread);
    driver->thread = NULL;
    return true;
}
```

**Akzeptanzkriterium:** Step-4-Tests gruen.

---

## Step 6 (RED) — Architektur-Contract-Tests ESP32

**Datei:** `src/tests/test_panel_esp32_architecture_contract.c`

```
TEST(test_panel_esp32_architecture_renderer_uses_shared_render_driver)
  → tinyclj_idf_renderer.c enthaelt "#include \"render_driver.h\""
  → tinyclj_idf_renderer.c enthaelt "vg_render_driver_start"

TEST(test_panel_esp32_architecture_run_registers_renderer_lifecycle)
  → tinyclj_idf_run.c enthaelt "tinyclj_idf_renderer_init"

TEST(test_panel_esp32_architecture_display_configures_orientation)
  → tinyclj_idf_display.c enthaelt "vg_panel_set_orientation"

TEST(test_panel_esp32_architecture_slot_tracker_uses_subjective_c_thread)
  → scene.h enthaelt "thread.h" oder "SubjectiveCMutex"
  → scene.h enthaelt NICHT "pthread_mutex_t"
```

---

## Step 7 (GREEN) — ESP32-Wiring

**`esp32-idf/main/tinyclj_idf_renderer.c`** — nur Config + Lifecycle:

```c
#include "tinyclj_idf_renderer.h"
#include "render_driver.h"
#include "renderer_lifecycle.h"
#include "vector_handheld_config.h"

static VgRenderDriver s_driver = {0};
static uint16_t s_stripe_pixels[VG_DISP_WIDTH * (VG_DISP_HEIGHT / 8)];

static bool esp_renderer_start_cb(ID slot_atoms, void *user_data) {
    (void)slot_atoms;
    VgRenderDriver *d = (VgRenderDriver *)user_data;
    return vg_render_driver_start(d);
}

static bool esp_renderer_stop_cb(void *user_data) {
    VgRenderDriver *d = (VgRenderDriver *)user_data;
    return vg_render_driver_stop(d);
}

bool tinyclj_idf_renderer_init(TinycljIdfDisplay *display) {
    if (!display || !display->initialized) return false;

    s_driver = (VgRenderDriver){
        .panel = &display->panel.panel,
        .stripe_pixels = s_stripe_pixels,
        .display_width = VG_DISP_WIDTH,
        .display_height = VG_DISP_HEIGHT,
        .stripe_rows = VG_DISP_HEIGHT / 8,
        .thread_name = "render",
        .thread_stack_bytes = 4096,
        .thread_priority = 5,
    };

    tiny_renderer_lifecycle_set_callbacks(
        esp_renderer_start_cb, esp_renderer_stop_cb, &s_driver);
    return true;
}
```

**In `tinyclj_idf_run.c`:**

```c
#if defined(TINY_FX_ENABLED) && TINY_FX_ENABLED
#include "tinyclj_idf_renderer.h"
    TinycljIdfDisplay *display = tinyclj_idf_display_get();
    if (display) (void)tinyclj_idf_renderer_init(display);
#endif
```

---

## Step 8 (GREEN) — Display-Orientierung

In `tinyclj_idf_display_init()` nach `vg_panel_init()`:

```c
(void)vg_panel_set_orientation(&display->panel.panel, false, false, true);
```

---

## Step 9 (GREEN) — CMake-Wiring

### Host-CMake (`CMakeLists.txt`)

```cmake
# Neue shared-Quellen zur FX-Liste
set(TINY_FX_SOURCES
  # ... bestehend ...
  src/render_driver.c
)

# Thread-Abstraktion (immer, nicht nur FX)
list(APPEND TINYCLJ_SOURCES "${TINYCLJ_SUBJECTIVE_C_SOURCE_DIR}/src/thread.c")
```

### ESP32-IDF Component (`esp32-idf/components/tinyclj/CMakeLists.txt`)

```cmake
set(TINYCLJ_FX_SRCS
  # ... bestehend ...
  "${TINYCLJ_SRC_DIR}/render_driver.c"
)

# Thread-Abstraktion
list(APPEND TINYCLJ_CORE_SRCS "${TINYCLJ_SUBJECTIVE_C_SOURCE_DIR}/src/thread.c")
```

### ESP32-IDF Main (`esp32-idf/main/CMakeLists.txt`)

```cmake
  SRCS
    # ... bestehend ...
    "tinyclj_idf_renderer.c"
```

---

## Step 10 — Boot-Screen

Erster sichtbarer Display-Output. Statische VgNode-Szene,
synchron via `vg_render_driver_stripe_push()` gerendert.
Kein laufender Render-Task noetig.

Aufgerufen in `tinyclj_idf_run.c` nach `tinyclj_idf_renderer_init()`,
VOR der REPL-Initialisierung.

### Design

- Hintergrund: dunkelblau (`0x000F`)
- Zentrierter Text: "TINY-CLJ" (VgNode text)
- Optional: Rahmen-Rect

### Tests

```
TEST(test_boot_screen_renders_without_crash)
  Mock-Panel → write_bitmap mind. 1× aufgerufen

TEST(test_boot_screen_covers_full_display)
  Union aller write_bitmap Y-Ranges = {0..240}
```

---

## Step 11 (CLEANUP & OPT)

### Aufraeumen
- [x] `#if VG_SLOT_CHANGE_TRACKER_USE_PTHREAD` komplett entfernt (scene.h/.c)
- [x] `#include <pthread.h>` aus scene.h entfernt
- [x] `esp_report_display_throughput_if_due()` evaluiert (bleibt als 1s-Telemetrie in `tinyclj_idf_run.c`)

### ESP32-Optimierung
- [x] Stripe-Hoehe kalibriert (default `TINYCLJ_RENDER_STRIPE_ROWS = 30`)
- [x] DMA-Alignment des Stripe-Buffers (`__attribute__((aligned(4)))`)
- [x] Task-Stack-Messung integriert (`subjective_c_thread_stack_high_water_mark_bytes`,
      Report beim Renderer-Stop)
- [x] Task-Prioritaet baselineisiert (`TINYCLJ_RENDER_THREAD_PRIORITY`, default `5`)
- [x] Task-Stack baselineisiert (`TINYCLJ_RENDER_THREAD_STACK_BYTES`, default `4096`)

### Host-Refactor (optional, Follow-up)
- [ ] `fx_host_app.c` Render-Thread auf `VgRenderDriver` umstellen
- [ ] Sound-Backend-Threads auf `subjective-c/thread` umstellen
- [ ] `atom.c` rwlock ggf. auf `SubjectiveCMutex` (niedrige Prio)

### Test-Suite
```
./build/unit-tests --quiet
```
`2149 Tests, 0 Failures, 8 Ignored`.

---

## Datei-Uebersicht

| Datei | Aenderung | Shared? |
|---|---|---|
| `subjective-c/src/subjective-c/thread.h` | NEU (Thread-API) | ja |
| `subjective-c/src/thread.c` | NEU (host+esp32 backend) | ja |
| `src/tests/test_subjective_c_thread.c` | NEU | ja |
| `src/render_driver.h` | NEU | ja |
| `src/render_driver.c` | NEU | ja |
| `src/tests/test_render_driver.c` | NEU | ja |
| `src/scene.h` | REFACTOR (pthread → SubjectiveCMutex/CondVar) | ja |
| `src/scene.c` | REFACTOR (einheitliche Impl) | ja |
| `esp32-idf/main/tinyclj_idf_renderer.h` | NEU (~20 Zeilen) | nein |
| `esp32-idf/main/tinyclj_idf_renderer.c` | NEU (Config + Lifecycle + Boot-Screen) | nein |
| `esp32-idf/main/tinyclj_idf_display.c` | orientation hinzufuegen | — |
| `esp32-idf/main/tinyclj_idf_run.c` | renderer_init + boot-screen einbinden | — |
| `esp32-idf/main/CMakeLists.txt` | renderer.c hinzufuegen | — |
| `esp32-idf/components/tinyclj/CMakeLists.txt` | render_driver.c einbinden | — |
| `subjective-c/CMakeLists.txt` | thread.c einbinden | — |
| `esp32-idf/components/subjective-c/CMakeLists.txt` | thread.c einbinden | — |
| `CMakeLists.txt` (Host) | render_driver.c + neue Tests einbinden | — |
| `src/tests/test_panel_esp32_architecture_contract.c` | Contract-Tests erweitern | — |
