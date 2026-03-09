# Sound DSL

This document describes the public step DSL used by [`tiny-fx.sound`](/Users/theisen/Projects/tiny-clj/src/tiny-fx.sound.clj).

## Scope

The DSL is the data format accepted by:

- `tiny-fx.sound/compile-track`
- `tiny-fx.sound/track-duration-ms`
- `tiny-fx.sound/play-steps!`
- `tiny-fx.sound/play-sfx!`

The low-level `trk1` byte format is separate. Most callers should stay at the DSL level.

## Note Symbols

Supported note keywords:

- natural notes: `:C4`, `:D5`, `:A3`
- sharps: `:Cs4` or `:C#4`
- flats: `:Db4`, `:Bb3`
- rest: `:REST`

Rules:

- pitch letters are uppercase `A` through `G`
- octaves are single-digit
- notes must resolve to a valid MIDI range (`0..127`)

`tiny-fx.sound/note->midi` is the canonical converter.

## Duration Symbols

Supported duration keywords:

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

Durations are interpreted relative to `:tempo-bpm`.

## Step Shapes

### Generic Polyphonic Step

```clojure
{:notes [:G5 :D5 :Bb4] :duration :q}
```

Meaning:

- one note per channel
- shorter note vectors are padded with `:REST`
- if `:duration` is omitted, it defaults to `:q`

### Full-Rest Step

```clojure
{:rest :e}
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

If any step in the sequence uses `:melody` or `:backing`, the full sequence is normalized through `melody-backing->steps` before compilation.

## Options

Generic options:

- `:channel-count` number of channels, `1..16`
- `:volumes` per-channel volume vector
- `:gate-percent` note gate length as a percentage of the step duration
- `:tempo-bpm` tempo in beats per minute, `20..400`

Melody/backing helpers:

- `:melody {:volume 220}` sets the melody-channel volume
- `:backing {:volume 150}` sets one shared backing-channel volume
- `:backing {:channels 3 :volumes [170 150 130]}` sets the backing-channel count and exact per-channel volumes
- in melody/backing mode, top-level `:channel-count` and `:volumes` are not allowed; channel layout belongs to the role maps

Defaults:

- generic `:channel-count` is inferred from the widest `:notes` step
- `:tempo-bpm` defaults to `120`
- `:gate-percent` defaults to `82`
- generic `:volumes` defaults to `[200 180 160 140]`
- missing `:duration` defaults to `:q`
- `:melody {:channels ...}` defaults to `1`
- `:melody {:volume ...}` defaults to `220`
- `:backing {:channels ...}` is inferred from the widest backing step, defaulting to `1` when backing role options are present
- `:backing {:volume ...}` / `:backing {:volumes ...}` default to `[160 140 120 110 100]` sliced to the resolved backing width

## Runtime Helpers

### `play-steps!`

```clojure
(tiny-fx.sound/play-steps!
  :intro
  [{:notes [:G5 :D5] :duration :q}
   {:rest :e}
   {:notes [:A5 :E5] :duration :q}]
  {:channel-count 2 :tempo-bpm 120})
```

Returns:

```clojure
{:status :playing
 :duration-ms 1250}
```

### `play-sfx!`

```clojure
(tiny-fx.sound/play-sfx!
  :laser
  [{:notes [:G6] :duration :s}]
  {:tempo-bpm 120})
```

Melody/backing example:

```clojure
(tiny-fx.sound/play-steps!
  :duet
  [{:melody :G5 :backing [:D4] :duration :q}
   {:melody :A5 :backing [:E4] :duration :q}]
  {:melody {:volume 220}
   :backing {:volume 150}
   :tempo-bpm 120})
```

Returns:

```clojure
{:status :playing
 :duration-ms 125}
```

`track-id` is an opaque runtime id. Keywords are the normal choice.

## Practical Rules

- prefer keywords for notes, durations, and track ids
- prefer `:notes` for straightforward polyphonic material
- use `:melody`/`:backing` only when you want the automatic melody-first channel split
- use `{:rest ...}` instead of filling a step manually with `:REST`
- keep public authoring data at the DSL level; only low-level code should handle `trk1` bytes directly
