#include "Suppressor.h"
#include "Mode.h"
#include <algorithm>
#include <cmath>
#include <numbers>

namespace chiller::dsp {

namespace {
constexpr float kDbFloorPow = 1.0e-12f;           // -120 dB
constexpr float kLn10Over20 = 0.115129255f;       // dB -> natural log of amplitude gain
inline float toDb(float pow) { return 10.f * std::log10(pow + kDbFloorPow); }
inline float smoothstep(float a, float b, float x)
{
    const float t = std::clamp((x - a) / (b - a), 0.f, 1.f);
    return t * t * (3.f - 2.f * t);
}
constexpr float kDetectSmooth = 0.5f;   // per-frame weight of the new spectrum in the detection average
constexpr float kClipDb = 4.f;          // robust expected curve: peaks are clipped to (mean + this) between passes
constexpr float kPitchLo = 60.f, kPitchHi = 1000.f, kPitchStepOct = 1.f / 150.f;
constexpr int kPitchHarmonics = 6;
// Low-end guard. Below ~kResolveBins bins a bass note or kick cannot be told apart from a resonance, so the
// detection weight fades to 0 over the octave below that frequency. Time constants also grow towards the lows
// (x sqrt(1000 Hz / f), at most x kLowSlowMax) so bass is never cut and released at the speed of the highs.
constexpr float kLowSlowMax = 4.f, kLowSlowAtkMax = 8.f;
// Low-end cap: below ~200 Hz at most this fraction of Max Cut is ever applied, rising to the full Max Cut at 900 Hz.
constexpr float kLowCutFraction = 0.3f, kLowCapLoHz = 200.f, kLowCapHiHz = 900.f;
} // namespace

Suppressor::Suppressor(double sampleRate, int fftSize, TripleBuffer<UiFrame>* uiBuf)
    : sr(sampleRate), n(fftSize), nb(fftSize / 2 + 1), hop(fftSize / 4), mask(fftSize - 1), fft(fftSize), ui(uiBuf)
{
    hopMs = (float) (1000.0 * hop / sr);
    binHz = (float) (sr / n);
    const float nrm = 4.f / (float) n;
    norm2 = nrm * nrm;
    outScale = 1.f / ((float) n * 1.5f);
    window.resize((size_t) n);
    for (int i = 0; i < n; ++i)
        window[(size_t) i] = (float) (0.5 - 0.5 * std::cos(2.0 * std::numbers::pi * (double) i / (double) n));
    for (auto* v : { &inA, &inB, &kA, &kB, &outA, &outB, &zr, &zi }) v->assign((size_t) n, 0.f);
    for (auto* v : { &aRe, &aIm, &bRe, &bIm, &pA, &pB, &kpA, &kpB, &tRe0, &tIm0, &tRe1, &tIm1, &dbMain, &dbKey, &dbLin,
                     &dbOwn[0], &dbOwn[1], &clipv, &capW, &M, &rangeW, &harmFactor, &hw, &aCoef, &rCoef, &gA, &gB })
        v->assign((size_t) nb, 0.f);
    std::fill(harmFactor.begin(), harmFactor.end(), 1.f);
    for (int k = 0; k < nb; ++k)
        capW[(size_t) k] = kLowCutFraction + (1.f - kLowCutFraction) * smoothstep(kLowCapLoHz, kLowCapHiHz, (float) k * binHz);
    ps.assign((size_t) nb + 1, 0.0);
    lo.assign((size_t) nb, 0); hi.assign((size_t) nb, 0); loW.assign((size_t) nb, 0); hiW.assign((size_t) nb, 0);
    for (auto& p : pipes) {
        p.E.assign((size_t) nb, 0.f); p.Gt.assign((size_t) nb, 0.f); p.Gs.assign((size_t) nb, 0.f);
        p.tmp.assign((size_t) nb, 0.f); p.sm.assign((size_t) nb, -120.f); p.prevL.assign((size_t) nb, -120.f);
    }
    // wide (1 octave) envelope for the pitch detector, constant for a given size
    pitchBins = std::min(nb - 1, (int) std::ceil(6500.f / binHz) + 2);
    const float r = std::sqrt(2.f);   // +-1/2 octave => one octave wide
    for (int k = 0; k < nb; ++k) {
        loW[(size_t) k] = std::clamp((int) std::lround((float) k / r), 0, nb - 1);
        hiW[(size_t) k] = std::clamp((int) std::lround((float) k * r), 0, nb - 1);
        loW[(size_t) k] = std::min(loW[(size_t) k], std::max(0, k - 3));
        hiW[(size_t) k] = std::max(hiW[(size_t) k], std::min(nb - 1, k + 3));
    }
    for (float f = kPitchLo; f <= kPitchHi; f *= std::exp2(kPitchStepOct)) candPos.push_back(f / binHz);
    Pbuf.assign((size_t) nb, 0.f);
    budgetLo = std::min(nb - 1, (int) std::ceil(200.f / binHz));
    budgetHi = std::max(budgetLo + 1, std::min(nb, (int) std::floor(16000.f / binHz) + 1));
    fluxLo = std::min(nb - 2, std::max(1, (int) std::ceil(100.f / binHz)));
    // display point mapping: partition the bins by the rounded edges between points
    const float step = std::exp2(std::log2(kUiMaxHz / kUiMinHz) / (float) (kUiPoints - 1));
    const float halfStep = std::sqrt(step);
    for (int i = 0; i < kUiPoints; ++i) {
        const float f = uiPointHz(i);
        const float fl = i == 0 ? 0.f : f / halfStep, fh = f * halfStep;
        ptLo[i] = std::clamp((int) std::lround(fl / binHz), 0, nb - 1);
        ptHi[i] = std::clamp((int) std::lround(fh / binHz), 0, nb - 1);
        ptPos[i] = std::min(f / binHz, (float) (nb - 1));
    }
    publishEvery = std::max(1, (int) std::ceil(0.012 * sr / hop));
    reset();
}

void Suppressor::reset()
{
    for (auto* v : { &inA, &inB, &kA, &kB, &outA, &outB }) std::fill(v->begin(), v->end(), 0.f);
    writePos = hopCount = frameCount = 0;
    for (auto& p : pipes) {
        std::fill(p.Gs.begin(), p.Gs.end(), 0.f);
        std::fill(p.Gt.begin(), p.Gt.end(), 0.f);
        std::fill(p.prevL.begin(), p.prevL.end(), -120.f);
        std::fill(p.sm.begin(), p.sm.end(), -120.f);
        p.fluxAvg = p.transEnv = 0.f;
    }
    f0 = f0Conf = 0.f;
}

//----------------------------------------------------------------------------------------------
void Suppressor::setParams(const SuppressorParams& np)
{
    const bool first = !paramsInit;
    const SuppressorParams old = P;
    P = np;
    P.thresholdDb = std::clamp(P.thresholdDb, 0.f, 24.f);
    P.amount = std::clamp(P.amount, 0.f, 1.f);
    P.focusOct = std::clamp(P.focusOct, 1.f / 24.f, 1.f);
    P.maxCutDb = std::clamp(P.maxCutDb, 0.f, 30.f);
    P.budgetDb = std::clamp(P.budgetDb, 0.f, 12.f);
    P.link = std::clamp(P.link, 0.f, 1.f);
    P.speedTilt = std::clamp(P.speedTilt, -1.f, 1.f);
    P.transientPass = std::clamp(P.transientPass, 0.f, 1.f);
    P.harmonicProtect = std::clamp(P.harmonicProtect, 0.f, 1.f);
    paramsInit = true;
    if (first || P.focusOct != old.focusOct) updateFocus();
    if (first || P.rangeLowHz != old.rangeLowHz || P.rangeHighHz != old.rangeHighHz) updateRange();
    if (first || P.attackMs != old.attackMs || P.releaseMs != old.releaseMs || P.speedTilt != old.speedTilt) updateSpeed();
}

void Suppressor::updateFocus()
{
    const float r = std::exp2(P.focusOct * 0.5f);
    for (int k = 0; k < nb; ++k) {
        int l = (int) std::lround((float) k / r), h = (int) std::lround((float) k * r);
        l = std::min(l, k - 4);
        h = std::max(h, k + 4);
        lo[(size_t) k] = std::max(0, l);
        hi[(size_t) k] = std::min(nb - 1, h);
    }
}

void Suppressor::updateRange()
{
    const float fl = std::clamp(P.rangeLowHz, 10.f, 30000.f), fh = std::clamp(P.rangeHighHz, 10.f, 30000.f);
    for (int k = 0; k < nb; ++k) {
        const float f = std::max((float) k, 0.25f) * binHz;
        const float a = fl / f, b = f / fh;
        const float a2 = a * a, b2 = b * b;
        const float fRes = resolvedFromHz(n, sr);                     // lowest frequency this STFT size can separate
        rangeW[(size_t) k] = 1.f / (1.f + a2 * a2) / (1.f + b2 * b2)  // 2nd-order-ish rolloff on each side
                             * smoothstep(0.5f * fRes, fRes, (float) k * binHz);
    }
}

void Suppressor::updateSpeed()
{
    for (int k = 0; k < nb; ++k) {
        const float f = std::max((float) k, 0.5f) * binHz;
        const float tilt = std::clamp(std::exp2(-P.speedTilt * std::log2(f / 1000.f) / 3.f), 0.2f, 5.f);
        const float lowSlow = std::clamp(std::sqrt(1000.f / f), 1.f, kLowSlowMax);
        const float lowSlowAtk = std::clamp(1000.f / f, 1.f, kLowSlowAtkMax);     // attack slows faster: short bass notes are not "resonances"
        aCoef[(size_t) k] = 1.f - std::exp(-hopMs / std::max(0.05f, P.attackMs * tilt * lowSlowAtk));
        rCoef[(size_t) k] = 1.f - std::exp(-hopMs / std::max(0.05f, P.releaseMs * tilt * lowSlow));
    }
}

//----------------------------------------------------------------------------------------------
void Suppressor::process(float* l, float* r, const float* scL, const float* scR, int count)
{
    scHave = scL != nullptr;
    const bool ms = P.midSide;
    for (int i = 0; i < count; ++i) {
        float a = l[i], b = r[i];
        if (ms) { const float m = (a + b) * 0.5f, s = (a - b) * 0.5f; a = m; b = s; }
        inA[(size_t) writePos] = a;
        inB[(size_t) writePos] = b;
        if (scHave) {
            float ka = scL[i], kb = scR ? scR[i] : scL[i];
            if (ms) { const float m = (ka + kb) * 0.5f, s = (ka - kb) * 0.5f; ka = m; kb = s; }
            kA[(size_t) writePos] = ka;
            kB[(size_t) writePos] = kb;
        }
        float oa = outA[(size_t) writePos], ob = outB[(size_t) writePos];
        outA[(size_t) writePos] = 0.f;
        outB[(size_t) writePos] = 0.f;
        writePos = (writePos + 1) & mask;
        if (++hopCount == hop) { hopCount = 0; processFrame(); }
        if (ms) { l[i] = oa + ob; r[i] = oa - ob; } else { l[i] = oa; r[i] = ob; }
    }
}

void Suppressor::analyse(const std::vector<float>& ringA, const std::vector<float>& ringB, float* r0, float* i0, float* r1, float* i1)
{
    for (int i = 0; i < n; ++i) {
        const size_t idx = (size_t) ((writePos + i) & mask);
        zr[(size_t) i] = ringA[idx] * window[(size_t) i];
        zi[(size_t) i] = ringB[idx] * window[(size_t) i];
    }
    fft.forward(zr.data(), zi.data());
    for (int k = 0; k < nb; ++k) {
        const int j = (n - k) & mask;
        const float zrk = zr[(size_t) k], zik = zi[(size_t) k], zrj = zr[(size_t) j], zij = zi[(size_t) j];
        r0[k] = 0.5f * (zrk + zrj);  i0[k] = 0.5f * (zik - zij);
        r1[k] = 0.5f * (zik + zij);  i1[k] = -0.5f * (zrk - zrj);
    }
}

float Suppressor::meanOver(const float* v) const
{
    double s = 0.0;
    for (int k = budgetLo; k < budgetHi; ++k) s += v[k];
    return (float) (s / (double) (budgetHi - budgetLo));
}

//----------------------------------------------------------------------------------------------
void Suppressor::processFrame()
{
    // ---- analysis of the main input ----
    analyse(inA, inB, aRe.data(), aIm.data(), bRe.data(), bIm.data());
    const bool useKey = P.scKey && scHave;
    const float linkedScale = P.midSide ? 1.f : 0.5f;   // M^2 + S^2 == (L^2 + R^2) / 2
    for (int k = 0; k < nb; ++k) {
        pA[(size_t) k] = (aRe[(size_t) k] * aRe[(size_t) k] + aIm[(size_t) k] * aIm[(size_t) k]) * norm2;
        pB[(size_t) k] = (bRe[(size_t) k] * bRe[(size_t) k] + bIm[(size_t) k] * bIm[(size_t) k]) * norm2;
        dbMain[(size_t) k] = toDb((pA[(size_t) k] + pB[(size_t) k]) * linkedScale);
    }
    const float* dPa = pA.data();
    const float* dPb = pB.data();
    if (useKey) {
        analyse(kA, kB, tRe0.data(), tIm0.data(), tRe1.data(), tIm1.data());
        for (int k = 0; k < nb; ++k) {
            kpA[(size_t) k] = (tRe0[(size_t) k] * tRe0[(size_t) k] + tIm0[(size_t) k] * tIm0[(size_t) k]) * norm2;
            kpB[(size_t) k] = (tRe1[(size_t) k] * tRe1[(size_t) k] + tIm1[(size_t) k] * tIm1[(size_t) k]) * norm2;
            dbKey[(size_t) k] = toDb((kpA[(size_t) k] + kpB[(size_t) k]) * linkedScale);
        }
        dPa = kpA.data();
        dPb = kpB.data();
        detLin = dbKey.data();
    } else {
        detLin = dbMain.data();
    }
    const bool linked = P.link >= 0.999f;
    if (!linked) {
        for (int k = 0; k < nb; ++k) {
            dbOwn[0][(size_t) k] = toDb(dPa[k]);
            dbOwn[1][(size_t) k] = toDb(dPb[k]);
        }
        detOwn[0] = dbOwn[0].data();
        detOwn[1] = dbOwn[1].data();
    }

    // ---- pitch / harmonic protection ----
    float* hf = nullptr;
    if (P.harmonicProtect > 0.001f && n >= 512) {
        detectPitch();
        const float eff = P.harmonicProtect * 0.9f * smoothstep(0.35f, 0.7f, f0Conf);
        if (eff > 0.005f && f0 > 0.f) {
            std::fill(hw.begin(), hw.end(), 0.f);
            const float sigOct = std::exp2(1.f / 40.f) - 1.f;
            for (int h = 1;; ++h) {
                const float f = f0 * (float) h;
                if (f > 10000.f || f >= (float) sr * 0.5f) break;
                const float sig = std::max(f * sigOct, 0.6f * binHz);
                const int k0 = std::max(0, (int) std::floor((f - 3.f * sig) / binHz)), k1 = std::min(nb - 1, (int) std::ceil((f + 3.f * sig) / binHz));
                for (int k = k0; k <= k1; ++k) {
                    const float d = ((float) k * binHz - f) / sig;
                    hw[(size_t) k] = std::max(hw[(size_t) k], std::exp(-0.5f * d * d));
                }
            }
            for (int k = 0; k < nb; ++k) harmFactor[(size_t) k] = 1.f - eff * hw[(size_t) k];
            hf = harmFactor.data();
        }
    } else {
        f0 = f0Conf = 0.f;
    }

    // ---- detection pipelines ----
    runPipe(pipes[0], detLin, hf);
    if (!linked) {
        runPipe(pipes[1], detOwn[0], hf);
        runPipe(pipes[2], detOwn[1], hf);
    } else {   // keep the per-channel states warm so lowering Link never jumps
        pipes[1].Gs = pipes[0].Gs;
        pipes[2].Gs = pipes[0].Gs;
    }

    // ---- per-channel gains, budget guard, apply ----
    const float lk = P.link;
    for (int k = 0; k < nb; ++k) {
        const float gl = pipes[0].Gs[(size_t) k];
        gA[(size_t) k] = linked ? gl : lk * gl + (1.f - lk) * pipes[1].Gs[(size_t) k];
        gB[(size_t) k] = linked ? gl : lk * gl + (1.f - lk) * pipes[2].Gs[(size_t) k];
    }
    for (auto* g : { &gA, &gB }) {
        const float m = meanOver(g->data());
        if (m > P.budgetDb) {
            const float s = P.budgetDb <= 0.f ? 0.f : P.budgetDb / m;
            for (auto& x : *g) x *= s;
        }
    }
    for (int k = 0; k < nb; ++k) {
        const float ga = std::exp(-gA[(size_t) k] * kLn10Over20), gb = std::exp(-gB[(size_t) k] * kLn10Over20);
        aRe[(size_t) k] *= ga; aIm[(size_t) k] *= ga;
        bRe[(size_t) k] *= gb; bIm[(size_t) k] *= gb;
    }

    // ---- synthesis ----
    const int half = n / 2;
    for (int k = 0; k <= half; ++k) {
        zr[(size_t) k] = aRe[(size_t) k] - bIm[(size_t) k];
        zi[(size_t) k] = aIm[(size_t) k] + bRe[(size_t) k];
        if (k > 0 && k < half) {
            zr[(size_t) (n - k)] = aRe[(size_t) k] + bIm[(size_t) k];
            zi[(size_t) (n - k)] = bRe[(size_t) k] - aIm[(size_t) k];
        }
    }
    fft.inverse(zr.data(), zi.data());
    for (int i = 0; i < n; ++i) {
        const size_t idx = (size_t) ((writePos + i) & mask);
        const float w = window[(size_t) i] * outScale;
        outA[idx] += zr[(size_t) i] * w;
        outB[idx] += zi[(size_t) i] * w;
    }

    if (ui && ++frameCount >= publishEvery) { frameCount = 0; publish(gA.data(), gB.data()); }
}

//----------------------------------------------------------------------------------------------
void Suppressor::runPipe(Pipe& p, const float* Lraw, const float* harm)
{
    // Detection works on the spectrum smoothed over time in dB (half a frame step per frame). Averaging
    // the log-magnitude of noise-like bins cuts the random "false resonances" of a single frame, so noise is
    // left alone while steady resonances still stand out.
    for (int k = 0; k < nb; ++k) p.sm[(size_t) k] += (Lraw[k] - p.sm[(size_t) k]) * kDetectSmooth;
    const float* L = p.sm.data();
    // expected curve: windowed mean in dB across frequency, then two robust passes that clip
    // peaks to (mean + 4 dB) so a resonance does not raise its own reference
    ps[0] = 0.0;
    for (int k = 0; k < nb; ++k) ps[(size_t) k + 1] = ps[(size_t) k] + (double) L[k];
    auto windowMean = [&](float* dst) {
        for (int k = 0; k < nb; ++k) {
            const int l = lo[(size_t) k], h = hi[(size_t) k];
            dst[k] = (float) ((ps[(size_t) h + 1] - ps[(size_t) l]) / (double) (h - l + 1));
        }
    };
    windowMean(M.data());
    for (int pass = 0; pass < 3; ++pass) {
        for (int k = 0; k < nb; ++k) ps[(size_t) k + 1] = ps[(size_t) k] + (double) std::min(L[k], M[(size_t) k] + kClipDb);
        windowMean(M.data());
    }
    std::copy(M.begin(), M.end(), p.E.begin());

    // onset detector (positive spectral flux vs a running average)
    float flux = 0.f;
    for (int k = fluxLo; k < nb; ++k) flux += std::max(0.f, Lraw[k] - p.prevL[(size_t) k]);
    flux /= (float) (nb - fluxLo);
    std::copy(Lraw, Lraw + nb, p.prevL.begin());
    const bool onset = flux > 1.8f * p.fluxAvg + 1.5f;
    p.fluxAvg += (flux - p.fluxAvg) * (1.f - std::exp(-hopMs / 300.f));
    if (p.fluxAvg < 1.0e-6f) p.fluxAvg = 0.f;   // no denormals in long silence
    p.transEnv = onset ? 1.f : p.transEnv * std::exp(-hopMs / 30.f);
    if (p.transEnv < 1.0e-4f) p.transEnv = 0.f;
    const float transFactor = 1.f - P.transientPass * 0.8f * p.transEnv;

    // target reduction
    const float thr = P.thresholdDb, amt = P.amount, mc = P.maxCutDb;
    for (int k = 0; k < nb; ++k) {
        const float ex = L[k] - p.E[(size_t) k] - thr;
        float g = 0.f;
        if (ex > 0.f) {
            g = amt * ex * transFactor;
            if (harm) g *= harm[k];
            // Range / resolvability weight scales the cut after the Max Cut cap: a strong tone in a region the STFT cannot resolve (or outside
            // Range) must not be cut by the full cap just because its excess is huge.
            g = std::min(g, mc * capW[(size_t) k]) * rangeW[(size_t) k];
        }
        p.Gt[(size_t) k] = g;
    }
    // smooth across neighbouring bins (never lowers the peak by more than 10 %)
    for (int k = 0; k < nb; ++k) {
        const float c = p.Gt[(size_t) k], a = p.Gt[(size_t) std::max(0, k - 1)], b = p.Gt[(size_t) std::min(nb - 1, k + 1)];
        p.tmp[(size_t) k] = std::max(0.9f * c, 0.25f * a + 0.5f * c + 0.25f * b);
    }
    // budget on the target
    const float m = meanOver(p.tmp.data());
    const float bs = m > P.budgetDb ? (P.budgetDb <= 0.f ? 0.f : P.budgetDb / m) : 1.f;
    // time smoothing per bin
    for (int k = 0; k < nb; ++k) {
        const float tg = p.tmp[(size_t) k] * bs;
        float& g = p.Gs[(size_t) k];
        g += (tg - g) * (tg > g ? aCoef[(size_t) k] : rCoef[(size_t) k]);
        if (g < 1.0e-4f) g = 0.f;
    }
}

//----------------------------------------------------------------------------------------------
void Suppressor::detectPitch()
{
    // peakiness P[k] = dB above a robust one-octave baseline, then a harmonic sum over candidate f0s
    const int pb = pitchBins;
    auto windowMean = [&](float* dst) {
        for (int k = 0; k <= pb; ++k) {
            const int l = loW[(size_t) k], h = std::min(hiW[(size_t) k], pb);
            dst[k] = (float) ((ps[(size_t) h + 1] - ps[(size_t) l]) / (double) (h - l + 1));
        }
    };
    ps[0] = 0.0;
    for (int k = 0; k <= pb; ++k) ps[(size_t) k + 1] = ps[(size_t) k] + (double) dbMain[(size_t) k];
    windowMean(M.data());
    for (int pass = 0; pass < 2; ++pass) {
        for (int k = 0; k <= pb; ++k) ps[(size_t) k + 1] = ps[(size_t) k] + (double) std::min(dbMain[(size_t) k], M[(size_t) k] + 3.f);
        windowMean(M.data());
    }
    for (int k = 0; k <= pb; ++k) Pbuf[(size_t) k] = std::max(0.f, dbMain[(size_t) k] - M[(size_t) k]);
    auto at = [&](float pos) {
        const int i = (int) pos;
        if (i >= pb) return 0.f;
        const float fr = pos - (float) i;
        return Pbuf[(size_t) i] * (1.f - fr) + Pbuf[(size_t) i + 1] * fr;
    };
    auto score = [&](float pos) {
        float s = 0.f;
        for (int h = 1; h <= kPitchHarmonics; ++h) s += std::min(24.f, at(pos * (float) h));
        return s;
    };
    float best = 0.f, bestPos = 0.f;
    for (float pos : candPos) {
        const float s = score(pos);
        if (s > best) { best = s; bestPos = pos; }
    }
    // octave guard: prefer a sub-multiple whose harmonics are (almost) all present too
    for (int d = 4; d >= 2 && best > 0.f; --d) {
        const float pos = bestPos / (float) d;
        if (pos < candPos.front()) continue;
        const float s = score(pos);
        if (s >= 0.85f * best) { bestPos = pos; best = std::max(s, 0.f); break; }
    }
    float conf = 0.f, fNew = 0.f;
    if (best > 0.f) {
        int cnt = 0;
        for (int h = 1; h <= kPitchHarmonics; ++h) cnt += at(bestPos * (float) h) > 6.f ? 1 : 0;
        const float mean = best / (float) kPitchHarmonics;
        conf = std::clamp((float) (cnt - 2) / 3.f, 0.f, 1.f) * std::clamp((mean - 5.f) / 9.f, 0.f, 1.f);
        fNew = bestPos * binHz;
    }
    const float a = 1.f - std::exp(-hopMs / 40.f);
    if (conf > 0.f) {
        if (f0 > 0.f && std::abs(std::log2(fNew / f0)) < 0.04f) f0 += (fNew - f0) * 0.4f;
        else if (conf > f0Conf * 0.8f || f0 <= 0.f) f0 = fNew;
    }
    f0Conf += (conf - f0Conf) * (conf > f0Conf ? std::min(1.f, a * 2.f) : a);
    if (f0Conf < 1.0e-4f) f0Conf = 0.f;
    if (f0Conf < 0.02f) f0 = 0.f;
}

//----------------------------------------------------------------------------------------------
void Suppressor::publish(const float* ga, const float* gb)
{
    UiFrame& f = ui->writeSlot();
    f.sampleRate = (float) sr;
    f.keyActive = P.scKey && scHave;
    f.f0 = f0;
    f.f0Conf = P.harmonicProtect > 0.001f ? f0Conf : 0.f;
    const float mix = P.displayMix;
    for (int i = 0; i < kUiPoints; ++i) {
        const int l = ptLo[i], h = ptHi[i];
        float vIn, vEx, vKey, vG;
        if (h > l) {
            float sIn = 0.f, sEx = 0.f, sKey = 0.f, mg = 0.f;
            for (int k = l; k < h; ++k) {
                sIn += dbMain[(size_t) k]; sEx += pipes[0].E[(size_t) k]; sKey += dbKey[(size_t) k];
                mg = std::max(mg, 0.5f * (ga[k] + gb[k]));
            }
            const float inv = 1.f / (float) (h - l);
            vIn = sIn * inv; vEx = sEx * inv; vKey = sKey * inv; vG = mg;
        } else {
            const float pos = ptPos[i];
            const int k = std::min((int) pos, nb - 2);
            const float fr = pos - (float) k;
            auto lerp = [fr, k](const float* v) { return v[k] * (1.f - fr) + v[k + 1] * fr; };
            vIn = lerp(dbMain.data()); vEx = lerp(pipes[0].E.data()); vKey = lerp(dbKey.data());
            vG = 0.5f * (lerp(ga) + lerp(gb));
        }
        f.inDb[i] = vIn;
        f.expDb[i] = vEx + P.thresholdDb;
        f.keyDb[i] = vKey;
        f.gr[i] = vG * mix;
    }
    // peaks: largest local maxima of gr, at least 8 points apart
    f.numPeaks = 0;
    bool taken[kUiPoints] = {};
    for (int round = 0; round < 5; ++round) {
        int bi = -1;
        float bv = 0.5f;
        for (int i = 1; i < kUiPoints - 1; ++i) {
            if (taken[i] || f.gr[i] < bv || f.gr[i] < f.gr[i - 1] || f.gr[i] <= f.gr[i + 1]) continue;
            bv = f.gr[i]; bi = i;
        }
        if (bi < 0) break;
        f.peaks[f.numPeaks++] = { uiPointHz(bi), bv };
        for (int j = std::max(0, bi - 8); j <= std::min(kUiPoints - 1, bi + 8); ++j) taken[j] = true;
    }
    // harshness: excess above the expected curve between 1.5 and 10 kHz, before and after the cut
    float hin = 0.f, hout = 0.f;
    for (int i = 0; i < kUiPoints; ++i) {
        const float hz = uiPointHz(i);
        if (hz < 1500.f || hz > 10000.f) continue;
        const float e = std::max(0.f, f.inDb[i] - (f.expDb[i] - P.thresholdDb) - 2.f);
        hin += e;
        hout += std::max(0.f, e - f.gr[i]);
    }
    f.harshIn = 100.f * (1.f - std::exp(-hin / 120.f));
    f.harshOut = 100.f * (1.f - std::exp(-hout / 120.f));
    double s = 0.0;
    for (int k = budgetLo; k < budgetHi; ++k) s += 0.5f * (ga[k] + gb[k]);
    f.avgReduction = (float) (s / (double) (budgetHi - budgetLo)) * mix;
    f.frameCounter = ++uiCount;
    ui->publish();
}

} // namespace chiller::dsp
