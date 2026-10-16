#include "PluginEditor.h"

ChillerEditor::ChillerEditor(ChillerProcessor& p) : AudioProcessorEditor(&p), proc(p)
{
    juce::LookAndFeel::setDefaultLookAndFeel(&laf);
    setLookAndFeel(&laf);

    addAndMakeVisible(content);
    top = std::make_unique<chui::TopBar>(p);
    explain = std::make_unique<chui::SpectrumView>(p);
    harshness = std::make_unique<chui::HarshnessStrip>();
    detect = chui::makeDetectPanel(p);
    speed = chui::makeSpeedPanel(p);
    protect = chui::makeProtectPanel(p);
    stereo = chui::makeStereoPanel(p);
    output = chui::makeOutputPanel(p);
    for (juce::Component* c : { (juce::Component*) top.get(), (juce::Component*) explain.get(), (juce::Component*) harshness.get(),
                                (juce::Component*) detect.get(), (juce::Component*) speed.get(), (juce::Component*) protect.get(),
                                (juce::Component*) stereo.get(), (juce::Component*) output.get() })
        content.addAndMakeVisible(c);

    // fixed design layout, scaled as a whole
    top->setBounds(8, 6, 1084, 46);
    explain->setBounds(8, 58, 860, 380);
    harshness->setBounds(876, 58, 216, 380);
    detect->setBounds(8, 446, 300, 226);
    speed->setBounds(316, 446, 220, 226);
    protect->setBounds(544, 446, 220, 226);
    stereo->setBounds(772, 446, 120, 226);
    output->setBounds(900, 446, 192, 226);

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

void ChillerEditor::timerCallback() { chui::Tickable::tickAll(); }
