#pragma once
// Processing mode: trades latency for resolution. Order matches the "mode" parameter's choices.
#include <algorithm>
#include <cmath>

namespace chiller::dsp {

enum class Mode : int { Live = 0, Mix = 1, Master = 2, Count = 3 };

// STFT size each mode uses at a given sample rate: 256 / 1024 / 4096 points at 48 kHz, scaled with the
// sample rate (nearest power of two) so the window keeps roughly the same duration.
// Live ~5.3 ms, Mix ~21 ms, Master ~85 ms (at 48 kHz). The reported latency equals the STFT size.
inline int fftSizeFor(Mode m, double sampleRate)
{
    const double base = m == Mode::Live ? 256.0 : (m == Mode::Mix ? 1024.0 : 4096.0);
    const double ideal = base * (sampleRate > 0.0 ? sampleRate : 48000.0) / 48000.0;
    int n = 64;
    while ((double) n * 1.41421356 < ideal && n < (1 << 18)) n <<= 1;
    return n;
}

// Lowest frequency this STFT size can reliably separate (about kResolveBins bins). Below it a bass note or kick
// cannot be told from a resonance, so the detector fades out over the octave beneath it (see Suppressor) and
// LEARN does not listen there. 48 kHz: Live 1.5 kHz, Mix 375 Hz, Master 300 Hz (floor; 8 bins would be 94 Hz).
// (Tested on real recordings: with 6 bins / a 150 Hz floor, voice fundamentals, string and bass harmonics at 150-300 Hz were cut and released
// like resonances, moving the 30-200 Hz level by 0.5-1.5 dB (std) in Master and Mix.)
constexpr float kResolveBins = 8.f;
constexpr float kBassFloorHz = 300.f;   // even a very fine STFT keeps bass notes and voice fundamentals (tonal, 30+ dB above their surroundings) out of detection
inline float resolvedFromHz(int fftSize, double sampleRate)
{
    return std::max(kBassFloorHz, kResolveBins * (float) (sampleRate / (double) fftSize));
}
inline float resolvedFromHz(Mode m, double sampleRate) { return resolvedFromHz(fftSizeFor(m, sampleRate), sampleRate); }

inline int latencyForMode(Mode m, double sampleRate) { return fftSizeFor(m, sampleRate); }

inline const char* modeName(Mode m)
{
    return m == Mode::Live ? "Live (low latency)" : (m == Mode::Mix ? "Mix" : "Master");
}

} // namespace chiller::dsp
