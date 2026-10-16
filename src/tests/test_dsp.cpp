#include "Helpers.h"
#include "TestMain.h"
#include "engine/Presets.h"
#include <random>

using namespace chiller::dsp;
using namespace chtest;

static const double kRates[] = { 44100.0, 48000.0, 96000.0 };

static std::vector<float> randomSignal(int n, unsigned seed, float amp = 0.9f) { return whiteNoise(n, amp, seed); }

static SuppressorParams amountZero()
{
    SuppressorParams p;
    p.amount = 0.f;
    return p;
}

CH_TEST(mode_fft_sizes_scale_with_sample_rate)
{
    CH_CHECK(fftSizeFor(Mode::Live, 48000.0) == 256);
    CH_CHECK(fftSizeFor(Mode::Mix, 48000.0) == 1024);
    CH_CHECK(fftSizeFor(Mode::Master, 48000.0) == 4096);
    CH_CHECK(fftSizeFor(Mode::Mix, 44100.0) == 1024);
    CH_CHECK(fftSizeFor(Mode::Mix, 96000.0) == 2048);
    CH_CHECK(fftSizeFor(Mode::Live, 96000.0) == 512);
    CH_CHECK(fftSizeFor(Mode::Master, 96000.0) == 8192);
    for (double sr : kRates)
        for (int m = 0; m < (int) Mode::Count; ++m) CH_CHECK(isPowerOfTwo(fftSizeFor((Mode) m, sr)));
}

CH_TEST(amount_zero_equals_delayed_input)
{
    for (double sr : kRates)
        for (int m = 0; m < (int) Mode::Count; ++m)
            for (int ms = 0; ms < 2; ++ms) {
                Engine e;
                e.prepare(sr, (Mode) m);
                auto p = amountZero();
                p.midSide = ms == 1;
                e.setParams(p);
                const int lat = e.latencySamples();
                const int total = lat * 6 + 777;
                auto xl = randomSignal(total, 5u + (unsigned) m), xr = randomSignal(total, 9u + (unsigned) m);
                auto yl = xl, yr = xr;
                run(e, yl, yr);
                double maxErr = 0.0;
                for (int i = 0; i < total; ++i) {
                    const double rl = i >= lat ? xl[(size_t) (i - lat)] : 0.0, rr = i >= lat ? xr[(size_t) (i - lat)] : 0.0;
                    maxErr = std::max({ maxErr, std::abs((double) yl[(size_t) i] - rl), std::abs((double) yr[(size_t) i] - rr) });
                }
                CH_INFO("sr %.0f mode %d ms %d: lat %d, max null error %.2g\n", sr, m, ms, lat, maxErr);
                CH_CHECK(maxErr < 1e-4);
            }
}

CH_TEST(latency_matches_impulse_all_modes_and_rates)
{
    for (double sr : kRates)
        for (int m = 0; m < (int) Mode::Count; ++m) {
            Engine e;
            e.prepare(sr, (Mode) m);
            e.setParams(amountZero());
            const int lat = e.latencySamples();
            CH_CHECK(lat == fftSizeFor((Mode) m, sr));
            std::vector<float> a((size_t) lat * 3, 0.f), b = a;
            a[10] = 1.f; b[10] = -0.5f;
            run(e, a, b);
            int peak = -1;
            float pv = 0.f;
            for (size_t i = 0; i < a.size(); ++i) if (std::abs(a[i]) > std::abs(pv)) { pv = a[i]; peak = (int) i; }
            CH_CHECK(peak == 10 + lat);
            CH_CHECK_NEAR(pv, 1.0, 1e-4);
            CH_CHECK_NEAR(b[(size_t) (10 + lat)], -0.5, 1e-4);
        }
}

CH_TEST(mode_switch_is_clean)
{
    Engine e;
    e.prepare(44100.0, Mode::Master);
    e.setParams(amountZero());
    auto x = randomSignal(12000, 3u);
    auto a = x, b = x;
    run(e, a, b);
    e.setMode(Mode::Mix);
    CH_CHECK(e.latencySamples() == 1024);
    a = x; b = x;
    run(e, a, b);
    for (int i = 1024; i < 12000; ++i) CH_CHECK_NEAR(a[(size_t) i], x[(size_t) (i - 1024)], 1e-4);
    e.setMode(Mode::Live);
    CH_CHECK(e.latencySamples() == fftSizeFor(Mode::Live, 44100.0));
}

// ---- resonance suppression ---------------------------------------------------------------------
struct Resonance { double hz; float amp; };

static std::vector<float> noiseWithResonances(int total, double sr, const std::vector<Resonance>& rs, float rms = 0.05f)
{
    auto x = pinkNoise(total, rms, 42);
    for (auto& r : rs) addSine(x, sr, r.hz, r.amp);
    return x;
}

// Returns (output, delayed-input) band energies for the band [f1, f2] over the last part of the signal.
static double attenuationDb(const std::vector<float>& in, const std::vector<float>& out, int lat, double sr, double f1, double f2)
{
    const int len = 8192, segs = 6, start = (int) in.size() - len * segs - 100;
    return bandDbAvg(out, start, len, segs, sr, f1, f2) - bandDbAvg(in, start - lat, len, segs, sr, f1, f2);
}

CH_TEST(resonance_is_attenuated_rest_is_not)
{
    const double sr = 48000.0;
    const int total = 48000 * 4;
    auto x = noiseWithResonances(total, sr, { { 2000.0, 0.02f } });
    for (int m = 1; m <= 2; ++m) {
        Engine e;
        e.prepare(sr, (Mode) m);
        SuppressorParams p;
        p.amount = 1.f; p.thresholdDb = 6.f; p.maxCutDb = 30.f; p.budgetDb = 12.f; p.focusOct = 1.f / 6.f;
        e.setParams(p);
        auto l = x, r = x;
        run(e, l, r);
        const int lat = e.latencySamples();
        const double atRes = attenuationDb(x, l, lat, sr, 1950.0, 2050.0);
        const double below = attenuationDb(x, l, lat, sr, 300.0, 1400.0);
        const double above = attenuationDb(x, l, lat, sr, 3000.0, 9000.0);
        const double low = attenuationDb(x, l, lat, sr, 60.0, 250.0);
        CH_INFO("mode %d: resonance %.2f dB, 300-1400 %.2f dB, 3k-9k %.2f dB, 60-250 %.2f dB\n", m, atRes, below, above, low);
        CH_CHECK(atRes < (m == 1 ? -4.5 : -8.0));
        CH_CHECK(atRes > -30.0);
        CH_CHECK(std::abs(below) < 1.0);
        CH_CHECK(std::abs(above) < 1.0);
        CH_CHECK(std::abs(low) < 1.0);
    }
}

CH_TEST(attenuation_scales_with_amount)
{
    const double sr = 48000.0;
    auto x = noiseWithResonances(48000 * 4, sr, { { 2000.0, 0.02f } });
    double att[2] = {};
    int i = 0;
    for (float amount : { 1.f, 0.5f }) {
        Engine e;
        e.prepare(sr, Mode::Mix);
        SuppressorParams p;
        p.amount = amount; p.maxCutDb = 30.f; p.budgetDb = 12.f;
        e.setParams(p);
        auto l = x, r = x;
        run(e, l, r);
        att[i++] = attenuationDb(x, l, e.latencySamples(), sr, 1950.0, 2050.0);
    }
    CH_INFO("amount 100%%: %.2f dB, 50%%: %.2f dB\n", att[0], att[1]);
    CH_CHECK(att[1] > att[0]);                           // less cut at lower amount
    CH_CHECK_NEAR(att[1], att[0] * 0.5, 2.5);            // roughly proportional (in dB)
}

CH_TEST(max_cut_is_respected)
{
    const double sr = 48000.0;
    auto x = noiseWithResonances(48000 * 4, sr, { { 2000.0, 0.05f }, { 5000.0, 0.05f } });
    for (float mc : { 0.f, 3.f, 6.f }) {
        Engine e;
        e.prepare(sr, Mode::Mix);
        SuppressorParams p;
        p.amount = 1.f; p.thresholdDb = 0.f; p.maxCutDb = mc; p.budgetDb = 12.f; p.focusOct = 1.f / 3.f;
        e.setParams(p);
        auto l = x, r = x;
        run(e, l, r);
        const double a1 = attenuationDb(x, l, e.latencySamples(), sr, 1950.0, 2050.0);
        const double a2 = attenuationDb(x, l, e.latencySamples(), sr, 4950.0, 5050.0);
        CH_INFO("maxCut %.0f: %.2f dB / %.2f dB\n", mc, a1, a2);
        CH_CHECK(a1 >= -mc - 0.25);
        CH_CHECK(a2 >= -mc - 0.25);
        if (mc == 0.f) CH_CHECK(std::abs(a1) < 0.05);
        else CH_CHECK(a1 < -mc * 0.6);                    // the cut does reach (most of) the ceiling
    }
}

CH_TEST(budget_limits_mean_reduction)
{
    const double sr = 48000.0;
    std::vector<Resonance> rs;
    for (double f : { 400.0, 700.0, 1100.0, 1700.0, 2600.0, 3500.0, 4800.0, 6500.0, 8500.0, 11000.0 }) rs.push_back({ f, 0.03f });
    auto x = noiseWithResonances(48000 * 3, sr, rs);
    float avg[2] = {};
    int i = 0;
    for (float budget : { 12.f, 1.0f }) {
        Engine e;
        e.prepare(sr, Mode::Mix);
        SuppressorParams p;
        p.amount = 1.f; p.thresholdDb = 3.f; p.maxCutDb = 30.f; p.budgetDb = budget; p.focusOct = 1.f / 3.f; p.attackMs = 1.f;
        e.setParams(p);
        auto l = x, r = x;
        run(e, l, r);
        UiFrame f;
        e.readUi(f);
        avg[i++] = f.avgReduction;
        CH_INFO("budget %.1f dB: mean reduction %.3f dB\n", budget, f.avgReduction);
        CH_CHECK(f.avgReduction <= budget + 0.02f);
    }
    CH_CHECK(avg[1] < avg[0]);          // the small budget really limits
    CH_CHECK(avg[1] > 0.5f);            // ...but still cuts
}

CH_TEST(harmonic_protect_spares_harmonics)
{
    const double sr = 48000.0;
    const int total = 48000 * 4;
    auto x = pinkNoise(total, 0.01f, 5);
    for (int h = 1; h <= 30; ++h) addSine(x, sr, 220.0 * h, 0.03f / std::sqrt((float) h) * (h == 10 ? 2.5f : 1.f));
    double att[2] = {};
    float f0 = 0.f, conf = 0.f;
    int i = 0;
    for (float protect : { 0.f, 1.f }) {
        Engine e;
        e.prepare(sr, Mode::Master);
        SuppressorParams p;
        p.amount = 1.f; p.thresholdDb = 4.f; p.maxCutDb = 20.f; p.budgetDb = 12.f; p.focusOct = 1.f / 3.f; p.harmonicProtect = protect;
        e.setParams(p);
        auto l = x, r = x;
        run(e, l, r);
        att[i++] = attenuationDb(x, l, e.latencySamples(), sr, 2150.0, 2250.0);
        UiFrame f;
        e.readUi(f);
        if (protect > 0.f) { f0 = f.f0; conf = f.f0Conf; }
    }
    CH_INFO("harmonic 10 attenuation: protect 0 -> %.2f dB, protect 100%% -> %.2f dB; f0 %.1f conf %.2f\n", att[0], att[1], f0, conf);
    CH_CHECK(att[0] < -3.0);
    CH_CHECK(att[1] > att[0] + 2.5);
    CH_CHECK(std::abs(f0 - 220.f) < 6.f);
    CH_CHECK(conf > 0.5f);
}

CH_TEST(transient_pass_backs_off_on_attacks)
{
    const double sr = 48000.0;
    const int total = 48000 * 3;
    auto x = pinkNoise(total, 0.03f, 8);
    // at 2 s a loud broadband burst starts together with a resonance
    const int on = 48000 * 2;
    auto burst = whiteNoise(2400, 0.6f, 3);
    for (size_t i = 0; i < burst.size(); ++i) x[(size_t) on + i] += burst[i] * (1.f - (float) i / (float) burst.size());
    addSine(x, sr, 3000.0, 0.15f, 0.0, on, total);
    double att[2] = {};
    int i = 0;
    for (float tp : { 0.f, 1.f }) {
        Engine e;
        e.prepare(sr, Mode::Mix);
        SuppressorParams p;
        p.amount = 1.f; p.thresholdDb = 4.f; p.maxCutDb = 30.f; p.budgetDb = 12.f; p.transientPass = tp; p.attackMs = 1.f;
        e.setParams(p);
        auto l = x, r = x;
        run(e, l, r);
        const int lat = e.latencySamples();
        // window: first 20 ms after the onset (output is delayed by lat)
        const int len = 1024;
        att[i++] = bandDb(l, on + lat, len, sr, 2900.0, 3100.0) - bandDb(x, on, len, sr, 2900.0, 3100.0);
    }
    CH_INFO("attack-window cut: transient 0 -> %.2f dB, 100%% -> %.2f dB\n", att[0], att[1]);
    CH_CHECK(att[1] > att[0] + 1.0);
}

CH_TEST(sidechain_key_drives_detection)
{
    const double sr = 48000.0;
    const int total = 48000 * 3;
    auto main = pinkNoise(total, 0.05f, 21);
    auto key = pinkNoise(total, 0.05f, 22);
    addSine(key, sr, 3000.0, 0.03f);
    double att[2] = {};
    int i = 0;
    for (bool useKey : { false, true }) {
        Engine e;
        e.prepare(sr, Mode::Mix);
        SuppressorParams p;
        p.amount = 1.f; p.thresholdDb = 4.f; p.maxCutDb = 12.f; p.budgetDb = 12.f; p.scKey = useKey;
        e.setParams(p);
        auto l = main, r = main;
        run(e, l, r, &key, &key);
        att[i++] = attenuationDb(main, l, e.latencySamples(), sr, 2900.0, 3100.0);
        UiFrame f;
        e.readUi(f);
        CH_CHECK(f.keyActive == useKey);
    }
    CH_INFO("band at 3 kHz: key off %.2f dB, key on %.2f dB\n", att[0], att[1]);
    CH_CHECK(std::abs(att[0]) < 1.5);
    CH_CHECK(att[1] < att[0] - 3.0);
}

CH_TEST(link_controls_channel_coupling)
{
    const double sr = 48000.0;
    const int total = 48000 * 3;
    auto xl = pinkNoise(total, 0.05f, 31), xr = pinkNoise(total, 0.05f, 32);
    addSine(xl, sr, 2500.0, 0.03f);   // resonance in the left channel only
    double rightCut[2] = {}, leftCut[2] = {};
    int i = 0;
    for (float link : { 1.f, 0.f }) {
        Engine e;
        e.prepare(sr, Mode::Mix);
        SuppressorParams p;
        p.amount = 1.f; p.thresholdDb = 4.f; p.maxCutDb = 12.f; p.budgetDb = 12.f; p.link = link;
        e.setParams(p);
        auto l = xl, r = xr;
        run(e, l, r);
        leftCut[i] = attenuationDb(xl, l, e.latencySamples(), sr, 2450.0, 2550.0);
        rightCut[i] = attenuationDb(xr, r, e.latencySamples(), sr, 2450.0, 2550.0);
        ++i;
    }
    CH_INFO("left cut %.2f / %.2f dB, right cut %.2f / %.2f dB (link 100%% / 0%%)\n", leftCut[0], leftCut[1], rightCut[0], rightCut[1]);
    CH_CHECK(leftCut[0] < -3.0 && leftCut[1] < -3.0);
    CH_CHECK(rightCut[0] < rightCut[1] - 1.5);                // linked: right follows the left's detection
    CH_CHECK(std::abs(rightCut[1]) < 1.0);       // unlinked: right is left alone
}

// ---- robustness --------------------------------------------------------------------------------
static bool allNormal(const std::vector<float>& v)
{
    for (float f : v) {
        if (!std::isfinite(f)) return false;
        if (f != 0.f && std::fpclassify(f) == FP_SUBNORMAL) return false;
        if (std::abs(f) > 50.f) return false;
    }
    return true;
}

CH_TEST(no_nan_or_denormals_under_parameter_fuzzing)
{
    std::mt19937 rng(1234);
    auto u = [&](float a, float b) { return std::uniform_real_distribution<float>(a, b)(rng); };
    for (double sr : { 44100.0, 48000.0, 96000.0 }) {
        Engine e;
        e.prepare(sr, Mode::Mix);
        bool ok = true;
        for (int iter = 0; iter < 60; ++iter) {
            SuppressorParams p;
            p.thresholdDb = u(0.f, 24.f); p.amount = u(0.f, 1.f); p.focusOct = std::exp2(u(-4.6f, 0.f)); p.maxCutDb = u(0.f, 30.f);
            p.rangeLowHz = std::exp(u(std::log(20.f), std::log(20000.f))); p.rangeHighHz = std::exp(u(std::log(20.f), std::log(20000.f)));
            p.attackMs = std::exp(u(0.f, std::log(100.f))); p.releaseMs = std::exp(u(std::log(10.f), std::log(500.f)));
            p.speedTilt = u(-1.f, 1.f); p.transientPass = u(0.f, 1.f); p.harmonicProtect = u(0.f, 1.f); p.budgetDb = u(0.f, 12.f);
            p.link = u(0.f, 1.f); p.midSide = iter % 2 == 0; p.scKey = iter % 3 == 0; p.displayMix = u(0.f, 1.f);
            e.setMode((Mode) (iter % 3));
            e.setParams(p);
            const int n = 6000 + (int) (rng() % 3000);
            std::vector<float> a, b;
            switch (iter % 5) {
                case 0: a = whiteNoise(n, u(0.001f, 1.f), rng()); b = whiteNoise(n, u(0.001f, 1.f), rng()); break;
                case 1: a.assign((size_t) n, 0.f); b = a; break;
                case 2: a.assign((size_t) n, 0.9f); b.assign((size_t) n, -0.9f); break;
                case 3: a.assign((size_t) n, 0.f); b = a; a[100] = 1.f; b[200] = -1.f; break;
                default: a = pinkNoise(n, 0.1f, rng()); b = a; addSine(a, sr, 1000.0 + iter * 50, 0.4f); break;
            }
            auto key = whiteNoise(n, 0.3f, rng());
            run(e, a, b, &key, &key, 64 + (int) (rng() % 300));
            ok = ok && allNormal(a) && allNormal(b);
        }
        // after a long silence the output is exactly zero (no denormal tail)
        std::vector<float> a((size_t) 40000, 0.f), b = a;
        run(e, a, b);
        for (size_t i = (size_t) e.latencySamples() * 2; i < a.size(); ++i) if (a[i] != 0.f) { ok = false; CH_INFO("nonzero %g at %zu (mode %d, sr %.0f)\n", a[i], i, (int) e.mode(), sr); break; }
        CH_CHECK(ok);
    }
}

CH_TEST(triple_buffer_delivers_newest)
{
    TripleBuffer<UiFrame> tb;
    UiFrame f;
    tb.read(f);
    CH_CHECK(f.frameCounter == 0);
    for (int i = 1; i <= 5; ++i) { tb.writeSlot().frameCounter = (unsigned long long) i; tb.publish(); }
    tb.read(f);
    CH_CHECK(f.frameCounter == 5);
    tb.read(f);
    CH_CHECK(f.frameCounter == 5);
}

CH_TEST(factory_presets_are_valid)
{
    using namespace chiller;
    const auto& ps = factoryPresets();
    CH_CHECK(ps.size() >= 6);
    for (size_t i = 0; i < ps.size(); ++i) {
        ParamState s;
        applyPreset(s, (int) i);
        for (int k = 0; k < pid::count; ++k) CH_CHECK(s[k] >= 0.f && s[k] <= 1.f);
        for (const auto& [id, v] : ps[i].values) {
            const auto& d = paramDefs()[(size_t) id];
            CH_CHECK(v >= d.min - 1e-4f && v <= d.max + 1e-4f);
        }
    }
}

// ---- low-end guard: kick + bass + harsh synth ("mix" signal), LEARN-like settings ---------------------------
CH_TEST(learn_leaves_bass_alone_and_is_gentle)
{
    const double sr = 48000.0;
    const auto x = mixSignal((int) (6 * sr), sr);
    for (int m = 0; m < 3; ++m) {
        SuppressorParams cur;
        cur.rangeLowHz = 40.f;   // an older project: LEARN must still lift it
        const auto r = emulateLearn(x, sr, (Mode) m, cur);
        CH_INFO("mode %d: thr %.1f amount %.2f release %.0f maxCut %.0f rangeLow %.0f\n", m, r.threshold, r.amount, r.release, r.maxCut, r.rangeLow);
        CH_CHECK(r.valid);
        CH_CHECK(r.rangeLow >= 120.f);
        CH_CHECK(r.release >= 150.f);
        CH_CHECK_NEAR(r.amount, 0.5, 1e-6);
        CH_CHECK(r.maxCut <= 9.f);
        CH_CHECK(r.threshold >= 4.f && r.threshold <= 16.f);
        // Mix and Master see the harsh resonances well above the kick/bass excess, so the threshold stays moderate
        if (m > 0) CH_CHECK(r.threshold < 10.f);
    }
}

CH_TEST(low_end_does_not_pump_but_resonances_are_still_cut)
{
    const double sr = 48000.0;
    const auto x = mixSignal((int) (10 * sr), sr);
    for (int m = 0; m < 3; ++m) {
        SuppressorParams def;
        const auto p = withLearn(def, emulateLearn(x, sr, (Mode) m, def));
        const auto b = measureBounce(x, sr, (Mode) m, p);
        CH_INFO("mode %d: GR 30-200 Hz std %.2f p2p %.2f dB, level std %.2f dB; resonances %.2f / %.2f / %.2f dB\n", m, b.grStd, b.grP2P,
                b.lvlStd, b.res[0], b.res[1], b.res[2]);
        CH_CHECK(b.grStd < 0.5);
        CH_CHECK(b.grP2P < 1.5);
        CH_CHECK(b.lvlStd < 1.0);
        for (double r : b.res) CH_CHECK(r < (m == 0 ? -0.8 : -3.0));   // Live has 187 Hz bins: a smaller, but real, cut
    }
}

CH_TEST(low_frequency_tone_is_not_a_resonance_in_mix_mode)
{
    // A steady 60 Hz tone 30 dB over the noise floor is a bass note, not a resonance, at 47 Hz per bin.
    const double sr = 48000.0;
    const int total = (int) (4 * sr);
    auto x = pinkNoise(total, 0.02f, 3);
    addSine(x, sr, 60.0, 0.2f);
    for (int m = 0; m < 2; ++m) {   // Live and Mix
        Engine e;
        e.prepare(sr, (Mode) m);
        SuppressorParams p;
        p.amount = 1.f; p.thresholdDb = 3.f; p.maxCutDb = 30.f; p.budgetDb = 12.f; p.rangeLowHz = 20.f;
        e.setParams(p);
        auto l = x, r = x;
        run(e, l, r);
        const double a = attenuationDb(x, l, e.latencySamples(), sr, 55.0, 65.0);
        CH_INFO("mode %d: 60 Hz tone %.2f dB\n", m, a);
        CH_CHECK(a > -0.5);
    }
}

// ---- regression: bass notes / voice fundamentals up to ~250 Hz must not be cut and released like resonances ----
// Found with real recordings (voices, strings, a dense mix) in Master mode, where 150-250 Hz harmonics were cut by 1-3 dB and moved the
// 30-200 Hz level by more than 0.5 dB (std) as the notes changed.
CH_TEST(bass_and_fundamentals_up_to_250hz_do_not_bounce)
{
    const double sr = 48000.0;
    const int n = (int) (10 * sr), note = (int) (0.4 * sr);
    auto x = pinkNoise(n, 0.02f, 21);
    const double f0s[] = { 82.4, 110.0, 146.8, 196.0, 164.8, 130.8, 185.0, 220.0, 123.5, 174.6 };   // a short bassline / melody
    for (int s = 0; s * note < n; ++s) {
        const double f0 = f0s[(size_t) s % 10];
        for (int i = 0; i < note && s * note + i < n; ++i) {
            const double t = (double) i / sr;
            const float env = (float) std::min(1.0, t / 0.01) * (float) std::min(1.0, (note - i) / (0.03 * sr));
            float v = 0.f;
            for (int h = 1; h <= 8; ++h) v += 0.2f / (float) h * (float) std::sin(2.0 * std::numbers::pi * f0 * h * (double) (s * note + i) / sr);
            x[(size_t) (s * note + i)] += env * v;
        }
    }
    for (int m = 1; m < 3; ++m) {
        SuppressorParams def;                         // defaults: the worst case (no LEARN)
        const auto b = measureBounce(x, sr, (Mode) m, def, 1.0);
        CH_INFO("mode %d: 30-200 Hz level change mean %.2f std %.2f dB\n", m, b.lvlMean, b.lvlStd);
        CH_CHECK(b.lvlStd < 0.5);
        CH_CHECK(b.lvlMean > -0.6);
    }
}

// ---- regression: clean tonal sources must not be dulled, a fixed resonance on top of them must still be cut ----
// Found with real recordings (acoustic guitar, strings, trumpet, piano lost 3-8 dB at 4-8 kHz with default settings): the sparse upper harmonics of
// a pitched source stand far above the floor between them and were treated like resonances. A sawtooth-like series (1/n roll-off) over a quiet noise
// floor, with the pitch changing every 0.5 s, stands for them. The optional resonance is a steady sine that does not follow the pitch.
static std::vector<float> harmonicMelody(int n, double sr, float resonanceAmp, double resonanceHz = 3300.0)
{
    auto x = pinkNoise(n, 0.0005f, 31);                       // quiet noise floor (about -66 dB)
    const double f0s[] = { 196.0, 220.0, 246.9, 261.6, 293.7, 329.6, 349.2, 392.0, 261.6, 220.0 };
    const int note = (int) (0.5 * sr);
    for (int s = 0; s * note < n; ++s) {
        const double f0 = f0s[(size_t) s % 10];
        for (int i = 0; i < note && s * note + i < n; ++i) {
            const double t = (double) i / sr;
            const float env = (float) std::min(1.0, t / 0.008) * (float) std::min(1.0, (note - i) / (0.02 * sr)) * (float) std::exp(-0.5 * t);
            float v = 0.f;
            for (int h = 1; f0 * h < 0.45 * sr; ++h)
                v += 0.12f / (float) h * (float) std::sin(2.0 * std::numbers::pi * f0 * h * (double) (s * note + i) / sr + 0.7 * h);
            x[(size_t) (s * note + i)] += env * v;
        }
    }
    if (resonanceAmp > 0.f) addSine(x, sr, resonanceHz, resonanceAmp);
    return x;
}

CH_TEST(harmonic_tone_is_not_dulled_with_default_settings)
{
    const double sr = 48000.0;
    const int n = (int) (8 * sr);
    const auto x = harmonicMelody(n, sr, 0.f);
    for (int m = 1; m < 3; ++m) {
        Engine e;
        e.prepare(sr, (Mode) m);
        SuppressorParams p;                                         // defaults (what Init gives)
        e.setParams(p);
        auto l = x, r = x;
        run(e, l, r);
        const int lat = e.latencySamples();
        double worst = 0.0;
        for (auto [f1, f2] : { std::pair<double, double> { 2000.0, 4000.0 }, { 4000.0, 8000.0 }, { 8000.0, 16000.0 } }) {
            const double d = bandDbAvg(l, (int) (1.0 * sr), 4096, 24, sr, f1, f2) - bandDbAvg(x, (int) (1.0 * sr) - lat, 4096, 24, sr, f1, f2);
            CH_INFO("mode %d: %.0f-%.0f Hz changed by %.2f dB\n", m, f1, f2, d);
            worst = std::min(worst, d);
        }
        CH_CHECK(worst > -1.0);
    }
}

CH_TEST(harmonic_tone_with_fixed_resonance_still_gets_the_resonance_cut)
{
    const double sr = 48000.0;
    const int n = (int) (8 * sr);
    const auto x = harmonicMelody(n, sr, 0.05f);                    // 3.3 kHz, about 14 dB above the neighbouring harmonics (which are 0.12 / n)
    for (int m = 1; m < 3; ++m) {
        Engine e;
        e.prepare(sr, (Mode) m);
        SuppressorParams p;
        e.setParams(p);
        auto l = x, r = x;
        run(e, l, r);
        const int lat = e.latencySamples();
        const double atRes = bandDbAvg(l, (int) (2.0 * sr), 4096, 16, sr, 3250.0, 3350.0) - bandDbAvg(x, (int) (2.0 * sr) - lat, 4096, 16, sr, 3250.0, 3350.0);
        const double above = bandDbAvg(l, (int) (2.0 * sr), 4096, 16, sr, 4000.0, 8000.0) - bandDbAvg(x, (int) (2.0 * sr) - lat, 4096, 16, sr, 4000.0, 8000.0);
        CH_INFO("mode %d: resonance %.2f dB, 4-8 kHz %.2f dB\n", m, atRes, above);
        CH_CHECK(atRes < -3.0);
        CH_CHECK(above > -1.0);
    }
}

CH_TEST(harmonic_protect_zero_restores_the_classic_detector)
{
    // Harmonic Protect 0 switches the whole tonal protection off (for material whose harmonics are the problem): the harmonic tone is cut again.
    const double sr = 48000.0;
    const int n = (int) (8 * sr);
    const auto x = harmonicMelody(n, sr, 0.f);
    Engine e;
    e.prepare(sr, Mode::Mix);
    SuppressorParams p;
    p.harmonicProtect = 0.f;
    e.setParams(p);
    auto l = x, r = x;
    run(e, l, r);
    const int lat = e.latencySamples();
    const double d = bandDbAvg(l, (int) (1.0 * sr), 4096, 24, sr, 4000.0, 8000.0) - bandDbAvg(x, (int) (1.0 * sr) - lat, 4096, 24, sr, 4000.0, 8000.0);
    CH_INFO("protect 0: 4-8 kHz changed by %.2f dB\n", d);
    CH_CHECK(d < -2.0);
}
