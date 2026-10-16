#pragma once
// Small self-contained radix-2 complex FFT (no JUCE, no allocation after construction).
#include <vector>

namespace chiller::dsp {

class Fft {
public:
    explicit Fft(int size = 1024);          // size must be a power of two >= 4
    int size() const { return n; }
    // In-place on split re/im arrays of length size(). inverse() is unscaled (multiply by 1/size()).
    void forward(float* re, float* im) const { run(re, im, false); }
    void inverse(float* re, float* im) const { run(re, im, true); }

private:
    void run(float* re, float* im, bool inv) const;
    int n = 0;
    std::vector<int> rev;
    std::vector<float> cosT, sinT;   // twiddles for k = 0 .. n/2 - 1
};

bool isPowerOfTwo(int v);

} // namespace chiller::dsp
