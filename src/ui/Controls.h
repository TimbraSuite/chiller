#pragma once
#include "../PluginProcessor.h"
#include "Look.h"
#include <algorithm>
#include <functional>

namespace chui {

// Anything that wants a 30 Hz refresh registers here; the editor owns the single timer.
struct Tickable {
    Tickable() { all().push_back(this); }
    virtual ~Tickable() { auto& v = all(); v.erase(std::remove(v.begin(), v.end(), this), v.end()); }
    virtual void tick() = 0;
    static std::vector<Tickable*>& all() { static std::vector<Tickable*> v; return v; }
    static void tickAll() { for (auto* t : all()) t->tick(); }
    // Offscreen rendering (tools/uishot) has no window peer, so isShowing() is false; this overrides it.
    static bool& forceShowing() { static bool f = false; return f; }
    static bool isLive(const juce::Component& c) { return forceShowing() || c.isShowing(); }
};

//==============================================================================================
// Rotary parameter control: arc + machined body + pointer, label underneath (value while touched).
class Knob : public juce::Component, public juce::SettableTooltipClient {
public:
    Knob(ChillerProcessor& p, int paramId, juce::String label = {});
    void setLabel(const juce::String& s) { label = s; repaint(); }
    void setAccent(juce::Colour c) { arcColour = c; repaint(); }   // meaning colour (e.g. protect purple)
    int getParamId() const { return pid; }
    float getValue() const { return value; }

    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseEnter(const juce::MouseEvent&) override { hover = true; repaint(); }
    void mouseExit(const juce::MouseEvent&) override { hover = false; repaint(); }
    bool keyPressed(const juce::KeyPress&) override;
    void focusGained(FocusChangeType) override { repaint(); }
    void focusLost(FocusChangeType) override { repaint(); }
    void enablementChanged() override { repaint(); }
    void enterValue();   // typed value entry (right-click menu / Enter key)

private:
    juce::Rectangle<float> circleArea() const;
    ChillerProcessor& proc;
    int pid;
    juce::String label;
    std::unique_ptr<juce::ParameterAttachment> att;
    float value = 0.f, defNorm = 0.f, dragStartVal = 0.f;
    int dragStartY = 0;
    juce::Colour arcColour = tok::accent;
    bool bipolar = false, hover = false, dragging = false, discrete = false;
};

class ParamCombo : public juce::ComboBox {
public:
    ParamCombo(ChillerProcessor& p, int paramId);
    int getIndex() const { return index; }
private:
    int pid, index = 0;
    bool updating = false;
    std::unique_ptr<juce::ParameterAttachment> att;
};

class ParamToggle : public juce::ToggleButton {
public:
    ParamToggle(ChillerProcessor& p, int paramId, const juce::String& text);
private:
    int pid;
    std::unique_ptr<juce::ParameterAttachment> att;
};

} // namespace chui
