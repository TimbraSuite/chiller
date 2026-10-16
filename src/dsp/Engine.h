#pragma once
// Stereo suppressor engine. One Suppressor per mode is allocated in prepare(), so switching modes is
// real-time safe. The audio path is delayed by latencySamples() in every mode.
#include "Mode.h"
#include "Suppressor.h"
#include "Telemetry.h"
#include <memory>
#include <vector>

namespace chiller::dsp {

class Engine {
public:
    void prepare(double sampleRate, Mode mode);
    void setMode(Mode m);                 // real-time safe; clears the new mode's state
    Mode mode() const { return current; }
    int latencySamples() const { return suppressors[(size_t) current] ? suppressors[(size_t) current]->latencySamples() : 0; }
    double sampleRate() const { return sr; }
    void reset();
    void setParams(const SuppressorParams& p) { if (auto* s = active()) s->setParams(p); lastParams = p; }
    // In place on two channels. scL/scR: optional sidechain (null = none).
    void process(float* l, float* r, const float* scL, const float* scR, int count);
    // UI side (message thread only): newest published frame.
    void readUi(UiFrame& out) { ui.read(out); }

private:
    Suppressor* active() const { return suppressors[(size_t) current].get(); }
    Mode current = Mode::Mix;
    double sr = 48000.0;
    SuppressorParams lastParams;
    TripleBuffer<UiFrame> ui;
    std::unique_ptr<Suppressor> suppressors[(size_t) Mode::Count];
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
