# JAMAL

JAMAL is a minimal live-coding audio environment for macOS (tested on 13.7.8) using CoreAudio. The engine is written in C with a tiny DSL that's a loose cross between SuperCollider (synthesis) and TidalCycles (pattern sequencing). It is particularly suited to explorations of arabic scales in an electronic music context. A basic Cocoa UI provides a text editor, play/stop controls, and audio metering. Quick video: [https://youtu.be/nKJLo3li188](https://youtu.be/nKJLo3li188) 

JAMAL is dedicated to [Jamal Ahmad Hamza Khashoggi](https://en.wikipedia.org/wiki/Jamal_Khashoggi) (13 October 1958 - 2 October 2018), murdered and dismembered at the Saudi consulate in Istanbul by agents of the Saudi government at the behest of Crown Prince Mohammed bin Salman.


## Build & Run

```bash
./run.sh
```

## DSL (v1)

### Commands

- `tempo <bpm>` (interpreted by `tempo_scale`, default 2.0)
- `tempo_scale <float>` (default 2.0, so `tempo 64` = 128 BPM)
- `tempo_map (<key>=<val>, ...)` where key is `intro`, `verse`, `chorus`, `bridge`, `final`, or section index `1-7`
- `master <amp>` or `master_amp <amp>` (overall output gain)
- `timesig <num>/<den>` or `time_signature <num>/<den>`
- `timesig_map (<key>=<num>/<den>, ...)` where key matches `tempo_map`
- `timesig_enforce on|off` to pad patterns to full bars (16th grid, denominators 1/2/4/8/16). When on, the current section’s `timesig_map` value is used.

### Render to WAV

Run headless render:

```
./build/livecode --render <script.jamal> <output.wav> <seconds> [sample_rate] [buffer_frames]
```

Example:

```
./build/livecode --render wild_experimental_demo.jamal render.wav 30 48000 256
```
- `amp <0..1>`
- `root <note>` (e.g., `C4`)
- `maqam <name>` (`rast`, `bayati`, `hijaz`, `nahawand`, `saba`, `kurd`)
- `drone <synth> <note|degree>`
- `synth <name> <type>`
- `set <synth> <param> <value>`
- `pattern <name> (<sequence>)`
- `play <pattern> <synth> [options]`
- `sequence <name> (<pattern[*n]>, ...)`
- `playseq <sequence> <synth> [options]`

### Synth Types

`sine`, `saw`, `square`, `tri`, `noise`, `pulse`, `fm`, `fm2`, `ring`, `acid`, `comb`,
`pm_string`, `pm_bell`, `pm_pipe`, `pm_kick`, `pm_snare`, `pm_hat`, `pm_clap`, `pm_tom`,
`kick`, `kick808`, `kick909`, `snare`, `snare808`, `snare909`, `clap`, `clap909`,
`hatc`, `hato`, `hat808`, `hat909`, `tom`, `rim`, `glitch`, `metal`, `bitperc`

### Params

`amp`, `cutoff`, `res`, `atk`, `dec`, `sus`, `rel`, `feedback`, `damp`, `excite`

### Patterns

Pattern strings are space-separated tokens, wrapped in `()`, or SuperCollider-style plist syntax:

- Notes: `C4`, `D#3`, `Bb2` or MIDI numbers like `60`
- Rests: `.` or `-`

### Example

```
tempo 120
synth lead saw
set lead cutoff 1400
set lead atk 0.01
set lead dec 0.15
set lead sus 0.6
set lead rel 0.25
pattern main (C4 E4 G4 B4 C5 . G4 .)
pattern main ([60, 62, 64, 65, 67, 69, 71, 72], inf)
pattern main ([60, 62, 64, 65, 67, 69, 71, 72], 2)
pattern main (1 2 3~60 4 5 6 7 1')
pattern main (1 2 3-~40 4 5 6 7 1')

drone lead 1
sequence song (main*2, fill, main)
playseq song lead slide 40 orn 0.3
play main lead fast 2 density 0.8
```

### Play Options (Tidal-style)

- `rate <float>`: multiply track speed (e.g., `rate 0.5` is half speed)
- `hurry <float>`: multiply track speed (Tidal-style hurry)
- `fast <n>`: speed up by integer factor
- `slow <n>`: slow down by integer factor
- `every <n>`: only trigger every Nth step
- `rev`: reverse pattern playback
- `density <0..1>`: probability of triggering each step
- `palindrome`: play forward then backward
- `iter <n>`: repeat each step n times
- `chunk <n>`: rotate through n chunks of the pattern per cycle
- `stut <n>`: retrigger each step n times within its duration
- `slide <ms>`: maqam-aware slide between notes
- `orn <0..1> [up|down|alt]`: ornament probability (grace note below/above/alternating)
  `acc <0..1>`: accent probability (303-style)
  Per-step accent: add `!` to a note token (e.g., `1! 3-! 5`)
  Per-note slide: append `~<ms>` to a note token (e.g., `3~60`, `C4~80`, `60~40`)

## Notes

- Buffer size is targeted at 256 frames (low latency).
- Live coding is immediate on Play: the script is re-parsed and the engine restarts with the new program.
