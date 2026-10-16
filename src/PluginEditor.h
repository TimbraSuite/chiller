#pragma once
#include "PluginProcessor.h"
#include "ui/Panels.h"

class ChillerEditor : public juce::AudioProcessorEditor, private juce::Timer {
public:
    explicit ChillerEditor(ChillerProcessor&);
    ~ChillerEditor() override;
    void paint(juce::Graphics&) override;
    void resized() override;

    // Design canvas; the whole UI is drawn at this size and scaled by one transform.
    static constexpr int kW = 1100, kH = 680;

private:
    void timerCallback() override;

    struct Content : juce::Component {
        void paint(juce::Graphics& g) override { g.fillAll(chui::tok::bg); }
    };

    ChillerProcessor& proc;
    chui::LookAndFeel laf;
    Content content;
    std::unique_ptr<chui::TopBar> top;
    std::unique_ptr<chui::SpectrumView> explain;
    std::unique_ptr<chui::HarshnessStrip> harshness;
    std::unique_ptr<chui::Panel> detect, speed, protect, stereo, output;
    juce::TooltipWindow tooltip { this, 700 };
    juce::ComponentBoundsConstrainer constrainer;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ChillerEditor)
};
