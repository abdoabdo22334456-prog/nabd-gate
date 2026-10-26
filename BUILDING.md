# Building Nabd Gate 1.2.0

Windows x64 native VST3 effect. C++17; Win32/GDI editor; no JUCE, audio driver, web runtime, or external runtime DLL required beyond Windows/UCRT.

The editable application source is in `source/`. The processor and controller are separate VST3 components. No heap allocation, locking, GUI calls, or file access takes place in the audio callback. Stereo channels share one gain envelope. Timings use host PPQ and tempo each block; parameters apply at queue sample offsets. A finite linear ramp reaches exact silence. Host bypass and mix changes ramp over 5 ms. Stopped or missing transport context passes input through.

Version 1.1 supports 1–4 sequential patterns, each containing 16 steps. IDs 0–19 keep their original meaning. Additional row steps use IDs 20–67 and pattern count uses ID 68. Hidden playhead ID 100 reports the absolute sequence cell as `(cell + 1) / 64`, with zero reserved for stopped transport. The editor grows with the active row count and asks the host frame to resize. Row presets affect the selected row only. State version 2 stores all rows and active count; version 1 states migrate to one active row while preserving the original pattern and controls. Class IDs remain unchanged so existing projects find the updated plugin. Older plugin versions cannot read new version 2 states.

Version 1.2 adds native Repeat/Stutter editing with independent 4x16 cells at IDs 128–191. Repeat enabled is 192, slice size is 193, and gate enabled is 194. Reserved IDs 69–127 (including read-only telemetry at 100/101) are not editable. State version 3 stores the full parameter array; versions 1 and 2 load with repeat disabled and gate enabled. Class IDs and all existing gate parameter IDs remain stable. Versions before 1.2 cannot read state version 3.

`StutterEngine.h` preallocates separate 8-second stereo input history and snapshot buffers in setupProcessing. An active cell captures the preceding musical slice and plays it at the input sample rate. A new active cell, size/tempo change, or 8-second maximum capture age requests a fresh snapshot. Before enough audio exists, live input passes through. Linear entry/exit blending and a short endpoint ease follow Smoothing. Gate is applied after repeat; the global wet/dry blend surrounds both effects. Input is recorded before processing and never fed back from the output. Recording, snapshots and play positions reset on transport stop, start, seek or project state restore. Captured audio is not saved in project state.

Slice storage is bounded to 8 seconds (very low tempos can exceed that capacity and are clamped). The processor conservatively reports a finite 16-second tail so a host can render buffered repeats safely. Both float32 and float64 paths use stereo double storage and preserve mono/stereo layouts. Windows VST3 remains the runtime; no web UI or browser process is involved.

## Dependencies used

- [Steinberg VST3 SDK](https://github.com/steinbergmedia/vst3sdk), tag `v3.8.1_build_84`, commit `3cdf9ca5d1f5b1b21e0a86832aa4abe55607bd96`.
- SDK submodules: base `fcf9da0bd27a16f7f03773a3a39822f28f5c8477`, pluginterfaces `4f547e8e102b47de4a8b8aaf343c73b700786372`, public.sdk `586dc5e6c8012c3e4b01c79389375cbe96bdb1da`.
- [LLVM-MinGW](https://github.com/mstorsjo/llvm-mingw/releases/tag/20260908), `llvm-mingw-20260908-ucrt-x86_64.zip`. Unpack it; installation is unnecessary.

Obtain the dependencies in your development folder:

```powershell
git clone --depth 1 --branch v3.8.1_build_84 https://github.com/steinbergmedia/vst3sdk.git vst3sdk
git -C vst3sdk submodule update --init --depth 1 base pluginterfaces public.sdk
```

From this source package, run in PowerShell (replace both dependency paths):

```powershell
./build.ps1 -Sdk C:/dev/vst3sdk -Compiler C:/dev/llvm-mingw-20260908-ucrt-x86_64/bin/clang++.exe
./tests/Run-Tests.ps1 -Sdk C:/dev/vst3sdk -Compiler C:/dev/llvm-mingw-20260908-ucrt-x86_64/bin/clang++.exe
```

The build writes the complete `Nabd Gate.vst3` directory beside this file. Build intermediates go to `build/`. The test script loads that compiled library and renders a preview BMP in the build directory. Neither script installs the plugin or changes FL Studio settings. `build.ps1` targets LLVM-MinGW; its flags are not MSVC command line flags.

The ready binary is for Windows x64. This Win32 editor is not a macOS implementation. Keep class IDs and parameter IDs stable to retain project compatibility. The host owns the audio; no sound file is loaded by the effect itself.

## Optional independent validation

Use [pluginval](https://github.com/Tracktion/pluginval), release v1.0.4:

```text
pluginval.exe --strictness-level 5 --validate "C:\path\Nabd Gate.vst3"
```

The release includes the ready binary and full application source, but not SDK or compiler downloads. Dependency licenses are supplied under `licenses/`.

## Standalone Windows installer

After building the plugin and including the dependency notices in its Resources folder:

```powershell
./installer/Build-Installer.ps1 -Compiler C:/dev/llvm-mingw-20260908-ucrt-x86_64/bin/clang++.exe -BuildDirectory ./build-installer -Output ./Nabd-Gate-Setup.exe
```

The setup embeds the entire VST3 bundle as resources and SHA256 checksums. It requests Windows administrator elevation only for installation to the standard common VST3 directory. It verifies a fresh staging directory before replacing an existing Nabd Gate bundle. It does not alter FL Studio settings. Existing unknown directories at the destination are rejected.

Test modes, which do not require elevation:

```text
Nabd-Gate-Setup.exe --verify --silent --log C:\dev\verify.txt
Nabd-Gate-Setup.exe --extract C:\dev\test-install --silent --log C:\dev\install.txt
```

Running without arguments installs immediately after any required Windows UAC consent and displays completion. `--silent` suppresses the setup's message boxes; it does not bypass Windows elevation consent. No system service, background task, or startup entry is installed.
