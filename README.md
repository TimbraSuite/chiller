# Chiller

**A free, open-source resonance suppressor.** Chiller finds the harsh, ringing, boxy and sibilant resonances in a
sound and tames them dynamically, so a mix or a master can sit smoother without dulling it. It is an audio
**effect** plugin: VST3, CLAP, Audio Unit (macOS), LV2 (Linux) and a standalone app.

> **Status: Foundation (v0.0.1), pre-alpha.** The plugin shell, parameters, UI layout, latency handling, build and
> CI are in place, but **there is no resonance suppression yet**: audio passes through unchanged (with the latency
> of the selected mode). The suppression engine arrives in v0.1. See [docs/PLAN.md](docs/PLAN.md) for the roadmap.

## What works today

- Transparent STFT passthrough (Hann window, 75 % overlap, weighted overlap-add) with exact, reported latency
- Mode: **Live** (0 latency passthrough for now), **Mix** (1024-point STFT), **Master** (4096-point STFT); the plugin
  re-reports its latency to the host when the mode changes
- Latency-compensated dry/wet mix, trim, output gain, Delta (listen to what is removed), soft bypass that keeps latency
- Full planned parameter set exposed to the host (most of it inert until v0.1 to v0.3)
- Resizable UI with a live input spectrum (the "Explain" view)
- Optional stereo sidechain input bus (unused until v0.2)

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

### UI screenshot

`-DCH_BUILD_TOOLS=ON` adds `chiller_uishot`, which renders the editor to a PNG without a host
(`xvfb-run -a chiller_uishot out.png` on a headless Linux box).

## Repository layout

| Path | What |
| --- | --- |
| `src/dsp/` | Pure C++20 DSP (no JUCE): FFT, STFT framework, engine. Static lib `chiller_dsp`. |
| `src/engine/` | Parameter table (`Params.*`), also JUCE-free. |
| `src/ui/` | JUCE UI (namespace `chui`): look and feel, knobs, displays, panels. |
| `src/Plugin*.{h,cpp}` | JUCE processor and editor. |
| `src/tests/` | Unit tests (`chiller_tests`). |
| `tools/` | Packaging script, UI screenshot tool. |
| `presets/community/` | Community presets (JSON), see [CONTRIBUTING.md](CONTRIBUTING.md). |

## License

Chiller is licensed under the **GNU Affero General Public License v3.0** (see [LICENSE](LICENSE)).
It embeds Roboto Condensed (Apache 2.0, see `assets/fonts`). Plugin formats use JUCE (AGPLv3 / commercial),
clap-juce-extensions (MIT) and the CLAP headers (MIT).
