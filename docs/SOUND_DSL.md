# Sound DSL

This document describes the public step DSL used by [`tiny-fx.trk1`](/Users/theisen/Projects/tiny-clj/libs/tiny-fx/trk1.clj) and how the compiled TRK1 bytes are played through [`tiny-fx.sound`](/Users/theisen/Projects/tiny-clj/libs/tiny-fx/sound.clj).

## Scope

The DSL is the data format accepted by:

- `tiny-fx.trk1/compile-track`
- `tiny-fx.trk1/track-duration-ms`
- `tiny-fx.trk1/prepare-track`

The low-level `trk1` byte format is separate. Most callers should author tracks at the DSL level, then explicitly compose `tiny-fx.trk1/prepare-track` with `tiny-fx.sound/sound-play-music!` or `tiny-fx.sound/sound-play-sfx!`.

## Note Symbols

Supported note forms:

- natural notes: `:C4`, `:D5`, `:A3`
- sharps: `:Cs4` or `:C#4`
- flats: `:Db4`, `:Bb3`
- rest: `:REST`
- **Frequency in Hz** as integer `0..20000` (e.g. `440` for A4, `0` = rest)

Rules:

- pitch letters are uppercase `A` through `G`
- octaves are single-digit
- keywords resolve to MIDI (`0..127`) via the internal table; **integers in steps are Hz**, not MIDI

`tiny-fx.trk1/note->midi` is the canonical converter.

## Duration Symbols

Supported duration forms:

- `:w` whole
- `:h` half
- `:q` quarter
- `:e` eighth
- `:s` sixteenth
- `:t` thirty-second
- `:dh` dotted half
- `:dq` dotted quarter
- `:de` dotted eighth
- `:ds` dotted sixteenth
- `:qt` quarter-note triplet
- `:et` eighth-note triplet
- `:st` sixteenth-note triplet
- integer milliseconds, e.g. `120`

Rules:

- duration keywords are interpreted relative to `:tempo-bpm`
- integer durations are interpreted directly as milliseconds
- integer durations must be `>= 1`
- `:tempo-bpm` is required when any step uses a duration keyword or falls back to the implicit default `:q`

## Step Shapes

### Generic Polyphonic Step

```clojure
{:notes [:G5 :D5 :Bb4] :duration :q}
```

```clojure
{:notes [440] :duration 120}
```

Meaning:

- one note per channel
- shorter note vectors are padded with `:REST`
- if `:duration` is omitted, it defaults to `:q`

### Compile-Time Bend Step

```clojure
{:notes [220] :bend [440] :duration 120}
```

Meaning:

- starts at the `:notes` frequency/frequencies
- linearly sweeps toward the matching `:bend` target frequencies
- expands at compile time into a bounded number of short `NOTE_HZ` segments
- preserves the original total step duration

Constraints in the current step:

- `:bend` currently works only in generic `:notes` mode
- `:notes` and `:bend` must use integer Hz values for bent channels
- `:bend` must be a vector and must not be wider than the resolved channel count
- `:bend` must not be combined with `:rest`

### Compile-Time Noise Step

```clojure
{:notes [220] :noise true :duration 120}
```

```clojure
{:melody :A4 :backing [440] :noise true :duration 120}
```

Meaning:

- `:noise` expands at compile time into a bounded number of short `NOTE_HZ` segments
- each segment stays near the specified base frequency or current bend path
- total step duration is preserved

Constraints in the current step:

- generic `:notes` mode currently allows `:noise` only for single-channel SFX-style steps
- noisy channels must use integer Hz values
- in melody/backing mode, `:noise` affects only backing channels
- `:noise` on a melody-only step is rejected

### Full-Rest Step

```clojure
{:rest :e}
```

```clojure
{:rest 90}
```

Meaning:

- all channels rest for the given duration

Constraint:

- a step must not contain both `:duration` and `:rest`

### Melody + Backing Step

```clojure
{:melody :G5 :backing [:D5 :Bb4] :duration :q}
```

Meaning:

- melody uses the first channel
- backing expands across the remaining channels
- if backing is shorter than the available backing channels, the pattern repeats
- if backing is empty, the remaining channels are filled with `:REST`
- if `:noise` is true, only the backing channels are noise-expanded

If any step in the sequence uses `:melody` or `:backing`, the full sequence is normalized internally before compilation.

## Options

Generic options:

- `:channel-count` number of channels, `1..16`
- `:volumes` per-channel volume vector
- `:gate-percent` note gate length as a percentage of the step duration
- `:tempo-bpm` tempo in beats per minute, `20..400`, required for musical duration keywords

Melody/backing helpers:

- `:melody {:volume 220}` sets the melody-channel volume
- `:backing {:volume 150}` sets one shared backing-channel volume
- `:backing {:channels 3 :volumes [170 150 130]}` sets the backing-channel count and exact per-channel volumes
- in melody/backing mode, top-level `:channel-count` and `:volumes` are not allowed; channel layout belongs to the role maps

Defaults:

- generic `:channel-count` is inferred from the widest `:notes` step
- `:tempo-bpm` has no global default; omit it only when all durations/rests are integer milliseconds
- `:gate-percent` defaults to `82`
- generic `:volumes` defaults to `[200 180 160 140]`
- missing `:duration` defaults to `:q`
- `:melody {:channels ...}` defaults to `1`
- `:melody {:volume ...}` defaults to `220`
- `:backing {:channels ...}` is inferred from the widest backing step, defaulting to `1` when backing role options are present
- `:backing {:volume ...}` / `:backing {:volumes ...}` default to `[160 140 120 110 100]` sliced to the resolved backing width

## Runtime Composition

Compile first, then pass the resulting TRK1 bytes into the runtime:

```clojure
(require 'tiny-fx.trk1)
(require 'tiny-fx.sound)

(let [prepared (tiny-fx.trk1/prepare-track
                 [{:notes [:G5 :D5] :duration :q}
                  {:rest :e}
                  {:notes [:A5 :E5] :duration :q}]
                 {:channel-count 2 :tempo-bpm 120})]
  {:status (if (tiny-fx.sound/sound-play-music! :intro (:track-bytes prepared) 1)
             :playing
             :stopped)
   :duration-ms (:duration-ms prepared)})
```

Single-channel SFX use the same pattern:

```clojure
(let [prepared (tiny-fx.trk1/prepare-track
                 [{:notes [5200] :bend [1200] :duration 160}
                  {:notes [550] :duration 56}]
                 {:channel-count 1})]
  {:status (if (tiny-fx.sound/sound-play-sfx! :laser (:track-bytes prepared))
             :playing
             :dropped)
   :duration-ms (:duration-ms prepared)})
```

`track-id` is an opaque runtime id used for stop and finished-notification routing. Keywords are the normal choice.

## Practical Rules

- prefer keywords for notes, durations, and track ids
- prefer `:notes` for straightforward polyphonic material
- prefer integer-Hz `:notes` plus `:bend` for compact single-channel sweeps and chirps
- prefer integer-Hz `:notes` plus `:noise` for compact single-channel SFX rumble/thruster textures
- use `:melody`/`:backing` only when you want the automatic melody-first channel split
- use `{:rest ...}` instead of filling a step manually with `:REST`
- keep public authoring data at the DSL level; only low-level code should handle `trk1` bytes directly
