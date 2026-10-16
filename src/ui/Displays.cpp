#include "Displays.h"

using namespace chiller;

namespace chui {

static const juce::String kMinus = juce::String::fromUTF8("\xe2\x88\x92");   // U+2212
static const juce::String kDot = juce::String::fromUTF8(" \xc2\xb7 ");       // " · "

juce::String noteName(float hz)
{
    static const char* names[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    const int m = (int) std::lround(69.0 + 12.0 * std::log2((double) hz / 440.0));
    return juce::String(names[((m % 12) + 12) % 12]) + juce::String((int) std::floor((double) m / 12.0) - 1);
}

juce::String shortHz(float hz)
{
    if (hz >= 1000.f) {
        auto s = juce::String(hz / 1000.f, 1);
        if (s.endsWith(".0")) s = s.dropLastCharacters(2);
        return s + "k";
    }
    return juce::String((int) std::lround(hz));
}

static juce::String hzLong(float hz)
{
    if (hz < 1000.f) return juce::String((int) std::lround(hz)) + " Hz";
    return juce::String(hz / 1000.f, hz < 10000.f ? 2 : 1) + " kHz";
}

static float textWidth(const juce::Font& f, const juce::String& t) { return juce::GlyphArrangement::getStringWidth(f, t); }

//==============================================================================================
void TabButton::paintButton(juce::Graphics& g, bool over, bool)
{
    using namespace tok;
    const bool on = getToggleState();
    auto r = getLocalBounds().toFloat();
    if (on || over) {
        g.setColour(on ? raised : raised.withAlpha(0.55f));
        g.fillRoundedRectangle(r, rControl);
    }
    if (on) {
        g.setColour(accent);
        g.fillRoundedRectangle(r.getX() + 6.f, r.getBottom() - 2.5f, r.getWidth() - 12.f, 2.5f, 1.f);
    }
    g.setColour(on ? textPrimary : (over ? textLabel : textSecondary));
    g.setFont(juce::Font(juce::FontOptions(typeface(Weight::SemiBold)).withHeight(12.5f).withKerningFactor(0.04f)));
    g.drawText(getButtonText(), getLocalBounds(), juce::Justification::centred);
}

//==============================================================================================
// Explain view geometry (design pixels, component is 880 x 452)
//==============================================================================================
namespace geo {
constexpr float x0 = 46.f, x1 = 880.f - 14.f, y0 = 40.f, y1 = 452.f - 24.f;
constexpr float yZero = y0 + 8.f, dbPx = 6.5f, levelTop = yZero + 40.f;
} // namespace geo

SpectrumView::SpectrumView(ChillerProcessor& p, UiState& s) : proc(p), st(s)
{
    addAndMakeVisible(specTab);
    addAndMakeVisible(histTab);
    specTab.setToggleState(true, juce::dontSendNotification);
    specTab.setTooltip("Live input spectrum with the cut drawn from the top");
    histTab.setTooltip("Reduction over the last 20 seconds. Brighter green is a deeper cut");
    specTab.onClick = [this] { setView(View::Spectrum); };
    histTab.onClick = [this] { setView(View::History); };
    keyToggle = std::make_unique<ParamToggle>(p, pid::scKey, "Sidechain key");
    keyToggle->getProperties().set("chTextColour", (juce::int64) tok::keyCyan.getARGB());
    keyToggle->setTooltip("Detect from the sidechain input instead of the main input (route a track to Chiller's sidechain in your host)");
    addAndMakeVisible(*keyToggle);
    heat = juce::Image(juce::Image::ARGB, kHistCols, N, true);
    {
        juce::Image::BitmapData bd(heat, juce::Image::BitmapData::writeOnly);
        for (int y = 0; y < N; ++y)
            for (int x = 0; x < kHistCols; ++x) bd.setPixelColour(x, y, tok::well);
    }
    setMouseCursor(juce::MouseCursor::CrosshairCursor);
}

void SpectrumView::resized()
{
    specTab.setBounds(8, 6, 88, 28);
    histTab.setBounds(98, 6, 76, 28);
    keyToggle->setBounds(186, 10, 112, 20);
}

void SpectrumView::setView(View v)
{
    view = v;
    specTab.setToggleState(v == View::Spectrum, juce::dontSendNotification);
    histTab.setToggleState(v == View::History, juce::dontSendNotification);
    repaint();
}

float SpectrumView::xOfHz(float hz) const { return geo::x0 + (geo::x1 - geo::x0) * std::log(hz / kMinHz) / std::log(kMaxHz / kMinHz); }
float SpectrumView::hzOfX(float x) const { return kMinHz * std::exp((x - geo::x0) / (geo::x1 - geo::x0) * std::log(kMaxHz / kMinHz)); }
float SpectrumView::yOfLevel(float db) const { return geo::y1 - (db + 100.f) / 80.f * (geo::y1 - geo::levelTop); }
float SpectrumView::nodeX(int node) const
{
    return xOfHz(proc.getReal(node == 1 ? pid::rangeLow : pid::rangeHigh));
}

int SpectrumView::hitNode(juce::Point<float> p) const
{
    if (view != View::Spectrum) return 0;
    const float y = geo::y1 - 10.f;
    for (int n = 1; n <= 2; ++n)
        if (std::hypot(nodeX(n) - p.x, y - p.y) < 14.f) return n;
    return 0;
}

//----------------------------------------------------------------------------------------------
void SpectrumView::tick()
{
    if (!isLive(*this)) return;
    if (st.haveFrame) {
        const auto& f = st.frame;
        auto sm = [&](std::array<float, N>& a, const float* src, float up, float down) {
            for (int i = 0; i < N; ++i) {
                const float t = src[i];
                a[(size_t) i] = primed ? a[(size_t) i] + (t - a[(size_t) i]) * (t > a[(size_t) i] ? up : down) : t;
            }
        };
        sm(inS, f.inDb, 0.7f, 0.35f);
        sm(expS, f.expDb, 0.4f, 0.4f);
        sm(keyS, f.keyDb, 0.7f, 0.35f);
        sm(grS, f.gr, 0.6f, 0.3f);
        primed = true;
        f0Conf += (f.f0Conf - f0Conf) * 0.3f;
        if (++histDivider >= 2) { histDivider = 0; pushHistoryColumn(); }
    }
    repaint();
}

void SpectrumView::pushHistoryColumn()
{
    juce::Image::BitmapData bd(heat, juce::Image::BitmapData::readWrite);
    for (int y = 0; y < N; ++y) {
        auto* row = bd.getLinePointer(y);
        std::memmove(row, row + bd.pixelStride, (size_t) (kHistCols - 1) * (size_t) bd.pixelStride);
    }
    for (int i = 0; i < N; ++i) {
        const float v = std::pow(juce::jlimit(0.f, 1.f, grS[(size_t) i] / 10.f), 0.8f);
        const auto c = juce::Colour((juce::uint8) juce::roundToInt(juce::jmap(v, 20.f, 180.f)), (juce::uint8) juce::roundToInt(juce::jmap(v, 21.f, 255.f)),
                                    (juce::uint8) juce::roundToInt(juce::jmap(v, 22.f, 60.f)));
        bd.setPixelColour(kHistCols - 1, N - 1 - i, c);
    }
}

//----------------------------------------------------------------------------------------------
void SpectrumView::paint(juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour(tok::well);
    g.fillRoundedRectangle(r, 6.f);
    if (view == View::Spectrum) paintSpectrum(g); else paintHistory(g);
    paintOverlays(g);
    g.setColour(tok::edgeSoft);
    g.drawRoundedRectangle(r.reduced(0.5f), 6.f, 1.f);
}

void SpectrumView::paintGridV(juce::Graphics& g, bool labels)
{
    using namespace tok;
    g.setFont(font(11.f, Weight::Regular));
    for (float f : { 50.f, 100.f, 500.f, 1000.f, 5000.f, 10000.f }) {
        const float x = std::round(xOfHz(f)) + 0.5f;
        g.setColour(edgeSoft);
        g.drawVerticalLine((int) x, geo::y0, geo::y1);
        if (labels) {
            g.setColour(textSecondary);
            g.drawText(f >= 1000.f ? juce::String((int) (f / 1000.f)) + "k" : juce::String((int) f), (int) x - 20, (int) H - 19, 40, 12, juce::Justification::centred);
        }
    }
    for (float f : { 20.f, 200.f, 2000.f, 20000.f }) {
        g.setColour(edgeSoft.withAlpha(0.45f));
        g.drawVerticalLine((int) (std::round(xOfHz(f)) + 0.5f), geo::y0, geo::y1);
    }
    if (labels) {
        g.setColour(textDisabled);
        g.drawText("20 Hz", (int) geo::x0, (int) H - 19, 50, 12, juce::Justification::left);
        g.drawText("20 kHz", (int) geo::x1 - 50, (int) H - 19, 50, 12, juce::Justification::right);
    }
}

void SpectrumView::paintSpectrum(juce::Graphics& g)
{
    using namespace tok;
    const auto& f = st.frame;
    const bool delta = proc.getReal(pid::delta) >= 0.5f;
    const bool keyOn = proc.getReal(pid::scKey) >= 0.5f;

    // ---- level grid (left labels) ----
    g.setFont(font(11.f, Weight::Regular));
    for (int d : { -30, -50, -70, -90 }) {
        const float y = std::round(yOfLevel((float) d)) + 0.5f;
        g.setColour(panel);
        g.drawHorizontalLine((int) y, geo::x0, geo::x1);
        g.setColour(textDisabled);
        g.drawText(kMinus + juce::String(std::abs(d)), (int) geo::x0 - 44, (int) y - 7, 38, 14, juce::Justification::right);
    }
    paintGridV(g, true);

    // ---- out-of-range shading ----
    const float lcx = juce::jlimit(geo::x0, geo::x1, nodeX(1)), hcx = juce::jlimit(geo::x0, geo::x1, nodeX(2));
    g.setColour(juce::Colours::black.withAlpha(0.38f));
    g.fillRect(geo::x0, geo::y0, lcx - geo::x0, geo::y1 - geo::y0);
    g.fillRect(hcx, geo::y0, geo::x1 - hcx, geo::y1 - geo::y0);

    auto xi = [&](int i) { return geo::x0 + (geo::x1 - geo::x0) * (float) i / (float) (N - 1); };
    auto yc = [&](float y) { return juce::jlimit(geo::y0, geo::y1, y); };
    auto curve = [&](const std::array<float, N>& a, auto yfn) {
        juce::Path p;
        for (int i = 0; i < N; ++i) {
            const float x = xi(i), y = yc(yfn(a[(size_t) i]));
            if (i == 0) p.startNewSubPath(x, y); else p.lineTo(x, y);
        }
        return p;
    };

    if (st.haveFrame && !delta) {
        auto p = curve(inS, [&](float v) { return yOfLevel(v); });
        auto fill = p;
        fill.lineTo(geo::x1, geo::y1); fill.lineTo(geo::x0, geo::y1); fill.closeSubPath();
        g.setGradientFill(juce::ColourGradient(inputIce.withAlpha(0.13f), 0.f, geo::levelTop, inputIce.withAlpha(0.01f), 0.f, geo::y1, false));
        g.fillPath(fill);
        g.setColour(inputIce);
        g.strokePath(p, juce::PathStrokeType(1.3f, juce::PathStrokeType::curved));
        // expected curve (dashed), drawn with the threshold added
        juce::Path ep = curve(expS, [&](float v) { return yOfLevel(v); }), dashed;
        const float dash[] = { 5.f, 4.f };
        juce::PathStrokeType(1.f).createDashedStroke(dashed, ep, dash, 2);
        g.setColour(textSecondary.withAlpha(0.9f));
        g.fillPath(dashed);
        if (keyOn && f.keyActive) {
            g.setColour(keyCyan.withAlpha(0.85f));
            g.strokePath(curve(keyS, [&](float v) { return yOfLevel(v); }), juce::PathStrokeType(1.1f));
            g.setFont(font(11.f, Weight::SemiBold));
            g.setColour(keyCyan);
            g.drawText("KEY", (int) geo::x0 + 6, (int) yc(yOfLevel(keyS[0]) - 18.f), 40, 12, juce::Justification::left);
        }
    } else if (st.haveFrame) {
        // delta: only what is removed (input + 10 log10(1 - 10^(-gr/10)))
        std::array<float, N> rem {};
        for (int i = 0; i < N; ++i) {
            const float gr = grS[(size_t) i];
            rem[(size_t) i] = gr > 0.02f ? inS[(size_t) i] + 10.f * std::log10(1.f - std::pow(10.f, -gr / 10.f) + 1e-9f) : -120.f;
        }
        auto p = curve(rem, [&](float v) { return yOfLevel(juce::jmax(v, -100.f)); });
        auto fill = p;
        fill.lineTo(geo::x1, geo::y1); fill.lineTo(geo::x0, geo::y1); fill.closeSubPath();
        g.setGradientFill(juce::ColourGradient(accent.withAlpha(0.40f), 0.f, geo::levelTop, accent.withAlpha(0.04f), 0.f, geo::y1, false));
        g.fillPath(fill);
        g.setColour(accent);
        g.strokePath(p, juce::PathStrokeType(1.4f));
    }

    // ---- the cut: filled area hanging from the 0 dB line ----
    {
        g.setFont(font(11.f, Weight::Regular));
        for (int d : { 6, 12, 18 }) {
            const float y = std::round(geo::yZero + (float) d * geo::dbPx) + 0.5f;
            juce::Path line, dashed;
            line.startNewSubPath(geo::x0, y); line.lineTo(geo::x1, y);
            const float dash[] = { 2.f, 4.f };
            juce::PathStrokeType(1.f).createDashedStroke(dashed, line, dash, 2);
            g.setColour(accent.withAlpha(0.25f));
            g.fillPath(dashed);
            g.setColour(accentDim.withAlpha(0.9f));
            g.drawText(kMinus + juce::String(d), (int) geo::x1 - 44, (int) y - 14, 40, 12, juce::Justification::right);
        }
        juce::Path area;
        area.startNewSubPath(geo::x0, geo::yZero);
        for (int i = 0; i < N; ++i) area.lineTo(xi(i), geo::yZero + juce::jmin(grS[(size_t) i], 40.f) * geo::dbPx);
        auto line = area;
        area.lineTo(geo::x1, geo::yZero); area.closeSubPath();
        g.setGradientFill(juce::ColourGradient(accent.withAlpha(0.18f), 0.f, geo::yZero, accent.withAlpha(0.42f), 0.f, geo::yZero + 120.f, false));
        g.fillPath(area);
        juce::Path edge;
        for (int i = 0; i < N; ++i) {
            const float x = xi(i), y = geo::yZero + juce::jmin(grS[(size_t) i], 40.f) * geo::dbPx;
            if (i == 0) edge.startNewSubPath(x, y); else edge.lineTo(x, y);
        }
        g.setColour(accent);
        g.strokePath(edge, juce::PathStrokeType(1.6f, juce::PathStrokeType::curved));
        g.setColour(accent.withAlpha(0.55f));
        g.fillRect(geo::x0, geo::yZero, geo::x1 - geo::x0, 1.f);
        g.setFont(font(11.f, Weight::SemiBold));
        g.setColour(accentDim);
        g.drawText("0 dB", (int) geo::x0 - 44, (int) geo::yZero - 7, 38, 14, juce::Justification::right);
        juce::ignoreUnused(line);
    }

    // ---- protected harmonics: purple ticks along the bottom axis ----
    const float hp = proc.getReal(pid::harmonicProtect) / 100.f;
    if (hp > 0.f && f.f0 > 20.f && f0Conf > 0.3f && st.haveFrame) {
        for (int h = 1; h < 200; ++h) {
            const float hz = f.f0 * (float) h;
            if (hz > 10000.f) break;
            const float x = std::round(xOfHz(hz)) + 0.5f, len = 4.f + 16.f * hp * std::exp(-(float) h / 40.f);
            g.setColour(protectPurple.withAlpha((0.35f + 0.65f * hp) * juce::jlimit(0.f, 1.f, f0Conf * 1.5f)));
            g.drawLine(x, geo::y1, x, geo::y1 - len, 1.5f);
        }
    }

    // ---- labelled peaks ----
    if (st.haveFrame) {
        std::vector<float> placed;
        for (int k = 0; k < f.numPeaks; ++k) {
            const float hz = f.peaks[k].hz, db = juce::jmin(f.peaks[k].db, 40.f);
            const float x = xOfHz(hz);
            if (std::any_of(placed.begin(), placed.end(), [x](float px) { return std::abs(px - x) < 62.f; })) continue;
            placed.push_back(x);
            const float yTip = geo::yZero + db * geo::dbPx;
            const juce::String l1 = shortHz(hz) + kDot + noteName(hz), l2 = kMinus + juce::String(db, 1) + " dB";
            g.setColour(accent);
            g.fillEllipse(x - 2.6f, yTip - 2.6f, 5.2f, 5.2f);
            const auto f1 = font(11.f, Weight::SemiBold);
            const float w = juce::jmax(textWidth(f1, l1), 36.f) + 12.f;
            const float px = juce::jlimit(geo::x0 + 2.f, geo::x1 - w - 2.f, x - w / 2.f), py = yTip + 9.f;
            g.setColour(juce::Colour(0xd9141516));
            g.fillRoundedRectangle(px, py, w, 28.f, 3.f);
            g.setColour(edge);
            g.drawRoundedRectangle(px + 0.5f, py + 0.5f, w - 1.f, 27.f, 3.f, 1.f);
            g.setColour(textPrimary);
            g.setFont(f1);
            g.drawText(l1, (int) px, (int) py + 3, (int) w, 12, juce::Justification::centred);
            g.setColour(accent);
            g.setFont(font(10.5f, Weight::Medium));
            g.drawText(l2, (int) px, (int) py + 14, (int) w, 12, juce::Justification::centred);
        }
    }

    // ---- range nodes (low cut / high cut) ----
    for (int n = 1; n <= 2; ++n) {
        const float hz = proc.getReal(n == 1 ? pid::rangeLow : pid::rangeHigh);
        const float x = nodeX(n), y = geo::y1 - 10.f;
        const bool hot = hoverNode == n || dragNode == n;
        juce::Path vline, dashed;
        vline.startNewSubPath(std::round(x) + 0.5f, geo::yZero + 3.f); vline.lineTo(std::round(x) + 0.5f, y);
        const float dash[] = { 2.f, 3.f };
        juce::PathStrokeType(1.f).createDashedStroke(dashed, vline, dash, 2);
        g.setColour((hot ? accent : textLabel).withAlpha(hot ? 0.5f : 0.22f));
        g.fillPath(dashed);
        const float rr = hot ? 8.f : 6.5f;
        g.setColour(hot ? accent : raised);
        g.fillRoundedRectangle(x - rr, y - rr, rr * 2.f, rr * 2.f, 2.f);
        g.setColour(hot ? accent : (hoverNode == n ? textPrimary : textLabel));
        g.drawRoundedRectangle(x - rr, y - rr, rr * 2.f, rr * 2.f, 2.f, 1.6f);
        const juce::String lab = juce::String(n == 1 ? "LC " : "HC ") + shortHz(hz) + " Hz";
        const auto lf = font(11.f, Weight::SemiBold);
        const float lw = textWidth(lf, lab) + 10.f, lx = juce::jlimit(geo::x0, geo::x1 - lw, x - lw / 2.f), ly = y - rr - 19.f;
        g.setColour(juce::Colour(0xd9141516));
        g.fillRoundedRectangle(lx, ly, lw, 16.f, 3.f);
        g.setColour(hot ? accent : textLabel);
        g.setFont(lf);
        g.drawText(lab, (int) lx, (int) ly + 1, (int) lw, 14, juce::Justification::centred);
    }
}

void SpectrumView::paintHistory(juce::Graphics& g)
{
    using namespace tok;
    g.setImageResamplingQuality(juce::Graphics::highResamplingQuality);
    g.drawImage(heat, juce::Rectangle<float>(geo::x0, geo::y0, geo::x1 - geo::x0, geo::y1 - geo::y0));
    g.setFont(font(11.f, Weight::Regular));
    auto yHz = [&](float hz) { return geo::y1 - std::log(hz / kMinHz) / std::log(kMaxHz / kMinHz) * (geo::y1 - geo::y0); };
    for (float hz : { 50.f, 100.f, 500.f, 1000.f, 5000.f, 10000.f }) {
        const float y = std::round(yHz(hz)) + 0.5f;
        g.setColour(edge.withAlpha(0.7f));
        g.drawHorizontalLine((int) y, geo::x0, geo::x1);
        g.setColour(textSecondary);
        g.drawText(hz >= 1000.f ? juce::String((int) (hz / 1000.f)) + "k" : juce::String((int) hz), (int) geo::x0 - 44, (int) y - 7, 38, 14, juce::Justification::right);
    }
    const char* labels[] = { "-20 s", "-15 s", "-10 s", "-5 s", "now" };
    for (int i = 0; i < 5; ++i) {
        const float u = (float) i / 4.f, x = std::round(geo::x0 + (geo::x1 - geo::x0) * u) + 0.5f;
        g.setColour(edge.withAlpha(0.7f));
        g.drawVerticalLine((int) x, geo::y0, geo::y1);
        g.setColour(textSecondary);
        const juce::String t = juce::String(labels[i]).replace("-", kMinus);
        g.drawText(t, i == 0 ? (int) x + 2 : (i == 4 ? (int) x - 60 : (int) x - 30), (int) H - 19, 60, 12,
                   i == 0 ? juce::Justification::left : (i == 4 ? juce::Justification::right : juce::Justification::centred));
    }
    g.setColour(edge);
    g.drawRect(juce::Rectangle<float>(geo::x0, geo::y0, geo::x1 - geo::x0, geo::y1 - geo::y0), 1.f);
    // legend ramp
    const float gx = geo::x1 - 150.f, gy = geo::y0 - 24.f;
    g.setGradientFill(juce::ColourGradient(well, gx, 0.f, accent, gx + 90.f, 0.f, false));
    g.fillRect(gx, gy, 90.f, 8.f);
    g.setColour(edge);
    g.drawRect(gx, gy, 90.f, 8.f, 1.f);
    g.setColour(textSecondary);
    g.drawText("0 dB", (int) gx - 46, (int) gy - 3, 40, 14, juce::Justification::right);
    g.drawText(kMinus + "10 dB cut", (int) gx + 96, (int) gy - 3, 70, 14, juce::Justification::left);
}

void SpectrumView::paintOverlays(juce::Graphics& g)
{
    using namespace tok;
    const bool delta = proc.getReal(pid::delta) >= 0.5f, bypass = proc.getReal(pid::bypass) >= 0.5f;
    if (delta) {
        const auto f = font(11.f, Weight::Bold);
        const juce::String t = "DELTA: REMOVED SIGNAL ONLY";
        const float w = textWidth(f, t) + 14.f;
        auto box = juce::Rectangle<float>((float) W - 10.f - w, 10.f, w, 20.f);
        g.setColour(amber);
        g.drawRoundedRectangle(box, 3.f, 1.f);
        g.setFont(f);
        g.drawText(t, box.toNearestInt(), juce::Justification::centred);
    }
    if (bypass) {
        g.setColour(amber.withAlpha(0.9f));
        g.setFont(font(12.f, Weight::Bold));
        g.drawText("BYPASSED", (int) geo::x1 - 106, (int) geo::y0 + 4, 100, 14, juce::Justification::right);
    }
    // learn sweep + listening card
    if (st.learnProgress >= 0.f) {
        const float u = std::fmod(st.learnProgress * 3.f, 1.f), x = geo::x0 + (geo::x1 - geo::x0) * u;
        g.setGradientFill(juce::ColourGradient(accent.withAlpha(0.f), x - 60.f, 0.f, accent.withAlpha(0.18f), x, 0.f, false));
        g.fillRect(x - 60.f, geo::y0, 60.f, geo::y1 - geo::y0);
        g.setColour(accent);
        g.fillRect(x - 0.6f, geo::y0, 1.2f, geo::y1 - geo::y0);
        auto card = juce::Rectangle<float>(280.f, 84.f).withCentre({ (float) W * 0.5f, (float) H * 0.44f });
        g.setColour(juce::Colour(0xf0232427));
        g.fillRoundedRectangle(card, 6.f);
        g.setColour(edge);
        g.drawRoundedRectangle(card.reduced(0.5f), 6.f, 1.f);
        g.setColour(textPrimary);
        g.setFont(font(16.f, Weight::Bold));
        g.drawText(juce::String::fromUTF8("Listening\xe2\x80\xa6"), card.withTrimmedTop(10.f).withHeight(22.f).toNearestInt(), juce::Justification::centred);
        g.setColour(textSecondary);
        g.setFont(font(12.f, Weight::Regular));
        g.drawText("Analysing resonances in the input", card.withTrimmedTop(34.f).withHeight(16.f).toNearestInt(), juce::Justification::centred);
        auto bar = juce::Rectangle<float>(card.getX() + 16.f, card.getBottom() - 22.f, card.getWidth() - 32.f, 6.f);
        g.setColour(track);
        g.fillRoundedRectangle(bar, 3.f);
        g.setColour(accent);
        g.fillRoundedRectangle(bar.withWidth(bar.getWidth() * juce::jlimit(0.f, 1.f, st.learnProgress)), 3.f);
    }
    // hover readout
    if (hoverX >= geo::x0 && hoverX <= geo::x1 && view == View::Spectrum) {
        const float hz = hzOfX(hoverX);
        g.setColour(textPrimary.withAlpha(0.25f));
        g.drawVerticalLine((int) std::round(hoverX), geo::y0, geo::y1);
        g.setColour(textLabel);
        g.setFont(font(12.f, Weight::Medium));
        g.drawText(hzLong(hz) + kDot + noteName(hz), (int) W - 12 - 200 - (delta ? 200 : 0), 12, 200, 14, juce::Justification::right);
    }
    if (st.toastVisible()) {
        const auto f = font(12.5f, Weight::Medium);
        const float w = juce::jmin((float) W - 40.f, textWidth(f, st.toast) + 26.f);
        auto box = juce::Rectangle<float>(w, 28.f).withCentre({ (float) W * 0.5f, (float) H - 44.f });
        g.setColour(raised);
        g.fillRoundedRectangle(box, 4.f);
        g.setColour(edge);
        g.drawRoundedRectangle(box.reduced(0.5f), 4.f, 1.f);
        g.setColour(textPrimary);
        g.setFont(f);
        g.drawText(st.toast, box.toNearestInt(), juce::Justification::centred);
    }
}

//----------------------------------------------------------------------------------------------
void SpectrumView::mouseMove(const juce::MouseEvent& e)
{
    hoverX = e.position.x;
    hoverNode = hitNode(e.position);
    setMouseCursor(hoverNode ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::CrosshairCursor);
    repaint();
}

void SpectrumView::mouseDown(const juce::MouseEvent& e)
{
    dragNode = hitNode(e.position);
    if (dragNode) proc.getParam(dragNode == 1 ? pid::rangeLow : pid::rangeHigh)->beginChangeGesture();
}

void SpectrumView::mouseDrag(const juce::MouseEvent& e)
{
    hoverX = e.position.x;
    if (!dragNode) return;
    float hz = hzOfX(juce::jlimit(geo::x0, geo::x1, e.position.x));
    const float lo = proc.getReal(pid::rangeLow), hi = proc.getReal(pid::rangeHigh);
    if (dragNode == 1) hz = juce::jlimit(20.f, std::max(20.f, hi / 1.3f), hz);
    else hz = juce::jlimit(std::min(20000.f, lo * 1.3f), 20000.f, hz);
    const int id = dragNode == 1 ? pid::rangeLow : pid::rangeHigh;
    proc.getParam(id)->setValueNotifyingHost(realToNorm(id, hz));
    repaint();
}

void SpectrumView::mouseUp(const juce::MouseEvent&)
{
    if (dragNode) {
        proc.getParam(dragNode == 1 ? pid::rangeLow : pid::rangeHigh)->endChangeGesture();
        proc.historyCommit();
    }
    dragNode = 0;
}

void SpectrumView::mouseDoubleClick(const juce::MouseEvent& e)
{
    if (const int n = hitNode(e.position)) {
        const int id = n == 1 ? pid::rangeLow : pid::rangeHigh;
        proc.setParamReal(id, paramDefs()[(size_t) id].def);
        proc.historyCommit();
    }
}

//==============================================================================================
// Right strip
//==============================================================================================
void HarshnessStrip::tick()
{
    if (!isLive(*this)) return;
    if (st.haveFrame) {
        hin += (st.frame.harshIn - hin) * 0.12f;
        hout += (st.frame.harshOut - hout) * 0.12f;
        avg += (st.frame.avgReduction - avg) * 0.15f;
    }
    repaint();
}

void HarshnessStrip::paint(juce::Graphics& g)
{
    using namespace tok;
    auto r = getLocalBounds();
    paintPanel(g, r);
    g.setColour(juce::Colour(0xff3d3f43));
    g.drawRoundedRectangle(r.toFloat().reduced(0.5f), 6.f, 1.f);
    const float W = (float) r.getWidth();
    g.setColour(textLabel);
    g.setFont(font(14.f, Weight::Bold));
    g.drawText("HARSHNESS", 12, 9, 170, 18, juce::Justification::left);

    // two columns
    const float colW = 60.f;
    const float cx[2] = { W * 0.25f, W * 0.75f };
    const float vals[2] = { hin, hout };
    const juce::Colour cols[2] = { inputIce, accent };
    const char* caps[2] = { "IN", "OUT" };
    for (int i = 0; i < 2; ++i) {
        g.setColour(cols[i]);
        g.setFont(font(22.f, Weight::Bold));
        g.drawText(juce::String(juce::roundToInt(vals[i])), (int) (cx[i] - colW / 2), 36, (int) colW, 26, juce::Justification::centred);
        auto bar = juce::Rectangle<float>(34.f, 92.f).withCentre({ cx[i], 0.f }).withY(68.f);
        g.setColour(well);
        g.fillRoundedRectangle(bar, 3.f);
        const float h = (bar.getHeight() - 2.f) * juce::jlimit(0.f, 100.f, vals[i]) / 100.f;
        g.setColour(cols[i]);
        g.fillRect(bar.getX() + 1.f, bar.getBottom() - 1.f - h, bar.getWidth() - 2.f, h);
        g.setColour(edge.withAlpha(0.7f));
        for (int k = 1; k <= 3; ++k) g.fillRect(bar.getX() + 1.f, bar.getY() + bar.getHeight() * (float) k * 0.25f, bar.getWidth() - 2.f, 1.f);
        g.setColour(edgeSoft);
        g.drawRoundedRectangle(bar.reduced(0.5f), 3.f, 1.f);
        g.setColour(textLabel);
        g.setFont(juce::Font(juce::FontOptions(typeface(Weight::SemiBold)).withHeight(12.f).withKerningFactor(0.06f)));
        g.drawText(caps[i], (int) (cx[i] - colW / 2), 166, (int) colW, 16, juce::Justification::centred);
    }
    // change in harshness
    {
        const float change = hin > 1.f ? (hout - hin) / hin * 100.f : 0.f;
        const juce::String num = (change > 0.5f ? "+" : (change < -0.5f ? kMinus : "")) + juce::String(juce::roundToInt(std::abs(change))) + "%";
        const auto f1 = font(12.f, Weight::SemiBold), f2 = font(12.f, Weight::Regular);
        const float w1 = textWidth(f1, num), w2 = textWidth(f2, " harshness");
        const float x = (W - w1 - w2) * 0.5f;
        g.setColour(accent);
        g.setFont(f1);
        g.drawText(num, (int) x, 190, (int) w1 + 2, 16, juce::Justification::left);
        g.setColour(textSecondary);
        g.setFont(f2);
        g.drawText(" harshness", (int) (x + w1), 190, (int) w2 + 4, 16, juce::Justification::left);
    }
    g.setColour(edgeSoft);
    g.fillRect(12.f, 214.f, W - 24.f, 1.f);

    // average reduction vs budget
    g.setColour(textLabel);
    g.setFont(font(14.f, Weight::Bold));
    g.drawText("AVG REDUCTION", 12, 224, 170, 18, juce::Justification::left);
    const float budget = proc.getReal(pid::budget);
    {
        const juce::String v = (avg > 0.05f ? kMinus : "") + juce::String(avg, 1);
        const auto f1 = font(26.f, Weight::Bold);
        g.setColour(accent);
        g.setFont(f1);
        g.drawText(v, 12, 246, 110, 30, juce::Justification::left);
        g.setColour(textLabel);
        g.setFont(font(13.f, Weight::Medium));
        g.drawText("dB", 12 + (int) textWidth(f1, v) + 4, 254, 30, 18, juce::Justification::left);
    }
    const float used = budget > 0.01f ? juce::jlimit(0.f, 1.f, avg / budget) : 0.f;
    auto bar = juce::Rectangle<float>(12.f, 286.f, W - 24.f, 8.f);
    g.setColour(well);
    g.fillRoundedRectangle(bar, 4.f);
    g.setColour(used > 0.92f ? amber : accent);
    g.fillRoundedRectangle(bar.reduced(1.f).withWidth(juce::jmax(0.f, (bar.getWidth() - 2.f) * used)), 3.f);
    g.setColour(edgeSoft);
    g.drawRoundedRectangle(bar.reduced(0.5f), 4.f, 1.f);
    g.setFont(font(12.f, Weight::Regular));
    g.setColour(textSecondary);
    g.drawText("budget", 12, 300, 44, 16, juce::Justification::left);
    g.setColour(textLabel);
    g.setFont(font(12.f, Weight::SemiBold));
    g.drawText(juce::String(budget, 1) + " dB", 54, 300, 60, 16, juce::Justification::left);
    g.setColour(textSecondary);
    g.setFont(font(12.f, Weight::Regular));
    g.drawText(juce::String(juce::roundToInt(used * 100.f)) + "% used", (int) W - 12 - 70, 300, 70, 16, juce::Justification::right);

    // legend
    struct Row { const char* text; int kind; juce::Colour c; };
    const Row rows[] = { { "Input", 0, inputIce }, { "Expected curve", 1, textSecondary }, { "Cut applied", 2, accent },
                         { "Protected harmonics", 3, protectPurple }, { "Sidechain key", 0, keyCyan } };
    g.setFont(font(11.5f, Weight::Regular));
    float y = (float) r.getHeight() - 12.f - 5.f * 17.f;
    for (const auto& row : rows) {
        const float cy = y + 8.f;
        g.setColour(row.c);
        if (row.kind == 0) g.fillRect(12.f, cy - 1.f, 18.f, 2.f);
        else if (row.kind == 1) { for (int k = 0; k < 3; ++k) g.fillRect(12.f + (float) k * 7.f, cy - 1.f, 4.f, 2.f); }
        else if (row.kind == 2) { g.setColour(accent.withAlpha(0.4f)); g.fillRect(12.f, cy - 4.f, 18.f, 8.f); g.setColour(accent); g.fillRect(12.f, cy + 2.f, 18.f, 2.f); }
        else g.fillRect(20.f, cy - 5.f, 2.f, 10.f);
        g.setColour(textSecondary);
        g.drawText(row.text, 40, (int) y, (int) W - 44, 16, juce::Justification::left);
        y += 17.f;
    }
}

//==============================================================================================
void Meter::tick()
{
    if (!isLive(*this)) return;
    const float nl = proc.peakL.load(std::memory_order_relaxed), nr = proc.peakR.load(std::memory_order_relaxed);
    const float pk = juce::jmax(nl, nr);
    const auto now = juce::Time::getMillisecondCounter();
    if (pk >= holdPeak || now - holdTime > 1500) { holdPeak = pk; holdTime = now; }
    if (pk >= 1.0f) clipped = true;
    l = nl; r = nr;
    repaint();
}

void Meter::mouseDown(const juce::MouseEvent&) { clipped = false; holdPeak = 0.f; repaint(); }

void Meter::paint(juce::Graphics& g)
{
    using namespace tok;
    auto b = getLocalBounds().toFloat();
    const float labW = 12.f;
    const float rowH = (b.getHeight() - 4.f) * 0.5f;
    const float hdb = 20.f * std::log10(holdPeak + 1e-6f);
    for (int ch = 0; ch < 2; ++ch) {
        auto row = juce::Rectangle<float>(b.getX(), b.getY() + 1.f + (float) ch * (rowH + 2.f), b.getWidth(), rowH);
        g.setColour(textSecondary);
        g.setFont(font(10.f, Weight::Bold));
        g.drawText(ch == 0 ? "L" : "R", row.withWidth(labW).toNearestInt(), juce::Justification::centredLeft);
        auto rc = row.withTrimmedLeft(labW + 2.f).reduced(0.f, 1.f);
        g.setColour(well);
        g.fillRoundedRectangle(rc, 2.f);
        const float db = 20.f * std::log10((ch == 0 ? l : r) + 1e-5f);
        const float u = juce::jlimit(0.f, 1.f, (db + 60.f) / 60.f);
        g.setColour(db > -1.f ? danger : (db > -9.f ? amber : accent));
        g.fillRoundedRectangle(rc.withWidth(rc.getWidth() * u).reduced(0.f, 0.5f), 2.f);
        const float hu = juce::jlimit(0.f, 1.f, (hdb + 60.f) / 60.f);
        g.setColour(textPrimary);
        g.fillRect(rc.getX() + rc.getWidth() * hu - 1.f, rc.getY(), 1.5f, rc.getHeight());
        if (clipped) { g.setColour(danger); g.fillRect(rc.getRight() - 3.f, rc.getY(), 3.f, rc.getHeight()); }
        g.setColour(edgeSoft);
        g.drawRoundedRectangle(rc.reduced(0.5f), 2.f, 1.f);
    }
}

} // namespace chui
