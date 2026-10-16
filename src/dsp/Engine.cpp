#include "Engine.h"
#include <algorithm>

namespace chiller::dsp {

void Engine::prepare(double sampleRate, int numChannels, Mode mode)
{
    sr = sampleRate;
    numCh = numChannels;
    for (int m = 0; m < (int) Mode::Count; ++m) {
        stft[m].clear();
        if ((Mode) m == Mode::Live) continue;   // passthrough: no STFT yet
        for (int c = 0; c < numCh; ++c) stft[m].push_back(std::make_unique<Stft>(fftSizeForMode((Mode) m)));
    }
    current = mode;
}

void Engine::reset()
{
    for (auto& v : stft) for (auto& s : v) s->reset();
}

void Engine::setMode(Mode m)
{
    if (m == current) return;
    current = m;
    for (auto& s : stft[(int) m]) s->reset();
}

void Engine::process(float* const* channels, int count)
{
    if (current == Mode::Live) return;
    auto& v = stft[(int) current];
    for (int c = 0; c < numCh && c < (int) v.size(); ++c) v[(size_t) c]->process(channels[c], channels[c], count);
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
