#pragma once
// Multi-channel suppressor engine. Foundation: transparent passthrough with mode-dependent
// latency. All three modes' STFTs are allocated in prepare(), so setMode() is real-time safe.
#include "Mode.h"
#include "Stft.h"
#include <memory>
#include <vector>

namespace chiller::dsp {

class Engine {
public:
    void prepare(double sampleRate, int numChannels, Mode mode);
    void setMode(Mode m);                 // real-time safe; clears the new mode's state
    Mode mode() const { return current; }
    int latencySamples() const { return latencyForMode(current); }
    void reset();
    // In place. channels = numChannels pointers of `count` samples each.
    void process(float* const* channels, int count);

private:
    Mode current = Mode::Mix;
    int numCh = 0;
    double sr = 48000.0;
    // [mode][channel]; Live has no STFT yet
    std::vector<std::unique_ptr<Stft>> stft[(int) Mode::Count];
};

// Fixed-length delay used for the latency-compensated dry path.
class DelayLine {
public:
    void prepare(int maxDelay, int numChannels);
    void setDelay(int samples) { delay = samples < 0 ? 0 : (samples > maxDelay ? maxDelay : samples); }
    int getDelay() const { return delay; }
    void reset();
    // Write x, return the sample written `delay` samples ago.
    float process(int channel, float x);
    void advance() { pos = (pos + 1) % size; }   // call once per sample after all channels
private:
    std::vector<std::vector<float>> buf;
    int size = 1, pos = 0, delay = 0, maxDelay = 0;
};

} // namespace chiller::dsp
