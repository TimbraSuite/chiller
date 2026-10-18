#pragma once
// Bands: up to kNumBands bell-shaped regions the user places on the spectrum to make the processing stronger or weaker there
// (Soothe-style "sensitivity" nodes). Pure logic (no JUCE) so the engine, the Explain view and the tests share the same curve.
//
// Each enabled band has a centre frequency, a width (octaves at half height) and a depth from -1 to +1:
//   - negative depth weakens the processing: the cut is scaled by (1 + depth * bell), so -100 % switches it off at the centre;
//   - positive depth strengthens it: the detector gets up to kBandSensDb more sensitive (a lower threshold) and the cut grows
//     by up to (1 + kBandBoost) at the centre, allowed to go that far past Max Cut.
// The weight scales the cut after the Max Cut cap, so -50 % halves even a cut that sits at Max Cut.
// Overlapping bands multiply (weight) and add (sensitivity), clamped.
#include <array>
#include <cmath>

namespace chiller::dsp {

constexpr int kNumBands = 6;
constexpr float kBandSensDb = 6.f;       // threshold shift at depth +100 %
constexpr float kBandBoost = 0.5f;       // extra cut factor at depth +100 %
constexpr float kBandMaxWeight = 2.5f, kBandMaxSensDb = 12.f;

struct Band {
    bool on = false;
    float hz = 1000.f;
    float depth = 0.f;      // -1..1
    float widthOct = 1.f;   // full width at half height, octaves
    bool operator==(const Band& o) const { return on == o.on && hz == o.hz && depth == o.depth && widthOct == o.widthOct; }
    bool operator!=(const Band& o) const { return !(*this == o); }
};
using Bands = std::array<Band, kNumBands>;

// Bell of one band at frequency f (1 at the centre, 0.5 at +-widthOct/2).
inline float bandBell(const Band& b, float f)
{
    if (f <= 0.f || b.hz <= 0.f) return 0.f;
    const float d = std::log2(f / b.hz) / std::fmax(0.02f, b.widthOct * 0.5f);
    return std::exp(-0.6931472f * d * d);   // 2^(-d^2)
}

struct BandEffect {
    float weight = 1.f;   // multiplies the cut
    float sensDb = 0.f;   // subtracted from the threshold
};

inline BandEffect bandEffect(const Bands& bands, float f)
{
    BandEffect e;
    for (const auto& b : bands) {
        if (!b.on || b.depth == 0.f) continue;
        const float d = std::fmax(-1.f, std::fmin(1.f, b.depth)) * bandBell(b, f);
        if (d < 0.f) e.weight *= 1.f + d;
        else { e.weight *= 1.f + kBandBoost * d; e.sensDb += kBandSensDb * d; }
    }
    e.weight = std::fmax(0.f, std::fmin(kBandMaxWeight, e.weight));
    e.sensDb = std::fmin(kBandMaxSensDb, e.sensDb);
    return e;
}

inline bool anyBandActive(const Bands& bands)
{
    for (const auto& b : bands) if (b.on && b.depth != 0.f) return true;
    return false;
}

} // namespace chiller::dsp
