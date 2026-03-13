#!/usr/bin/env python3
"""
Convert a MIDI file into tiny-fx.sound DSL.

This tool is intentionally deterministic:
- parse note timing from MIDI tracks
- quantize to a fixed musical grid
- insert explicit rests
- emit either generic `:notes` steps or `:melody`/`:backing` steps

Examples:
  python3 scripts/midi_to_sound_dsl.py build/entertainer.mid --list-tracks
  python3 scripts/midi_to_sound_dsl.py build/entertainer.mid --format melody-backing --melody-track 1 --backing-track 2
  python3 scripts/midi_to_sound_dsl.py build/entertainer.mid --format notes --track 1
"""

from __future__ import annotations

import argparse
import json
import math
import sys
from dataclasses import dataclass
from functools import lru_cache
from pathlib import Path
from typing import Dict, Iterable, List, Optional, Sequence, Tuple

try:
    import mido
except ImportError as exc:  # pragma: no cover - exercised in live use
    print(
        "Missing dependency: mido. Install it with `python3 -m pip install --user mido`.",
        file=sys.stderr,
    )
    raise SystemExit(2) from exc


DEFAULT_GRID = ("w", "dh", "h", "dq", "q", "de", "e", "ds", "s", "qt", "et", "st", "t")
DEFAULT_TEMPO = 500000  # microseconds per quarter note (120 BPM)

_DURATION_FACTORS: Dict[str, Tuple[int, int]] = {
    "w": (4, 1),
    "dh": (3, 1),
    "h": (2, 1),
    "dq": (3, 2),
    "q": (1, 1),
    "de": (3, 4),
    "e": (1, 2),
    "ds": (3, 8),
    "s": (1, 4),
    "qt": (2, 3),
    "et": (1, 3),
    "st": (1, 6),
    "t": (1, 8),
}

_NOTE_NAMES = ("C", "Cs", "D", "Ds", "E", "F", "Fs", "G", "Gs", "A", "As", "B")


@dataclass(frozen=True)
class MidiNote:
    track_index: int
    channel: int
    pitch: int
    start_tick: int
    end_tick: int
    velocity: int


@dataclass(frozen=True)
class QuantizedNote:
    track_index: int
    channel: int
    pitch: int
    start_tick: int
    end_tick: int
    velocity: int
    original_start_tick: int
    original_end_tick: int


@dataclass(frozen=True)
class Segment:
    start_tick: int
    end_tick: int
    pitches: Tuple[int, ...]


@dataclass(frozen=True)
class MelodyBackingSegment:
    start_tick: int
    end_tick: int
    melody_pitch: Optional[int]
    original_melody_pitches: Tuple[int, ...]
    dropped_melody_pitches: Tuple[int, ...]
    original_backing_pitches: Tuple[int, ...]
    reduced_backing_pitches: Tuple[int, ...]
    dropped_backing_pitches: Tuple[int, ...]


@dataclass(frozen=True)
class TrackSummary:
    track_index: int
    name: str
    note_count: int


def fail(message: str) -> "NoReturn":
    raise SystemExit(message)


def note_keyword(pitch: int) -> str:
    octave = (pitch // 12) - 1
    name = _NOTE_NAMES[pitch % 12]
    return f":{name}{octave}"


def strip_ext(path: Path) -> str:
    return path.stem.replace("-", "_").replace(" ", "_")


def absolute_messages(track: mido.MidiTrack) -> Iterable[Tuple[int, mido.Message]]:
    abs_tick = 0
    for msg in track:
        abs_tick += msg.time
        yield abs_tick, msg


def extract_track_notes(mid: mido.MidiFile, track_index: int) -> List[MidiNote]:
    if track_index < 0 or track_index >= len(mid.tracks):
        fail(f"Track index out of range: {track_index}")

    active: Dict[Tuple[int, int], List[Tuple[int, int]]] = {}
    notes: List[MidiNote] = []

    for abs_tick, msg in absolute_messages(mid.tracks[track_index]):
        if msg.type == "note_on" and msg.velocity > 0:
            key = (msg.channel, msg.note)
            active.setdefault(key, []).append((abs_tick, msg.velocity))
            continue

        if msg.type not in ("note_off", "note_on"):
            continue
        if msg.type == "note_on" and msg.velocity > 0:
            continue

        key = (msg.channel, msg.note)
        stack = active.get(key)
        if not stack:
            continue
        start_tick, velocity = stack.pop()
        if not stack:
            active.pop(key, None)
        if abs_tick <= start_tick:
            abs_tick = start_tick + 1
        notes.append(
            MidiNote(
                track_index=track_index,
                channel=msg.channel,
                pitch=msg.note,
                start_tick=start_tick,
                end_tick=abs_tick,
                velocity=velocity,
            )
        )

    return sorted(notes, key=lambda n: (n.start_tick, n.pitch, n.end_tick))


def summarize_tracks(mid: mido.MidiFile) -> List[TrackSummary]:
    out: List[TrackSummary] = []
    for idx, track in enumerate(mid.tracks):
        name = ""
        for msg in track:
            if msg.type == "track_name":
                name = msg.name
                break
        note_count = len(extract_track_notes(mid, idx))
        out.append(TrackSummary(track_index=idx, name=name, note_count=note_count))
    return out


def infer_tempo(mid: mido.MidiFile) -> int:
    tempo_events: List[Tuple[int, int]] = []
    for track in mid.tracks:
        abs_tick = 0
        for msg in track:
            abs_tick += msg.time
            if msg.type == "set_tempo":
                tempo_events.append((abs_tick, msg.tempo))

    if not tempo_events:
        return DEFAULT_TEMPO

    first_tempo = tempo_events[0][1]
    changed = [(tick, tempo) for tick, tempo in tempo_events if tempo != first_tempo]
    if changed:
        fail("Tempo changes are not supported yet; split or simplify the MIDI first.")
    return first_tempo


def bpm_from_tempo(tempo: int) -> int:
    return int(round(60000000 / tempo))


def duration_ticks_map(ticks_per_beat: int, grid: Sequence[str]) -> Dict[str, int]:
    out: Dict[str, int] = {}
    for key in grid:
        if key not in _DURATION_FACTORS:
            fail(f"Unsupported grid duration keyword: {key}")
        num, den = _DURATION_FACTORS[key]
        scaled = ticks_per_beat * num
        if scaled % den != 0:
            continue
        out[key] = scaled // den
    if "q" not in out:
        fail("The selected grid does not support quarter-note quantization for this MIDI resolution.")
    return out


def gcd_many(values: Sequence[int]) -> int:
    if not values:
        return 1
    acc = values[0]
    for value in values[1:]:
        acc = math.gcd(acc, value)
    return acc


def quantize_tick(tick: int, unit: int) -> int:
    return int(round(tick / unit)) * unit


def quantize_notes(notes: Sequence[MidiNote], unit: int) -> List[QuantizedNote]:
    out: List[QuantizedNote] = []
    for note in notes:
        start_tick = quantize_tick(note.start_tick, unit)
        end_tick = quantize_tick(note.end_tick, unit)
        if end_tick <= start_tick:
            end_tick = start_tick + unit
        out.append(
            QuantizedNote(
                track_index=note.track_index,
                channel=note.channel,
                pitch=note.pitch,
                start_tick=start_tick,
                end_tick=end_tick,
                velocity=note.velocity,
                original_start_tick=note.start_tick,
                original_end_tick=note.end_tick,
            )
        )
    return sorted(out, key=lambda n: (n.start_tick, n.pitch, n.end_tick))


def build_segments(notes: Sequence[QuantizedNote]) -> List[Segment]:
    if not notes:
        return []

    boundaries = sorted({tick for note in notes for tick in (note.start_tick, note.end_tick)})
    segments: List[Segment] = []

    for start_tick, end_tick in zip(boundaries, boundaries[1:]):
        if end_tick <= start_tick:
            continue
        active = sorted(
            {
                note.pitch
                for note in notes
                if note.start_tick <= start_tick < note.end_tick
            }
        )
        segments.append(Segment(start_tick=start_tick, end_tick=end_tick, pitches=tuple(active)))

    merged: List[Segment] = []
    for segment in segments:
        if merged and merged[-1].pitches == segment.pitches and merged[-1].end_tick == segment.start_tick:
            prev = merged[-1]
            merged[-1] = Segment(prev.start_tick, segment.end_tick, prev.pitches)
        else:
            merged.append(segment)
    return merged


def build_melody_backing_segments(
    melody_notes: Sequence[QuantizedNote],
    backing_notes: Sequence[QuantizedNote],
    backing_channel_limit: int,
    dedupe_backing_octaves: bool,
) -> List[MelodyBackingSegment]:
    all_notes = list(melody_notes) + list(backing_notes)
    if not all_notes:
        return []

    boundaries = sorted({tick for note in all_notes for tick in (note.start_tick, note.end_tick)})
    out: List[MelodyBackingSegment] = []

    for start_tick, end_tick in zip(boundaries, boundaries[1:]):
        if end_tick <= start_tick:
            continue
        melody_active = sorted(
            {
                note.pitch
                for note in melody_notes
                if note.start_tick <= start_tick < note.end_tick
            }
        )
        backing_active = sorted(
            {
                note.pitch
                for note in backing_notes
                if note.start_tick <= start_tick < note.end_tick
            }
        )
        melody_pitch = melody_active[-1] if melody_active else None
        dropped_melody = tuple(pitch for pitch in melody_active if pitch != melody_pitch)
        reduced_backing = reduce_backing_pitches(
            backing_active,
            max_notes=backing_channel_limit,
            dedupe_octaves=dedupe_backing_octaves,
        )
        reduced_set = set(reduced_backing)
        dropped_backing = tuple(pitch for pitch in backing_active if pitch not in reduced_set)
        out.append(
            MelodyBackingSegment(
                start_tick=start_tick,
                end_tick=end_tick,
                melody_pitch=melody_pitch,
                original_melody_pitches=tuple(melody_active),
                dropped_melody_pitches=dropped_melody,
                original_backing_pitches=tuple(backing_active),
                reduced_backing_pitches=tuple(reduced_backing),
                dropped_backing_pitches=dropped_backing,
            )
        )

    merged: List[MelodyBackingSegment] = []
    for segment in out:
        if (
            merged
            and merged[-1].melody_pitch == segment.melody_pitch
            and merged[-1].reduced_backing_pitches == segment.reduced_backing_pitches
            and merged[-1].end_tick == segment.start_tick
        ):
            prev = merged[-1]
            merged[-1] = MelodyBackingSegment(
                start_tick=prev.start_tick,
                end_tick=segment.end_tick,
                melody_pitch=prev.melody_pitch,
                original_melody_pitches=tuple(sorted(set(prev.original_melody_pitches) | set(segment.original_melody_pitches))),
                dropped_melody_pitches=tuple(sorted(set(prev.dropped_melody_pitches) | set(segment.dropped_melody_pitches))),
                original_backing_pitches=tuple(sorted(set(prev.original_backing_pitches) | set(segment.original_backing_pitches))),
                reduced_backing_pitches=prev.reduced_backing_pitches,
                dropped_backing_pitches=tuple(sorted(set(prev.dropped_backing_pitches) | set(segment.dropped_backing_pitches))),
            )
        else:
            merged.append(segment)
    return merged


def choose_default_tracks(mid: mido.MidiFile) -> List[int]:
    summaries = summarize_tracks(mid)
    note_tracks = [summary.track_index for summary in summaries if summary.note_count > 0]
    if not note_tracks:
        fail("No note tracks found in the MIDI file.")
    return note_tracks


def remove_octave_doublings(pitches: Sequence[int]) -> List[int]:
    # Keep the lowest representative for each pitch class so backing stays grounded.
    seen_pitch_classes: set[int] = set()
    out: List[int] = []
    for pitch in sorted(pitches):
        pitch_class = pitch % 12
        if pitch_class in seen_pitch_classes:
            continue
        seen_pitch_classes.add(pitch_class)
        out.append(pitch)
    return out


def reduce_backing_pitches(
    pitches: Sequence[int], max_notes: int, dedupe_octaves: bool
) -> List[int]:
    reduced = sorted(pitches)
    if dedupe_octaves:
        reduced = remove_octave_doublings(reduced)
    if max_notes > 0:
        reduced = reduced[:max_notes]
    return reduced


def resolve_melody_backing_channel_layout(args: argparse.Namespace) -> Tuple[int, int]:
    melody_channels = args.melody_channels
    if melody_channels < 1:
        fail("--melody-channels must be >= 1")
    if melody_channels != 1:
        fail("The current melody-backing DSL supports exactly 1 melody channel")

    if args.backing_channels is not None:
        if args.backing_channels < 0:
            fail("--backing-channels must be >= 0")
        backing_channels = args.backing_channels
    elif args.hardware_channels is not None:
        if args.hardware_channels < 1:
            fail("--hardware-channels must be >= 1")
        backing_channels = args.hardware_channels - melody_channels
        if backing_channels < 0:
            fail("--hardware-channels must be >= --melody-channels")
    else:
        backing_channels = args.max_backing_notes

    return melody_channels, backing_channels


def parse_grid(grid_arg: str) -> Tuple[str, ...]:
    keys = tuple(part.strip() for part in grid_arg.split(",") if part.strip())
    if not keys:
        fail("Grid must contain at least one duration keyword.")
    return keys


def duration_decomposer(duration_ticks: Dict[str, int]):
    ordered = sorted(duration_ticks.items(), key=lambda item: (-item[1], item[0]))
    values = tuple(value for _key, value in ordered)
    keys = tuple(key for key, _value in ordered)

    @lru_cache(maxsize=None)
    def decompose(total: int) -> Optional[Tuple[str, ...]]:
        if total == 0:
            return ()
        best: Optional[Tuple[str, ...]] = None
        for key, value in zip(keys, values):
            if value > total:
                continue
            tail = decompose(total - value)
            if tail is None:
                continue
            candidate = (key,) + tail
            if best is None or len(candidate) < len(best):
                best = candidate
        return best

    return decompose


def duration_entry(duration_key: str) -> str:
    return "" if duration_key == "q" else f" :duration :{duration_key}"


def render_notes_step(pitches: Sequence[int], duration_key: str) -> str:
    if not pitches:
        return f"{{:rest :{duration_key}}}"
    notes = " ".join(note_keyword(pitch) for pitch in pitches)
    duration_part = duration_entry(duration_key)
    return f"{{:notes [{notes}]{duration_part}}}"


def render_melody_backing_step(
    melody_pitch: Optional[int], backing_pitches: Sequence[int], duration_key: str
) -> str:
    if melody_pitch is None and not backing_pitches:
        return f"{{:rest :{duration_key}}}"

    parts: List[str] = []
    if melody_pitch is not None:
        parts.append(f":melody {note_keyword(melody_pitch)}")
    else:
        parts.append(":melody :REST")
    if backing_pitches:
        backing = " ".join(note_keyword(pitch) for pitch in backing_pitches)
        parts.append(f":backing [{backing}]")
    if duration_key != "q":
        parts.append(f":duration :{duration_key}")
    return "{" + " ".join(parts) + "}"


def render_vector(items: Sequence[str]) -> str:
    if not items:
        return "[]"
    lines = ["["]
    for idx, item in enumerate(items):
        suffix = "" if idx == len(items) - 1 else ""
        lines.append(f"  {item}{suffix}")
    lines.append("]")
    return "\n".join(lines)


def steps_from_segments(segments: Sequence[Segment], duration_ticks: Dict[str, int]) -> List[str]:
    decompose = duration_decomposer(duration_ticks)
    out: List[str] = []
    for segment in segments:
        total = segment.end_tick - segment.start_tick
        keys = decompose(total)
        if keys is None:
            fail(f"Could not decompose segment duration {total} ticks into the selected grid.")
        for key in keys:
            out.append(render_notes_step(segment.pitches, key))
    return out


def steps_from_melody_backing_segments(
    segments: Sequence[MelodyBackingSegment], duration_ticks: Dict[str, int]
) -> List[str]:
    decompose = duration_decomposer(duration_ticks)
    out: List[str] = []
    for segment in segments:
        total = segment.end_tick - segment.start_tick
        keys = decompose(total)
        if keys is None:
            fail(f"Could not decompose segment duration {total} ticks into the selected grid.")
        for key in keys:
            out.append(
                render_melody_backing_step(
                    segment.melody_pitch,
                    segment.reduced_backing_pitches,
                    key,
                )
            )
    return out


def build_debug_payload(
    ticks_per_beat: int,
    tempo: int,
    base_unit: int,
    notes: Sequence[QuantizedNote],
    melody_backing_segments: Optional[Sequence[MelodyBackingSegment]] = None,
    reduction_config: Optional[Dict[str, object]] = None,
) -> Dict[str, object]:
    payload: Dict[str, object] = {
        "ticks_per_beat": ticks_per_beat,
        "tempo_us_per_quarter": tempo,
        "tempo_bpm": bpm_from_tempo(tempo),
        "quantize_unit_ticks": base_unit,
        "notes": [
            {
                "track": note.track_index,
                "channel": note.channel,
                "pitch": note.pitch,
                "note": note_keyword(note.pitch),
                "start_tick": note.start_tick,
                "end_tick": note.end_tick,
                "original_start_tick": note.original_start_tick,
                "original_end_tick": note.original_end_tick,
                "velocity": note.velocity,
            }
            for note in notes
        ],
    }
    if reduction_config is not None:
        payload["reduction"] = reduction_config
    if melody_backing_segments is not None:
        payload["segments"] = [
            {
                "start_tick": segment.start_tick,
                "end_tick": segment.end_tick,
                "melody_pitch": None if segment.melody_pitch is None else note_keyword(segment.melody_pitch),
                "original_melody_pitches": [note_keyword(p) for p in segment.original_melody_pitches],
                "dropped_melody_pitches": [note_keyword(p) for p in segment.dropped_melody_pitches],
                "original_backing_pitches": [note_keyword(p) for p in segment.original_backing_pitches],
                "reduced_backing_pitches": [note_keyword(p) for p in segment.reduced_backing_pitches],
                "dropped_backing_pitches": [note_keyword(p) for p in segment.dropped_backing_pitches],
            }
            for segment in melody_backing_segments
        ]
    return payload


def emit_track_listing(mid: mido.MidiFile) -> int:
    for summary in summarize_tracks(mid):
        name_part = f" name={summary.name!r}" if summary.name else ""
        print(f"track={summary.track_index} notes={summary.note_count}{name_part}")
    return 0


def main(argv: List[str]) -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("midi_path", help="Path to a MIDI file")
    ap.add_argument(
        "--format",
        choices=("notes", "melody-backing"),
        default="melody-backing",
        help="Output format",
    )
    ap.add_argument("--track", type=int, action="append", default=[], help="Track index for --format notes")
    ap.add_argument("--melody-track", type=int, help="Track index for melody")
    ap.add_argument("--backing-track", type=int, help="Track index for backing")
    ap.add_argument(
        "--hardware-channels",
        type=int,
        help="Available hardware channels for melody-backing output; backing channels are derived from this",
    )
    ap.add_argument(
        "--melody-channels",
        type=int,
        default=1,
        help="Reserved melody channels for melody-backing output (currently must be 1)",
    )
    ap.add_argument(
        "--backing-channels",
        type=int,
        help="Optional explicit backing-channel count override for melody-backing output",
    )
    ap.add_argument(
        "--max-backing-notes",
        type=int,
        default=2,
        help="Fallback maximum number of backing notes per step when no hardware/backing channel count is provided (default: 2)",
    )
    ap.add_argument(
        "--keep-backing-octaves",
        action="store_true",
        help="Keep octave doublings in backing instead of collapsing them to one pitch per pitch class",
    )
    ap.add_argument("--list-tracks", action="store_true", help="Print track indices, names, and note counts")
    ap.add_argument(
        "--grid",
        default=",".join(DEFAULT_GRID),
        help="Comma-separated duration keywords to use for quantization/decomposition",
    )
    ap.add_argument("--name", help="Optional DSL var prefix, e.g. entertainer")
    ap.add_argument("--emit-opts", action="store_true", help="Emit a matching opts map with tempo")
    ap.add_argument("--debug-json", help="Optional path for detailed quantization/debug output")
    args = ap.parse_args(argv)

    midi_path = Path(args.midi_path)
    if not midi_path.exists():
        fail(f"MIDI file not found: {midi_path}")

    mid = mido.MidiFile(str(midi_path))

    if args.list_tracks:
        return emit_track_listing(mid)

    grid = parse_grid(args.grid)
    duration_ticks = duration_ticks_map(mid.ticks_per_beat, grid)
    base_unit = gcd_many(list(duration_ticks.values()))
    tempo = infer_tempo(mid)
    bpm = bpm_from_tempo(tempo)

    debug_notes: List[QuantizedNote] = []
    debug_segments: Optional[List[MelodyBackingSegment]] = None
    reduction_config: Optional[Dict[str, object]] = None

    if args.format == "notes":
        track_indices = args.track or choose_default_tracks(mid)[:1]
        notes: List[MidiNote] = []
        for track_index in track_indices:
            notes.extend(extract_track_notes(mid, track_index))
        quantized = quantize_notes(notes, base_unit)
        debug_notes = quantized
        segments = build_segments(quantized)
        step_lines = steps_from_segments(segments, duration_ticks)
    else:
        note_tracks = choose_default_tracks(mid)
        melody_track = args.melody_track if args.melody_track is not None else note_tracks[0]
        backing_track = args.backing_track if args.backing_track is not None else (note_tracks[1] if len(note_tracks) > 1 else note_tracks[0])
        melody_channels, backing_channel_limit = resolve_melody_backing_channel_layout(args)

        melody_notes = quantize_notes(extract_track_notes(mid, melody_track), base_unit)
        backing_notes = quantize_notes(extract_track_notes(mid, backing_track), base_unit)
        debug_notes = list(melody_notes) + list(backing_notes)
        segments = build_melody_backing_segments(
            melody_notes,
            backing_notes,
            backing_channel_limit=backing_channel_limit,
            dedupe_backing_octaves=not args.keep_backing_octaves,
        )
        debug_segments = segments
        reduction_config = {
            "format": args.format,
            "hardware_channels": args.hardware_channels,
            "melody_channels": melody_channels,
            "backing_channels": backing_channel_limit,
            "keep_backing_octaves": args.keep_backing_octaves,
            "fallback_max_backing_notes": args.max_backing_notes,
            "melody_track": melody_track,
            "backing_track": backing_track,
        }
        step_lines = steps_from_melody_backing_segments(segments, duration_ticks)

    if args.debug_json:
        debug_path = Path(args.debug_json)
        debug_path.parent.mkdir(parents=True, exist_ok=True)
        debug_path.write_text(
            json.dumps(
                build_debug_payload(
                    mid.ticks_per_beat,
                    tempo,
                    base_unit,
                    debug_notes,
                    melody_backing_segments=debug_segments,
                    reduction_config=reduction_config,
                ),
                indent=2,
            ),
            encoding="utf-8",
        )

    rendered_steps = render_vector(step_lines)
    if not args.name:
        print(rendered_steps)
        if args.emit_opts:
            print()
            print(f"{{:tempo-bpm {bpm}}}")
        return 0

    prefix = args.name.replace("-", "_")
    print(f"(def {prefix}-steps")
    for line in rendered_steps.splitlines():
        print(f"  {line}" if line else "")
    print(")")

    if args.emit_opts:
        print()
        print(f"(def {prefix}-opts")
        print(f"  {{:tempo-bpm {bpm}}})")

    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
