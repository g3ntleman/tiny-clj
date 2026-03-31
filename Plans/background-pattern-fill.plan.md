---
name: Background Pattern Fill
overview: "Neues Pattern-Record in gfx-records.clj, Rename :erase-color -> :background in FrameScene/Scene, parametrischer Hintergrund-Effekt mit Integer-only Arithmetik, SIN[256] LUT, 16er-Palette aus 2 Farben, Single-Pass mit 32-Bit-Writes."
todos:
  - id: choose-effect
    content: "Effekt aus der Kandidatenliste auswaehlen (via HTML-Preview experimentieren)"
    status: pending
  - id: pattern-record
    content: "Pattern-Record in gfx-records.clj definieren (effect-type, color-a, color-b, freq, phase, spread) + C-Overlay in tiny_fx_gfx.h"
    status: pending
  - id: rename-erase-to-background
    content: ":erase-color -> :background in FrameScene/Scene (gfx-records.clj, tiny_fx_gfx.h/.c, scene.c, tests, breakout, game-demo)"
    status: pending
  - id: optimize-solid-clear
    content: vg_framebuffer_clear und vg_framebuffer_clear_rect auf 32-Bit-Writes umstellen
    status: pending
  - id: sin-lut
    content: "SIN_LUT[256] (int8) + ATAN_LUT[65] (uint8) beim Startup mit sinf/atanf erzeugen"
    status: pending
  - id: palette-derivation
    content: "RGB565-Palette[16] aus Pattern-Record ableiten: Mittelpunkt + symmetrische Stufen mit Spread"
    status: pending
  - id: effect-render
    content: "Effekt-Render-Funktion: pro Pixel Integer-only (SIN-Lookups, idist, shifts, imul), Single-Pass 32-Bit-Writes"
    status: pending
  - id: scene-integration
    content: ":background Typ-Dispatch: Fixnum -> Solid, Pattern-Record -> Effekt-Render; VgRenderSlot erweitern"
    status: pending
  - id: breakout-scene
    content: "Breakout-Szene auf :background mit Pattern-Record umstellen"
    status: pending
  - id: unit-tests
    content: Tests fuer Solid-Clear-Optimierung, Effekt-Render, Palette-Ableitung, Rename-Kompatibilitaet
    status: pending
  - id: cleanup
    content: Sourcecode aufraeumen – Debug-Code, temporaere Workarounds, tote Codepfade, ueberfluessige Kommentare und nicht mehr benoetigte Hilfsfunktionen entfernen
    status: pending
isProject: false
---

# Background Pattern Fill

Live-Preview: [experiments/plasma-preview.html](experiments/plasma-preview.html) (20 Effekte zum Ausprobieren)

## Constraints

- **Integer-only** im Hot-Loop (pro Pixel, pro Frame): kein float, kein sqrt, kein atan2
- **Kleine Tabellen**: SIN[256] (256 B) + ATAN[65] (65 B) = max 321 B; Tabellen per `sinf()`/`atanf()` beim Startup erzeugt
- **RAM-Budget**: max 2 KB fuer Tabellen + Palette
- **Palette**: 16 Farben (RGB565, 32 B), aus 2 Eingabefarben mit konfigurierbarem Spread abgeleitet
- **32-Bit-Writes**: Zwei RGB565-Pixel pro Store fuer schnellere Framebuffer-Zugriffe
- **Pro Frame live berechnet** (kein Precompute-Buffer)

## Effekt-Kandidaten

### Organisch / Natuerlich

| Effekt | Ops/px | Tabellen | Beschreibung |
|--------|--------|----------|-------------|
| **Plasma** | 3 imul + 4 SIN | SIN[256] | 4 Sinus-Summen + Integer-Distanz, weiche Kreisformen |
| **Interference** | 3 imul + 3 SIN + 3 idist | SIN[256] | 3 Punktquellen-Wellen ueberlagert |
| **Clouds** | 8 imul + 8 SIN | SIN[256] | Multi-Oktav Rauschen (4 Stufen, halbierende Amplitude) |
| **Domain Warp** | 6 imul + 6 SIN | SIN[256] | IQ-Style rekursive Sinus-Verzerrung, 2 Warp-Layer |
| **Caustics** | 3 imul + 3 SIN + 3 abs | SIN[256] | 3 Wellenfronten, invertiertes abs = Lichtbrechung |
| **XOR Warp** | 2 imul + 2 SIN + 1 XOR | SIN[256] | XOR-Muster mit Sinus-Verzerrung beider Achsen |
| **Lava** | 3 imul + 4 SIN + 4 abs | SIN[256] | Plasma + abs() = scharfe Risse/Kanten |

### Geometrisch / Strukturiert

| Effekt | Ops/px | Tabellen | Beschreibung |
|--------|--------|----------|-------------|
| **Moire** | 2 imul + 1 XOR + 2 idist | keine | XOR von 2 Integer-Distanzen, billigster Effekt |
| **Voronoi** | N*idist + min | keine LUT | Manhattan-Distanz zu N Seedpunkten, Zell-Muster |
| **Diamonds** | 2 mod + 1 SIN | SIN[256] | 45-Grad Rautengitter + Sinus-Modulation |
| **Hex Grid** | 2 div + idist + hash | keine LUT | Hexagonale Zellen mit Hash-Farbvariation |
| **Rotozoom** | 4 imul + 1 XOR | SIN[256] | Rotiertes XOR-Schachbrett via sin/cos-LUT |
| **Crosshatch** | 3 imul + 3 SIN + 2 mul | SIN[256] | 3 Lagen diagonaler Linien, Multiply-Blend |
| **Scales** | 2 div + 1 SIN + idist | SIN[256] | Ueberlappende Boegen (Fischschuppen) |

### Radial / Zentral

| Effekt | Ops/px | Tabellen | Beschreibung |
|--------|--------|----------|-------------|
| **Nova** | 2 imul + 3 SIN + 1 ATAN + 1 idiv | SIN+ATAN | Radialer Glow + Winkel-Modulation |
| **Wood** | 2 imul + 4 SIN + 1 idist | SIN[256] | Konzentrische Ringe + Sinus-Turbulenz |
| **Spiral** | 1 imul + 1 SIN + 1 ATAN + 1 idiv | SIN+ATAN | Spiralarme aus Winkel + Distanz |
| **Metaballs** | 3 idiv + 3 idist | keine LUT | 3 Blob-Zentren, Summe von 1/dist |

### Richtungsbasiert

| Effekt | Ops/px | Tabellen | Beschreibung |
|--------|--------|----------|-------------|
| **Marble** | 3 imul + 4 SIN | SIN[256] | Diagonale Adern mit Sinus-Turbulenz |

### Automaton (nur Precompute)

| Effekt | Ops/px | Tabellen | Beschreibung |
|--------|--------|----------|-------------|
| **Fire** | 4 add + 1 shift (N Passes) | buf[76800] | Pixel-Averaging-Automaton, eingefrorenes Feuer |

## Integer-Hilfsfunktionen (fuer alle Effekte)

```c
// Integer-Distanz: max(|a|,|b|) + 3*min/8 (~3.5% Fehler vs sqrt)
static inline int idist(int dx, int dy) {
    int ax = dx < 0 ? -dx : dx, ay = dy < 0 ? -dy : dy;
    int mn = ax < ay ? ax : ay, mx = ax > ay ? ax : ay;
    return mx + ((mn * 3) >> 3);
}
```

## Pattern-Record

```clojure
(defrecord Pattern [effect color-a color-b freq phase spread])
```

- `effect` – Keyword (`:plasma`, `:nova`, etc.) waehlt den Render-Algorithmus
- `color-a`, `color-b` – RGB565 Eingabefarben
- `freq` – Frequenz-Multiplikator (1-24)
- `phase` – Phase/Seed (0-255)
- `spread` – Palettenspreizung in % (5-100)

## Rename :erase-color -> :background

Betroffene Dateien:
- `libs/tiny-fx/gfx-records.clj`: FrameScene + Scene defrecord
- `src/tiny_fx_gfx.h/.c`: DEFRECORD, Symbol, Schema
- `src/scene.c`: decode_scene_fields, vg_decode_frame_slot_record
- `libs/tiny-breakout/scene.clj`: Szenen-Definition
- `src/tests/test_vector_scene_graph.c`: Teststrings

## Szenen-Integration

`VgRenderSlot` erweitern:

```c
typedef struct {
    // ... bestehende Felder ...
    uint16_t clear_color;
    bool has_pattern;
    uint8_t pattern_effect;      // enum: EFFECT_PLASMA, EFFECT_NOVA, ...
    uint16_t pattern_palette[16];
    uint8_t pattern_freq;
    uint8_t pattern_phase;
} VgRenderSlot;
```

`:background` Typ-Dispatch: Fixnum -> Solid, Pattern-Record -> Effekt-Render.
