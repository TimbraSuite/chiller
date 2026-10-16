#pragma once
#include "Controls.h"
#include "Displays.h"
#include "dsp/Learn.h"
#include <functional>
#include <memory>
#include <vector>

namespace chui {

// A titled group of controls with a custom layout function.
class Panel : public juce::Component {
public:
    explicit Panel(juce::String t) : title(std::move(t)) {}
    void paint(juce::Graphics& g) override { paintPanel(g, getLocalBounds(), title); }
    void resized() override { if (layout) layout(getLocalBounds().withTrimmedTop(28).reduced(8, 0)); }

    template <typename T> T* add(std::unique_ptr<T> c)
    {
        auto* raw = c.get();
        addAndMakeVisible(raw);
        owned.push_back(std::move(c));
        return raw;
    }
    std::function<void(juce::Rectangle<int> body)> layout;

private:
    juce::String title;
    std::vector<std::unique_ptr<juce::Component>> owned;
};

class LearnButton : public juce::TextButton {
public:
    LearnButton() : juce::TextButton("LEARN") { setStyle(*this, "primary"); }
    float progress = -1.f;
    void paintOverChildren(juce::Graphics&) override;
};

class TopBar : public juce::Component, public Tickable {
public:
    TopBar(ChillerProcessor&, UiState&);
    void paint(juce::Graphics&) override;
    void resized() override;
    void tick() override;
private:
    void startLearn();
    void finishLearn();
    void syncPresetCombo();

    ChillerProcessor& proc;
    UiState& st;
    IconButton prev { IconButton::Prev }, next { IconButton::Next }, undo { IconButton::Undo }, redo { IconButton::Redo };
    juce::ComboBox preset;
    juce::TextButton abA { "A" }, abB { "B" }, abCopy { "A>B" };
    LearnButton learn;
    std::unique_ptr<ParamCombo> mode;
    std::unique_ptr<ParamButton> bypass;
    std::unique_ptr<Meter> meter;
    juce::String latencyText, cpuText;
    juce::Rectangle<int> logoArea, latencyArea, cpuArea;
    std::vector<int> separators;

    bool learning = false;
    juce::uint32 learnStart = 0;
    unsigned long long lastLearnFrame = 0;
    chiller::dsp::LearnAccumulator learnAcc;
};

std::unique_ptr<Panel> makeDetectPanel(ChillerProcessor&);
std::unique_ptr<Panel> makeSpeedPanel(ChillerProcessor&);
std::unique_ptr<Panel> makeProtectPanel(ChillerProcessor&);
std::unique_ptr<Panel> makeStereoPanel(ChillerProcessor&);
std::unique_ptr<Panel> makeOutputPanel(ChillerProcessor&);

} // namespace chui
