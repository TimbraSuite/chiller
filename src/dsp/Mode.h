#pragma once
// Processing mode: trades latency for resolution. Order matches the "mode" parameter's choices.
namespace chiller::dsp {

enum class Mode : int { Live = 0, Mix = 1, Master = 2, Count = 3 };

// STFT size each mode will use. Provisional: tuned in v0.1 once the detector exists.
constexpr int fftSizeForMode(Mode m)
{
    switch (m) {
        case Mode::Live:   return 512;
        case Mode::Mix:    return 1024;
        case Mode::Master: return 4096;
        default:           return 1024;
    }
}

// Reported latency of a mode. Live is a plain passthrough for now (0); it will move to the
// 512-point STFT once its engine exists.
constexpr int latencyForMode(Mode m) { return m == Mode::Live ? 0 : fftSizeForMode(m); }

constexpr const char* modeName(Mode m)
{
    return m == Mode::Live ? "Live" : (m == Mode::Mix ? "Mix" : "Master");
}

} // namespace chiller::dsp
