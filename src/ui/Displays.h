#pragma once
#include "Controls.h"
#include <array>

namespace chui {

// The Explain view: live spectrum of the plugin input (ice-white line) on a log-frequency grid.
// Later versions overlay the detected resonances and the applied reduction on top of it.
class SpectrumView : public juce::Component, public Tickable {
public:
    explicit SpectrumView(ChillerProcessor& p);
    void paint(juce::Graphics&) override;
    void tick() override;
    static constexpr float kMinHz = 20.f, kMaxHz = 20000.f, kMinDb = -90.f, kMaxDb = 0.f;
private:
    float xForHz(juce::Rectangle<float> in, float hz) const;
    ChillerProcessor& proc;
    static constexpr int kOrder = 12, kSize = 1 << kOrder;   // 4096-point analysis
    static constexpr int kBars = 220;
    juce::dsp::FFT fft { kOrder };
    std::array<float, kSize> buf {};
    std::array<float, 2 * kSize> fftData {};
    std::array<float, kSize> window {};
    std::array<float, kBars> bars {};   // 0..1 within kMinDb..kMaxDb
};

class Meter : public juce::Component, public juce::SettableTooltipClient, public Tickable {
public:
    explicit Meter(ChillerProcessor& p) : proc(p) { setTooltip("Output sample peak (1.5 s hold). CLIP latches at 0 dBFS; click to reset."); }
    void paint(juce::Graphics&) override;
    void tick() override;
    void mouseDown(const juce::MouseEvent&) override;
private:
    ChillerProcessor& proc;
    float l = 0.f, r = 0.f, holdPeak = 0.f, lastShown = 0.f;
    juce::uint32 holdTime = 0;
    bool clipped = false;
};

} // namespace chui
