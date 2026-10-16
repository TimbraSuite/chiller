#pragma once
// Signal generators and measurement helpers shared by the DSP tests and the offline render tool.
#include "dsp/Engine.h"
#include "dsp/Fft.h"
#include <algorithm>
#include <cmath>
#include <numbers>
#include <vector>

namespace chtest {

struct Rng {
    unsigned long long s;
    explicit Rng(unsigned long long seed = 1) : s(seed * 6364136223846793005ULL + 1442695040888963407ULL) {}
    unsigned next() { s ^= s << 13; s ^= s >> 7; s ^= s << 17; return (unsigned) (s >> 16); }
    float uni() { return (float) (next() & 0xffffff) / (float) 0x1000000 * 2.f - 1.f; }   // [-1, 1)
    float gauss() { float a = 0.f; for (int i = 0; i < 6; ++i) a += uni(); return a * 0.7071f; }
};

// Pink-ish noise (Paul Kellet's filter), scaled to roughly `rms`.
inline std::vector<float> pinkNoise(int n, float rms, unsigned long long seed)
{
    Rng r(seed);
    std::vector<float> v((size_t) n);
    float b0 = 0, b1 = 0, b2 = 0, b3 = 0, b4 = 0, b5 = 0, b6 = 0;
    double sum2 = 0.0;
    for (auto& x : v) {
        const float w = r.gauss();
        b0 = 0.99886f * b0 + w * 0.0555179f; b1 = 0.99332f * b1 + w * 0.0750759f; b2 = 0.96900f * b2 + w * 0.1538520f;
        b3 = 0.86650f * b3 + w * 0.3104856f; b4 = 0.55000f * b4 + w * 0.5329522f; b5 = -0.7616f * b5 - w * 0.0168980f;
        x = b0 + b1 + b2 + b3 + b4 + b5 + b6 + w * 0.5362f;
        b6 = w * 0.115926f;
        sum2 += (double) x * x;
    }
    const float g = rms / (float) std::sqrt(sum2 / n);
    for (auto& x : v) x *= g;
    return v;
}

inline std::vector<float> whiteNoise(int n, float amp, unsigned long long seed)
{
    Rng r(seed);
    std::vector<float> v((size_t) n);
    for (auto& x : v) x = r.uni() * amp;
    return v;
}

inline void addSine(std::vector<float>& v, double sr, double hz, float amp, double phase = 0.0, int from = 0, int to = -1)
{
    if (to < 0 || to > (int) v.size()) to = (int) v.size();
    for (int i = from; i < to; ++i) v[(size_t) i] += amp * (float) std::sin(2.0 * std::numbers::pi * hz * (double) i / sr + phase);
}

// Run an engine over stereo signals in blocks of an awkward size. Output has the same length.
inline void run(chiller::dsp::Engine& e, std::vector<float>& l, std::vector<float>& r, const std::vector<float>* scL = nullptr,
                const std::vector<float>* scR = nullptr, int block = 211)
{
    const int total = (int) l.size();
    for (int pos = 0; pos < total; pos += block) {
        const int n = std::min(block, total - pos);
        e.process(l.data() + pos, r.data() + pos, scL ? scL->data() + pos : nullptr, scR ? scR->data() + pos : nullptr, n);
    }
}

// Energy (dB, arbitrary reference) in [f1, f2] of a Hann-windowed segment [start, start+len), len a power of two.
inline double bandDb(const std::vector<float>& x, int start, int len, double sr, double f1, double f2)
{
    chiller::dsp::Fft fft(len);
    std::vector<float> re((size_t) len), im((size_t) len, 0.f);
    for (int i = 0; i < len; ++i) {
        const double w = 0.5 - 0.5 * std::cos(2.0 * std::numbers::pi * i / len);
        const int idx = start + i;
        re[(size_t) i] = idx >= 0 && idx < (int) x.size() ? x[(size_t) idx] * (float) w : 0.f;
    }
    fft.forward(re.data(), im.data());
    double e = 0.0;
    for (int k = 1; k < len / 2; ++k) {
        const double f = k * sr / len;
        if (f >= f1 && f <= f2) e += (double) re[(size_t) k] * re[(size_t) k] + (double) im[(size_t) k] * im[(size_t) k];
    }
    return 10.0 * std::log10(e + 1e-30);
}

// Average of bandDb over consecutive segments (more stable for noise).
inline double bandDbAvg(const std::vector<float>& x, int start, int len, int segments, double sr, double f1, double f2)
{
    double e = 0.0;
    for (int s = 0; s < segments; ++s) e += std::pow(10.0, bandDb(x, start + s * len, len, sr, f1, f2) / 10.0);
    return 10.0 * std::log10(e / segments + 1e-30);
}

inline chiller::dsp::SuppressorParams defaultParams()
{
    chiller::dsp::SuppressorParams p;
    return p;
}

} // namespace chtest
