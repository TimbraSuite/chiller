#pragma once
// Signal generators and measurement helpers shared by the DSP tests and the offline render tool.
#include "dsp/Engine.h"
#include "dsp/Fft.h"
#include "dsp/Learn.h"
#include <algorithm>
#include <cmath>
#include <numbers>
#include <tuple>
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


// ---- "mix" test signal and low-end bounce measurement ---------------------------------------------------
// Four-on-the-floor kick (sine with pitch drop + click, 120 BPM), a bassline that changes note every beat
// (40..150 Hz), and a bright harsh synth with resonances at 2.5k / 4k / 6k on top of pink noise.
inline std::vector<float> mixSignal(int n, double sr, unsigned long long seed = 11)
{
    std::vector<float> x = pinkNoise(n, 0.03f, seed);
    const int beat = (int) (sr * 0.5);
    Rng rng(seed + 5);
    // harsh synth: three narrow resonances, slowly amplitude-modulated, plus a bright saw pad
    for (auto [hz, amp, lfo] : { std::tuple<double, float, double> { 2500.0, 0.020f, 0.7 }, { 4000.0, 0.018f, 0.5 }, { 6000.0, 0.014f, 0.9 } })
        for (int i = 0; i < n; ++i)
            x[(size_t) i] += amp * (0.8f + 0.2f * (float) std::sin(2.0 * std::numbers::pi * lfo * i / sr))
                             * (float) std::sin(2.0 * std::numbers::pi * hz * (double) i / sr);
    {
        double ph[3] = { 0, 0, 0 };
        const double f0[3] = { 329.6, 331.2, 493.9 };
        float lp = 0.f;
        for (int i = 0; i < n; ++i) {
            float s = 0.f;
            for (int v = 0; v < 3; ++v) { ph[v] += f0[v] / sr; ph[v] -= std::floor(ph[v]); s += (float) (2.0 * ph[v] - 1.0); }
            lp += (s - lp) * 0.5f;     // bright, only gently tilted
            x[(size_t) i] += 0.02f * lp;
        }
    }
    // kick: pitch-dropping sine + click
    for (int b = 0; b * beat < n; ++b) {
        const int s0 = b * beat;
        double ph = 0.0;
        for (int i = 0; i < beat && s0 + i < n; ++i) {
            const double t = (double) i / sr, f = 50.0 + 90.0 * std::exp(-t / 0.025);
            ph += 2.0 * std::numbers::pi * f / sr;
            float v = 0.55f * (float) (std::sin(ph) * std::exp(-t / 0.16));
            if (i < (int) (0.003 * sr)) v += 0.25f * rng.uni() * (float) (1.0 - i / (0.003 * sr));
            x[(size_t) (s0 + i)] += v;
        }
    }
    // bassline: a new note every beat, saw-ish (sine + 2nd + 3rd harmonics), slight gap at the end of each note
    const double notes[] = { 41.2, 55.0, 49.0, 73.4, 61.7, 98.0, 43.7, 130.8, 65.4, 51.9, 87.3, 146.8 };
    for (int b = 0; b * beat < n; ++b) {
        const double f = notes[(size_t) (b * 5 % 12)];
        const int s0 = b * beat + beat / 4;      // starts between kicks, overlaps the next one
        const int len = (int) (beat * 0.9);
        double ph = 0.0;
        for (int i = 0; i < len && s0 + i < n; ++i) {
            const double t = (double) i / sr;
            const float env = (float) std::min(1.0, t / 0.006) * (float) std::min(1.0, (len - i) / (0.02 * sr));
            ph += 2.0 * std::numbers::pi * f / sr;
            x[(size_t) (s0 + i)] += 0.22f * env * (float) (std::sin(ph) + 0.5 * std::sin(2 * ph) + 0.3 * std::sin(3 * ph));
        }
    }
    return x;
}

struct BounceStats {
    double grStd = 0, grP2P = 0, grMean = 0;         // gain reduction averaged over 30..200 Hz, over time (dB)
    double lvlStd = 0, lvlMean = 0;                  // output - input level in 30..200 Hz, short windows (dB)
    double res[3] = { 0, 0, 0 };                     // change at 2.5k / 4k / 6k (dB, negative = reduced)
};

// Runs `x` through an engine and measures how much the low end moves. `skipSec` of start-up is ignored.
inline BounceStats measureBounce(const std::vector<float>& x, double sr, chiller::dsp::Mode mode, const chiller::dsp::SuppressorParams& p,
                                 double skipSec = 1.5)
{
    using namespace chiller::dsp;
    Engine e;
    e.prepare(sr, mode);
    e.setParams(p);
    auto l = x, r = x;
    std::vector<double> gr;
    unsigned long long lastFrame = 0;
    const int total = (int) x.size(), block = 512;
    for (int pos = 0; pos < total; pos += block) {
        const int n = std::min(block, total - pos);
        e.process(l.data() + pos, r.data() + pos, nullptr, nullptr, n);
        if (pos < (int) (skipSec * sr)) continue;
        UiFrame f;
        e.readUi(f);
        if (f.frameCounter == lastFrame) continue;
        lastFrame = f.frameCounter;
        double s = 0; int c = 0;
        for (int i = 0; i < kUiPoints; ++i) { const float hz = uiPointHz(i); if (hz >= 30.f && hz <= 200.f) { s += f.gr[i]; ++c; } }
        gr.push_back(s / c);
    }
    BounceStats b;
    if (!gr.empty()) {
        double mn = 1e9, mx = -1e9, m = 0;
        for (double g : gr) { mn = std::min(mn, g); mx = std::max(mx, g); m += g; }
        m /= (double) gr.size();
        double v = 0;
        for (double g : gr) v += (g - m) * (g - m);
        b.grStd = std::sqrt(v / (double) gr.size()); b.grP2P = mx - mn; b.grMean = m;
    }
    const int lat = e.latencySamples(), len = 2048;
    std::vector<double> d;
    for (int s = (int) (skipSec * sr); s + len <= total - 2048; s += 1024)
        d.push_back(bandDb(l, s, len, sr, 30.0, 200.0) - bandDb(x, s - lat, len, sr, 30.0, 200.0));
    if (!d.empty()) {
        double m = 0, v = 0;
        for (double q : d) m += q;
        m /= (double) d.size();
        for (double q : d) v += (q - m) * (q - m);
        b.lvlMean = m; b.lvlStd = std::sqrt(v / (double) d.size());
    }
    const double centers[3] = { 2500.0, 4000.0, 6000.0 };
    const int w = 8192, segs = 6, start = total - w * segs - 100;
    for (int i = 0; i < 3; ++i)
        b.res[i] = bandDbAvg(l, start, w, segs, sr, centers[i] * 0.97, centers[i] * 1.03)
                   - bandDbAvg(x, start - lat, w, segs, sr, centers[i] * 0.97, centers[i] * 1.03);
    return b;
}

// What LEARN would do after listening to the first `learnSec` of `x` (default parameters during listening).
inline chiller::dsp::LearnResult emulateLearn(const std::vector<float>& x, double sr, chiller::dsp::Mode mode,
                                              const chiller::dsp::SuppressorParams& cur, double learnSec = 3.0)
{
    using namespace chiller::dsp;
    Engine e;
    e.prepare(sr, mode);
    e.setParams(cur);
    auto l = x, r = x;
    LearnAccumulator acc;
    acc.reset();
    unsigned long long last = 0;
    const int total = std::min((int) x.size(), (int) (learnSec * sr)), block = 512;
    for (int pos = 0; pos < total; pos += block) {
        const int n = std::min(block, total - pos);
        e.process(l.data() + pos, r.data() + pos, nullptr, nullptr, n);
        UiFrame f;
        e.readUi(f);
        if (f.frameCounter != last) { last = f.frameCounter; acc.add(f, cur.thresholdDb); }
    }
    return acc.finish(cur.rangeLowHz, cur.rangeHighHz, cur.releaseMs, cur.maxCutDb, resolvedFromHz(mode, sr));
}

inline chiller::dsp::SuppressorParams withLearn(chiller::dsp::SuppressorParams p, const chiller::dsp::LearnResult& r)
{
    p.thresholdDb = r.threshold; p.amount = r.amount; p.releaseMs = r.release; p.maxCutDb = r.maxCut; p.rangeLowHz = r.rangeLow;
    return p;
}

inline chiller::dsp::SuppressorParams defaultParams()
{
    chiller::dsp::SuppressorParams p;
    return p;
}

} // namespace chtest
