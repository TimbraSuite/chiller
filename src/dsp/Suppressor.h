#pragma once
// The resonance suppressor core for one STFT size: stereo (L/R or M/S) Hann/WOLA analysis-synthesis,
// per-bin detection against an "expected" spectral envelope, protection, budget, time smoothing, and
// per-bin gain. Real-time safe: everything is allocated in the constructor.
#include "Fft.h"
#include "Telemetry.h"
#include <vector>

namespace chiller::dsp {

struct SuppressorParams {
    float thresholdDb = 6.f;      // 0..24
    float amount = 0.5f;          // 0..1
    float focusOct = 1.f / 6.f;   // width of the expected-curve window, 1/24 .. 1 octave
    float maxCutDb = 12.f;        // 0..30
    float rangeLowHz = 40.f, rangeHighHz = 18000.f;
    float attackMs = 10.f, releaseMs = 120.f;
    float speedTilt = 0.f;        // -1..1, + = faster highs
    float transientPass = 0.f;    // 0..1
    float harmonicProtect = 0.f;  // 0..1
    float budgetDb = 6.f;         // 0..12
    float link = 1.f;             // 0..1
    float displayMix = 1.f;       // only scales what the UI reports
    bool midSide = false;
    bool scKey = false;           // detect from the sidechain input when one is supplied
};

class Suppressor {
public:
    Suppressor(double sampleRate, int fftSize, TripleBuffer<UiFrame>* ui);
    int fftSize() const { return n; }
    int latencySamples() const { return n; }
    void setParams(const SuppressorParams& p);
    void reset();
    // In place. scL/scR may be null (no sidechain). Output is delayed by latencySamples().
    void process(float* l, float* r, const float* scL, const float* scR, int count);

private:
    struct Pipe {
        std::vector<float> E, Gt, Gs, tmp, prevL, sm;
        float fluxAvg = 0.f, transEnv = 0.f;
    };
    void processFrame();
    void analyse(const std::vector<float>& ringA, const std::vector<float>& ringB, float* r0, float* i0, float* r1, float* i1);
    void runPipe(Pipe& p, const float* L, const float* harmFactor);
    void detectPitch();
    void publish(const float* gA, const float* gB);
    void updateFocus();
    void updateRange();
    void updateSpeed();
    float meanOver(const float* v) const;

    double sr;
    int n, nb, hop, mask, writePos = 0, hopCount = 0, frameCount = 0, publishEvery = 1;
    float hopMs, binHz, norm2, outScale;
    SuppressorParams P, applied;
    bool paramsInit = false, scHave = false;
    Fft fft;
    std::vector<float> window;
    std::vector<float> inA, inB, kA, kB, outA, outB;     // rings
    std::vector<float> zr, zi;
    std::vector<float> aRe, aIm, bRe, bIm, pA, pB, kpA, kpB, tRe0, tIm0, tRe1, tIm1;
    std::vector<float> dbMain, dbKey, dbLin, dbOwn[2];
    const float* detLin = nullptr;
    const float* detOwn[2] = { nullptr, nullptr };
    Pipe pipes[3];
    std::vector<double> ps;
    std::vector<float> clipv, M, rangeW, harmFactor, hw, aCoef, rCoef, gA, gB;
    std::vector<int> lo, hi, loW, hiW;
    int budgetLo = 0, budgetHi = 0, pitchBins = 0, fluxLo = 0;
    // pitch
    std::vector<float> candPos, Pbuf;
    float f0 = 0.f, f0Conf = 0.f;
    // ui mapping
    int ptLo[kUiPoints], ptHi[kUiPoints];
    float ptPos[kUiPoints];
    TripleBuffer<UiFrame>* ui;
    unsigned long long uiCount = 0;
};

} // namespace chiller::dsp
