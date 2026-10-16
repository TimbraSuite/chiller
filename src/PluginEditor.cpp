#include "PluginEditor.h"

ChillerEditor::ChillerEditor(ChillerProcessor& p) : AudioProcessorEditor(&p), proc(p)
{
    juce::LookAndFeel::setDefaultLookAndFeel(&laf);
    setLookAndFeel(&laf);

    addAndMakeVisible(content);
    top = std::make_unique<chui::TopBar>(p, state);
    explain = std::make_unique<chui::SpectrumView>(p, state);
    harshness = std::make_unique<chui::HarshnessStrip>(p, state);
    detect = chui::makeDetectPanel(p);
    speed = chui::makeSpeedPanel(p);
    protect = chui::makeProtectPanel(p);
    stereo = chui::makeStereoPanel(p);
    output = chui::makeOutputPanel(p);
    for (juce::Component* c : { (juce::Component*) top.get(), (juce::Component*) explain.get(), (juce::Component*) harshness.get(),
                                (juce::Component*) detect.get(), (juce::Component*) speed.get(), (juce::Component*) protect.get(),
                                (juce::Component*) stereo.get(), (juce::Component*) output.get() })
        content.addAndMakeVisible(c);

    // fixed design layout (same geometry as the HTML mock-up)
    top->setBounds(8, 8, 1084, 46);
    explain->setBounds(8, 62, 880, 452);
    harshness->setBounds(896, 62, 196, 452);
    // bottom zone: 5 panels, weights 3:3:4:2:4, 8 px gaps (1084 px wide in total)
    {
        const int total = 1084 - 4 * 8, unitsX16[5] = { 3, 3, 4, 2, 4 };
        chui::Panel* panels[5] = { detect.get(), speed.get(), protect.get(), stereo.get(), output.get() };
        float x = 8.f;
        for (int i = 0; i < 5; ++i) {
            const float w = (float) total * (float) unitsX16[i] / 16.f;
            panels[i]->setBounds(juce::Rectangle<float>(x, 522.f, w, 150.f).toNearestInt());
            x += w + 8.f;
        }
    }

    constrainer.setFixedAspectRatio((double) kW / (double) kH);
    constrainer.setSizeLimits(kW / 2, kH / 2, kW * 2, kH * 2);   // 550x340 .. 2200x1360
    setConstrainer(&constrainer);
    setResizable(true, true);
    setSize(kW, kH);
    startTimerHz(30);
}

ChillerEditor::~ChillerEditor()
{
    stopTimer();
    setLookAndFeel(nullptr);
    if (&juce::LookAndFeel::getDefaultLookAndFeel() == &laf) juce::LookAndFeel::setDefaultLookAndFeel(nullptr);
}

void ChillerEditor::paint(juce::Graphics& g) { g.fillAll(chui::tok::bg); }

void ChillerEditor::resized()
{
    const float s = (float) getWidth() / (float) kW;
    content.setTransform(juce::AffineTransform::scale(s));
    content.setBounds(0, 0, kW, kH);
}

void ChillerEditor::tickNow()
{
    proc.readUi(state.frame);
    state.haveFrame = state.frame.frameCounter > 0;
    chui::Tickable::tickAll();
}
