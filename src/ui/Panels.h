#pragma once
#include "Controls.h"
#include "Displays.h"
#include <functional>
#include <memory>
#include <vector>

namespace chui {

// A titled group of controls. Default layout is a centred grid with `cols` columns; panels with
// special content (stereo, output) supply their own layout function.
class Panel : public juce::Component {
public:
    explicit Panel(juce::String title, int columns = 3) : title(std::move(title)), cols(columns) {}
    void paint(juce::Graphics& g) override { paintPanel(g, getLocalBounds(), title); }
    void resized() override;

    template <typename T> T* add(std::unique_ptr<T> c, bool inGrid = true)
    {
        auto* raw = c.get();
        addAndMakeVisible(raw);
        if (inGrid) cells.push_back(raw);
        owned.push_back(std::move(c));
        return raw;
    }
    std::function<void(juce::Rectangle<int> body)> customLayout;
    juce::Rectangle<int> bodyArea() const { return getLocalBounds().withTrimmedTop(24).reduced(6, 4); }

private:
    juce::String title;
    int cols;
    std::vector<juce::Component*> cells;
    std::vector<std::unique_ptr<juce::Component>> owned;
};

// Right strip next to the Explain view: reserved for the harshness meter (v0.3).
class HarshnessStrip : public juce::Component {
public:
    void paint(juce::Graphics&) override;
};

class TopBar : public juce::Component, public Tickable {
public:
    explicit TopBar(ChillerProcessor&);
    void paint(juce::Graphics&) override;
    void resized() override;
    void tick() override;
private:
    ChillerProcessor& proc;
    juce::TextButton preset { "Init" }, abA { "A" }, abB { "B" }, undo { "Undo" }, redo { "Redo" }, learn { "LEARN" };
    std::unique_ptr<ParamCombo> mode;
    std::unique_ptr<ParamToggle> bypass;
    std::unique_ptr<Meter> meter;
    juce::String latencyText;
    juce::Rectangle<int> wordmarkArea, modeLabelArea, latencyArea;
};

std::unique_ptr<Panel> makeDetectPanel(ChillerProcessor&);
std::unique_ptr<Panel> makeSpeedPanel(ChillerProcessor&);
std::unique_ptr<Panel> makeProtectPanel(ChillerProcessor&);
std::unique_ptr<Panel> makeStereoPanel(ChillerProcessor&);
std::unique_ptr<Panel> makeOutputPanel(ChillerProcessor&);

} // namespace chui
