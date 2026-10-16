#pragma once
// Single-channel streaming STFT: Hann analysis + Hann synthesis window (weighted overlap-add),
// 75 % overlap (hop = fftSize / 4). Between analysis and synthesis every bin is scaled by
// binGains() (all 1.0 for now, so the processor is a transparent delay of latencySamples()).
#include "Fft.h"
#include <vector>

namespace chiller::dsp {

class Stft {
public:
    explicit Stft(int fftSize = 1024);
    int fftSize() const { return n; }
    int hopSize() const { return hop; }
    // Output sample y[i] equals input sample x[i - latencySamples()] (zero before the start).
    int latencySamples() const { return n; }
    void reset();

    // One gain per bin 0 .. fftSize/2 (inclusive). Applied to the mirrored bins too.
    std::vector<float>& binGains() { return gains; }
    const std::vector<float>& binGains() const { return gains; }

    float processSample(float x);
    void process(const float* in, float* out, int count);   // in == out is fine

private:
    void processFrame();
    int n, hop, mask, writePos = 0, hopCount = 0;
    float outScale;
    Fft fft;
    std::vector<float> window, inRing, outRing, re, im, gains;
};

} // namespace chiller::dsp
