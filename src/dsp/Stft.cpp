#include "Stft.h"
#include <algorithm>
#include <cmath>
#include <numbers>

namespace chiller::dsp {

Stft::Stft(int fftSize) : n(isPowerOfTwo(fftSize) && fftSize >= 16 ? fftSize : 1024), hop(n / 4), mask(n - 1), fft(n)
{
    window.resize((size_t) n);
    for (int i = 0; i < n; ++i)   // periodic Hann: sum of squares at 75 % overlap is exactly 1.5
        window[(size_t) i] = (float) (0.5 - 0.5 * std::cos(2.0 * std::numbers::pi * (double) i / (double) n));
    outScale = 1.f / ((float) n * 1.5f);   // 1/N for the unscaled inverse FFT, 1/1.5 for the window overlap
    inRing.assign((size_t) n, 0.f);
    outRing.assign((size_t) n, 0.f);
    re.assign((size_t) n, 0.f);
    im.assign((size_t) n, 0.f);
    gains.assign((size_t) n / 2 + 1, 1.f);
}

void Stft::reset()
{
    std::fill(inRing.begin(), inRing.end(), 0.f);
    std::fill(outRing.begin(), outRing.end(), 0.f);
    writePos = hopCount = 0;
}

float Stft::processSample(float x)
{
    inRing[(size_t) writePos] = x;
    const float y = outRing[(size_t) writePos];   // slot of absolute position (now - N): complete
    outRing[(size_t) writePos] = 0.f;
    writePos = (writePos + 1) & mask;
    if (++hopCount == hop) { hopCount = 0; processFrame(); }
    return y;
}

void Stft::process(const float* in, float* out, int count)
{
    for (int i = 0; i < count; ++i) out[i] = processSample(in[i]);
}

void Stft::processFrame()
{
    // oldest sample of the frame sits at writePos
    for (int i = 0; i < n; ++i) {
        re[(size_t) i] = inRing[(size_t) ((writePos + i) & mask)] * window[(size_t) i];
        im[(size_t) i] = 0.f;
    }
    fft.forward(re.data(), im.data());

    // ---- spectral processing hook: per-bin gain (currently unity) ----
    const int half = n / 2;
    for (int k = 0; k <= half; ++k) {
        const float g = gains[(size_t) k];
        re[(size_t) k] *= g; im[(size_t) k] *= g;
        if (k > 0 && k < half) { re[(size_t) (n - k)] *= g; im[(size_t) (n - k)] *= g; }
    }

    fft.inverse(re.data(), im.data());
    for (int i = 0; i < n; ++i)
        outRing[(size_t) ((writePos + i) & mask)] += re[(size_t) i] * window[(size_t) i] * outScale;
}

} // namespace chiller::dsp
