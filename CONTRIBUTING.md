# Contributing to Chiller

Thanks for helping! Chiller is a free, open-source resonance suppressor and is still very young (alpha, v0.1).
Please read the [Code of Conduct](CODE_OF_CONDUCT.md) first, and the roadmap in [docs/PLAN.md](docs/PLAN.md) so your
work fits the current phase.

## Building

See the build section of the [README](README.md). The short version:

```bash
git clone --depth 1 --branch 8.0.4 https://github.com/juce-framework/JUCE.git libs/JUCE
git clone https://github.com/free-audio/clap-juce-extensions.git libs/clap-juce-extensions   # optional (CLAP)
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

For DSP work you do not need JUCE at all: `cmake -S . -B build-dsp -DCH_BUILD_PLUGIN=OFF`.

## Tests

```bash
build/chiller_tests            # all tests
build/chiller_tests stft       # only tests whose name contains "stft"
```

Every change to `src/dsp/` or `src/engine/` needs a test in `src/tests/test_*.cpp` (tiny harness: `CH_TEST`,
`CH_CHECK`, `CH_CHECK_NEAR`). CI runs the tests on Windows, macOS and Linux, then validates the CLAP build with
clap-validator, the VST3 build with pluginval (Linux) and the AU with auval (macOS).

## Code conventions

- C++20. `src/dsp/` and `src/engine/` are pure C++ with **no JUCE includes**.
- No allocation, locks or I/O on the audio thread.
- Parameters live in one table (`src/engine/Params.cpp`); ids are stable, so never rename one (add a new one).
- UI colours, sizes and fonts come from the tokens in `src/ui/Look.h`. Chiller is neutral grey: no blue tint, no textures.
- Match the surrounding style; keep functions short and comments about *why*.

## Pull requests

1. Open an issue first for anything larger than a small fix, so we can agree on the approach.
2. Fork, create a branch from `main`, make focused commits.
3. Make sure `chiller_tests` passes locally and the plugin builds.
4. Open a PR using the template. Describe what changed and how you verified it. UI changes need a screenshot
   (`chiller_uishot`, see README).

### Sound changes need evidence

Anything that changes how Chiller sounds (detection, gain computation, smoothing, windowing, defaults, modes) must
include:

- **before/after renders** of the same source material (links or small files; say which parameters were used), and
- the result of a **blind ABX round** (for example with `abxer`, foobar2000's ABX comparator or a DAW's blind-test
  tool) with the number of trials and the score, or a clear note that no audible difference was intended.

Null tests, spectrograms and meter screenshots are welcome extras but do not replace listening.

## Community presets

Presets are plain JSON files in [`presets/community/`](presets/community). One file per preset, named
`<author>-<short-name>.json`:

```json
{
  "name": "Gentle Vocal Air",
  "author": "your-github-name",
  "chillerVersion": "0.2.0",
  "mode": "Mix",
  "params": { "threshold": 6.0, "amount": 40.0, "focus": 0.1667, "attack": 8.0, "release": 150.0 }
}
```

`params` keys are parameter ids from `src/engine/Params.cpp`, values are in real units (dB, %, ms, Hz, octaves);
omitted parameters keep their defaults. Presets are loadable once the preset system lands in v0.2; until then you
can already submit them. Only submit presets you made yourself, and say what source material they suit.

## Reporting bugs

Use the issue templates. Include the plugin version, host and OS, and the smallest steps that reproduce the problem.
