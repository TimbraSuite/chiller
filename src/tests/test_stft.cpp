#include "TestMain.h"
#include "dsp/Engine.h"
#include "dsp/Stft.h"
#include <cmath>
#include <random>

using namespace chiller::dsp;

static std::vector<float> randomSignal(int n, unsigned seed, float amp = 0.9f)
{
    std::mt19937 rng(seed);
    std::uniform_real_distribution<float> d(-amp, amp);
    std::vector<float> v((size_t) n);
    for (auto& x : v) x = d(rng);
    return v;
}

CH_TEST(stft_passthrough_reconstructs_delayed_input)
{
    for (int size : { 512, 1024, 4096 }) {
        Stft s(size);
        CH_CHECK(s.latencySamples() == size);
        const int total = size * 8 + 123;
        auto x = randomSignal(total, 7u + (unsigned) size);
        std::vector<float> y((size_t) total);
        // feed in odd-sized blocks to exercise streaming
        for (int pos = 0; pos < total;) {
            const int len = std::min(total - pos, 97);
            s.process(x.data() + pos, y.data() + pos, len);
            pos += len;
        }
        double maxErr = 0.0;
        for (int i = 0; i < total; ++i) {
            const double ref = i >= size ? x[(size_t) (i - size)] : 0.0;
            maxErr = std::max(maxErr, std::abs((double) y[(size_t) i] - ref));
        }
        CH_INFO("fft %d: max error %.3g\n", size, maxErr);
        CH_CHECK(maxErr < 1e-5);
    }
}

CH_TEST(stft_latency_matches_impulse)
{
    for (int size : { 512, 1024, 4096 }) {
        Stft s(size);
        int peak = -1;
        float peakVal = 0.f;
        for (int i = 0; i < size * 3; ++i) {
            const float y = s.processSample(i == 5 ? 1.f : 0.f);
            if (std::abs(y) > std::abs(peakVal)) { peakVal = y; peak = i; }
        }
        CH_CHECK(peak == 5 + s.latencySamples());
        CH_CHECK_NEAR(peakVal, 1.0, 1e-5);
    }
}

CH_TEST(engine_latency_matches_impulse_per_mode)
{
    for (int m = 0; m < (int) Mode::Count; ++m) {
        Engine e;
        e.prepare(48000.0, 2, (Mode) m);
        const int lat = e.latencySamples();
        CH_CHECK(lat == latencyForMode((Mode) m));
        std::vector<float> a((size_t) 6000, 0.f), b((size_t) 6000, 0.f);
        a[10] = 1.f; b[10] = -0.5f;
        float* ch[2] = { a.data(), b.data() };
        e.process(ch, 6000);
        CH_CHECK_NEAR(a[(size_t) (10 + lat)], 1.0, 1e-5);
        CH_CHECK_NEAR(b[(size_t) (10 + lat)], -0.5, 1e-5);
    }
    CH_CHECK(latencyForMode(Mode::Live) == 0);
}

CH_TEST(engine_mode_switch_is_clean)
{
    Engine e;
    e.prepare(44100.0, 2, Mode::Master);
    auto x = randomSignal(5000, 3u);
    std::vector<float> a = x, b = x;
    float* ch[2] = { a.data(), b.data() };
    e.process(ch, 5000);
    e.setMode(Mode::Mix);
    CH_CHECK(e.latencySamples() == 1024);
    a = x; b = x;
    e.process(ch, 5000);
    for (int i = 1024; i < 5000; ++i) CH_CHECK_NEAR(a[(size_t) i], x[(size_t) (i - 1024)], 1e-5);
    e.setMode(Mode::Live);
    a = x; b = x;
    e.process(ch, 5000);
    CH_CHECK(a == x);
}

static bool allNormal(const std::vector<float>& v)
{
    for (float f : v) {
        if (!std::isfinite(f)) return false;
        if (f != 0.f && std::fpclassify(f) == FP_SUBNORMAL) return false;
    }
    return true;
}

CH_TEST(engine_no_nan_or_denormals)
{
    for (int m = 0; m < (int) Mode::Count; ++m) {
        Engine e;
        e.prepare(48000.0, 2, (Mode) m);
        const int n = 20000;
        std::vector<float> noise = randomSignal(n, 11u, 1.0f);
        std::vector<float> silence((size_t) n, 0.f), dc((size_t) n, 0.7f);
        for (auto* src : { &noise, &silence, &dc, &silence }) {
            std::vector<float> a = *src, b = *src;
            float* ch[2] = { a.data(), b.data() };
            e.process(ch, n);
            CH_CHECK(allNormal(a));
            CH_CHECK(allNormal(b));
        }
        // after a long silence the output must be exactly zero (no denormal tail)
        std::vector<float> a((size_t) 9000, 0.f), b = a;
        float* ch[2] = { a.data(), b.data() };
        e.process(ch, 9000);
        for (float f : a) CH_CHECK(f == 0.f);
    }
}

CH_TEST(stft_dc_is_preserved)
{
    Stft s(1024);
    float last = 0.f;
    for (int i = 0; i < 8192; ++i) last = s.processSample(0.5f);
    CH_CHECK_NEAR(last, 0.5, 1e-5);
}

CH_TEST(delay_line_delays_exactly)
{
    DelayLine d;
    d.prepare(2048, 1);
    d.setDelay(100);
    for (int i = 0; i < 500; ++i) {
        const float y = d.process(0, (float) i);
        d.advance();
        CH_CHECK(y == (i >= 100 ? (float) (i - 100) : 0.f));
    }
}
