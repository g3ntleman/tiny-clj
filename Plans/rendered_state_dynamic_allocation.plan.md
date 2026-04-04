---
name: g_rendered_slots eliminieren
overview: >
  g_rendered_slots belegt 41 580 Bytes BSS (ESP32). Die Datenstruktur dupliziert
  Entity-Zustand, der bereits im retained Szenen-Snapshot (immutable subjective-c
  Records) vorliegt. Entities ohne Timeline werden vom Render-Thread nicht
  veraendert — Clojure kann sie direkt aus dem retained Snapshot lesen. Entities
  mit Timeline brauchen eine kleine Shallow-Copy mit den aktualisierten
  Timeline-Feldern. Dirty-Rect-Vergleich kann auf dem Diff zweier retained
  Szenen-Snapshots basieren. Der einzige grosse Buffer bleibt der globale
  RGB-Pixel-Buffer (g_render_buffer), der bereits geteilt ist.
status: active
todos:
  - id: baseline-test
    content: >
      Test der aktuellen BSS-Groesse von g_rendered_slots als
      Regressions-Anker. Prueft sizeof und Gesamtbelegung.
    status: completed
  - id: query-entity-from-scene
    content: >
      query_entity fuer Entities OHNE Timeline so umstellen, dass es
      direkt aus dem retained Szenen-Snapshot liest statt aus
      g_rendered_slots. Bestehende Tests muessen gruen bleiben.
    status: completed
  - id: timeline-overlay
    content: >
      Kleine Timeline-Overlay-Tabelle einfuehren: nur Entities mit
      aktiver Timeline bekommen einen Eintrag (entity_id + shallow-copy
      der aktualisierten Felder). Vom Render-Thread geschrieben, vom
      Clojure-Thread gelesen. query_timeline liest aus dem Overlay.
    status: completed
  - id: query-entity-with-timeline
    content: >
      query_entity fuer Entities MIT Timeline: Overlay-Eintrag hat
      Vorrang, Basis-Daten kommen weiterhin aus dem retained Snapshot.
      Zusammenfuehrung in der Query-Funktion.
    status: completed
  - id: collision-from-render-context
    content: >
      Collision-Detection und Dirty-Rect lesen jetzt aus dem
      dynamischen Timeline-Overlay statt aus g_rendered_slots.
      fx_collision_detect_step, builtins query_entity/query_timeline,
      und collect_dirty_rects alle auf overlay API umgestellt.
    status: completed
  - id: dirty-rect-from-scene-diff
    content: >
      Dirty-Rect-Vergleich laeuft jetzt ueber das Overlay
      (vg_timeline_overlay_capture_compute_dirty_rect /
      vg_timeline_overlay_capture_collect_dirty_rects).
      Alle Tests migriert.
    status: completed
  - id: remove-g-rendered-slots
    content: >
      g_rendered_slots, RenderedSlotStore, RenderedSlotSnapshot,
      RenderedEntityRow, RenderedTimelineRow und die zugehoerige
      alte query/dirty-rect API entfernt. Capture-Funktionen
      schreiben nur noch ins Overlay. Lazy-Init in capture_begin
      fuer Abwaertskompatibilitaet mit Tests.
    status: completed
  - id: bss-validation
    content: >
      vg_rendered_state_static_footprint() gibt 0 zurueck.
      Baseline-Test verifiziert footprint == 0.
      2116 Tests, 0 Failures.
    status: completed
isProject: false
---

# g_rendered_slots eliminieren

## Ist-Zustand

```
g_rendered_slots[3]             41 580 Bytes BSS (ESP32)

RenderedSlotStore = buffers[2]  (Double-Buffer pro Slot)
RenderedSlotSnapshot =
    Header (16 B)
  + RenderedEntityRow[64]       (52 B × 64 = 3 328 B)   ← dupliziert Scene-Daten
  + RenderedTimelineRow[128]    (28 B × 128 = 3 584 B)   ← einziger neuer Zustand
  ≈ 6 928 B pro Snapshot
```

Der RGB-Pixel-Buffer (`g_render_buffer`) ist separat, global geteilt ueber alle
Slots und wird hier nicht angefasst.

## Kern-Einsicht

Die Clojure-Runtime liefert eine **deklarative, persistente Szenen-Beschreibung**
als subjective-c Records. Der Render-Thread bekommt eine billige Kopie via
RETAIN. Daraus folgt:

| Entity-Typ | Zustandsquelle | g_rendered_slots noetig? |
|---|---|---|
| Ohne Timeline | Retained Scene-Snapshot (immutable) | Nein — Clojure liest direkt |
| Mit Timeline | Render-Thread aktualisiert Felder | Nur Timeline-Delta |

Drei Consumer lesen heute aus `g_rendered_slots`:

| Consumer | Thread | Braucht | Alternative |
|---|---|---|---|
| `query_entity` (Builtins) | Clojure | world_t, AABB | Retained Scene-Snapshot (+ Timeline-Overlay) |
| `query_timeline` (Builtins) | Clojure | timeline_sample | Timeline-Overlay |
| `fx_collision_detect_step` | Render | AABB | Direkt aus Render-Kontext (gleicher Cycle) |
| `compute_dirty_rect` | Render | AABB, content_sig | Scene-Diff (prev vs. current) |

Collision-Detection laeuft auf dem Render-Thread direkt nach dem Rendern
(`fx_host_app.c:1115`). Die Entity-AABBs werden waehrend des Render-Traversals
berechnet — sie stehen im selben Cycle zur Verfuegung, ohne Cross-Thread-Zugriff.

`g_rendered_slots` dupliziert Entity-Zustand (world_t, AABB), der bereits
im retained Snapshot bzw. im Render-Kontext vorliegt. Nur die Timeline-Felder
(phase_ms, step_index, at_end, ...) sind genuiner Render-Thread-Zustand.

## Ziel-Architektur

```
Clojure-Thread                              Render-Thread

  scene-v2 = (assoc scene ...)              prev-scene (RETAIN'd)
  RETAIN(scene-v2)                          new-scene (RETAIN'd)
  publish(scene-v2)  ───────────────→
                                            Dirty-Rect = diff(prev-scene, new-scene)
                                              Fuer jedes Dirty-Rect-Stueck:
                                                clear + render → Pixel-Buffer

                                            Fuer Entities mit Timeline:
                                              Shallow-Copy mit aktualisierten Feldern
                                              → Timeline-Overlay-Tabelle

  query_entity(eid):                        RELEASE(prev-scene)
    hat Overlay? → merge Overlay + Scene    prev = new
    sonst → direkt aus retained Scene

  query_timeline(eid, field):
    → aus Timeline-Overlay
```

### Timeline-Overlay

Kleine, dynamisch wachsende Tabelle (nur Entities mit aktiver Timeline):

```c
typedef struct {
    uintptr_t entity_id_bits;
    uint8_t slot_index;
    VgRenderedTimelineSample sample;   // 20 B
    VgTransformFixed world_t;          // 24 B  (interpoliert)
} TimelineOverlayEntry;               // ≈ 52 B pro Entry
```

Typisch: 5-15 Entries → 260-780 Bytes (vs. 41 580 B).

Publish: Render-Thread schreibt in Write-Tabelle, Commit = Pointer-Swap
(analog zum heutigen active_buffer_index). Clojure-Thread liest via
atomic_load.

### Dirty-Rect aus Scene-Diff

Beide Szenen (prev + current) sind immutable subjective-c Records. Der
Render-Thread haelt jeweils einen RETAIN'd Pointer pro Slot:

```c
static ID g_prev_scene[MAX_SLOTS];   // RETAIN'd, Render-Thread only
```

Beim Rendern:
1. Traversiere current Scene, berechne world_t pro Entity
2. Vergleiche mit prev Scene (gleicher Entity-Key → Position geaendert?)
3. Geaenderte Entities liefern Dirty-Rects via AABB-Vergleich
4. Wenn Dirty-Rect > Pixel-Buffer → aufteilen, stueckweise rendern

`g_prev_scene` pro Slot ist nur ein Pointer (4-8 Bytes), kein Datenkopie.

## Schritte (test-first, DRY)

### 1. Baseline-Test

```c
// test_vector_scene_graph.c
void test_rendered_state_bss_baseline(void) {
    // Wird rot, sobald g_rendered_slots schrumpft/verschwindet
    TEST_ASSERT(sizeof(RenderedSlotStore) > 0);
}
```

### 2. query_entity aus retained Scene (ohne Timeline)

**Test zuerst**: Neuer Test, der ein Entity ohne Timeline aus dem retained
Scene-Snapshot liest und das gleiche Ergebnis wie query_entity erhaelt.

**Implementierung**: In `builtins_tiny_fx_gfx.c` den query_entity-Pfad fuer
nicht-animierte Entities auf den retained Snapshot umlenken. Der Snapshot ist
bereits als `g_scene_slot_snapshots[slot]` (RETAIN'd ID) im fx_host_app
verfuegbar.

Wiederverwende die vorhandene Scene-Traversal-Logik zum Extrahieren von
world_t — kein neuer Code fuer Transform-Berechnung.

### 3. Timeline-Overlay einfuehren

**Test zuerst**: Test, der ein Entity mit aktiver Timeline rendert und
prueft, dass die Timeline-Daten aus dem Overlay lesbar sind.

**Implementierung**:
- Kleine dynamische Tabelle (realloc bei Bedarf, initial 16 Entries)
- `vg_rendered_state_capture_record_timeline()` schreibt ins Overlay
  statt in RenderedSlotSnapshot
- Double-Pointer-Swap fuer atomare Publikation (wie heute)
- Allokation auf dem Clojure-Thread bei Init

### 4. query_entity mit Timeline-Merge

**Test zuerst**: Test, der ein Entity mit Timeline abfragt und prueft, dass
die interpolierte world_t aus dem Overlay kommt, waehrend Basis-Daten
(z.B. style, children) aus dem Scene-Snapshot stammen.

**Implementierung**: query_entity prueft zuerst das Overlay. Treffer →
world_t aus Overlay. Kein Treffer → world_t aus retained Scene.

### 5. Collision-Detection aus Render-Kontext

**Test zuerst**: Bestehende Collision-Tests (test_breakout_runtime_startup.c)
muessen gruen bleiben, nachdem fx_collision_detect_step nicht mehr ueber
vg_rendered_state_query_entity geht.

**Implementierung**: Waehrend des Render-Traversals werden Entity-AABBs
bereits berechnet (scene.c:504). Statt sie in g_rendered_slots zu schreiben,
fuellt der Traversal eine leichtgewichtige, Render-Thread-lokale AABB-Tabelle:

```c
typedef struct {
    uintptr_t entity_id_bits;
    VgAabb world_aabb;
} RenderCycleEntityAabb;   // 20 B auf ESP32
```

Lebensdauer: ein Render-Cycle. Wird am Anfang jedes Cycles zurueckgesetzt.
fx_collision_detect_step liest daraus statt aus g_rendered_slots.
Dirty-Rect-Vergleich nutzt dieselbe Tabelle — eine Datenquelle, zwei Consumer
(DRY).

### 6. Dirty-Rect aus Scene-Diff

**Test zuerst**: Test mit zwei Scene-Snapshots (Entity bewegt sich),
prueft berechnetes Dirty-Rect.

**Implementierung**:
- Render-Thread haelt `prev_scene` (RETAIN'd) pro Slot
- Dirty-Rect-Berechnung traversiert current + prev Scene parallel
- Entity-Matching ueber entity_id (Key im Entity-Map)
- AABB-Vergleich fuer geaenderte Entities
- Zu grosse Dirty-Rects werden in Pixel-Buffer-passende Stuecke geteilt

Wiederverwende `vg_dirty_rect_plan_passes()` fuer die Aufteilung —
die Funktion existiert bereits.

### 7. g_rendered_slots entfernen

**Test zuerst**: Alle bestehenden Tests, die g_rendered_slots /
capture_record_entity / query_entity nutzen, auf die neue Architektur
umstellen.

**Implementierung**:
- RenderedSlotStore, RenderedSlotSnapshot, RenderedEntityRow,
  RenderedTimelineRow, g_rendered_slots, g_capture_ctx entfernen
- capture_begin / capture_commit / capture_discard entfernen
- record_entity / record_entity_aabb / record_entity_content_signature
  entfernen
- compute_dirty_rect / collect_dirty_rects entfernen
  (ersetzt durch Scene-Diff)

### 8. BSS-Validation

```c
// Pruefe, dass rendered_state_snapshot.o keinen BSS-Beitrag mehr hat
// (oder dass das Modul ganz entfaellt)
```

## Speichervergleich

```
                          Ist (ESP32)    Ziel
g_rendered_slots BSS      41 580 B       0 B
Timeline-Overlay Heap          0 B     ~800 B  (15 Entries)
g_prev_scene Pointer           0 B      12 B   (3 Slots × 4 B)
                          ----------   ------
Total                      41 580 B    ~812 B
```
