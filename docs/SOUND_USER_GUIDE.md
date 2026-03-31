# Sound User Guide

`tiny-fx.sound-demos` provides small two-buzzer demo phrases that can be played directly from the REPL. The demos stay in ordinary Clojure files under `libs/` and use the same `tiny-fx.sound` step DSL that you can use for your own tracks.

For the full DSL reference, see `SOUND_DSL.md`. If that file is not present yet in your checkout, treat it as the planned canonical reference for note symbols, duration keywords, step shapes, and option maps.

## Playing The Built-In Demos

Load the namespace and call any demo function directly:

```clojure
(require 'tiny-fx.sound-demos)

(tiny-fx.sound-demos/play-minuet-in-g!)
(tiny-fx.sound-demos/play-the-entertainer!)
(tiny-fx.sound-demos/play-gymnopedie-no-1!)
(tiny-fx.sound-demos/play-rondo-alla-turca!)
(tiny-fx.sound-demos/play-hall-of-the-mountain-king!)
(tiny-fx.sound-demos/play-can-can!)
```

If you want to wait until a phrase has finished, use the returned `:duration-ms`
instead of a fixed sleep:

```clojure
(let [ret (tiny-fx.sound-demos/play-the-entertainer!)]
  (Thread/sleep (:duration-ms ret))
  ret)
```

Each function returns a map like:

```clojure
{:status :playing
 :duration-ms 6848}
```

The demos are intentionally short, loop-friendly phrases:

- `play-minuet-in-g!`: light 3/4 phrase with a simple fifth-based bass.
- `play-the-entertainer!`: syncopated ragtime fragment in 2/4.
- `play-gymnopedie-no-1!`: slow three-beat phrase with a sustained bass.
- `play-rondo-alla-turca!`: fast sixteenth-note lead pattern.
- `play-hall-of-the-mountain-king!`: steady pulse with a pedal-tone bass.
- `play-can-can!`: compact gallop-style phrase for arcade pacing.

## Annotated Step Example

This excerpt shows the shape used by the built-in demos. The formal meaning of symbols such as `:q`, `:e`, or note keywords like `:D5` is defined in `SOUND_DSL.md`; the comments below focus on how the pieces are structured in practice.

```clojure
[
  ;; Melody on buzzer 0, bass note on buzzer 1, quarter-note duration.
  {:melody :D5 :backing [:G3] :duration :q}

  ;; Two shorter upbeat notes that keep the same bass support.
  {:melody :G5 :backing [:D4] :duration :e}
  {:melody :A5 :backing [:D4] :duration :e}

  ;; Back to a quarter-note landing tone.
  {:melody :B5 :backing [:G3] :duration :q}

  ;; A dotted value lets the phrase breathe before the next pickup.
  {:melody :G5 :backing [:G3] :duration :dq}

  ;; A full pause affects both buzzers.
  {:rest :e}

  ;; Final longer tone closes the phrase.
  {:melody :G5 :backing [:D4] :duration :dh}
]
```

The important idea is that `:melody` carries the lead line, while `:backing` supplies the second buzzer with a simpler support tone. For two-buzzer music, that backing line is usually more effective when it is rhythmically simpler than the melody.

## Writing Your Own Track

Start with a small step vector, compile it, and then hand the bytes to the runtime:

```clojure
(require 'tiny-fx.trk1)
(require 'tiny-fx.sound)

(def my-steps
  [{:melody :C5 :backing [:C3] :duration :q}
   {:melody :E5 :backing [:G3] :duration :q}
   {:melody :G5 :backing [:C3] :duration :q}
   {:rest :q}])

(def my-opts
  {:melody {:volume 220}
   :backing {:volume 150}
   :tempo-bpm 120
   :gate-percent 78})

(let [prepared (tiny-fx.trk1/prepare-track my-steps my-opts)]
  {:status (if (tiny-fx.sound/sound-play-music! :my-track (:track-bytes prepared) 1)
             :playing
             :stopped)
   :duration-ms (:duration-ms prepared)})
```

Recommended workflow:

1. Start with four to eight steps.
2. Make the melody recognizable first.
3. Add only one backing note per step.
4. Use rests deliberately so the phrase can breathe.
5. Adjust `:tempo-bpm` and `:gate-percent` last.

If you need the exact supported note spellings, duration keywords, or option names, look them up in `SOUND_DSL.md` instead of treating this guide as the exhaustive specification.

## Two-Voice Layout For Two Buzzers

The built-in demos assume a simple two-output layout:

- Channel 0 carries the melodic line.
- Channel 1 carries the backing line.

In step form that usually means:

```clojure
{:melody :A5 :backing [:D4] :duration :q}
```

The matching options map can stay role-oriented, so the compiler infers two active voices from one melody channel plus one backing channel:

```clojure
{:melody {:volume 220}
 :backing {:volume 150}
 :tempo-bpm 120
 :gate-percent 78}
```

For piezo hardware, simpler backing usually sounds better than dense harmony. A strong root, fifth, or pedal tone is often enough to make the second buzzer useful without making the phrase muddy.
