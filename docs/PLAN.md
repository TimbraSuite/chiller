# Chiller roadmap

Chiller is a free, open-source resonance suppressor (effect plugin). The work is split into phases; each phase ends
in a tagged pre-release. Phases may shift, the order will not change much.

## Foundation: v0.0 (this repository state)

Plugin shell, no suppression yet.

- JUCE 8 plugin (VST3, AU, CLAP, LV2, Standalone), CI on Windows/macOS/Linux, packaging, validators
- STFT framework (Hann, 75 % overlap, weighted overlap-add) with per-bin gain hook (unity), exact latency reporting
- Modes Live / Mix / Master, latency-compensated dry/wet, trim/output, Delta, soft bypass
- Full parameter table, UI shell (top bar, Explain view, panels), unit tests

## Core: v0.1 alpha

The first version that actually suppresses resonances.

- Mix-mode engine: spectral-envelope detection, per-bin gain computation, smoothing
- Main controls live: Threshold, Amount, Focus, Attack, Release, Max Cut, Range Low/High, Mix
- **Delta** listen mode returns the removed signal
- **Explain view** overlays detected resonances and applied reduction on the input spectrum

## Shaping: v0.2

- Bands with per-band overrides
- L/R and M/S processing with Link
- Sidechain **key** input (detect from the sidechain, apply to the main signal)
- **Master** mode (long window, higher resolution)
- Presets, A/B compare, undo/redo
- Auto-gain (level-matched output)
- Speed Tilt (attack/release shaped across frequency)

## Protect: v0.3 beta

- **Harmonic Protect**: keep musically meaningful harmonic peaks
- **Transient Pass**: let attacks through untouched
- **Budget**: cap total reduction so the sound is never over-processed
- Harshness meter and reduction history in the right-hand strip

## Smart: v0.4 to v1.0

- **Learn**: listen to a passage and set up detection automatically
- **Unmask** with a sidechain: carve space for another track
- **Freeze-to-EQ**: convert the current reduction curve to static EQ bands
- **Live** mode: low-latency engine (512-point STFT)
- LV2 polish and wider host testing
- User manual, community preset pack, 1.0 release

## Principles

- Transparent by default: unity gain must stay bit-clean apart from the latency
- Every sound change comes with before/after renders and a blind ABX round (see CONTRIBUTING.md)
- Pure C++ DSP in `src/dsp`, tested without a host
