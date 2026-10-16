# Chiller

**A free, open-source resonance suppressor.** Chiller finds the harsh, ringing, boxy and sibilant resonances in a
sound and tames them dynamically, so a mix or a master can sit smoother without dulling it. It is an audio
**effect** plugin: VST3, CLAP, Audio Unit (macOS), LV2 (Linux) and a standalone app.

> **Status: v0.1.0 alpha.** The suppression engine works: per-bin resonance detection against an expected curve,
> protection (harmonics, transients, budget), Live / Mix / Master modes, M/S and sidechain key, and the Explain view.
> It has had no hands-on host testing on Windows and macOS yet. Release notes: [docs/release-notes/v0.1.0-alpha.md](docs/release-notes/v0.1.0-alpha.md).
> Roadmap: [docs/PLAN.md](docs/PLAN.md).

## What works today

- **Resonance suppression**: Threshold, Amount, Focus, Max Cut, Range Low / High, Attack, Release, Speed Tilt
- **Protect**: Harmonic Protect (pitch-aware), Transient Pass, Budget (caps the average reduction)
- **Modes** with exact, reported latency: Live (low latency, 5.3 ms @ 48 kHz), Mix (21 ms), Master (85 ms)
- Stereo: L/R or M/S, Link; **sidechain key** input
- Output: latency-compensated Mix, Trim, Output, **Delta** (listen to what is removed), Auto-gain, soft Bypass
- **Explain view**: input spectrum, expected curve, applied cut, labelled peaks, harmonic ticks, history heat map,
  harshness in/out and average reduction vs budget
- Six factory presets, A/B compare, undo/redo, LEARN

## Install

Pre-built zips are produced by CI for every push (GitHub Actions artifacts `chiller-windows` and `chiller-macos`) and
attached to releases.

**Windows:** copy the whole folder `Chiller.vst3` to `C:\Program Files\Common Files\VST3\` and rescan in your DAW.

**macOS:** copy `Chiller.vst3` to `~/Library/Audio/Plug-Ins/VST3/` and `Chiller.component` (Audio Unit) to
`~/Library/Audio/Plug-Ins/Components/`. The builds are unsigned, so remove the quarantine flag once and restart the DAW:

```bash
xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/VST3/Chiller.vst3 ~/Library/Audio/Plug-Ins/Components/Chiller.component
```

Or use the one-line installer, which avoids the quarantine: `curl -fsSL https://raw.githubusercontent.com/huuba91/chiller/main/install-mac.sh | bash`

**Ableton Live:** Preferences > Plug-ins > switch on "Use VST3 Plug-in System Folders" (macOS: also "Use Audio Units"),
press Rescan. Chiller appears under Plug-ins > Effects (manufacturer Neuroforge). For the sidechain key, pick the source
track in the plug-in's sidechain chooser and switch on "Sidechain key" in the Explain view.

## Suite

Chiller is a sibling of [NeuroForge](https://github.com/huuba91/neuroforge), a free neuro-bass synthesizer.
They share a manufacturer id, code conventions and UI design language, so they look and build alike.

## Build

Requirements: CMake 3.22+, a C++20 compiler, and git. JUCE and clap-juce-extensions are cloned into `libs/`
(not committed).

```bash
git clone --depth 1 --branch 8.0.4 https://github.com/juce-framework/JUCE.git libs/JUCE
git clone https://github.com/free-audio/clap-juce-extensions.git libs/clap-juce-extensions
git -C libs/clap-juce-extensions checkout 7adee3a1bd4684d4caa5601100e364abccff4b4d
git -C libs/clap-juce-extensions submodule update --init --recursive
```

CLAP support is optional: if `libs/clap-juce-extensions` is missing, Chiller builds without it.

### Linux

```bash
sudo apt install ninja-build libasound2-dev libjack-jackd2-dev libfreetype-dev libfontconfig1-dev libx11-dev \
  libxcomposite-dev libxcursor-dev libxext-dev libxinerama-dev libxrandr-dev libxrender-dev libgl1-mesa-dev \
  libcurl4-openssl-dev
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --target Chiller_VST3 Chiller_CLAP Chiller_LV2 Chiller_Standalone chiller_tests
build/chiller_tests
```

Outputs are in `build/Chiller_artefacts/Release/`.

### macOS

```bash
brew install ninja
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release     # universal arm64 + x86_64, macOS 10.15+
cmake --build build --target Chiller_VST3 Chiller_AU Chiller_CLAP Chiller_Standalone chiller_tests
build/chiller_tests
```

### Windows (Visual Studio 2022)

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --target Chiller_VST3 Chiller_CLAP Chiller_Standalone chiller_tests
build\Release\chiller_tests.exe
```

### DSP only (no JUCE needed)

```bash
cmake -S . -B build-dsp -DCH_BUILD_PLUGIN=OFF && cmake --build build-dsp && build-dsp/chiller_tests
```

### Offline render check

`chiller_render` (no JUCE needed) processes generated signals (pink noise with fixed and swept resonances, a harmonic tone)
and prints before / after band energies; `chiller_render --bench` prints the CPU time per second of audio;
`--wav <dir>` writes before / after files for listening.

### UI screenshot

`-DCH_BUILD_TOOLS=ON` adds `chiller_uishot`, which renders the editor to a PNG without a host
(`xvfb-run -a chiller_uishot out.png` on a headless Linux box).

## Repository layout

| Path | What |
| --- | --- |
| `src/dsp/` | Pure C++20 DSP (no JUCE): FFT, `Suppressor` (STFT + detection + gain), engine, UI telemetry. Static lib `chiller_dsp`. |
| `src/engine/` | Parameter table (`Params.*`) and factory presets (`Presets.*`), also JUCE-free. |
| `src/ui/` | JUCE UI (namespace `chui`): look and feel, knobs, displays, panels. |
| `src/Plugin*.{h,cpp}` | JUCE processor and editor. |
| `src/tests/` | Unit tests (`chiller_tests`). |
| `tools/` | Packaging script, UI screenshot tool (`uishot`), offline render check (`render`). |
| `presets/community/` | Community presets (JSON), see [CONTRIBUTING.md](CONTRIBUTING.md). |

## License

Chiller is licensed under the **GNU Affero General Public License v3.0** (see [LICENSE](LICENSE)).
It embeds Roboto Condensed (Apache 2.0, see `assets/fonts`). Plugin formats use JUCE (AGPLv3 / commercial),
clap-juce-extensions (MIT) and the CLAP headers (MIT).
