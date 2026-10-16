// Offline render check (no JUCE, no host): runs generated test signals through the engine and prints
// before / after band energies. Optionally writes before/after WAV files for listening.
//   chiller_render --bench                 CPU time per second of stereo audio, per mode and sample rate
//   chiller_render [--wav <dir>] [--mode mix|master|live] [--amount 0..100] [--threshold dB] [--seconds s]
#include "../../src/tests/Helpers.h"
#include <cstdio>
#include <cstdlib>
#include <chrono>
#include <cstring>
#include <string>

using namespace chtest;
using namespace chiller::dsp;

static void writeWav(const std::string& path, const std::vector<float>& l, const std::vector<float>& r, int sr)
{
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return;
    const unsigned n = (unsigned) l.size(), dataBytes = n * 2 * 4;
    auto w32 = [&](unsigned v) { std::fwrite(&v, 4, 1, f); };
    auto w16 = [&](unsigned short v) { std::fwrite(&v, 2, 1, f); };
    std::fwrite("RIFF", 1, 4, f); w32(36 + dataBytes); std::fwrite("WAVEfmt ", 1, 8, f);
    w32(16); w16(3); w16(2); w32((unsigned) sr); w32((unsigned) sr * 8); w16(8); w16(32);
    std::fwrite("data", 1, 4, f); w32(dataBytes);
    for (unsigned i = 0; i < n; ++i) { std::fwrite(&l[i], 4, 1, f); std::fwrite(&r[i], 4, 1, f); }
    std::fclose(f);
}

struct Test {
    std::string name;
    std::vector<float> x;
    std::vector<std::pair<double, std::string>> marks;   // frequencies to report
};

static void bench()
{
    std::printf("CPU benchmark: processing time per second of stereo audio (single thread, 512-sample blocks)\n");
    std::printf("%-8s %-10s %-28s %10s %8s\n", "rate", "mode", "settings", "ms / s", "CPU %");
    for (double sr : { 48000.0, 96000.0 })
        for (int m = 0; m < 3; ++m)
            for (int heavy = 0; heavy < 2; ++heavy) {
                const int total = (int) sr * 10;
                auto x = pinkNoise(total, 0.05f, 7);
                addSine(x, sr, 2000.0, 0.02f);
                auto key = x;
                Engine e;
                e.prepare(sr, (Mode) m);
                SuppressorParams p;
                p.amount = 0.7f; p.thresholdDb = 4.f;
                if (heavy) { p.harmonicProtect = 1.f; p.transientPass = 0.5f; p.link = 0.5f; p.midSide = true; p.scKey = true; }
                e.setParams(p);
                auto l = x, r = x;
                const auto t0 = std::chrono::steady_clock::now();
                for (int pos = 0; pos < total; pos += 512) {
                    const int n = std::min(512, total - pos);
                    e.process(l.data() + pos, r.data() + pos, heavy ? key.data() + pos : nullptr, heavy ? key.data() + pos : nullptr, n);
                }
                const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
                std::printf("%-8.0f %-10s %-28s %10.2f %7.2f%%\n", sr, m == 0 ? "Live" : (m == 1 ? "Mix" : "Master"),
                            heavy ? "all features, key, M/S, link 50" : "defaults, amount 70", 1000.0 * secs / 10.0, 10.0 * secs);
            }
}

int main(int argc, char** argv)
{
    for (int i = 1; i < argc; ++i) if (!std::strcmp(argv[i], "--bench")) { bench(); return 0; }
    std::string wavDir, modeName = "mix";
    float amount = 100.f, threshold = 4.f, seconds = 6.f;
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--wav") && i + 1 < argc) wavDir = argv[++i];
        else if (!std::strcmp(argv[i], "--mode") && i + 1 < argc) modeName = argv[++i];
        else if (!std::strcmp(argv[i], "--amount") && i + 1 < argc) amount = (float) std::atof(argv[++i]);
        else if (!std::strcmp(argv[i], "--threshold") && i + 1 < argc) threshold = (float) std::atof(argv[++i]);
        else if (!std::strcmp(argv[i], "--seconds") && i + 1 < argc) seconds = (float) std::atof(argv[++i]);
    }
    const double sr = 48000.0;
    const int total = (int) (seconds * sr);
    const Mode mode = modeName == "live" ? Mode::Live : (modeName == "master" ? Mode::Master : Mode::Mix);

    std::vector<Test> tests;
    {   // pink noise with four fixed resonances
        Test t { "pink noise + fixed resonances", pinkNoise(total, 0.05f, 1), {} };
        for (auto [hz, amp] : { std::pair<double, float> { 300.0, 0.012f }, { 1200.0, 0.015f }, { 3100.0, 0.02f }, { 6000.0, 0.01f } }) {
            addSine(t.x, sr, hz, amp);
            t.marks.push_back({ hz, std::to_string((int) hz) + " Hz resonance" });
        }
        tests.push_back(std::move(t));
    }
    {   // swept resonance
        Test t { "pink noise + swept resonance (500 Hz -> 8 kHz)", pinkNoise(total, 0.05f, 2), {} };
        double ph = 0.0;
        for (int i = 0; i < total; ++i) {
            const double u = (double) i / total, f = 500.0 * std::pow(16.0, u);
            ph += 2.0 * std::numbers::pi * f / sr;
            t.x[(size_t) i] += 0.02f * (float) std::sin(ph);
        }
        tests.push_back(std::move(t));
    }
    {   // harmonic tone with one over-emphasised harmonic
        Test t { "harmonic tone 220 Hz (+ boosted 10th harmonic) in noise", pinkNoise(total, 0.01f, 3), {} };
        for (int h = 1; h <= 30; ++h) addSine(t.x, sr, 220.0 * h, 0.03f / std::sqrt((float) h) * (h == 10 ? 2.5f : 1.f));
        t.marks.push_back({ 2200.0, "10th harmonic (boosted)" });
        t.marks.push_back({ 880.0, "4th harmonic" });
        tests.push_back(std::move(t));
    }

    struct Setting { const char* name; float amount, threshold, protect; };
    const Setting settings[] = { { "default (threshold 6 dB, amount 50 %)", 50.f, 6.f, 0.f },
                                 { "custom", amount, threshold, 0.f }, { "custom + Harmonic Protect 100 %", amount, threshold, 100.f } };
    std::printf("Mode: %s, sample rate %.0f Hz, %.1f s per signal\n", modeName.c_str(), sr, seconds);
    for (const auto& t : tests) {
        std::printf("\n=== %s ===\n", t.name.c_str());
        const int len = 8192, segs = std::max(1, (total - 8192 * 2) / len - 1), start = total - len * segs - 100;
        for (const auto& s : settings) {
            if (s.protect > 0.f && t.name.find("harmonic") == std::string::npos) continue;
            Engine e;
            e.prepare(sr, mode);
            SuppressorParams p;
            p.amount = s.amount / 100.f; p.thresholdDb = s.threshold; p.harmonicProtect = s.protect / 100.f;
            p.maxCutDb = 12.f; p.budgetDb = 6.f;
            e.setParams(p);
            auto l = t.x, r = t.x;
            run(e, l, r);
            const int lat = e.latencySamples();
            std::printf("-- %s, latency %d samples (%.1f ms)\n", s.name, lat, 1000.0 * lat / sr);
            auto row = [&](const char* label, double f1, double f2) {
                const double b = bandDbAvg(t.x, start - lat, len, segs, sr, f1, f2), a = bandDbAvg(l, start, len, segs, sr, f1, f2);
                std::printf("   %-28s before %7.2f dB   after %7.2f dB   change %+6.2f dB\n", label, b, a, a - b);
            };
            for (const auto& m : t.marks) row(m.second.c_str(), m.first * 0.97, m.first * 1.03);
            const double edges[] = { 40, 80, 160, 320, 640, 1280, 2560, 5120, 10240, 16000 };
            for (int b = 0; b + 1 < 10; ++b) {
                char buf[64];
                std::snprintf(buf, sizeof buf, "octave %5.0f-%5.0f Hz", edges[b], edges[b + 1]);
                row(buf, edges[b], edges[b + 1]);
            }
            row("broadband 40-16000 Hz", 40, 16000);
            UiFrame f;
            e.readUi(f);
            std::printf("   avg reduction %.2f dB, harshness %.0f -> %.0f, f0 %.1f Hz (conf %.2f)\n", f.avgReduction, f.harshIn, f.harshOut, f.f0, f.f0Conf);
            if (!wavDir.empty() && s.protect == 0.f && &s == &settings[1]) {
                std::string base = wavDir + "/" + std::to_string(&t - tests.data());
                writeWav(base + "_before.wav", t.x, t.x, (int) sr);
                writeWav(base + "_after.wav", l, r, (int) sr);
            }
        }
    }
    return 0;
}
