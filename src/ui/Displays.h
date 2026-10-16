#pragma once
#include "Controls.h"
#include <array>

namespace chui {

// Small tab used in the Explain view header.
class TabButton : public juce::Button {
public:
    explicit TabButton(const juce::String& t) : juce::Button(t) { setClickingTogglesState(false); }
    void paintButton(juce::Graphics&, bool over, bool down) override;
};

// The Explain view: SPECTRUM (input, expected curve, applied cut hanging from 0 dB, labelled peaks,
// protected harmonics, sidechain key, range nodes) and HISTORY (scrolling heat map of the reduction).
class SpectrumView : public juce::Component, public Tickable {
public:
    SpectrumView(ChillerProcessor& p, UiState& s);
    void paint(juce::Graphics&) override;
    void resized() override;
    void tick() override;
    void mouseMove(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override { hoverX = -1.f; hoverNode = 0; repaint(); }
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;

    void showHistory(bool on) { setView(on ? View::History : View::Spectrum); }   // also used by tools/uishot
    static constexpr int W = 880, H = 452;
    static constexpr float kMinHz = 20.f, kMaxHz = 20000.f;

private:
    enum class View { Spectrum, History };
    void setView(View v);
    void paintSpectrum(juce::Graphics&);
    void paintHistory(juce::Graphics&);
    void paintGridV(juce::Graphics&, bool labels);
    void paintOverlays(juce::Graphics&);
    float xOfHz(float hz) const;
    float hzOfX(float x) const;
    float yOfLevel(float db) const;
    float nodeX(int node) const;
    int hitNode(juce::Point<float> p) const;
    void pushHistoryColumn();

    ChillerProcessor& proc;
    UiState& st;
    TabButton specTab { "SPECTRUM" }, histTab { "HISTORY" };
    std::unique_ptr<ParamToggle> keyToggle;
    View view = View::Spectrum;

    static constexpr int N = chiller::dsp::kUiPoints;
    std::array<float, N> inS {}, expS {}, keyS {}, grS {};
    bool primed = false;
    float f0Conf = 0.f;

    static constexpr int kHistCols = 300;
    juce::Image heat;
    int histDivider = 0;

    float hoverX = -1.f;
    int hoverNode = 0, dragNode = 0;   // 1 = low cut, 2 = high cut
};

// Right-hand strip: harshness in/out, average reduction vs budget, legend.
class HarshnessStrip : public juce::Component, public Tickable {
public:
    HarshnessStrip(ChillerProcessor& p, UiState& s) : proc(p), st(s) {}
    void paint(juce::Graphics&) override;
    void tick() override;
private:
    ChillerProcessor& proc;
    UiState& st;
    float hin = 0.f, hout = 0.f, avg = 0.f;
};

class Meter : public juce::Component, public juce::SettableTooltipClient, public Tickable {
public:
    explicit Meter(ChillerProcessor& p) : proc(p) { setTooltip("Output peak level (1.5 s hold). The right end turns red above 0 dBFS; click to reset."); }
    void paint(juce::Graphics&) override;
    void tick() override;
    void mouseDown(const juce::MouseEvent&) override;
private:
    ChillerProcessor& proc;
    float l = 0.f, r = 0.f, holdPeak = 0.f;
    juce::uint32 holdTime = 0;
    bool clipped = false;
};

// "G#4" for a frequency
juce::String noteName(float hz);
// "3.1k", "404"
juce::String shortHz(float hz);

} // namespace chui
