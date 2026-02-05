# JAMAL Programming Manual

This manual covers JAMAL's language, audio engine features, and workflow. It includes a tutorial, a full reference, and practical examples.

---

## Quick Start

1. Open JAMAL.
2. Type a minimal script:

```jamal
tempo 64
maqam minor
root A3

synth lead supersaw
set lead amp 0.6
set lead cutoff 1400

pattern p1 (1 . 2 . 3 . 4 . 5 . 6 . 7 . 1' .)
play p1 lead
```

3. Press `Cmd+Enter` to evaluate.
4. Press `Play`.

---

## Tutorial: From Silence to Music

### 1) Set tempo and tuning

```jamal
tempo 64
maqam hijaz
root D3
```

### 2) Create a synth

```jamal
synth acid acid
set acid cutoff 1200
set acid res 0.6
set acid dec 0.12
```

### 3) Write a pattern

```jamal
pattern a1 (1 5 3- 6 . 2 7 4 1' . 6 5 2' 4 7 3-)
```

### 4) Play it

```jamal
play a1 acid
```

### 5) Add drum layers

```jamal
synth k kick909
synth s snare909
pattern k1 (1 . . . 1 . . . 1 . . . 1 . . .)
pattern s1 (. . 1 . . . 1 . . . 1 . . . 1 .)
play k1 k
play s1 s
```

---

## Language Reference

### Commands Overview

- `tempo <bpm>`
- `tempo_scale <mult>`
- `tempo_map (intro=0.5,verse=1.0,chorus=1.2)`
- `timesig <n/d>`
- `timesig_map (intro=7/4,chorus=4/4)`
- `timesig_enforce on|off`
- `timesig_seq (1/1,1/1,2/1,3/1,...)` (bar-by-bar sequence)
- `maqam <name>`
- `root <note>`
- `master <0..4>`
- `synth <name> <type>`
- `set <synth> <param> <value>`
- `pattern <name> ( ... )`
- `sequence <name> (pat1, pat2, pat3*2, ...)`
- `play <pattern> <synth> [options...]`
- `playseq <sequence> <synth> [options...]`
- `drone <synth> <note>`
- `mod <synth> <dest> <source> <rate> <depth> [offset] [lag_ms] [slew_ms]`

---

## Notes, Degrees, and Rests

### Degrees
- Use scale degrees `1..7` tied to the current `maqam`.
- Octaves: `1'` (up), `1''` (two up), `1,` (down).
- Microtones: `+` or `-` quarter tone.
  - Example: `3+` (quarter tone up), `6-` (quarter tone down).

### Note Names
- You can also use note names: `C3`, `F#4`, `Gb2`, etc.

### Rests
- `.` or `-` means rest.

---

## Patterns

### Pattern Syntax

```jamal
pattern name (1 . 2 . 3 . 4 .)
```

Important:
- Patterns must be on one line.
- Patterns must have at least one step.
- Multi-line patterns are not supported.

### Pattern Lists (SuperCollider-style)

```jamal
pattern p1 ([60, 62, 64, 65, 67, 69, 71, 72], inf)
pattern p2 ([60, 62, 64, 65, 67, 69, 71, 72], 2)
```

---

## Sequences

```jamal
sequence seqA (a1, a2, a3*2, a4)
playseq seqA lead
```

- `*N` repeats a pattern.
- A sequence can be restricted with `only` in `play` / `playseq`.

---

## Play Options

### Common Options

- `fast N` / `slow N`
- `rate X` / `hurry X`
- `every N`
- `density 0..1`
- `iter N`
- `chunk N`
- `stut N`
- `slide ms`
- `acc 0..1` (accent probability)
- `orn 0..1` plus `up|down|alt`
- `palindrome`
- `rev` (reverse)
- `trans N` (transpose semitones, used with `rev`)
- `offset N` (bar delay; applies only when `rev` is set)

Example:

```jamal
playseq seqA lead rev trans 3 fast 2 density 0.7
```

---

## Modulation (UGens)

### Sources
- `lfo`
- `env`
- `noise`
- `sample_hold` (alias `s&h`)
- `ring`
- `sync`

### Destinations
- `amp`
- `cutoff`
- `res`
- `pan`
- `pitch`

### Example

```jamal
mod lead cutoff lfo 0.3 800 0 120 0
mod lead pan sample_hold 1.2 0.5 0 0 80
mod lead pitch lfo 0.1 0.2 0
```

---

## Synthesis Types

### Core
- `sine`, `saw`, `square`, `tri`, `noise`
- `pulse`, `fm`, `fm2`, `ring`
- `acid`, `supersaw`

### Drums
- `kick`, `kick808`, `kick909`
- `snare`, `snare808`, `snare909`
- `clap`, `clap909`
- `hatc`, `hato`, `hat808`, `hat909`
- `tom`, `rim`, `bitperc`

### Physical Modeling
- `pm_string`, `pm_bell`, `pm_pipe`
- `pm_kick`, `pm_snare`, `pm_hat`, `pm_clap`, `pm_tom`
- `comb`

---

## Synth Parameters

Common:
- `amp`, `cutoff`, `res`
- `atk`, `dec`, `sus`, `rel`
- `drive` (soft saturation)

Acid:
- `res` affects 303 resonance.

Comb / PM:
- `feedback`, `damp`, `excite`

Supersaw:
- `detune_rate` (slow LFO rate for voice detune)
- `detune_depth` (detune modulation in cents)

---

## Time Signature Tools

### Static signature
```jamal
timesig 7/4
```

### Section map
```jamal
timesig_map (intro=7/4,verse=4/4,chorus=5/4)
```

### Enforce bar length (pads patterns)
```jamal
timesig_enforce on
```

### Bar-by-bar sequence
```jamal
timesig_seq (1/1,1/1,2/1,3/1,5/1,8/1,13/1)
```

`timesig_seq` overrides `timesig_map` while active and advances by elapsed time.

---

## Scales / Maqamat

Arabic:
- `rast`, `bayati`, `hijaz`, `nahawand`, `saba`, `kurd`

Western:
- `major` / `ionian`
- `minor` / `aeolian`
- `dorian`, `phrygian`, `lydian`, `mixolydian`, `locrian`
- `harmonic_minor`, `melodic_minor`
- `pentatonic`, `pentatonic_major`, `pentatonic_minor`
- `blues`, `blues_minor`, `blues_major`
- `whole_tone`, `octatonic`, `octatonic_wh`, `octatonic_hw`

---

## Rendering Audio

From the command line:

```bash
./build/livecode --render myscript.jamal out.wav 60 48000 256
```

Parameters:
1. script path
2. output wav path
3. seconds
4. sample rate (optional)
5. buffer frames (optional)

---

## Editor Shortcuts

- `Cmd+Enter` re‑evaluate
- `Cmd+Z` undo
- `Cmd+Shift+Z` redo

---

## Example: Acid Track

```jamal
tempo 64
maqam hijaz
root D3

synth acid acid
set acid cutoff 1400
set acid res 0.7
set acid dec 0.12
set acid drive 0.8

pattern a1 (1 7 3- 6 . 2 7 4 1' . 6 5 2' 4 7 3-)
play a1 acid fast 2 acc 0.3
```

---

## Example: Radigue‑style Drone

```jamal
tempo 48
maqam hijaz
root D2

synth dr1 supersaw
set dr1 amp 0.2
set dr1 cutoff 500
set dr1 res 0.08
set dr1 atk 4.0
set dr1 dec 8.0
set dr1 sus 0.9
set dr1 rel 12.0
set dr1 detune_rate 0.01
set dr1 detune_depth 3.2

pattern d1 (1,, - - - - - - -)
sequence seqD (d1*8)
playseq seqD dr1 slow 4
```

---

## Example: Oval‑Style Micro‑Glitch

```jamal
tempo 96
maqam minor
root A3

synth k kick909
synth s snare909
synth tone fm

pattern c1 (1 . 1 . 1 . 1 . 1 . 1 . 1 . 1 .)
pattern m1 (1 3 2 5 . 4 3 2 . 6 5 . 3 2 1 .)

sequence seqC (c1, c1, c1, c1)
sequence seqM (m1, m1, m1, m1)

playseq seqC k fast 2
playseq seqC s fast 2
playseq seqM tone fast 3 stut 2
```

---

## Tips

- Use `density` for subtle variation without rewriting patterns.
- Use `stut` for micro‑repeats.
- Use `chunk` to focus on a smaller window of a pattern.
- `rev trans` is great for variation without writing new patterns.
- Lower `cutoff` + `drive` = thicker textures.

---

