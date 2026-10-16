#include "Look.h"

namespace chui {

//==============================================================================================
// Fonts
//==============================================================================================
juce::Typeface::Ptr typeface(tok::Weight w)
{
    static const juce::Typeface::Ptr faces[4] = {
        juce::Typeface::createSystemTypefaceFor(BinaryData::RobotoCondensedRegular_ttf, BinaryData::RobotoCondensedRegular_ttfSize),
        juce::Typeface::createSystemTypefaceFor(BinaryData::RobotoCondensedMedium_ttf, BinaryData::RobotoCondensedMedium_ttfSize),
        juce::Typeface::createSystemTypefaceFor(BinaryData::RobotoCondensedSemiBold_ttf, BinaryData::RobotoCondensedSemiBold_ttfSize),
        juce::Typeface::createSystemTypefaceFor(BinaryData::RobotoCondensedBold_ttf, BinaryData::RobotoCondensedBold_ttfSize),
    };
    return faces[juce::jlimit(0, 3, (int) w)];
}

juce::Font font(float height, tok::Weight w)
{
    return juce::Font(juce::FontOptions(typeface(w)).withHeight(height));
}

//==============================================================================================
LookAndFeel::LookAndFeel()
{
    using namespace tok;
    setColour(juce::ResizableWindow::backgroundColourId, bg);
    setColour(juce::PopupMenu::backgroundColourId, raised);
    setColour(juce::PopupMenu::textColourId, textPrimary);
    setColour(juce::PopupMenu::highlightedBackgroundColourId, accent.withAlpha(0.85f));
    setColour(juce::PopupMenu::highlightedTextColourId, juce::Colours::black);
    setColour(juce::PopupMenu::headerTextColourId, textSecondary);
    setColour(juce::ComboBox::backgroundColourId, raised);
    setColour(juce::ComboBox::textColourId, textPrimary);
    setColour(juce::ComboBox::outlineColourId, edge);
    setColour(juce::ComboBox::arrowColourId, textSecondary);
    setColour(juce::Label::textColourId, textPrimary);
    setColour(juce::TextEditor::backgroundColourId, well);
    setColour(juce::TextEditor::textColourId, textPrimary);
    setColour(juce::TextEditor::outlineColourId, edge);
    setColour(juce::TextEditor::focusedOutlineColourId, accent);
    setColour(juce::TextEditor::highlightColourId, accent.withAlpha(0.35f));
    setColour(juce::AlertWindow::backgroundColourId, panel);
    setColour(juce::AlertWindow::textColourId, textPrimary);
    setColour(juce::AlertWindow::outlineColourId, edge);
    setColour(juce::TextButton::buttonColourId, raised);
    setColour(juce::TextButton::textColourOffId, textPrimary);
    setColour(juce::ScrollBar::thumbColourId, edge.brighter(0.3f));
    setColour(juce::Slider::thumbColourId, accent);
    setColour(juce::Slider::trackColourId, accent);
    setColour(juce::Slider::backgroundColourId, track);
    setColour(juce::CaretComponent::caretColourId, accent);
    setColour(juce::TooltipWindow::backgroundColourId, raised);
    setColour(juce::TooltipWindow::textColourId, textPrimary);
    setColour(juce::TooltipWindow::outlineColourId, edge);
}

juce::Typeface::Ptr LookAndFeel::getTypefaceForFont(const juce::Font& f)
{
    // Any JUCE-drawn text (popup menus, alert windows, tooltips) uses the embedded face too.
    return typeface(f.isBold() ? tok::Weight::SemiBold : tok::Weight::Medium);
}

//==============================================================================================
void LookAndFeel::drawComboBox(juce::Graphics& g, int w, int h, bool down, int, int, int, int, juce::ComboBox& box)
{
    using namespace tok;
    auto r = juce::Rectangle<float>(0.f, 0.f, (float) w, (float) h).reduced(0.5f);
    const bool over = box.isMouseOver(true);
    g.setColour(down ? raised.brighter(0.12f) : (over ? raised.brighter(0.05f) : raised));
    g.fillRoundedRectangle(r, rControl);
    g.setColour(box.hasKeyboardFocus(true) ? accent : (over ? edge.brighter(0.25f) : edge));
    g.drawRoundedRectangle(r, rControl, 1.f);
    juce::Path p;
    const float cx = (float) w - 10.f, cy = (float) h * 0.5f;
    p.addTriangle(cx - 3.f, cy - 1.5f, cx + 3.f, cy - 1.5f, cx, cy + 2.f);
    g.setColour(!box.isEnabled() ? textDisabled : (over ? textLabel : textSecondary));
    g.fillPath(p);
}

void LookAndFeel::positionComboBoxText(juce::ComboBox& box, juce::Label& label)
{
    label.setBounds(6, 1, box.getWidth() - 22, box.getHeight() - 2);
    label.setFont(getComboBoxFont(box));
    label.setColour(juce::Label::textColourId, box.isEnabled() ? box.findColour(juce::ComboBox::textColourId) : tok::textDisabled);
}

//==============================================================================================
void LookAndFeel::drawButtonBackground(juce::Graphics& g, juce::Button& b, const juce::Colour& base, bool over, bool down)
{
    using namespace tok;
    auto r = b.getLocalBounds().toFloat().reduced(0.5f);
    const auto style = styleOf(b);
    const bool on = b.getToggleState(), enabled = b.isEnabled();

    if (style == "primary") { // accent-filled primary action (dimmed when disabled)
        g.setColour(!enabled ? accentDim.withAlpha(0.55f) : (down ? accent.brighter(0.2f) : (over ? accent.brighter(0.1f) : accent)));
        g.fillRoundedRectangle(r, rControl);
        return;
    }
    if (style == "ghost") { // low-emphasis: hover plate only
        if (over || down) { g.setColour(raised.withAlpha(down ? 0.9f : 0.6f)); g.fillRoundedRectangle(r, rControl); }
        g.setColour(edge);
        g.drawRoundedRectangle(r, rControl, 1.f);
        return;
    }
    if (style == "slot" || style == "warn") { // outline buttons: toggled on = coloured text + border (accent / amber)
        const auto on_edge = style == "warn" ? amber : accentDim;
        auto c = enabled ? (down ? raised.darker(0.15f) : (over ? raised.brighter(0.12f) : raised)) : raised;
        g.setColour(c);
        g.fillRoundedRectangle(r, rControl);
        g.setColour(!enabled ? edgeSoft : (on ? on_edge : (over ? edge.brighter(0.3f) : edge)));
        g.drawRoundedRectangle(r, rControl, 1.f);
        return;
    }
    // standard button; a toggled-on standard button is a primary (acid-green) state
    auto c = on ? accent : base;
    if (!enabled) c = c.withMultipliedAlpha(0.5f);
    else if (down) c = c.brighter(0.2f);
    else if (over) c = c.brighter(0.08f);
    g.setColour(c);
    g.fillRoundedRectangle(r, rControl);
    g.setColour(on ? accent : (over && enabled ? edge.brighter(0.3f) : edge));
    g.drawRoundedRectangle(r, rControl, 1.f);
}

void LookAndFeel::drawButtonText(juce::Graphics& g, juce::TextButton& b, bool over, bool)
{
    using namespace tok;
    const auto style = styleOf(b);
    const bool on = b.getToggleState(), enabled = b.isEnabled();
    juce::Colour c;
    if (style == "primary") c = enabled ? juce::Colours::black : juce::Colour(0xff1b2208);
    else if (style == "slot" || style == "warn") c = !enabled ? textDisabled : (on ? (style == "warn" ? amber : accent) : (over ? textPrimary : textPrimary));
    else if (style == "ghost") c = enabled ? (over ? textPrimary : textLabel) : textDisabled;
    else if (!enabled) c = textSecondary.withAlpha(0.55f);
    else c = on ? juce::Colours::black : b.findColour(juce::TextButton::textColourOffId);
    g.setFont(getTextButtonFont(b, b.getHeight()));
    g.setColour(c);
    g.drawFittedText(b.getButtonText(), b.getLocalBounds().reduced(4, 0), juce::Justification::centred, 1, 0.9f);
}

void LookAndFeel::drawToggleButton(juce::Graphics& g, juce::ToggleButton& b, bool over, bool)
{
    using namespace tok;
    auto r = b.getLocalBounds().toFloat();
    const bool on = b.getToggleState(), enabled = b.isEnabled();
    const float ph = juce::jmin(14.f, r.getHeight() - 2.f), pw = ph * 1.8f;
    auto pill = juce::Rectangle<float>(r.getX() + 1.f, r.getCentreY() - ph * 0.5f, pw, ph);
    g.setColour(on ? accent.withAlpha(enabled ? 0.9f : 0.4f) : track);
    g.fillRoundedRectangle(pill, ph * 0.5f);
    g.setColour(on ? accent : (over ? edge.brighter(0.3f) : edge));
    g.drawRoundedRectangle(pill, ph * 0.5f, 1.f);
    const float kd = ph - 4.f;
    g.setColour(on ? juce::Colours::black : (over ? textLabel : textSecondary));
    g.fillEllipse(on ? pill.getRight() - kd - 2.f : pill.getX() + 2.f, pill.getY() + 2.f, kd, kd);
    if (b.getButtonText().isNotEmpty()) {
        juce::Colour tc = on ? textPrimary : textLabel;
        if (b.getProperties().contains("chTextColour")) tc = juce::Colour((juce::uint32) (juce::int64) b.getProperties()["chTextColour"]);
        g.setColour(!enabled ? textDisabled : tc);
        g.setFont(fLabel());
        g.drawFittedText(b.getButtonText(), juce::Rectangle<int>((int) (pill.getRight() + 6.f), 0, b.getWidth() - (int) pw - 8, b.getHeight()),
                         juce::Justification::centredLeft, 1, 0.78f);
    }
    if (b.hasKeyboardFocus(false)) {
        g.setColour(focus.withAlpha(0.6f));
        g.drawRoundedRectangle(pill.expanded(2.f), ph * 0.5f + 2.f, 1.f);
    }
}

void LookAndFeel::drawPopupMenuBackground(juce::Graphics& g, int w, int h)
{
    g.fillAll(tok::raised);
    g.setColour(tok::edge);
    g.drawRect(0, 0, w, h, 1);
}

void LookAndFeel::drawScrollbar(juce::Graphics& g, juce::ScrollBar&, int x, int y, int w, int h, bool vertical,
                                int thumbStart, int thumbSize, bool over, bool)
{
    juce::ignoreUnused(x, y);
    auto t = vertical ? juce::Rectangle<float>((float) w * 0.5f - 2.f, (float) thumbStart, 4.f, (float) thumbSize)
                      : juce::Rectangle<float>((float) thumbStart, (float) h * 0.5f - 2.f, (float) thumbSize, 4.f);
    g.setColour(over ? tok::accent.withAlpha(0.7f) : tok::edge.brighter(0.4f));
    g.fillRoundedRectangle(t, 2.f);
}

} // namespace chui
