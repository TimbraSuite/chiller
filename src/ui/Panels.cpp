#include "Panels.h"

using namespace chiller;

namespace chui {

//==============================================================================================
void Panel::resized()
{
    const auto body = bodyArea();
    if (customLayout) { customLayout(body); return; }
    if (cells.empty()) return;
    const int rows = ((int) cells.size() + cols - 1) / cols;
    const int cw = body.getWidth() / cols, ch = std::min(body.getHeight() / rows, 100);
    const int x0 = body.getX() + (body.getWidth() - cw * cols) / 2;
    for (size_t i = 0; i < cells.size(); ++i) {
        const int cx = (int) i % cols, cy = (int) i / cols;
        cells[i]->setBounds(x0 + cx * cw, body.getY() + cy * ch, cw, ch);
    }
}

//==============================================================================================
void HarshnessStrip::paint(juce::Graphics& g)
{
    auto r = getLocalBounds();
    paintPanel(g, r, "HARSHNESS");
    auto body = r.withTrimmedTop(26).reduced(14, 10);
    auto bar = body.removeFromLeft(34).toFloat();
    g.setColour(tok::well);
    g.fillRoundedRectangle(bar, tok::rControl);
    g.setColour(tok::edgeSoft);
    g.drawRoundedRectangle(bar.reduced(0.5f), tok::rControl, 1.f);
    g.setFont(fMeta());
    for (int i = 0; i <= 4; ++i) {   // scale ticks (placeholder scale, 0 .. 100)
        const float y = bar.getY() + 6.f + (bar.getHeight() - 12.f) * (float) i / 4.f;
        g.setColour(tok::edgeSoft);
        g.drawHorizontalLine((int) y, bar.getX() + 4.f, bar.getRight() - 4.f);
        g.setColour(tok::textDisabled);
        g.drawText(juce::String(100 - i * 25), (int) bar.getRight() + 6, (int) y - 6, 30, 12, juce::Justification::left);
    }
    body.removeFromLeft(40);
    g.setColour(tok::textSecondary);
    g.setFont(fLabel());
    g.drawFittedText("Per-band harshness and\nreduction history.\n\nComing in v0.3.", body.withTrimmedTop(110), juce::Justification::topLeft, 6, 0.9f);
}

//==============================================================================================
TopBar::TopBar(ChillerProcessor& p) : proc(p)
{
    for (auto* b : { &preset, &abA, &abB, &undo, &redo }) { addAndMakeVisible(b); setStyle(*b, "ghost"); }
    preset.setTooltip("Preset browser: coming in v0.2");
    abA.setTooltip("A/B compare: coming in v0.2");
    abB.setTooltip("A/B compare: coming in v0.2");
    undo.setTooltip("Undo: coming in v0.2");
    redo.setTooltip("Redo: coming in v0.2");
    for (auto* b : { &preset, &abA, &abB, &undo, &redo }) b->setEnabled(false);

    addAndMakeVisible(learn);
    setStyle(learn, "primary");
    learn.setEnabled(false);
    learn.setTooltip("Learn: coming in v0.4");

    mode = std::make_unique<ParamCombo>(p, pid::mode);
    bypass = std::make_unique<ParamToggle>(p, pid::bypass, "Bypass");
    meter = std::make_unique<Meter>(p);
    addAndMakeVisible(*mode);
    addAndMakeVisible(*bypass);
    addAndMakeVisible(*meter);
    tick();
}

void TopBar::resized()
{
    const int h = getHeight(), by = (h - 28) / 2;
    wordmarkArea = { 14, 0, 110, h };
    preset.setBounds(128, by, 190, 28);
    abA.setBounds(326, by, 28, 28);
    abB.setBounds(358, by, 28, 28);
    undo.setBounds(396, by, 48, 28);
    redo.setBounds(448, by, 48, 28);
    learn.setBounds(508, by, 68, 28);
    modeLabelArea = { 636, 0, 40, h };
    mode->setBounds(678, by, 84, 28);
    latencyArea = { 770, 0, 96, h };
    bypass->setBounds(874, by, 84, 28);
    meter->setBounds(getWidth() - 12 - 114, 6, 114, h - 12);
}

void TopBar::paint(juce::Graphics& g)
{
    paintPanel(g, getLocalBounds());
    g.setColour(tok::textPrimary);
    g.setFont(fLogo());
    g.drawText("CHILLER", wordmarkArea, juce::Justification::centredLeft);
    g.setColour(tok::textSecondary);
    g.setFont(fBadge());
    g.drawText("MODE", modeLabelArea, juce::Justification::centredRight);
    g.setColour(tok::textLabel);
    g.setFont(fValue());
    g.drawText(latencyText, latencyArea, juce::Justification::centredLeft);
}

void TopBar::tick()
{
    // latency for the selected mode (what the plugin reports to the host)
    const int m = std::clamp((int) std::lround(proc.getReal(pid::mode)), 0, (int) dsp::Mode::Count - 1);
    const double sr = proc.currentSampleRate > 0.0 ? proc.currentSampleRate : 48000.0;
    const double ms = 1000.0 * dsp::latencyForMode((dsp::Mode) m) / sr;
    const juce::String t = "Latency " + juce::String(ms, 1) + " ms";
    if (t != latencyText) { latencyText = t; repaint(latencyArea); }
}

//==============================================================================================
static Knob* knob(Panel& pn, ChillerProcessor& p, int id, const char* label = nullptr, juce::Colour c = tok::accent)
{
    auto* k = pn.add(std::make_unique<Knob>(p, id, label ? juce::String(label) : juce::String()));
    k->setAccent(c);
    return k;
}

std::unique_ptr<Panel> makeDetectPanel(ChillerProcessor& p)
{
    auto pn = std::make_unique<Panel>("DETECT", 3);
    knob(*pn, p, pid::threshold);
    knob(*pn, p, pid::amount);
    knob(*pn, p, pid::focus);
    knob(*pn, p, pid::maxCut);
    knob(*pn, p, pid::rangeLow);
    knob(*pn, p, pid::rangeHigh);
    return pn;
}

std::unique_ptr<Panel> makeSpeedPanel(ChillerProcessor& p)
{
    auto pn = std::make_unique<Panel>("SPEED", 2);
    knob(*pn, p, pid::attack);
    knob(*pn, p, pid::release);
    knob(*pn, p, pid::speedTilt);
    return pn;
}

std::unique_ptr<Panel> makeProtectPanel(ChillerProcessor& p)
{
    auto pn = std::make_unique<Panel>("PROTECT", 2);
    knob(*pn, p, pid::transientPass, "Transient Pass", tok::protectPurple);
    knob(*pn, p, pid::harmonicProtect, "Harmonic Protect", tok::protectPurple);
    knob(*pn, p, pid::budget, nullptr, tok::protectPurple);
    return pn;
}

std::unique_ptr<Panel> makeStereoPanel(ChillerProcessor& p)
{
    auto pn = std::make_unique<Panel>("STEREO", 1);
    auto* combo = pn->add(std::make_unique<ParamCombo>(p, pid::stereoMode), false);
    auto* link = knob(*pn, p, pid::link);
    pn->customLayout = [combo, link](juce::Rectangle<int> body) {
        combo->setBounds(body.removeFromTop(28).reduced(4, 0));
        body.removeFromTop(14);
        link->setBounds(body.removeFromTop(100));
    };
    return pn;
}

std::unique_ptr<Panel> makeOutputPanel(ChillerProcessor& p)
{
    auto pn = std::make_unique<Panel>("OUTPUT", 2);
    auto* mix = knob(*pn, p, pid::mix);
    auto* trim = knob(*pn, p, pid::trim);
    auto* out = knob(*pn, p, pid::output);
    auto* delta = pn->add(std::make_unique<ParamToggle>(p, pid::delta, "Delta"), false);
    auto* autoGain = pn->add(std::make_unique<ParamToggle>(p, pid::autoGain, "Auto Gain"), false);
    delta->setTooltip("Delta: listen to what the suppressor removes (dry minus wet)");
    autoGain->setTooltip("Auto Gain: level-match the output (active from v0.2)");
    pn->customLayout = [=](juce::Rectangle<int> body) {
        const int cw = body.getWidth() / 2, ch = std::min(body.getHeight() / 2, 100);
        mix->setBounds(body.getX(), body.getY(), cw, ch);
        trim->setBounds(body.getX() + cw, body.getY(), cw, ch);
        out->setBounds(body.getX(), body.getY() + ch, cw, ch);
        auto cell = juce::Rectangle<int>(body.getX() + cw, body.getY() + ch, cw, ch).reduced(4, 14);
        delta->setBounds(cell.removeFromTop(26));
        cell.removeFromTop(6);
        autoGain->setBounds(cell.removeFromTop(26));
    };
    return pn;
}

} // namespace chui
