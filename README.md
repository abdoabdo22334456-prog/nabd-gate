# Nabd Gate

A tempo-synced **rhythmic gate and repeat/stutter VST3 audio effect for Windows x64**.

[Download the Windows installer](https://github.com/abdoabdo22334456-prog/nabd-gate/releases/latest/download/Nabd-Gate-Setup.exe) · [Download the VST3 bundle and source](https://github.com/abdoabdo22334456-prog/nabd-gate/releases/latest/download/Nabd-Gate-v1.2.0-Windows.zip) · [الشرح بالعربي](README-AR.md)

![Repeat / Stutter interface](Nabd-Repeat-preview.png)

## Features

- Two independent pattern editors: **Volume / Gate** and **Repeat / Stutter**.
- Up to **four sequential patterns**, each with 16 steps, synchronized to host tempo and song position.
- Click or drag to paint steps. Pattern presets and All On / All Off / Invert affect the selected row.
- Gate: enabled cells pass audio; disabled cells mute it.
- Repeat: enabled cells capture and repeat a preceding audio slice; disabled cells pass live audio.
- Musical repeat sizes: 1/4, 1/8, 1/16, 1/32, 1/64 and 1/16 triplet.
- Independent section switches, shared smoothing and wet/dry controls, and global bypass.
- Automatable controls and project-state persistence, including migration from versions 1.0 and 1.1.
- Native Win32 interface with DPI scaling; mono/stereo and float32/float64 processing.

## Install and use in FL Studio

1. Download and run **Nabd-Gate-Setup.exe** from [Releases](https://github.com/abdoabdo22334456-prog/nabd-gate/releases/latest). Approve the Windows administrator request if shown.
2. The installer copies the complete plugin bundle to `C:\Program Files\Common Files\VST3\Nabd Gate.vst3` and verifies its files.
3. In FL Studio, open **Manage plugins**, enable **Verify plugins**, and select **Find installed plugins**.
4. Add **Nabd Gate** to an audio channel's Mixer effect slot and start transport playback.

For repeat alone, turn **Gate Off**, open **Repeat / Stutter**, turn **Repeat On**, choose **1/32** or **1/64**, and paint the purple LOOP cells. Keep both sections enabled to gate the repeated audio.

The effect processes incoming DAW audio. It does not load audio files itself. Repeat initially passes live input until a complete slice has been recorded; seek/restart clears that temporary recording. The installer does not change FL Studio settings.

## Build and validation

The application and installer are written in C++17 using the Steinberg VST3 SDK and native Windows APIs. See [BUILDING.md](BUILDING.md) for pinned dependencies and PowerShell build commands. The ready installer and compiled VST3 are distributed through Releases; generated binaries are excluded from Git history.

The compiled library passed custom audio/state/native-editor tests and **pluginval v1.0.4 at strictness level 5**. See [VALIDATION.md](VALIDATION.md) and the logs in [tests](tests). FL Studio itself has not been exercised by these automated tests; a DAW scan and playback check are still needed on the user's installation.

## License

Application source: [MIT](LICENSE.txt). Third-party SDK/runtime notices: [licenses](licenses).
