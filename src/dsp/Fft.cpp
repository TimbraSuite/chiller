#include "Fft.h"
#include <cmath>
#include <numbers>
#include <utility>

namespace chiller::dsp {

bool isPowerOfTwo(int v) { return v > 0 && (v & (v - 1)) == 0; }

Fft::Fft(int size) : n(size)
{
    if (!isPowerOfTwo(n) || n < 4) n = 4;
    rev.resize((size_t) n);
    int bits = 0;
    while ((1 << bits) < n) ++bits;
    for (int i = 0; i < n; ++i) {
        int r = 0;
        for (int b = 0; b < bits; ++b)
            if (i & (1 << b)) r |= 1 << (bits - 1 - b);
        rev[(size_t) i] = r;
    }
    cosT.resize((size_t) n / 2);
    sinT.resize((size_t) n / 2);
    for (int k = 0; k < n / 2; ++k) {
        const double a = -2.0 * std::numbers::pi * (double) k / (double) n;
        cosT[(size_t) k] = (float) std::cos(a);
        sinT[(size_t) k] = (float) std::sin(a);
    }
}

void Fft::run(float* re, float* im, bool inv) const
{
    for (int i = 0; i < n; ++i) {
        const int j = rev[(size_t) i];
        if (j > i) { std::swap(re[i], re[j]); std::swap(im[i], im[j]); }
    }
    const float sgn = inv ? -1.f : 1.f;   // inverse uses conjugated twiddles
    for (int len = 2; len <= n; len <<= 1) {
        const int half = len >> 1, step = n / len;
        for (int i = 0; i < n; i += len) {
            for (int k = 0; k < half; ++k) {
                const float wr = cosT[(size_t) (k * step)], wi = sgn * sinT[(size_t) (k * step)];
                const int a = i + k, b = a + half;
                const float tr = re[b] * wr - im[b] * wi;
                const float ti = re[b] * wi + im[b] * wr;
                re[b] = re[a] - tr; im[b] = im[a] - ti;
                re[a] += tr;        im[a] += ti;
            }
        }
    }
}

} // namespace chiller::dsp
