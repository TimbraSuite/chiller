#pragma once
// LEARN: turns a few seconds of published UI frames into parameter suggestions. Pure logic (no JUCE) so the
// editor, the offline render tool and the tests all use the same code.
#include "Telemetry.h"
#include <algorithm>
#include <cmath>
#include <functional>
#include <vector>

namespace chiller::dsp {

// LEARN never listens below kLearnMinHz (nor below the lowest frequency the current mode resolves): bass and kick are not resonances, and their excess would otherwise
// raise the threshold until the real (harsh) peaks are no longer cut.
constexpr float kLearnMinHz = 150.f;
constexpr float kLearnRangeLowHz = 120.f;     // Range Low is raised to at least this
constexpr float kLearnMinReleaseMs = 150.f;   // slow enough not to pump
constexpr float kLearnAmount = 0.5f;
constexpr float kLearnPitchConf = 0.8f;     // a frame counts as "pitched" above this pitch confidence
constexpr float kLearnProtectBase = 0.3f;    // Harmonic Protect is never lowered by LEARN, and raised towards kLearnProtectMax for strongly pitched material
constexpr float kLearnProtectMax = 0.8f;
constexpr float kLearnMaxCutDb = 9.f;         // Max Cut is lowered to at most this

struct LearnResult {
    bool valid = false;
    float threshold = 6.f, amount = kLearnAmount, release = 120.f, maxCut = 12.f, rangeLow = 100.f;
    float pitchShare = 0.f;       // fraction of the listened frames with a confident pitch
    float harmonicProtect = 0.f;  // 0..1, never below the current setting
};

class LearnAccumulator {
public:
    void reset() { excess.assign(kUiPoints, 0.f); level.assign(kUiPoints, 0.f); frames = 0; pitched = 0; }
    void add(const UiFrame& f, float thresholdDb)
    {
        if (excess.empty()) reset();
        for (int i = 0; i < kUiPoints; ++i) {
            excess[(size_t) i] += std::max(0.f, f.inDb[i] - (f.expDb[i] - thresholdDb));
            level[(size_t) i] += f.inDb[i];
        }
        ++frames;
        if (f.f0Conf > kLearnPitchConf) ++pitched;
    }
    int numFrames() const { return frames; }

    LearnResult finish(float curRangeLow, float curRangeHigh, float curRelease, float curMaxCut, float resolvedHz = 0.f, float curProtect = 0.f) const
    {
        LearnResult r;
        r.release = curRelease; r.maxCut = curMaxCut; r.rangeLow = curRangeLow; r.harmonicProtect = curProtect;
        std::vector<float> ex;
        for (int i = 0; i < kUiPoints; ++i) {
            const float hz = uiPointHz(i);
            if (hz < std::max({ curRangeLow, kLearnMinHz, resolvedHz }) || hz > curRangeHigh || frames == 0) continue;
            if (level[(size_t) i] / (float) frames < -100.f) continue;
            ex.push_back(excess[(size_t) i] / (float) frames);
        }
        if (ex.size() < 20) return r;
        std::sort(ex.begin(), ex.end(), std::greater<float>());
        const float p = ex[std::min(ex.size() - 1, (size_t) std::ceil(0.06 * (double) ex.size()))];   // top ~6 % of the spectrum
        r.threshold = std::clamp(p + 1.f, 4.f, 16.f);
        r.amount = kLearnAmount;
        r.release = std::max(curRelease, kLearnMinReleaseMs);
        r.maxCut = std::min(curMaxCut, kLearnMaxCutDb);
        r.rangeLow = std::max(curRangeLow, kLearnRangeLowHz);
        r.pitchShare = (float) pitched / (float) frames;
        // Strongly pitched material (a solo instrument, a voice) has partials that look like resonances to any detector: protect them harder.
        // A dense mix with a lead on top is pitched too, but only part of the time, so it stays near the base value.
        const float t = std::clamp((r.pitchShare - 0.75f) / 0.2f, 0.f, 1.f), u = t * t * (3.f - 2.f * t);
        r.harmonicProtect = std::max(curProtect, kLearnProtectBase + (kLearnProtectMax - kLearnProtectBase) * u);
        r.valid = true;
        return r;
    }

private:
    std::vector<float> excess, level;
    int frames = 0, pitched = 0;
};

} // namespace chiller::dsp
