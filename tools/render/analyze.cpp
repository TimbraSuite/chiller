// chiller_analyze: run a real recording through the Chiller engine and measure what happened.
//
//   chiller_analyze --in file.wav [--start s] [--len s] --preset "<factory preset name>|LEARN|Init"
//                   [--mode live|mix|master] [--learn-at s] [--out processed.wav] [--delta delta.wav] [--orig original.wav]
//                   [--delta-gain dB]
//
// Mix is 100 %, no trim / output gain (what the plugin does at default output settings). "LEARN" emulates pressing LEARN on
// the 3 s of the input starting at --learn-at (default: the start of the excerpt; times are relative to the file) and then
// processing the excerpt with the learned settings on top of the default parameters. A factory preset name processes with that
// preset. The output is latency-compensated (aligned with the input). WAV input: 16/24/32-bit PCM or 32-bit float.
//
// Prints one "RESULT key=value ..." line (parsed by the report scripts) and, with --verbose, a readable summary.
#include "../../src/engine/Presets.h"
#include "../../src/tests/Helpers.h"
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>

using namespace chiller;
using namespace chiller::dsp;

namespace {

struct Audio { std::vector<float> l, r; int sr = 48000; };

bool readWav(const std::string& path, Audio& a)
{
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    std::vector<unsigned char> d;
    unsigned char buf[65536];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) d.insert(d.end(), buf, buf + n);
    std::fclose(f);
    auto u32 = [&](size_t o) { return (unsigned) d[o] | (unsigned) d[o + 1] << 8 | (unsigned) d[o + 2] << 16 | (unsigned) d[o + 3] << 24; };
    auto u16 = [&](size_t o) { return (unsigned) d[o] | (unsigned) d[o + 1] << 8; };
    if (d.size() < 44 || std::memcmp(d.data(), "RIFF", 4) || std::memcmp(d.data() + 8, "WAVE", 4)) return false;
    int fmt = 0, ch = 0, bits = 0;
    size_t pos = 12, dataOff = 0, dataLen = 0;
    while (pos + 8 <= d.size()) {
        const size_t len = u32(pos + 4);
        if (!std::memcmp(d.data() + pos, "fmt ", 4)) {
            fmt = (int) u16(pos + 8); ch = (int) u16(pos + 10); a.sr = (int) u32(pos + 12); bits = (int) u16(pos + 22);
            if (fmt == 0xFFFE && len >= 26) fmt = (int) u16(pos + 32);
        } else if (!std::memcmp(d.data() + pos, "data", 4)) {
            dataOff = pos + 8; dataLen = std::min(len, d.size() - dataOff); break;
        }
        pos += 8 + len + (len & 1);
    }
    if (!dataOff || ch < 1 || ch > 2) return false;
    const int bps = bits / 8;
    const size_t frames = dataLen / (size_t) (bps * ch);
    a.l.resize(frames); a.r.resize(frames);
    for (size_t i = 0; i < frames; ++i) {
        float v[2] = { 0, 0 };
        for (int c = 0; c < ch; ++c) {
            const unsigned char* p = d.data() + dataOff + (i * (size_t) ch + (size_t) c) * (size_t) bps;
            if (fmt == 3 && bits == 32) { float x; std::memcpy(&x, p, 4); v[c] = x; }
            else if (bits == 16) v[c] = (float) (short) (p[0] | p[1] << 8) / 32768.f;
            else if (bits == 24) v[c] = (float) ((int) (p[0] << 8 | p[1] << 16 | p[2] << 24) >> 8) / 8388608.f;
            else if (bits == 32) v[c] = (float) ((int) (p[0] | p[1] << 8 | p[2] << 16 | (unsigned) p[3] << 24)) / 2147483648.f;
        }
        a.l[i] = v[0]; a.r[i] = ch == 2 ? v[1] : v[0];
    }
    return true;
}

void writeWav16(const std::string& path, const std::vector<float>& l, const std::vector<float>& r, int sr, float gainDb = 0.f)
{
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return;
    const float g = std::pow(10.f, gainDb / 20.f);
    const unsigned n = (unsigned) l.size(), bytes = n * 4;
    auto w32 = [&](unsigned v) { std::fwrite(&v, 4, 1, f); };
    auto w16 = [&](unsigned short v) { std::fwrite(&v, 2, 1, f); };
    std::fwrite("RIFF", 1, 4, f); w32(36 + bytes); std::fwrite("WAVEfmt ", 1, 8, f);
    w32(16); w16(1); w16(2); w32((unsigned) sr); w32((unsigned) sr * 4); w16(4); w16(16);
    std::fwrite("data", 1, 4, f); w32(bytes);
    auto q = [&](float x) { return (short) std::lrint(std::clamp(x * g, -1.f, 1.f) * 32767.f); };
    for (unsigned i = 0; i < n; ++i) { w16((unsigned short) q(l[i])); w16((unsigned short) q(r[i])); }
    std::fclose(f);
}

SuppressorParams toParams(const ParamState& s)
{
    SuppressorParams p;
    p.thresholdDb = s.real(pid::threshold);
    p.amount = s.real(pid::amount) / 100.f;
    p.focusOct = s.real(pid::focus);
    p.maxCutDb = s.real(pid::maxCut);
    p.rangeLowHz = s.real(pid::rangeLow);
    p.rangeHighHz = s.real(pid::rangeHigh);
    p.attackMs = s.real(pid::attack);
    p.releaseMs = s.real(pid::release);
    p.speedTilt = s.real(pid::speedTilt);
    p.transientPass = s.real(pid::transientPass) / 100.f;
    p.harmonicProtect = s.real(pid::harmonicProtect) / 100.f;
    p.budgetDb = s.real(pid::budget);
    p.link = s.real(pid::link) / 100.f;
    p.midSide = s.real(pid::stereoMode) >= 0.5f;
    p.scKey = false;
    return p;
}

// ---- measurement ------------------------------------------------------------------------------------------------
constexpr int kW = 2048;   // ~43 ms analysis window, hop 2400 samples (50 ms)
constexpr int kBands = 9;
const double kEdges[kBands + 1] = { 31.25, 62.5, 125, 250, 500, 1000, 2000, 4000, 8000, 16000 };

struct WinStats {
    std::vector<std::array<double, kBands>> band;   // dB per window
    std::vector<double> low;                        // 30-200 Hz dB
    std::vector<double> total;                      // broadband dB
};

WinStats windowed(const std::vector<float>& x, double sr)
{
    WinStats w;
    Fft fft(kW);
    std::vector<float> re(kW), im(kW);
    std::vector<float> win(kW);
    for (int i = 0; i < kW; ++i) win[(size_t) i] = (float) (0.5 - 0.5 * std::cos(2.0 * std::numbers::pi * i / kW));
    const int hop = (int) std::lround(0.05 * sr);
    for (int s = 0; s + kW <= (int) x.size(); s += hop) {
        for (int i = 0; i < kW; ++i) { re[(size_t) i] = x[(size_t) (s + i)] * win[(size_t) i]; im[(size_t) i] = 0.f; }
        fft.forward(re.data(), im.data());
        std::array<double, kBands> e {};
        double lowE = 0, tot = 0;
        for (int k = 1; k < kW / 2; ++k) {
            const double f = k * sr / kW, p = (double) re[(size_t) k] * re[(size_t) k] + (double) im[(size_t) k] * im[(size_t) k];
            for (int b = 0; b < kBands; ++b) if (f >= kEdges[b] && f < kEdges[b + 1]) e[(size_t) b] += p;
            if (f >= 30 && f <= 200) lowE += p;
            if (f >= 30) tot += p;
        }
        for (auto& v : e) v = 10 * std::log10(v + 1e-20);
        w.band.push_back(e);
        w.low.push_back(10 * std::log10(lowE + 1e-20));
        w.total.push_back(10 * std::log10(tot + 1e-20));
    }
    return w;
}

void meanStd(const std::vector<double>& v, double& m, double& s)
{
    m = s = 0;
    if (v.empty()) return;
    for (double q : v) m += q;
    m /= (double) v.size();
    for (double q : v) s += (q - m) * (q - m);
    s = std::sqrt(s / (double) v.size());
}

// Independent harshness measure: persistent narrow peaks. Power spectra are averaged over ~0.5 s (12 x 4096-sample windows) so random
// per-bin fluctuations of noise average out; the score is the mean dB by which bins in 1.5-10 kHz stick out (> 3 dB) of the local
// median (+-1/4 octave), averaged over the groups that are not silent.
double harshness(const std::vector<float>& x, double sr, const std::vector<char>& use)
{
    const int W = 4096, G = 12;
    Fft fft(W);
    std::vector<float> re(W), im(W), win(W), db((size_t) W / 2), tmp;
    std::vector<double> pw((size_t) W / 2);
    for (int i = 0; i < W; ++i) win[(size_t) i] = (float) (0.5 - 0.5 * std::cos(2.0 * std::numbers::pi * i / W));
    const int k0 = (int) (1500.0 * W / sr), k1 = (int) (10000.0 * W / sr);
    const int hop50 = (int) std::lround(0.05 * sr);
    double acc = 0; int cnt = 0, inGroup = 0;
    std::fill(pw.begin(), pw.end(), 0.0);
    for (int s = 0; s + W <= (int) x.size(); s += W) {
        const size_t u = std::min((size_t) (s / hop50), use.size() - 1);
        if (use[u]) {
            for (int i = 0; i < W; ++i) { re[(size_t) i] = x[(size_t) (s + i)] * win[(size_t) i]; im[(size_t) i] = 0.f; }
            fft.forward(re.data(), im.data());
            for (int k = 0; k < W / 2; ++k) pw[(size_t) k] += (double) re[(size_t) k] * re[(size_t) k] + (double) im[(size_t) k] * im[(size_t) k];
            ++inGroup;
        }
        if (inGroup == G) {
            for (int k = 0; k < W / 2; ++k) db[(size_t) k] = (float) (10.0 * std::log10(pw[(size_t) k] / G + 1e-20));
            double sum = 0;
            for (int k = k0; k < k1; ++k) {
                const int a = (int) (k / 1.189), b = (int) (k * 1.189) + 1;
                tmp.assign(db.begin() + a, db.begin() + b);
                std::nth_element(tmp.begin(), tmp.begin() + (long) tmp.size() / 2, tmp.end());
                sum += std::max(0.f, db[(size_t) k] - tmp[tmp.size() / 2] - 3.f);
            }
            acc += sum / (k1 - k0); ++cnt; inGroup = 0;
            std::fill(pw.begin(), pw.end(), 0.0);
        }
    }
    return cnt ? acc / cnt : 0.0;
}

double centroid(const std::vector<float>& x, double sr)
{
    const int W = 4096;
    Fft fft(W);
    std::vector<float> re(W), im(W);
    std::vector<double> ps((size_t) W / 2, 0.0);
    for (int s = 0; s + W <= (int) x.size(); s += W) {
        for (int i = 0; i < W; ++i) { re[(size_t) i] = x[(size_t) (s + i)] * (float) (0.5 - 0.5 * std::cos(2.0 * std::numbers::pi * i / W)); im[(size_t) i] = 0.f; }
        fft.forward(re.data(), im.data());
        for (int k = 0; k < W / 2; ++k) ps[(size_t) k] += (double) re[(size_t) k] * re[(size_t) k] + (double) im[(size_t) k] * im[(size_t) k];
    }
    double num = 0, den = 0;
    for (int k = 1; k < W / 2; ++k) { const double f = k * sr / W; num += f * ps[(size_t) k]; den += ps[(size_t) k]; }
    return den > 0 ? num / den : 0;
}

double bandLongTerm(const std::vector<float>& x, double sr, double f1, double f2)
{
    const int W = 4096;
    const int segs = (int) x.size() / W;
    if (segs < 1) return -200;
    return chtest::bandDbAvg(x, 0, W, segs, sr, f1, f2);
}

// Onsets (fast rise of >1 kHz energy). Returns frame indices in 1 ms frames.
std::vector<float> hfEnergy1ms(const std::vector<float>& x, double sr)
{
    const int fr = (int) (sr / 1000.0);
    float lp1 = 0, lp2 = 0;
    const float a = 1.f - std::exp(-2.f * (float) std::numbers::pi * 1000.f / (float) sr);
    std::vector<float> e(x.size() / (size_t) fr, 0.f);
    for (size_t i = 0; i < e.size() * (size_t) fr; ++i) {
        lp1 += (x[i] - lp1) * a; lp2 += (lp1 - lp2) * a;
        const float hp = x[i] - lp2;
        e[i / (size_t) fr] += hp * hp;
    }
    return e;
}

} // namespace

int main(int argc, char** argv)
{
    std::string in, presetName = "Init", modeName = "mix", outPath, deltaPath, origPath;
    double start = 0, len = 0, learnAt = -1, deltaGain = 0;
    bool verbose = false, thirds = false;
    std::vector<std::pair<std::string, float>> sets;   // --set param_id=value (real units), applied after the preset
    for (int i = 1; i < argc; ++i) {
        auto arg = [&](const char* n) { return !std::strcmp(argv[i], n) && i + 1 < argc; };
        if (arg("--in")) in = argv[++i];
        else if (arg("--start")) start = std::atof(argv[++i]);
        else if (arg("--len")) len = std::atof(argv[++i]);
        else if (arg("--preset")) presetName = argv[++i];
        else if (arg("--mode")) modeName = argv[++i];
        else if (arg("--learn-at")) learnAt = std::atof(argv[++i]);
        else if (arg("--out")) outPath = argv[++i];
        else if (arg("--delta")) deltaPath = argv[++i];
        else if (arg("--orig")) origPath = argv[++i];
        else if (arg("--delta-gain")) deltaGain = std::atof(argv[++i]);
        else if (!std::strcmp(argv[i], "--verbose")) verbose = true;
        else if (!std::strcmp(argv[i], "--thirds")) thirds = true;
        else if (arg("--set")) { std::string kv = argv[++i]; const auto eq = kv.find('='); sets.push_back({ kv.substr(0, eq), (float) std::atof(kv.c_str() + eq + 1) }); }
    }
    Audio a;
    if (in.empty() || !readWav(in, a)) { std::fprintf(stderr, "cannot read --in WAV\n"); return 1; }
    const double sr = a.sr;
    const Mode mode = modeName == "live" ? Mode::Live : (modeName == "master" ? Mode::Master : Mode::Mix);
    const int s0 = std::clamp((int) (start * sr), 0, (int) a.l.size());
    const int n = len > 0 ? std::min((int) (len * sr), (int) a.l.size() - s0) : (int) a.l.size() - s0;
    std::vector<float> xl(a.l.begin() + s0, a.l.begin() + s0 + n), xr(a.r.begin() + s0, a.r.begin() + s0 + n);

    // parameters
    SuppressorParams p;
    std::string learnInfo;
    if (presetName == "LEARN") {
        const double la = learnAt >= 0 ? learnAt : start;
        const int ls = std::clamp((int) (la * sr), 0, (int) a.l.size());
        const int ln = std::min((int) (3.0 * sr), (int) a.l.size() - ls);
        Engine e;
        e.prepare(sr, mode);
        ParamState st0;
        for (auto& [k, v] : sets) { const int id = findParam(k); if (id >= 0) st0.setReal(id, v); }
        const SuppressorParams def = toParams(st0);
        e.setParams(def);
        auto l = std::vector<float>(a.l.begin() + ls, a.l.begin() + ls + ln), r = std::vector<float>(a.r.begin() + ls, a.r.begin() + ls + ln);
        LearnAccumulator acc;
        acc.reset();
        unsigned long long last = 0;
        for (int pos = 0; pos < ln; pos += 512) {
            const int m = std::min(512, ln - pos);
            e.process(l.data() + pos, r.data() + pos, nullptr, nullptr, m);
            UiFrame f;
            e.readUi(f);
            if (f.frameCounter != last) { last = f.frameCounter; acc.add(f, def.thresholdDb); }
        }
        const auto lr = acc.finish(def.rangeLowHz, def.rangeHighHz, def.releaseMs, def.maxCutDb, resolvedFromHz(mode, sr), def.harmonicProtect);
        p = chtest::withLearn(def, lr);
        char b[200];
        std::snprintf(b, sizeof b, "thr %.1f amt %.0f rel %.0f maxCut %.0f lo %.0f hp %.0f pitched %.2f%s", lr.threshold, lr.amount * 100, lr.release, lr.maxCut, lr.rangeLow, lr.harmonicProtect * 100, lr.pitchShare, lr.valid ? "" : " (invalid)");
        learnInfo = b;
    } else {
        int idx = -1;
        const auto& ps = factoryPresets();
        for (int i = 0; i < (int) ps.size(); ++i) if (presetName == ps[(size_t) i].name) idx = i;
        if (idx < 0) { std::fprintf(stderr, "unknown preset '%s'\n", presetName.c_str()); return 1; }
        ParamState st;
        applyPreset(st, idx);
        for (auto& [k, v] : sets) { const int id = findParam(k); if (id >= 0) st.setReal(id, v); else { std::fprintf(stderr, "unknown param %s\n", k.c_str()); return 1; } }
        p = toParams(st);
    }

    // process (+ latency of zeros so the tail is flushed), then align
    Engine e;
    e.prepare(sr, mode);
    e.setParams(p);
    const int lat = e.latencySamples();
    auto yl = xl, yr = xr;
    yl.insert(yl.end(), (size_t) lat, 0.f); yr.insert(yr.end(), (size_t) lat, 0.f);
    double sumAvg = 0, maxAvg = 0, sumHin = 0, sumHout = 0, maxGr = 0;
    int frames = 0;
    unsigned long long last = 0;
    const auto t0 = std::chrono::steady_clock::now();
    for (int pos = 0; pos < (int) yl.size(); pos += 512) {
        const int m = std::min(512, (int) yl.size() - pos);
        e.process(yl.data() + pos, yr.data() + pos, nullptr, nullptr, m);
        UiFrame f;
        e.readUi(f);
        if (f.frameCounter == last || pos < 2 * lat) continue;
        last = f.frameCounter;
        sumAvg += f.avgReduction; maxAvg = std::max<double>(maxAvg, f.avgReduction);
        sumHin += f.harshIn; sumHout += f.harshOut; ++frames;
        for (int i = 0; i < kUiPoints; ++i) maxGr = std::max<double>(maxGr, f.gr[i]);
    }
    const double procSecs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    yl.erase(yl.begin(), yl.begin() + lat); yr.erase(yr.begin(), yr.begin() + lat);

    if (!origPath.empty()) writeWav16(origPath, xl, xr, (int) sr);
    if (!outPath.empty()) writeWav16(outPath, yl, yr, (int) sr);
    if (!deltaPath.empty()) {
        std::vector<float> dl(xl.size()), dr(xl.size());
        for (size_t i = 0; i < xl.size(); ++i) { dl[i] = xl[i] - yl[i]; dr[i] = xr[i] - yr[i]; }
        writeWav16(deltaPath, dl, dr, (int) sr, (float) deltaGain);
    }

    // ---- metrics on the mid (L+R)/2 ----
    std::vector<float> mi(xl.size()), mo(xl.size());
    for (size_t i = 0; i < xl.size(); ++i) { mi[i] = 0.5f * (xl[i] + xr[i]); mo[i] = 0.5f * (yl[i] + yr[i]); }
    const auto wi = windowed(mi, sr), wo = windowed(mo, sr);
    const size_t nw = std::min(wi.total.size(), wo.total.size());
    std::vector<char> use(nw, 0);
    for (size_t w = 0; w < nw; ++w) use[w] = wi.total[w] > 20.0;   // skip digital silence / fades (Hann-window power, ~-85 dBFS)
    double bandMean[kBands], bandStd[kBands];
    for (int b = 0; b < kBands; ++b) {
        std::vector<double> d;
        for (size_t w = 0; w < nw; ++w) if (use[w] && wi.band[w][(size_t) b] > 0.0) d.push_back(wo.band[w][(size_t) b] - wi.band[w][(size_t) b]);
        meanStd(d, bandMean[b], bandStd[b]);
    }
    std::vector<double> dLow, inLow, outLow, dTot;
    for (size_t w = 0; w < nw; ++w) if (use[w] && wi.low[w] > 10.0) {
        dLow.push_back(wo.low[w] - wi.low[w]); inLow.push_back(wi.low[w]); outLow.push_back(wo.low[w]);
        dTot.push_back(wo.total[w] - wi.total[w]);
    }
    double lowDM, lowDS, lowIM, lowIS, lowOM, lowOS, totM, totS;
    meanStd(dLow, lowDM, lowDS); meanStd(inLow, lowIM, lowIS); meanStd(outLow, lowOM, lowOS); meanStd(dTot, totM, totS);
    double maxLowStep = 0;   // largest change of the low-end gain between consecutive 50 ms windows
    for (size_t i = 1; i < dLow.size(); ++i) maxLowStep = std::max(maxLowStep, std::abs(dLow[i] - dLow[i - 1]));

    const double harshIn = harshness(mi, sr, use), harshOut = harshness(mo, sr, use);
    const double cenIn = centroid(mi, sr), cenOut = centroid(mo, sr);
    const double loss48 = bandLongTerm(mo, sr, 4000, 8000) - bandLongTerm(mi, sr, 4000, 8000);
    const double loss820 = bandLongTerm(mo, sr, 8000, 16000) - bandLongTerm(mi, sr, 8000, 16000);

    double pkIn = 0, pkOut = 0, sIn = 0, sOut = 0;
    for (size_t i = 0; i < xl.size(); ++i) {
        pkIn = std::max({ pkIn, (double) std::abs(xl[i]), (double) std::abs(xr[i]) });
        pkOut = std::max({ pkOut, (double) std::abs(yl[i]), (double) std::abs(yr[i]) });
        sIn += (double) xl[i] * xl[i] + (double) xr[i] * xr[i]; sOut += (double) yl[i] * yl[i] + (double) yr[i] * yr[i];
    }
    const double rmsIn = 10 * std::log10(sIn / (2.0 * xl.size()) + 1e-20), rmsOut = 10 * std::log10(sOut / (2.0 * xl.size()) + 1e-20);

    // clicks: second difference of the output exceeding what the input has nearby
    int clicks = 0;
    {
        auto d2 = [](const std::vector<float>& v, size_t i) { return v[i] - 2.f * v[i - 1] + v[i - 2]; };
        const int R = 12;
        for (int ch : { 0, 1 }) {
            const auto& xi = ch == 0 ? xl : xr;
            const auto& yo = ch == 0 ? yl : yr;
            std::vector<float> di(xi.size(), 0.f);
            for (size_t i = 2; i < xi.size(); ++i) di[i] = std::abs(d2(xi, i));
            // running local max of the input's second difference
            for (size_t i = R + 2; i + R < xi.size(); ++i) {
                float mx = 0;
                for (int j = -R; j <= R; ++j) mx = std::max(mx, di[(size_t) ((long) i + j)]);
                if (std::abs(d2(yo, i)) > 2.5f * mx + 0.05f) { ++clicks; if (verbose) std::printf("CLICK ch%d t=%.4f s out2d=%.3f in-local-max2d=%.3f in=%.3f out=%.3f\n", ch, (double) i / sr, d2(yo, i), mx, xi[i], yo[i]); i += 64; }
            }
        }
    }

    // transient attack energy (>1 kHz) at onsets of the input, before vs after
    double attackDb = 0, hfDb = 0;
    int onsets = 0;
    {
        const auto ei = hfEnergy1ms(mi, sr), eo = hfEnergy1ms(mo, sr);
        double sumI = 0, sumO = 0, totI = 0, totO = 0;
        for (size_t i = 0; i < ei.size(); ++i) { totI += ei[i]; totO += eo[i]; }
        hfDb = 10 * std::log10((totO + 1e-20) / (totI + 1e-20));
        for (size_t i = 25; i + 12 < ei.size(); ++i) {
            double base = 0;
            for (size_t j = i - 22; j < i - 2; ++j) base += ei[j];
            base /= 20.0;
            if (ei[i] > 5.0 * base && ei[i] > 2e-6) {
                double a1 = 0, a2 = 0;
                for (size_t j = i; j < i + 10; ++j) { a1 += ei[j]; a2 += eo[j]; }
                sumI += a1; sumO += a2; ++onsets;
                i += 50;
            }
        }
        attackDb = onsets ? 10 * std::log10((sumO + 1e-20) / (sumI + 1e-20)) : 0;
    }

    std::printf("RESULT preset=\"%s\" mode=%s start=%.1f len=%.1f learnAt=%.1f lat=%d", presetName.c_str(), modeName.c_str(), start, (double) n / sr, learnAt >= 0 ? learnAt : start, lat);
    std::printf(" learn=\"%s\" rtx=%.0f", learnInfo.c_str(), (double) n / sr / std::max(procSecs, 1e-9));
    std::printf(" bandMean=");
    for (int b = 0; b < kBands; ++b) std::printf("%s%.2f", b ? "," : "", bandMean[b]);
    std::printf(" bandStd=");
    for (int b = 0; b < kBands; ++b) std::printf("%s%.2f", b ? "," : "", bandStd[b]);
    std::printf(" lowDStd=%.3f lowDMean=%.2f lowInStd=%.2f lowOutStd=%.2f lowStep=%.2f totMean=%.2f totStd=%.2f", lowDS, lowDM, lowIS, lowOS, maxLowStep, totM, totS);
    std::printf(" harshIn=%.3f harshOut=%.3f engHarshIn=%.1f engHarshOut=%.1f avgRed=%.2f maxAvgRed=%.2f maxGr=%.1f", harshIn, harshOut, frames ? sumHin / frames : 0, frames ? sumHout / frames : 0,
                frames ? sumAvg / frames : 0, maxAvg, maxGr);
    std::printf(" peakIn=%.1f peakOut=%.1f rmsIn=%.1f rmsOut=%.1f crestIn=%.1f crestOut=%.1f clicks=%d", 20 * std::log10(pkIn + 1e-9), 20 * std::log10(pkOut + 1e-9), rmsIn, rmsOut,
                20 * std::log10(pkIn + 1e-9) - rmsIn, 20 * std::log10(pkOut + 1e-9) - rmsOut, clicks);
    std::printf(" cenIn=%.0f cenOut=%.0f loss4_8=%.2f loss8_16=%.2f attack=%.2f hfAll=%.2f onsets=%d\n", cenIn, cenOut, loss48, loss820, attackDb, hfDb, onsets);
    if (thirds) {   // detail: level change and std per 1/3 octave, 100 Hz .. 12.7 kHz
        Fft fft(kW);
        std::vector<float> re(kW), im(kW);
        const int hop = (int) std::lround(0.05 * sr);
        const int nT = 21;
        std::vector<std::vector<double>> dd((size_t) nT);
        auto spec = [&](const std::vector<float>& x, int st, std::array<double, 21>& out) {
            for (int i = 0; i < kW; ++i) { re[(size_t) i] = x[(size_t) (st + i)] * (float) (0.5 - 0.5 * std::cos(2.0 * std::numbers::pi * i / kW)); im[(size_t) i] = 0.f; }
            fft.forward(re.data(), im.data());
            out.fill(0.0);
            for (int k = 1; k < kW / 2; ++k) {
                const double f = k * sr / kW;
                const int t = (int) std::floor(3.0 * std::log2(f / 100.0));
                if (t >= 0 && t < nT) out[(size_t) t] += (double) re[(size_t) k] * re[(size_t) k] + (double) im[(size_t) k] * im[(size_t) k];
            }
        };
        std::array<double, 21> a1, a2;
        for (size_t w = 0; w < nw; ++w) {
            if (!use[w]) continue;
            const int st = (int) w * hop;
            if (st + kW > (int) mi.size()) break;
            spec(mi, st, a1); spec(mo, st, a2);
            for (int t = 0; t < nT; ++t) if (a1[(size_t) t] > 1e-3) dd[(size_t) t].push_back(10 * std::log10((a2[(size_t) t] + 1e-20) / a1[(size_t) t]));
        }
        std::printf("THIRDS");
        for (int t = 0; t < nT; ++t) { double m, sd; meanStd(dd[(size_t) t], m, sd); std::printf(" %.0f:%.2f/%.2f", 100.0 * std::pow(2.0, (t + 0.5) / 3.0), m, sd); }
        std::printf("\n");
    }
    if (verbose) {
        std::printf("%s / %s, %.1f s from %.1f s, latency %d (%.1f ms), %s\n", presetName.c_str(), modeName.c_str(), (double) n / sr, start, lat, 1000.0 * lat / sr, learnInfo.c_str());
        for (int b = 0; b < kBands; ++b) std::printf("  %6.0f-%6.0f Hz: change %+6.2f dB (std %.2f)\n", kEdges[b], kEdges[b + 1], bandMean[b], bandStd[b]);
        std::printf("  low end 30-200 Hz: out-in mean %+.2f std %.2f dB; in std %.2f, out std %.2f; max step %.2f\n", lowDM, lowDS, lowIS, lowOS, maxLowStep);
        std::printf("  harshness %.3f -> %.3f; centroid %.0f -> %.0f Hz; 4-8k %+.2f dB; attack energy %+.2f dB (%d onsets, HF overall %+.2f); clicks %d\n", harshIn, harshOut, cenIn, cenOut, loss48, attackDb, onsets, hfDb, clicks);
    }
    return 0;
}
