#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "BinaryData.h"
#include <cmath>

namespace chui {

//==============================================================================================
// Design tokens. Every colour, type size and spacing value used by the UI comes from here.
// Chiller palette: neutral grey, no blue tint, no textures. Colour is reserved for meaning.
//==============================================================================================
namespace tok {
// ---- surfaces (dark neutral grey, darkest to lightest) ----
inline const juce::Colour bg       { 0xff1b1c1e };  // window background
inline const juce::Colour well     { 0xff141516 };  // graphs and displays
inline const juce::Colour panel    { 0xff232427 };  // main panels
inline const juce::Colour card     { 0xff2a2b2e };  // cards
inline const juce::Colour raised   { 0xff323337 };  // buttons, combos, inputs
inline const juce::Colour edge     { 0xff3d3f43 };  // control outlines
inline const juce::Colour edgeSoft { 0xff2f3033 };  // subtle separators
inline const juce::Colour track    { 0xff393a3e };  // inactive knob/slider track

// ---- text ----
inline const juce::Colour textPrimary   { 0xffececec }; // titles, values
inline const juce::Colour textLabel     { 0xffb4b6b9 }; // parameter labels
inline const juce::Colour textSecondary { 0xff898c90 }; // metadata, inactive tabs
inline const juce::Colour textDisabled  { 0xff5a5c60 };

// ---- accents ----
inline const juce::Colour accent    { 0xffb4ff3c }; // acid green: values, active state, primary actions
inline const juce::Colour accentDim { 0xff6f9a2c }; // values of inactive/disabled controls
inline const juce::Colour danger    { 0xffff5470 };
inline const juce::Colour amber     { 0xffffd23c };
inline const juce::Colour focus     { 0xffececec }; // keyboard focus ring

// ---- signal colours (meaning, not decoration) ----
inline const juce::Colour keyCyan       { 0xff3cf0ff }; // sidechain / key signal
inline const juce::Colour protectPurple { 0xffb48cff }; // protection (harmonics, transients)
inline const juce::Colour inputIce      { 0xffdfe6f0 }; // input spectrum

// ---- type scale (font height in px at 100% UI scale) ----
constexpr float fsLogo    = 20.f;
constexpr float fsSection = 14.f;  // panel headings
constexpr float fsButton  = 12.5f;
constexpr float fsLabel   = 12.f;  // parameter labels
constexpr float fsValue   = 12.f;  // parameter values (tabular digits)
constexpr float fsMeta    = 11.f;  // secondary information
constexpr float fsBadge   = 10.f;  // chips

enum class Weight { Regular, Medium, SemiBold, Bold };

// ---- spacing / shape ----
constexpr int sp1 = 2, sp2 = 4, sp3 = 6, sp4 = 8, sp5 = 12;
constexpr float rCard = 5.f, rControl = 4.f;
} // namespace tok

// Embedded Roboto Condensed (assets/fonts). Its digits are tabular, so values don't jitter.
juce::Typeface::Ptr typeface(tok::Weight w);
juce::Font font(float height, tok::Weight w);
inline juce::Font font(float height, bool bold = false) { return font(height, bold ? tok::Weight::SemiBold : tok::Weight::Medium); }

// Named type styles
inline juce::Font fLogo()    { return juce::Font(juce::FontOptions(typeface(tok::Weight::Bold)).withHeight(tok::fsLogo).withKerningFactor(0.12f)); }
inline juce::Font fSection() { return font(tok::fsSection, tok::Weight::Bold); }
inline juce::Font fButton()  { return font(tok::fsButton, tok::Weight::SemiBold); }
inline juce::Font fLabel()   { return font(tok::fsLabel, tok::Weight::Medium); }
inline juce::Font fValue()   { return font(tok::fsValue, tok::Weight::SemiBold); }
inline juce::Font fMeta()    { return font(tok::fsMeta, tok::Weight::Regular); }
inline juce::Font fBadge()   { return font(tok::fsBadge, tok::Weight::Bold); }

// Component property that selects a style variant in the LookAndFeel.
//   TextButton: "chStyle" = "primary" (accent-filled action), "ghost" (low emphasis), default = standard
inline void setStyle(juce::Component& c, const char* style) { c.getProperties().set("chStyle", style); }
inline juce::String styleOf(const juce::Component& c) { return c.getProperties()["chStyle"].toString(); }

class LookAndFeel : public juce::LookAndFeel_V4 {
public:
    LookAndFeel();
    juce::Typeface::Ptr getTypefaceForFont(const juce::Font&) override;
    void drawComboBox(juce::Graphics&, int w, int h, bool down, int bx, int by, int bw, int bh, juce::ComboBox&) override;
    void positionComboBoxText(juce::ComboBox&, juce::Label&) override;
    juce::Font getComboBoxFont(juce::ComboBox&) override { return fLabel(); }
    juce::Font getPopupMenuFont() override { return font(13.f, tok::Weight::Medium); }
    juce::Font getTextButtonFont(juce::TextButton&, int) override { return fButton(); }
    void drawButtonBackground(juce::Graphics&, juce::Button&, const juce::Colour&, bool over, bool down) override;
    void drawButtonText(juce::Graphics&, juce::TextButton&, bool over, bool down) override;
    void drawToggleButton(juce::Graphics&, juce::ToggleButton&, bool over, bool down) override;
    void drawPopupMenuBackground(juce::Graphics&, int w, int h) override;
    void drawScrollbar(juce::Graphics&, juce::ScrollBar&, int x, int y, int w, int h, bool vertical, int thumbStart,
                       int thumbSize, bool over, bool down) override;
    int getDefaultScrollbarWidth() override { return 8; }
    juce::Font getAlertWindowMessageFont() override { return fLabel(); }
    juce::Font getAlertWindowTitleFont() override { return fSection(); }
};

// Panel painter shared by all panels: flat plate, 1 px outline, small title. No textures.
inline void paintPanel(juce::Graphics& g, juce::Rectangle<int> r, const juce::String& title = {})
{
    const auto rf = r.toFloat();
    g.setColour(tok::panel);
    g.fillRoundedRectangle(rf, tok::rCard);
    g.setColour(tok::edgeSoft);
    g.drawRoundedRectangle(rf.reduced(0.5f), tok::rCard, 1.f);
    if (title.isNotEmpty()) {
        g.setColour(tok::textLabel);
        g.setFont(fSection());
        g.drawText(title, r.getX() + 12, r.getY() + 4, r.getWidth() - 24, 16, juce::Justification::left);
    }
}

} // namespace chui
