#include "Engine.h"
#include <algorithm>

namespace chiller::dsp {

void Engine::prepare(double sampleRate, Mode mode)
{
    sr = sampleRate;
    for (int m = 0; m < (int) Mode::Count; ++m)
        suppressors[(size_t) m] = std::make_unique<Suppressor>(sr, fftSizeFor((Mode) m, sr), &ui);
    current = mode;
    active()->setParams(lastParams);
}

void Engine::reset()
{
    for (auto& s : suppressors) if (s) s->reset();
}

void Engine::setMode(Mode m)
{
    if (m == current) return;
    current = m;
    active()->reset();
    active()->setParams(lastParams);
}

void Engine::process(float* l, float* r, const float* scL, const float* scR, int count)
{
    if (auto* s = active()) s->process(l, r, scL, scR, count);
}

void DelayLine::prepare(int maxDelaySamples, int numChannels)
{
    maxDelay = maxDelaySamples;
    size = maxDelay + 1;
    buf.assign((size_t) numChannels, std::vector<float>((size_t) size, 0.f));
    pos = 0;
    delay = 0;
}

void DelayLine::reset()
{
    for (auto& b : buf) std::fill(b.begin(), b.end(), 0.f);
    pos = 0;
}

float DelayLine::process(int channel, float x)
{
    auto& b = buf[(size_t) channel];
    b[(size_t) pos] = x;
    int r = pos - delay;
    if (r < 0) r += size;
    return b[(size_t) r];
}

} // namespace chiller::dsp
