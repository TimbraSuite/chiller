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

// State shared by the editor's displays: the newest frame from the audio thread plus transient UI state.
struct UiState {
    chiller::dsp::UiFrame frame;
    bool haveFrame = false;
    float learnProgress = -1.f;          // < 0 idle, 0..1 while LEARN is listening
    juce::String toast;
    juce::uint32 toastUntil = 0;
    void showToast(const juce::String& t) { toast = t; toastUntil = juce::Time::getMillisecondCounter() + 3500; }
    bool toastVisible() const { return toast.isNotEmpty() && (juce::int32) (toastUntil - juce::Time::getMillisecondCounter()) > 0; }
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
    void commit() { proc.historyCommit(); }
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
    // Text that changes with the state (e.g. "L/R" / "M/S").
    void setStateTexts(const juce::String& off, const juce::String& on);
private:
    int pid;
    juce::String offText, onText;
    std::unique_ptr<juce::ParameterAttachment> att;
};

// Parameter toggle drawn as a normal button ("slot" = accent when on, "warn" = amber when on).
class ParamButton : public juce::TextButton {
public:
    ParamButton(ChillerProcessor& p, int paramId, const juce::String& text, const char* style = "warn");
private:
    int pid;
    std::unique_ptr<juce::ParameterAttachment> att;
};

// Small square button with a drawn icon.
class IconButton : public juce::Button {
public:
    enum Icon { Prev, Next, Undo, Redo };
    explicit IconButton(Icon i) : juce::Button({}), icon(i) {}
    void paintButton(juce::Graphics&, bool over, bool down) override;
private:
    Icon icon;
};

} // namespace chui
