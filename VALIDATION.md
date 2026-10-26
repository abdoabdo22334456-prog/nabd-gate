# Nabd Gate 1.2.0 validation

Build and tests: 26 October 2026, Windows x64.

## Compiled-library tests

`tests/HostTests.cpp` loads the actual VST3 DLL through GetPluginFactory, rather
than testing a second implementation of the effect. Result: PASS, 304,653
assertions. Most assertions check individual audio samples and stereo equality.

Covered behavior:

- DLL exports, processor/controller construction, activation, processing, teardown.
- Mono/stereo layouts, separate and in-place buffers, float32 and float64 processing.
- 120 BPM sixteenth-note sample boundaries and a change to 150 BPM.
- Negative preroll, seeking/loop realignment, six step divisions and pattern wrap.
- Sample-offset step automation and zero-sample parameter flushes.
- Two-row and four-row sequencing, exact row transitions, complete-sequence
  looping, negative preroll across rows, sample-offset pattern-count automation,
  independent row settings, and correct multi-row playhead reporting.
- 3 ms smoothing reaches exact silence with bounded changes per sample.
- Wet/dry, bypass, stopped transport, absent host context, silence flags.
- Pattern and settings save/restore, rejection of corrupt state.
- Version 1.0 state migration keeps the original single-row loop and control IDs.
- 96 kHz mono double processing.
- Real native editor attach/reopen, click toggles, drag painting, rate buttons,
  pattern actions, sliders, bypass, balanced host automation gestures, 150% DPI.
- Add Pattern, row-count limits, dynamic editor height, independent second-row
  editing, and presets affecting only the selected row.
- Rendering and visual inspection of the real native editor.
- Changing input-waveform tests verify the actual captured samples and repeated
  period, first-capture warmup, six musical slice sizes at 150 BPM, tempo changes,
  adjacent-cell retriggering, sample-offset repeat enable automation, global dry
  and bypass, gate applied to repeat, silent-input replay flags, seeking and restart.
- Exact double-precision mono repeat at 96 kHz over multiple processing blocks.
- State versions 1.0 and 1.1 migrate with repeat disabled; version 1.2 restores
  repeat cells, size, and independent section switches.
- Native Repeat tab, musical size buttons, independent matrix editing, repeat
  drag painting, section toggles, and section-local presets.

## Independent host check

Tracktion pluginval v1.0.4, strictness level 5: SUCCESS, exit code 0.
The final run log is included as `tests/pluginval-level5.txt`.
It covers loading, editor lifecycle, float/double audio processing at
44.1/48/96 kHz and multiple block sizes, state, automation and bus layouts.

pluginval's separate Steinberg-validator step is skipped because that standalone
validator was not configured. Its embedded LV2 scan emitted missing temporary
fixture warnings; this artifact is VST3 and the VST3 tests completed successfully.

## Practical limits

FL Studio itself was not exercised by these automated tests. A scan and a
playback test in the user's FL Studio version are still needed. No macOS or
32-bit Windows build is provided. Pattern phase follows absolute quarter-note
position; each row of 16 sixteenth steps spans four beats, including across bar lines in
other time signatures. Transport must be playing for rhythmic gating; while
stopped, input passes through for preview.

Repeat requires a full input slice before its first capture, and history is
reset on seeking or restarting. Slice storage is capped at 8 seconds. The
host-facing tail is conservatively reported as 16 seconds. The effect does not
load samples from disk or synthesize a replacement audio source.

## Installer

The standalone Windows installer passed embedded SHA256 verification, a fresh
workspace installation, and replacement of that installation. All five bundled
files matched their source SHA256 checksums. It then installed successfully to
`C:\Program Files\Common Files\VST3\Nabd Gate.vst3`, returning exit code 0.
The installed files were verified against the release payload. FL Studio's
plugin scan still needs to be run by the user.
