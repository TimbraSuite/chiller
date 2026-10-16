#pragma once
// Processing mode: trades latency for resolution. Order matches the "mode" parameter's choices.
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

inline int latencyForMode(Mode m, double sampleRate) { return fftSizeFor(m, sampleRate); }

inline const char* modeName(Mode m)
{
    return m == Mode::Live ? "Live (low latency)" : (m == Mode::Mix ? "Mix" : "Master");
}

} // namespace chiller::dsp
