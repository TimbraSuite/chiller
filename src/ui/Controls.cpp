#include "Controls.h"
#include <cmath>

using namespace chiller;

namespace chui {

static constexpr float kLabelH = 14.f;

Knob::Knob(ChillerProcessor& p, int paramId, juce::String lbl) : proc(p), pid(paramId), label(std::move(lbl))
{
    const auto& d = paramDefs()[(size_t) pid];
    if (label.isEmpty()) label = juce::String(d.name);
    bipolar = d.min < 0.f && d.max > 0.f && d.curve != Curve::Log;
    discrete = d.curve == Curve::Disc || d.curve == Curve::Toggle;
    defNorm = realToNorm(d, d.def);
    setTooltip(juce::String(d.name) + "\nDrag (Ctrl = fine) / wheel / arrow keys. Double-click = reset. Right-click = enter value.");
    setWantsKeyboardFocus(true);
    setMouseCursor(juce::MouseCursor::UpDownResizeCursor);
    att = std::make_unique<juce::ParameterAttachment>(*proc.getParam(pid), [this](float v) { value = v; repaint(); });
    att->sendInitialUpdate();
}

juce::Rectangle<float> Knob::circleArea() const
{
    auto b = getLocalBounds().toFloat().withTrimmedBottom(kLabelH);
    const float s = juce::jmin(b.getWidth(), b.getHeight(), 64.f);
    return juce::Rectangle<float>(s, s).withCentre(b.getCentre());
}

void Knob::paint(juce::Graphics& g)
{
    using namespace tok;
    const bool enabled = isEnabled();
    const auto area = circleArea();
    const float R = area.getWidth() * 0.5f - 1.f;
    const auto c = area.getCentre();
    const float arcW = juce::jlimit(2.2f, 3.6f, R * 0.13f);
    const float rk = R - arcW * 0.5f - 0.5f;                       // value-arc radius
    const float bodyR = rk - arcW * 0.5f - juce::jmax(1.5f, R * 0.08f);
    const float a0 = juce::MathConstants<float>::pi * 1.25f, a1 = juce::MathConstants<float>::pi * 2.75f;
    auto ang = [&](float v) { return a0 + (a1 - a0) * juce::jlimit(0.f, 1.f, v); };
    auto arc = [&](float r, float from, float to) { juce::Path p; p.addCentredArc(c.x, c.y, r, r, 0.f, from, to, true); return p; };
    auto polar = [&](float a, float r) { const float aa = a - juce::MathConstants<float>::halfPi; return juce::Point<float>(c.x + std::cos(aa) * r, c.y + std::sin(aa) * r); };
    const juce::PathStrokeType stroke(arcW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);

    // ---- value lane ----
    g.setColour(hover && enabled ? track.brighter(0.18f) : track);
    g.strokePath(arc(rk, a0, a1), stroke);
    float from = bipolar ? ang(0.5f) : a0, to = ang(value);
    if (to < from) std::swap(from, to);
    if (to - from > 0.015f) {
        g.setColour(enabled ? arcColour : arcColour.interpolatedWith(track, 0.5f));
        g.strokePath(arc(rk, from, to), stroke);
    }
    if (bipolar) { // zero tick
        const auto p0 = polar(ang(0.5f), rk - arcW), p1 = polar(ang(0.5f), rk + arcW);
        g.setColour(textSecondary);
        g.drawLine(p0.x, p0.y, p1.x, p1.y, 1.f);
    }

    // ---- flat graphite body ----
    {
        juce::ColourGradient grad(raised.brighter(0.08f), c.x - bodyR * 0.6f, c.y - bodyR * 0.7f,
                                  card, c.x + bodyR * 0.6f, c.y + bodyR * 0.8f, false);
        g.setGradientFill(grad);
        g.fillEllipse(c.x - bodyR, c.y - bodyR, bodyR * 2.f, bodyR * 2.f);
        g.setColour(edge);
        g.drawEllipse(c.x - bodyR, c.y - bodyR, bodyR * 2.f, bodyR * 2.f, 1.f);
    }
    {
        const auto p0 = polar(ang(value), bodyR * 0.30f), p1 = polar(ang(value), bodyR - 1.5f);
        g.setColour(enabled ? textPrimary : textDisabled);
        g.drawLine(p0.x, p0.y, p1.x, p1.y, 1.8f);
    }
    if (hasKeyboardFocus(false)) {
        juce::Path fr; fr.addEllipse(c.x - R, c.y - R, 2.f * R, 2.f * R);
        juce::Path dashed; const float dash[] = { 2.f, 2.f };
        juce::PathStrokeType(1.f).createDashedStroke(dashed, fr, dash, 2);
        g.setColour(focus.withAlpha(0.5f));
        g.fillPath(dashed);
    }

    // ---- label / value ----
    const juce::Rectangle<int> labelRect(0, getHeight() - (int) kLabelH, getWidth(), (int) kLabelH);
    const bool showVal = hover || dragging || hasKeyboardFocus(false);
    g.setColour(!enabled ? textDisabled : (showVal ? accent : textLabel));
    g.setFont(showVal ? fValue() : fLabel());
    g.drawFittedText(showVal ? chFormat(pid, value, true) : label, labelRect, juce::Justification::centred, 1, 0.85f);
}

void Knob::mouseDown(const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu()) {
        juce::PopupMenu m;
        m.addItem(1, "Reset to default");
        m.addItem(2, "Enter value...");
        juce::Component::SafePointer<Knob> sp(this);
        m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this), [sp](int r) {
            if (sp == nullptr) return;
            if (r == 1) sp->att->setValueAsCompleteGesture(sp->defNorm);
            else if (r == 2) sp->enterValue();
        });
        return;
    }
    dragging = true;
    dragStartY = e.y;
    dragStartVal = value;
    att->beginGesture();
    repaint();
}

void Knob::mouseDrag(const juce::MouseEvent& e)
{
    if (!dragging) return;
    const float dy = (float) (dragStartY - e.y);
    const float fine = (e.mods.isCommandDown() || e.mods.isCtrlDown()) ? 0.2f : 1.f;
    const float sens = fine < 1.f ? 1.f / 1000.f : 1.f / 180.f;
    att->setValueAsPartOfGesture(juce::jlimit(0.f, 1.f, dragStartVal + dy * sens));
}

void Knob::mouseUp(const juce::MouseEvent&)
{
    if (dragging) att->endGesture();
    dragging = false;
    repaint();
}

void Knob::mouseDoubleClick(const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu()) return;
    att->setValueAsCompleteGesture(defNorm);
}

void Knob::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    const auto& d = paramDefs()[(size_t) pid];
    const float steps = discrete ? std::max(1.f, d.max - d.min) : 50.f;
    const float step = (e.mods.isCtrlDown() || e.mods.isCommandDown() ? 0.25f : 1.f) / steps;
    const float dir = w.deltaY > 0 ? 1.f : (w.deltaY < 0 ? -1.f : 0.f);
    if (dir != 0.f) att->setValueAsCompleteGesture(juce::jlimit(0.f, 1.f, value + dir * step));
}

bool Knob::keyPressed(const juce::KeyPress& k)
{
    const auto& d = paramDefs()[(size_t) pid];
    const float steps = discrete ? std::max(1.f, d.max - d.min) : 100.f;
    const float step = (k.getModifiers().isShiftDown() ? 0.2f : 1.f) / steps;
    const int code = k.getKeyCode();
    if (code == juce::KeyPress::upKey || code == juce::KeyPress::rightKey) { att->setValueAsCompleteGesture(juce::jlimit(0.f, 1.f, value + step)); return true; }
    if (code == juce::KeyPress::downKey || code == juce::KeyPress::leftKey) { att->setValueAsCompleteGesture(juce::jlimit(0.f, 1.f, value - step)); return true; }
    if (code == juce::KeyPress::deleteKey || code == juce::KeyPress::backspaceKey) { att->setValueAsCompleteGesture(defNorm); return true; }
    if (code == juce::KeyPress::returnKey) { enterValue(); return true; }
    return false;
}

void Knob::enterValue()
{
    auto* prm = proc.getParam(pid);
    auto* w = new juce::AlertWindow(juce::String(paramDefs()[(size_t) pid].name), "Type a value (units optional):",
                                    juce::MessageBoxIconType::NoIcon, this);
    w->addTextEditor("v", chFormat(pid, value, true));
    w->addButton("OK", 1, juce::KeyPress(juce::KeyPress::returnKey));
    w->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
    if (auto* ed = w->getTextEditor("v")) { ed->setFont(fValue()); ed->selectAll(); }
    juce::Component::SafePointer<Knob> sp(this);
    w->enterModalState(true, juce::ModalCallbackFunction::create([sp, w, prm](int r) {
        if (sp != nullptr && r == 1)
            if (auto* ed = w->getTextEditor("v"))
                sp->att->setValueAsCompleteGesture(prm->getValueForText(ed->getText().trim()));
    }), true);
}

//==============================================================================================
ParamCombo::ParamCombo(ChillerProcessor& p, int paramId) : pid(paramId)
{
    const auto& d = paramDefs()[(size_t) pid];
    int i = 1;
    for (auto& c : d.choices) addItem(juce::String(c), i++);
    setJustificationType(juce::Justification::centredLeft);
    setTooltip(juce::String(d.name));
    att = std::make_unique<juce::ParameterAttachment>(*p.getParam(pid), [this](float v) {
        const auto& dd = paramDefs()[(size_t) pid];
        index = (int) std::lround(normToReal(dd, v));
        updating = true;
        setSelectedItemIndex(index, juce::dontSendNotification);
        updating = false;
    });
    onChange = [this] {
        if (updating) return;
        att->setValueAsCompleteGesture(realToNorm(paramDefs()[(size_t) pid], (float) getSelectedItemIndex()));
    };
    att->sendInitialUpdate();
}

ParamToggle::ParamToggle(ChillerProcessor& p, int paramId, const juce::String& text) : pid(paramId)
{
    setButtonText(text);
    setTooltip(juce::String(paramDefs()[(size_t) pid].name));
    att = std::make_unique<juce::ParameterAttachment>(*p.getParam(pid), [this](float v) {
        setToggleState(v >= 0.5f, juce::dontSendNotification);
    });
    onClick = [this] { att->setValueAsCompleteGesture(getToggleState() ? 1.f : 0.f); };
    att->sendInitialUpdate();
}

} // namespace chui
