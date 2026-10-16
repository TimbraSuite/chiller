#include "Panels.h"
#include <algorithm>

using namespace chiller;

namespace chui {

static constexpr int kKnobW = 58, kKnobH = 70;

//==============================================================================================
void LearnButton::paintOverChildren(juce::Graphics& g)
{
    if (progress < 0.f) return;
    auto r = getLocalBounds().toFloat().reduced(0.5f);
    g.setColour(juce::Colours::black.withAlpha(0.28f));
    g.fillRoundedRectangle(r.withWidth(r.getWidth() * juce::jlimit(0.f, 1.f, progress)), tok::rControl);
}

//==============================================================================================
TopBar::TopBar(ChillerProcessor& p, UiState& s) : proc(p), st(s)
{
    for (auto* c : std::initializer_list<juce::Component*> { &prev, &next, &preset, &abA, &abB, &abCopy, &undo, &redo, &learn })
        addAndMakeVisible(*c);
    prev.setTooltip("Previous preset");
    next.setTooltip("Next preset");
    int id = 1;
    for (const auto& pr : factoryPresets()) preset.addItem(pr.name, id++);
    preset.setTooltip("Factory presets (they set the detection, speed and protection controls; output and mode stay as they are)");
    preset.setJustificationType(juce::Justification::centredLeft);
    preset.onChange = [this] {
        const int i = preset.getSelectedItemIndex();
        if (i >= 0) proc.loadPreset(i);
    };
    prev.onClick = [this] { proc.loadPreset(proc.presetIndex - 1); syncPresetCombo(); };
    next.onClick = [this] { proc.loadPreset(proc.presetIndex + 1); syncPresetCombo(); };

    for (auto* b : { &abA, &abB }) setStyle(*b, "slot");
    setStyle(abCopy, "ghost");
    abA.setClickingTogglesState(false);
    abB.setClickingTogglesState(false);
    abA.setTooltip("Compare slot A");
    abB.setTooltip("Compare slot B");
    abCopy.setTooltip("Copy the active slot to the other one");
    abA.onClick = [this] { proc.selectAB(0); };
    abB.onClick = [this] { proc.selectAB(1); };
    abCopy.onClick = [this] {
        proc.copyToOtherAB();
        st.showToast(proc.abActive() == 0 ? "Copied A to B" : "Copied B to A");
    };

    undo.setTooltip("Undo the last change");
    redo.setTooltip("Redo");
    undo.onClick = [this] { proc.undo(); };
    redo.onClick = [this] { proc.redo(); };

    learn.setTooltip("Listen to 3 seconds of audio and set Threshold and Amount for you");
    learn.onClick = [this] { if (!learning) startLearn(); };

    mode = std::make_unique<ParamCombo>(p, pid::mode);
    mode->setTooltip("Live (low latency): ~5 ms. Mix: ~21 ms. Master: ~85 ms, finest resolution.");
    bypass = std::make_unique<ParamButton>(p, pid::bypass, "BYPASS", "warn");
    bypass->setTooltip("Bypass the whole plugin (keeps the latency)");
    meter = std::make_unique<Meter>(p);
    addAndMakeVisible(*mode);
    addAndMakeVisible(*bypass);
    addAndMakeVisible(*meter);
    syncPresetCombo();
    tick();
}

void TopBar::syncPresetCombo() { preset.setSelectedItemIndex(proc.presetIndex, juce::dontSendNotification); }

void TopBar::resized()
{
    const int h = getHeight(), by = (h - 26) / 2;
    int x = 10;
    separators.clear();
    auto place = [&](juce::Component& c, int w, int gap = 4) { c.setBounds(x, by, w, 26); x += w + gap; };
    auto sep = [&] { separators.push_back(x + 3); x += 12; };
    logoArea = { x, 0, 124, h };
    x += 124;
    sep();
    place(prev, 28); place(preset, 150); place(next, 28, 0);
    sep();
    place(abA, 28); place(abB, 28); place(abCopy, 40, 0);
    sep();
    place(undo, 28); place(redo, 28, 8);
    place(learn, 74, 0);
    sep();
    place(*mode, 128, 6);
    latencyArea = { x, 0, 50, h };
    x += 52;
    cpuArea = { x, 0, 66, h };
    x += 68;
    place(*bypass, 64, 8);
    const int mw = getWidth() - 10 - x;
    meter->setBounds(x, 8, std::max(40, mw), h - 16);
}

void TopBar::paint(juce::Graphics& g)
{
    using namespace tok;
    paintPanel(g, getLocalBounds());
    // logo: glyph + wordmark
    juce::Path glyph;
    glyph.startNewSubPath(1.f, 3.f); glyph.lineTo(7.f, 3.f); glyph.lineTo(11.f, 13.f); glyph.lineTo(15.f, 3.f); glyph.lineTo(21.f, 3.f);
    glyph.applyTransform(juce::AffineTransform::translation((float) logoArea.getX(), (float) (getHeight() - 16) * 0.5f));
    g.setColour(accent);
    g.strokePath(glyph, juce::PathStrokeType(2.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour(textPrimary);
    g.setFont(juce::Font(juce::FontOptions(typeface(Weight::Bold)).withHeight(17.f).withKerningFactor(0.16f)));
    g.drawText("CHILLER", logoArea.withTrimmedLeft(30), juce::Justification::centredLeft);
    g.setColour(edge);
    for (int sx : separators) g.fillRect((float) sx, (float) (getHeight() - 24) * 0.5f, 1.f, 24.f);
    g.setFont(fValue());
    g.setColour(textSecondary);
    g.drawText(latencyText, latencyArea, juce::Justification::centredLeft);
    g.drawText(cpuText, cpuArea, juce::Justification::centredLeft);
}

void TopBar::tick()
{
    // latency of the selected mode (what the plugin reports to the host)
    const int m = std::clamp((int) std::lround(proc.getReal(pid::mode)), 0, (int) dsp::Mode::Count - 1);
    const double sr = proc.currentSampleRate > 0.0 ? proc.currentSampleRate : 48000.0;
    const double ms = 1000.0 * dsp::latencyForMode((dsp::Mode) m, sr) / sr;
    const juce::String lt = juce::String(ms, 1) + " ms";
    const juce::String ct = "CPU " + juce::String(proc.cpuLoad.load(std::memory_order_relaxed) * 100.f, 1) + "%";
    if (lt != latencyText || ct != cpuText) { latencyText = lt; cpuText = ct; repaint(latencyArea.getUnion(cpuArea)); }

    abA.setToggleState(proc.abActive() == 0, juce::dontSendNotification);
    abB.setToggleState(proc.abActive() == 1, juce::dontSendNotification);
    abCopy.setButtonText(proc.abActive() == 0 ? "A>B" : "B>A");
    undo.setEnabled(proc.canUndo());
    redo.setEnabled(proc.canRedo());
    if (!preset.isPopupActive()) syncPresetCombo();

    if (learning) {
        const float u = (float) (juce::Time::getMillisecondCounter() - learnStart) / 3000.f;
        st.learnProgress = juce::jlimit(0.f, 1.f, u);
        learn.progress = st.learnProgress;
        if (st.haveFrame && st.frame.frameCounter != lastLearnFrame) {
            lastLearnFrame = st.frame.frameCounter;
            const float thr = proc.getReal(pid::threshold);
            for (int i = 0; i < dsp::kUiPoints; ++i) {
                const float ex = st.frame.inDb[i] - (st.frame.expDb[i] - thr);
                learnExcess[(size_t) i] += std::max(0.f, ex);
                learnLevel[(size_t) i] += st.frame.inDb[i];
            }
            ++learnSamples;
        }
        learn.repaint();
        if (u >= 1.f) finishLearn();
    }
}

void TopBar::startLearn()
{
    if (!st.haveFrame) { st.showToast("No audio yet. Play something through Chiller, then press LEARN"); return; }
    learning = true;
    learnStart = juce::Time::getMillisecondCounter();
    learnSamples = 0;
    lastLearnFrame = st.frame.frameCounter;
    learnExcess.assign(dsp::kUiPoints, 0.f);
    learnLevel.assign(dsp::kUiPoints, 0.f);
    learn.setButtonText("LISTENING");
    st.learnProgress = 0.f;
}

void TopBar::finishLearn()
{
    learning = false;
    st.learnProgress = -1.f;
    learn.progress = -1.f;
    learn.setButtonText("LEARN");
    // mean excess above the expected curve per display point, restricted to the active range and to points with signal
    const float lo = proc.getReal(pid::rangeLow), hi = proc.getReal(pid::rangeHigh);
    std::vector<float> ex;
    for (int i = 0; i < dsp::kUiPoints; ++i) {
        const float hz = dsp::uiPointHz(i);
        if (hz < lo || hz > hi || learnSamples == 0) continue;
        if (learnLevel[(size_t) i] / (float) learnSamples < -100.f) continue;
        ex.push_back(learnExcess[(size_t) i] / (float) learnSamples);
    }
    if (ex.size() < 20) { st.showToast("Nothing heard. Play audio through Chiller and try again"); return; }
    std::sort(ex.begin(), ex.end(), std::greater<float>());
    const float p = ex[std::min(ex.size() - 1, (size_t) std::ceil(0.06 * (double) ex.size()))];   // top ~6 % of the spectrum
    const float thr = juce::jlimit(4.f, 16.f, p + 1.f);
    proc.setParamReal(pid::threshold, thr);
    proc.setParamReal(pid::amount, 60.f);
    proc.historyCommit();
    st.showToast("Learned: Threshold " + juce::String(thr, 1) + " dB, Amount 60 %");
}

//==============================================================================================
static Knob* knob(Panel& pn, ChillerProcessor& p, int id, const char* label = nullptr, juce::Colour c = tok::accent)
{
    auto* k = pn.add(std::make_unique<Knob>(p, id, label ? juce::String(label) : juce::String()));
    k->setAccent(c);
    return k;
}

// Knobs left-to-right at a fixed pitch, centred in the available width.
static void rowLayout(juce::Rectangle<int> body, std::vector<juce::Component*> comps, int cell = kKnobW)
{
    const int total = cell * (int) comps.size();
    int x = body.getX() + (body.getWidth() - total) / 2;
    for (auto* c : comps) { c->setBounds(x, body.getY() + 2, cell, kKnobH); x += cell; }
}

std::unique_ptr<Panel> makeDetectPanel(ChillerProcessor& p)
{
    auto pn = std::make_unique<Panel>("DETECT");
    std::vector<juce::Component*> ks { knob(*pn, p, pid::threshold), knob(*pn, p, pid::amount), knob(*pn, p, pid::focus) };
    pn->layout = [ks](juce::Rectangle<int> b) { rowLayout(b, ks); };
    return pn;
}

std::unique_ptr<Panel> makeSpeedPanel(ChillerProcessor& p)
{
    auto pn = std::make_unique<Panel>("SPEED");
    std::vector<juce::Component*> ks { knob(*pn, p, pid::attack), knob(*pn, p, pid::release), knob(*pn, p, pid::speedTilt, "Tilt") };
    pn->layout = [ks](juce::Rectangle<int> b) { rowLayout(b, ks); };
    return pn;
}

std::unique_ptr<Panel> makeProtectPanel(ChillerProcessor& p)
{
    auto pn = std::make_unique<Panel>("PROTECT");
    std::vector<juce::Component*> ks {
        knob(*pn, p, pid::harmonicProtect, "Harmonics", tok::protectPurple), knob(*pn, p, pid::transientPass, "Transients", tok::protectPurple),
        knob(*pn, p, pid::maxCut), knob(*pn, p, pid::budget) };
    pn->layout = [ks](juce::Rectangle<int> b) { rowLayout(b, ks); };
    return pn;
}

std::unique_ptr<Panel> makeStereoPanel(ChillerProcessor& p)
{
    auto pn = std::make_unique<Panel>("STEREO");
    auto* tg = pn->add(std::make_unique<ParamToggle>(p, pid::stereoMode, "L/R"));
    tg->setStateTexts("L/R (left / right)", "M/S (mid / side)");
    tg->setTooltip("Process left/right or mid/side");
    auto* link = knob(*pn, p, pid::link);
    link->setTooltip("Link\nHow much both channels share the same cut. 100 % keeps the stereo image.");
    pn->layout = [tg, link](juce::Rectangle<int> b) {
        tg->setBounds(b.getX() + 2, b.getY() + 2, b.getWidth() - 4, 20);
        link->setBounds(b.getCentreX() - kKnobW / 2, b.getY() + 28, kKnobW, kKnobH);
    };
    return pn;
}

std::unique_ptr<Panel> makeOutputPanel(ChillerProcessor& p)
{
    auto pn = std::make_unique<Panel>("OUTPUT");
    auto* mix = knob(*pn, p, pid::mix);
    auto* trim = knob(*pn, p, pid::trim);
    auto* out = knob(*pn, p, pid::output);
    trim->setTooltip("Trim\nLevel of the processed signal before the mix. Use it to match loudness by ear.");
    auto* delta = pn->add(std::make_unique<ParamToggle>(p, pid::delta, "Delta"));
    auto* autoGain = pn->add(std::make_unique<ParamToggle>(p, pid::autoGain, "Auto-gain"));
    delta->setTooltip("Delta: listen to what the suppressor removes (dry minus processed)");
    autoGain->setTooltip("Auto-gain: make up the level the cuts remove (up to +6 dB)");
    pn->layout = [=](juce::Rectangle<int> b) {
        const int cell = 54;
        int x = b.getX() + 1;
        for (auto* c : std::initializer_list<juce::Component*> { mix, trim, out }) { c->setBounds(x, b.getY() + 2, cell, kKnobH); x += cell; }
        x += 4;
        const int w = b.getRight() - x;
        delta->setBounds(x, b.getY() + 14, w, 20);
        autoGain->setBounds(x, b.getY() + 44, w, 20);
    };
    return pn;
}

} // namespace chui
