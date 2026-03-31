---
name: Predictive CCD – sweep-aabb als Engine-Primitive
overview: >
  Fuehrt ein generisches C-Primitive vg_sweep_aabb und Clojure-Builtin fx/sweep-aabb
  ein. Game-Scripts planen Segmente bis zum ersten Hindernis vor; Response passiert
  exakt am Segment-Ende auf dem Interpreter-Thread statt mit 20-40ms Verzoegerung.
  Integer-only, keine Division im Inner Loop (Kreuzprodukt-Vergleich), alle
  Zwischenwerte passen in int32_t. Obstacle-Seq wird direkt als Clojure-Seq
  gelesen (subjective-c), keine Konvertierung/Stack-Kopie noetig.
  Test-first, DRY, schrittweise.
todos:
  - id: step-1a-no-hit-test
    content: "Test: vg_sweep_aabb gibt no-hit zurueck wenn Velocity am Obstacle vorbeilaeuft"
    status: done
  - id: step-1b-single-hit-test
    content: "Test: vg_sweep_aabb findet Kollision, liefert korrekten gap_num/gap_denom und normal_axis"
    status: done
  - id: step-1c-earliest-hit-test
    content: "Test: vg_sweep_aabb waehlt naehestes Obstacle (Kreuzprodukt-Vergleich korrekt)"
    status: done
  - id: step-1d-tie-test
    content: "Test: gleiche Distanz, zwei Obstacles – konsistente Auswahl (niedrigste obstacle_id)"
    status: done
  - id: step-1e-max-time-test
    content: "Test: Obstacle ausserhalb max_t_ms wird nicht gemeldet"
    status: done
  - id: step-1f-impl
    content: "Impl: vg_sweep_aabb() in src/fx_collision.h + src/fx_collision.c"
    status: done
  - id: step-2a-clj-nil-test
    content: "Test: fx/sweep-aabb gibt nil zurueck wenn kein Obstacle im Weg"
    status: done
  - id: step-2b-clj-hit-test
    content: "Test: fx/sweep-aabb gibt {:hit-id :normal} Map zurueck"
    status: done
  - id: step-2c-clj-impl
    content: "Impl: fx/sweep-aabb Builtin – liest Clojure-Seq direkt via subjective-c"
    status: done
  - id: step-2d-interpolate-test
    content: "Test: fx/interpolate-segment liefert Positionen an Grenzen und in der Mitte"
    status: done
  - id: step-2e-interpolate-impl
    content: "Impl: fx/interpolate-segment als C-Builtin (integer-Lerp)"
    status: done
  - id: step-3a-validate-hit-test
    content: "Test: apply-segment-end mit :collision, Obstacle vorhanden – Response korrekt"
    status: done
  - id: step-3b-phantom-test
    content: "Test: apply-segment-end mit :collision, Obstacle verschwunden – Phantom ignoriert, replan"
    status: done
  - id: step-3c-impl
    content: "Impl: Validate-at-End in apply-segment-end-at-ms"
    status: done
  - id: step-4a-brick-in-path-test
    content: "Test: choose-segment-target gibt Segment mit :collision zurueck wenn Brick in Bahn"
    status: done
  - id: step-4b-brick-before-wall-test
    content: "Test: Brick naeher als Wand – Brick gewinnt"
    status: done
  - id: step-4c-hit-response-test
    content: "Test: Segment-Ende Brick-Hit entfernt Brick, kehrt Velocity, plant neues Segment"
    status: done
  - id: step-4d-sound-test
    content: "Test: :brick-hit in :events, kein separates on-spatial-event! noetig"
    status: done
  - id: step-4e-choose-impl
    content: "Impl: choose-segment-wall -> choose-segment-target via fx/sweep-aabb"
    status: done
  - id: step-4f-end-impl
    content: "Impl: apply-segment-end-at-ms Brick-Fall mit Validate-at-End"
    status: done
  - id: step-4g-bricks-hashmap
    content: "Impl: :bricks als {id->brick} HashMap fuer O(1) lookup; levels.clj + scene.clj migriert"
    status: done
  - id: step-5a-no-brick-rules-test
    content: "Test: Breakout-Szene enthaelt keine :ball-vs-brick SpatialRules"
    status: done
  - id: step-5b-rules-impl
    content: "Impl: Ball-vs-Brick SpatialRules aus with-expanded-collision-rules entfernen"
    status: done
  - id: step-5c-paddle-only-impl
    content: "Impl: on-spatial-event! / flush-scene! auf Paddle-only reduzieren"
    status: done
  - id: step-5d-direct-dispatch-impl
    content: "Impl: on-spatial-event! zu direktem Dispatch vereinfachen"
    status: done
  - id: step-5e-coalescing-timer-impl
    content: "Impl: scene-flush Coalescing-Timer entfernt; paddle-only braucht kein Batching"
    status: done
  - id: step-5f-brick-ranker-cleanup
    content: "Cleanup: overlap-width/height, ball-vs-brick branch, brick-rule-* Helpers entfernt"
    status: done
isProject: true
---

# Predictive CCD – `fx/sweep-aabb` als Engine-Primitive

## Plan-Ort (Single Source of Truth)

Dieser Plan lebt **nur** unter
[`Plans/predictive_ccd_sweep_aabb.plan.md`](predictive_ccd_sweep_aabb.plan.md).

---

## Problem

20–40 ms Roundtrip-Latenz: C-AABB erkennt Brick-Kollision → Ingress-Queue →
Interpreter-Thread → Clojure-Replan → Render-Thread.
Waehrend dieser Zeit animiert der Render-Thread den Ball entlang des alten Segments
→ Ball fliegt durch den Brick → Ruckler + Sound-Versatz.

**Root cause:** Zwei getrennte Systeme ohne gemeinsames Zeitmodell.

| System | Art | Latenz |
|--------|-----|--------|
| C-AABB (reaktiv, per Frame) | entdeckt Overlap post-facto | 20–40 ms |
| Clojure-Segment (praediktiv) | geplant, Wand-zu-Wand | 0 ms |

Das C-AABB-System weiss nichts vom geplanten Segment; das Clojure-Segment weiss
nichts von Hindernissen.

---

## Loesung

Generisches `vg_sweep_aabb` / `fx/sweep-aabb`: Game-Script sweept das bewegte
AABB waehrend der Segmentplanung gegen alle Hindernisse (Waende und Bricks in
derselben Seq). Segment endet exakt am ersten Treffer. Response passiert
synchron auf dem Interpreter-Thread.

Das C-AABB-Ingress-System bleibt fuer **dynamische** Kollisionen (Paddle,
Proximity-Trigger, NPC-vs-NPC).

---

## Invalidierungsprotokoll

| Fall | Trigger | Mechanismus |
|------|---------|-------------|
| **A – Reaktiv** | C-AABB mid-segment (Paddle) | Vorhanden: `:self-aabb` im Event → `anchor-ball` → replan → neue Segment-ID → `rearm-timeline-watch-edge!` |
| **B – Input mid-segment** | Jump, Richtungswechsel | `fx/interpolate-segment` C-Builtin (via `vg_anim_progress_q13` + `vg_anim_lerp_q13`) liefert pixel-deterministische Position |
| **C – Obstacle verschwunden** | Geplantes Obstacle nicht mehr vorhanden | **Validate-at-End:** Existenz-Check → vorhanden: Response; fehlt: Phantom ignorieren + replan ab Endpunkt |

---

## Arithmetik und Typen

### Wertebereich (ESP32, 320 × 240 Display)

| Groesse | Bereich | Typ |
|---------|---------|-----|
| Koordinaten x, y | 0 .. 320 / 0 .. 240 | `int32_t` |
| Velocity vx, vy | ±1 .. ±10 px/ms | `int32_t` |
| Segment-Dauer | 0 .. ~5000 ms | `int32_t` |
| Kreuzprodukt `gap · |v|` | max 320 × 10 = 3 200 | `int32_t` ✅ |
| Max-Time-Check `max_t · |v|` | max 5000 × 10 = 50 000 | `int32_t` ✅ |
| `time_ms` Rueckgabe | max 5000 | `int32_t` → Fixnum ✅ |

### Keine Division im Inner Loop

Slab-Methode naiv: 2 Divisionen pro Obstacle (Xtensa LX6: ~38 Cycles/Div).

**Kreuzprodukt-Vergleich** (divisionsfrei):

`gap_x / |vx| < gap_y / |vy|` ⟺ `gap_x · |vy| < gap_y · |vx|`

Alle Terme ≤ 3200 → kein Overflow in int32_t. Kandidatenvergleich analog.

**Einzige Division:** am Ende, einmal, fuer `time_ms = gap_num / gap_denom`.

### Kein Stack-Array, keine Konvertierung

`vg_sweep_aabb` liest die Obstacle-Seq direkt via subjective-c
(`seq_first`, `seq_rest`, `map_get`, `as_fixnum`). Keyword-IDs fuer
`:x`, `:y`, `:w`, `:h`, `:id` werden beim Startup gecacht.

- Keine `VgAabb obstacles_buf[64]` Stack-Allokation
- Kein 64-Eintraege-Limit
- Kein Brick-Cache in C – Clojure-State ist einzige Quelle
- Kein Sonder-Code fuer Waende in C – Waende sind Clojure-Maps wie Bricks

### Loop-Invariant-Hoisting

```
// Einmalig vor der Schleife:
bool moving_right = (vx > 0);
bool moving_down  = (vy > 0);
int32_t abs_vx = moving_right ? vx : -vx;
int32_t abs_vy = moving_down  ? vy : -vy;
int32_t max_gap_x = (abs_vx > 0) ? max_t_ms * abs_vx : INT32_MAX;
int32_t max_gap_y = (abs_vy > 0) ? max_t_ms * abs_vy : INT32_MAX;
```

---

## ESP32-Performance-Verbesserung

Das praediktive Modell verbessert die ESP32-Performance strukturell:

| Bereich | Aktuell (reaktiv, pro Frame) | Neu (praediktiv, pro Segment) |
|---------|------------------------------|-------------------------------|
| C-AABB-Tests/sec (47 Bricks) | 60fps × 47 ≈ 2800 | 1/seg × 51 ≈ 34 |
| `with-expanded-collision-rules` | Jedes `publish-state!` | entfaellt fuer Bricks |
| GC-Druck pro Brick-Hit | SpatialEvent-Map | 0 |
| Coalescing-Delay | 1 ms blockiert einzigen Thread | entfaellt |
| Clojure-Eval pro Brick-Hit | Backtracking + Rule-Rebuild | Validate-at-End (O(1)) |

ESP32 ist single-threaded (ein FreeRTOS-Task fuer Rendering + Clojure).
Jede gesparte Operation geht direkt ins Frame-Budget.

---

## Phase 1 – C-Primitive `vg_sweep_aabb`

Dateien: `src/fx_collision.h`, `src/fx_collision.c`, `src/tests/test_sweep_aabb.c` (neu)

### Signatur

```c
typedef struct {
    int32_t obstacle_id;   /* -1 = kein Treffer */
    int32_t gap_num;       /* Zeitpunkt als Bruch: gap_num / gap_denom ms */
    int32_t gap_denom;     /* == |vx| oder |vy| je nach Eintrittsachse   */
    int     normal_axis;   /* 0 = horizontal (links/rechts), 1 = vertikal */
} VgSweepResult;

VgSweepResult vg_sweep_aabb(VgAabb       mover,
                             int32_t      vx,
                             int32_t      vy,
                             ID           obstacles_seq,
                             int32_t      max_t_ms,
                             EvalState   *st);
```

### Algorithmus (Inner Loop, divisionsfrei)

```
Keyword-IDs gecacht: kw_x, kw_y, kw_w, kw_h, kw_id (Startup)

Lese Velocity-Vorzeichen und |v| einmalig (hoisted)
Berechne max_gap_x, max_gap_y einmalig

Fuer jedes Obstacle in obstacles_seq (via seq_first/seq_rest):
  Extrahiere x, y, w, h, id via map_get + as_fixnum
  Berechne obs_aabb: {min_x=x, max_x=x+w, min_y=y, max_y=y+h}

  Berechne Eintritts-Luecke gap_x, gap_y (Vorzeichen-abhaengig)
  Ueberspringe wenn negativ auf beiden Achsen (schon vorbei)
  Max-Time-Early-Exit: gap > max_gap → skip

  Eintritts-Zeitpunkt: max(gap_x/|vx|, gap_y/|vy|) via Kreuzprodukt
  Austritts-Zeitpunkt: min(exit_x/|vx|, exit_y/|vy|) via Kreuzprodukt
  Ueberspringe wenn t_exit < t_entry

  Vergleiche mit bisherigem Besten (Kreuzprodukt)
  Bei Gleichstand: niedrigere obstacle_id gewinnt
```

### Tests

**1a – kein Treffer:** Ball links, Velocity links, Obstacle rechts → no-hit.

**1b – einfacher Treffer:**
```
mover: {min_x=0, max_x=8, min_y=46, max_y=54}
vx=3, vy=0
obstacle id=1: {x=30, y=46, w=16, h=16}
max_t_ms=1000
expect: obstacle_id=1, gap_num=22, gap_denom=3, normal_axis=0
verify: mover.max_x + vx * (gap_num/gap_denom) == obs.min_x
```

**1c – naehestes gewinnt:** Zwei Obstacles bei t≈7ms und t≈14ms → erstes.

**1d – Gleichstand:** Gleiche Distanz → niedrigere ID.

**1e – max_t_ms:** Obstacle bei t=200ms, max_t_ms=100 → no-hit.

**Pruefung:** `./build/unit-tests --test "test_sweep_aabb/*"`

---

## Phase 2 – Clojure-Builtins

Dateien: `src/builtins.c` (native_-Registrierung), `src/fx_collision.c`

### `fx/sweep-aabb`

```
(fx/sweep-aabb {:x 0 :y 50 :w 8 :h 8}    ; mover
               {:vx 3 :vy 0}               ; velocity
               [{:id 1 :x 30 :y 46 :w 16 :h 16}]  ; obstacles (beliebige Seq)
               1000)                        ; max-ms
→ {:hit-id 1 :time-ms 7 :normal :left}
→ nil (kein Treffer)
```

Implementierung: C-Builtin (`native_fx_sweep_aabb`), liest Mover/Velocity via
`map_get` + `as_fixnum`, iteriert Obstacle-Seq direkt, ruft `vg_sweep_aabb` auf,
konvertiert Ergebnis zu Clojure-Map. Einzige Division: `time_ms = gap_num / gap_denom`.

Walls sind normale Obstacles in der Seq – kein Sonder-Code.

### `fx/interpolate-segment`

```
(fx/interpolate-segment {:start-ms 0 :end-ms 1000 :from-x 0 :from-y 0 :to-x 100 :to-y 200}
                        500)
→ {:x 50 :y 100}
```

Implementierung: C-Builtin (`native_fx_interpolate_segment`), ruft intern
`vg_anim_progress_q13(elapsed, duration)` und `vg_anim_lerp_q13(from, to, t)` auf.
Exakt die gleichen Funktionen, die der Renderer fuer Timeline-Interpolation verwendet
→ pixel-deterministisch.

### Tests

**2a:** `fx/sweep-aabb` → nil bei keinem Hit.

**2b:** `fx/sweep-aabb` → korrekte Map.

**2d:** `fx/interpolate-segment` an Grenzen (0ms, 1000ms) und Mitte (500ms).

**Pruefung:** `./build/unit-tests --test "test_sweep_aabb/*"` + Clojure-REPL

---

## Phase 3 – Generisches Validate-at-End-Protokoll

Kein Breakout-spezifischer Code.

Segment-Datenstruktur erhaelt optionales Feld:
```
{:id 7
 :start-ms 1000 :end-ms 1847
 :from-x 40 :from-y 80 :to-x 72 :to-y 32
 :collision {:hit-id 3 :normal :bottom}}  ; optional
```

### `apply-segment-end`

Erhaelt drei Fns als Parameter:

```clojure
(apply-segment-end state segment-id now-ms
                   obstacle-lookup-fn   ; (fn [id]) → obstacle oder nil
                   on-collision!-fn     ; (fn [state obstacle collision] → new-state
                   replan-fn)           ; (fn [state now-ms] → new-state
```

Ablauf:
```
wenn Segment :collision hat:
  hit = (obstacle-lookup-fn (:hit-id collision))
  wenn hit vorhanden:
    (on-collision!-fn state hit collision)
  sonst:
    Phantom-Hit → (replan-fn state now-ms)
wenn Segment kein :collision hat:
  normale Wand-Logik (unveraendert)
```

### Tests

**3a:** Obstacle vorhanden → `on-collision!` aufgerufen, State aktualisiert.

**3b:** Obstacle nil → Phantom ignoriert, `replan-fn` aufgerufen, neues Segment.

---

## Phase 4 – Breakout als erster Nutzer

Dateien: `libs/tiny-breakout/core.clj`, `libs/tiny-breakout/runtime.clj`

### `choose-segment-target` (ersetzt `choose-segment-wall`)

```clojure
(defn- choose-segment-target [ball-x ball-y vx vy bricks]
  (let [mover    {:x ball-x :y ball-y :w ball-size :h ball-size}
        velocity {:vx vx :vy vy}
        ;; Waende als normale Obstacle-Maps (unveraenderlich pro Level):
        walls    (wall-obstacles)
        all      (concat walls (map brick->obstacle bricks))
        budget   5000  ; max Segment-Dauer ms
        hit      (fx/sweep-aabb mover velocity all budget)]
    (if hit
      (hit->segment ball-x ball-y vx vy hit)
      nil)))
```

`wall-obstacles` gibt 4 Maps zurueck (def, level-konstant).
`brick->obstacle` konvertiert `{:id :x :y :w :h :points}` → `{:id :x :y :w :h}`.

### `apply-segment-end-at-ms` – Brick-Fall

```clojure
(defn apply-segment-end-at-ms [state segment-id now-ms]
  (let [segment (:ball-segment state)]
    (if (or (nil? segment) (not= (:id segment) segment-id))
      (clear-events state)  ; stale → ignore
      (let [collision (:collision segment)]
        (if collision
          ;; Validate-at-End:
          (let [hit-id    (:hit-id collision)
                brick     (find-brick-by-id (:bricks state) hit-id)]
            (if brick
              ;; Brick vorhanden → Response:
              (let [normal    (:normal collision)
                    bounced   (bounce-velocity state normal)
                    anchored  (anchor-ball state (:to-x segment) (:to-y segment))
                    removed   (remove-brick-by-id (:bricks anchored) hit-id)
                    advanced  (-> anchored
                                 (assoc :bricks (:bricks removed)
                                        :ball-vx (:vx bounced)
                                        :ball-vy (:vy bounced)
                                        :score (+ (:score anchored) (:points (:hit removed)))
                                        :events (conj (:events anchored) :brick-hit)))]
                (plan-next-segment advanced now-ms))
              ;; Phantom → weiterplanen:
              (plan-next-segment (anchor-ball state (:to-x segment) (:to-y segment))
                                 now-ms)))
          ;; Kein :collision → Wall-Logik (unveraendert):
          (apply-wall-bounce state segment now-ms))))))
```

### Tests

**4a:** Brick in Trajektorie → Segment `:collision` korrekt.

**4b:** Brick naeher als Wand → Brick gewinnt.

**4c:** Segment-Ende: Brick entfernt, Velocity umgekehrt, neues Segment geplant.

**4d:** `:brick-hit` in `:events`, kein separates `on-spatial-event!`.

**Pruefung:** `./build/unit-tests --test "test_breakout*"` vollstaendig gruen.
Manuell: kein Ball-durch-Brick.

---

## Phase 5 – Abzuestung und Vereinfachung

*Erst wenn Phase 4 vollstaendig gruen ist.*

### Tier 1 – Vollstaendig entfernbar (Brick-Latenz-Kompensation)

| Was | Datei | Warum |
|-----|-------|-------|
| `brick-event-backtrack-rank` | `libs/tiny-breakout/core.clj` | Nachtraegliches Ranking → Sweep macht das voraus |
| `axis-backtrack-distance` | `libs/tiny-breakout/core.clj` | Helper fuer Backtracking |
| `better-brick-event?` | `libs/tiny-breakout/core.clj` | Vergleich verspeateter Events |
| `select-earliest-brick-event` | `libs/tiny-breakout/core.clj` | Auswahl aus Batch |
| `split-same-snapshot-brick-events` | `libs/tiny-breakout/core.clj` | Batch-Trennung |
| `last-brick-bounce-gen` im State | `libs/tiny-breakout/core.clj` | Duplicate-Filter |
| `pending-brick-snapshot-gen*` | `libs/tiny-breakout/runtime.clj` | Snapshot-Gen-Tracking |
| `:ball-vs-brick` Case in `apply-spatial-event` | `libs/tiny-breakout/core.clj` | Gesamter Brick-Reaktions-Zweig |
| Ball-vs-Brick `SpatialRules` | `libs/tiny-breakout/scene.clj` | C-AABB Brick-Regeln |
| Brick-Expansion in `with-expanded-collision-rules` | `libs/tiny-breakout/scene.clj` | Rule-Rebuild-Logik fuer Bricks |

### Tier 2 – Vereinfachbar (generisch, ueberbleibsel)

| Was | Heute | Nach Migration |
|-----|-------|---------------|
| `pending-spatial-events*` + `flush-scene!` | Batcht N Brick + 1 Paddle | Nur Paddle → direkter Dispatch |
| `schedule 1` Coalescing-Timer | Fenster fuer Brick-Batch | Fuer Paddle sinnlos → entfernen |
| `on-spatial-event!` | Queuing + Scheduling | Vereinfacht zu direktem `apply-spatial-event` |
| `with-expanded-collision-rules` | Pflegt Paddle + Brick-Regeln | Pflegt nur eine Paddle-Regel |

### Tier 3 – Bleibt (weiterhin benoetigt)

| Konzept | Warum |
|---------|-------|
| C-AABB-Infrastruktur fuer Paddle | Paddle durch User-Input unvorhersagbar |
| `segment-watch-segment-id*` + `rearm-timeline-watch-edge!` | Paddle-Hit mid-segment |
| `anchor-ball` + `ball-anchor-from-event` (Paddle-Pfad) | Position aus C-AABB-Event |
| Timeline-End-Watcher + `apply-segment-end-at-ms` | Segment-Ende triggert Replan |
| `vg_sweep_aabb` + `fx/sweep-aabb` | Neues Primitive |
| `fx/interpolate-segment` | Fall B: Input mid-segment |

### Schritte

**5a:** Test: Breakout-Szene enthaelt keine `:ball-vs-brick` SpatialRules.

**5b:** `with-expanded-collision-rules` Brick-Expansion entfernen.

**5c:** `on-spatial-event!` / `flush-scene!` auf Paddle-only reduzieren.

**5d:** `on-spatial-event!` zu direktem Dispatch vereinfachen,
       `pending-spatial-events*` Atom entfernen.

**5e:** `schedule 1` Coalescing-Timer entfernen.

**5f:** Tier-1-Funktionen entfernen (Brick-Ranker, Backtracking, Snapshot-Gen).

**Pruefung:** `./build/unit-tests` vollstaendig. Paddle-Kollision unveraendert.

---

## Relevante Dateien

| Datei | Aenderung |
|-------|-----------|
| `src/fx_collision.h` | `VgSweepResult` Typ, `vg_sweep_aabb()` Deklaration |
| `src/fx_collision.c` | `vg_sweep_aabb()` Implementierung |
| `src/tests/test_sweep_aabb.c` | neue Testdatei Phase 1 |
| `src/builtins.c` | `native_fx_sweep_aabb`, `native_fx_interpolate_segment` Registrierung |
| `src/vector_scene_graph.h` | Bestehendes `vg_anim_lerp_q13`, `vg_anim_progress_q13` (benutzt, nicht geaendert) |
| `libs/tiny-breakout/core.clj` | `choose-segment-target`, `apply-segment-end-at-ms` Brick-Fall, Tier-1-Entfernung |
| `libs/tiny-breakout/scene.clj` | Phase 5: Ball-vs-Brick-Rules entfernen |
| `libs/tiny-breakout/runtime.clj` | Phase 5: Paddle-only, Tier-2-Vereinfachung |

---

## Scope / Abgrenzung

- `vg_sweep_aabb`: einziges Swept-AABB-Primitive – kein zweiter Sweep-Pfad (DRY)
- `fx/interpolate-segment`: einzige Interpolation – via Renderer-C-Code (DRY)
- Kein Float, kein Q19.13 in Sweep – Integer-Only, eine Division am Schluss
- Kein `add-watch`-Replan fuer Obstacle-Aenderungen – Validate-at-End genuegt
- Paddle bleibt reaktiv (C-AABB) – Player-Input unvorhersagbar
- Gravity/Acceleration liegt im Spiel-Script (Velocity pro Segment konstant)
- Waende sind normale Obstacle-Maps – kein C-Sondercode
- Obstacle-Seq wird via subjective-c direkt gelesen – keine Konvertierung
