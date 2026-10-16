#include "Displays.h"

namespace chui {

static void paintDisplayBg(juce::Graphics& g, juce::Rectangle<int> r)
{
    g.setColour(tok::well);
    g.fillRoundedRectangle(r.toFloat(), tok::rControl);
    g.setColour(tok::edgeSoft);
    g.drawRoundedRectangle(r.toFloat().reduced(0.5f), tok::rControl, 1.f);
}

//==============================================================================================
SpectrumView::SpectrumView(ChillerProcessor& p) : proc(p)
{
    for (size_t i = 0; i < window.size(); ++i)
        window[i] = 0.5f - 0.5f * std::cos(2.f * juce::MathConstants<float>::pi * (float) i / (float) window.size());
}

float SpectrumView::xForHz(juce::Rectangle<float> in, float hz) const
{
    return in.getX() + in.getWidth() * std::log(hz / kMinHz) / std::log(kMaxHz / kMinHz);
}

void SpectrumView::tick()
{
    if (!isLive(*this)) return;
    proc.copyInputScope(buf.data(), kSize);
    std::fill(fftData.begin(), fftData.end(), 0.f);
    for (size_t i = 0; i < (size_t) kSize; ++i) fftData[i] = buf[i] * window[i];
    fft.performFrequencyOnlyForwardTransform(fftData.data());
    const float sr = (float) juce::jmax(8000.0, proc.currentSampleRate);
    const float binHz = sr / (float) kSize;
    for (int b = 0; b < kBars; ++b) {
        const float f0 = kMinHz * std::pow(kMaxHz / kMinHz, (float) b / (float) kBars);
        const float f1 = kMinHz * std::pow(kMaxHz / kMinHz, (float) (b + 1) / (float) kBars);
        const int i0 = juce::jlimit(1, kSize / 2 - 1, (int) std::floor(f0 / binHz));
        const int i1 = juce::jlimit(i0, kSize / 2 - 1, (int) std::ceil(f1 / binHz));
        float mag = 0.f;
        for (int i = i0; i <= i1; ++i) mag = std::max(mag, fftData[(size_t) i]);
        if (f1 - f0 < binHz) {   // band narrower than one bin (low end): interpolate instead of stair-stepping
            const float pos = juce::jlimit(1.f, (float) kSize / 2 - 2.f, 0.5f * (f0 + f1) / binHz);
            const int i = (int) pos;
            const float fr = pos - (float) i;
            mag = fftData[(size_t) i] * (1.f - fr) + fftData[(size_t) i + 1] * fr;
        }
        // 0 dBFS sine under a Hann window reads about kSize/4
        const float db = 20.f * std::log10(mag / ((float) kSize * 0.25f) + 1e-7f);
        const float v = juce::jlimit(0.f, 1.f, (db - kMinDb) / (kMaxDb - kMinDb));
        bars[(size_t) b] = std::max(v, bars[(size_t) b] * 0.88f);
    }
    repaint();
}

void SpectrumView::paint(juce::Graphics& g)
{
    auto r = getLocalBounds();
    paintDisplayBg(g, r);
    auto in = r.toFloat().reduced(8.f, 6.f).withTrimmedBottom(14.f).withTrimmedRight(16.f);   // labels sit outside
    g.setFont(fMeta());
    // dB grid
    for (int db = -80; db <= -10; db += 10) {
        const float y = in.getBottom() - in.getHeight() * (float) (db - kMinDb) / (kMaxDb - kMinDb);
        g.setColour(db % 20 == 0 ? tok::edgeSoft : tok::edgeSoft.withAlpha(0.45f));
        g.drawHorizontalLine((int) y, in.getX(), in.getRight());
        if (db % 20 == 0) {
            g.setColour(tok::textSecondary.withAlpha(0.8f));
            g.drawText(juce::String(db), (int) in.getRight() + 2, (int) y - 6, 22, 12, juce::Justification::left);
        }
    }
    // frequency grid
    for (float f : { 20.f, 30.f, 40.f, 50.f, 60.f, 70.f, 80.f, 90.f, 100.f, 200.f, 300.f, 400.f, 500.f, 600.f, 700.f, 800.f, 900.f,
                     1000.f, 2000.f, 3000.f, 4000.f, 5000.f, 6000.f, 7000.f, 8000.f, 9000.f, 10000.f, 20000.f }) {
        const bool major = f == 20.f || f == 100.f || f == 1000.f || f == 10000.f || f == 20000.f;
        const float x = xForHz(in, f);
        g.setColour(major ? tok::edgeSoft : tok::edgeSoft.withAlpha(0.4f));
        g.drawVerticalLine((int) x, in.getY(), in.getBottom());
    }
    for (float f : { 20.f, 50.f, 100.f, 200.f, 500.f, 1000.f, 2000.f, 5000.f, 10000.f, 20000.f }) {
        const float x = xForHz(in, f);
        g.setColour(tok::textSecondary);
        g.drawText(f >= 1000.f ? juce::String(f / 1000.f, f == 1000.f || f == 2000.f || f == 5000.f || f >= 10000.f ? 0 : 1) + "k" : juce::String((int) f),
                   (int) x - 16, (int) in.getBottom() + 2, 32, 11, juce::Justification::centred);
    }

    juce::Path p;
    for (int b = 0; b < kBars; ++b) {
        const float x = in.getX() + in.getWidth() * (float) b / (float) (kBars - 1);
        const float y = in.getBottom() - in.getHeight() * bars[(size_t) b];
        if (b == 0) p.startNewSubPath(x, y); else p.lineTo(x, y);
    }
    auto fill = p;
    fill.lineTo(in.getRight(), in.getBottom());
    fill.lineTo(in.getX(), in.getBottom());
    fill.closeSubPath();
    g.setGradientFill(juce::ColourGradient(tok::inputIce.withAlpha(0.16f), 0.f, in.getY(), tok::inputIce.withAlpha(0.01f), 0.f, in.getBottom(), false));
    g.fillPath(fill);
    g.setColour(tok::inputIce.withAlpha(0.95f));
    g.strokePath(p, juce::PathStrokeType(1.4f));

    g.setColour(tok::textLabel);
    g.setFont(fSection());
    g.drawText("EXPLAIN", r.reduced(10, 6), juce::Justification::topLeft);
    g.setColour(tok::textSecondary);
    g.setFont(fMeta());
    g.drawText("input spectrum", r.reduced(10, 6).withTrimmedLeft(64), juce::Justification::topLeft);
}

//==============================================================================================
void Meter::tick()
{
    if (!isLive(*this)) return;
    const float nl = proc.peakL.load(std::memory_order_relaxed), nr = proc.peakR.load(std::memory_order_relaxed);
    const float pk = juce::jmax(nl, nr);
    const auto now = juce::Time::getMillisecondCounter();
    if (pk >= holdPeak || now - holdTime > 1500) { holdPeak = pk; holdTime = now; } // 1.5 s peak hold
    if (pk >= 1.0f) clipped = true;                                                 // latches until clicked
    if (std::abs(nl - l) > 0.002f || std::abs(nr - r) > 0.002f || std::abs(pk - lastShown) > 0.002f) { l = nl; r = nr; lastShown = pk; repaint(); }
}

void Meter::mouseDown(const juce::MouseEvent&) { clipped = false; holdPeak = 0.f; repaint(); }

void Meter::paint(juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    auto top = b.removeFromTop(13.f);
    auto led = top.removeFromRight(28.f);
    const float hdb = 20.f * std::log10(holdPeak + 1e-6f);
    g.setFont(fValue());
    g.setColour(hdb > -0.1f ? tok::danger : tok::textPrimary);
    g.drawText(holdPeak < 1.0e-4f ? juce::String::fromUTF8("-\xe2\x88\x9e dB") : juce::String(hdb, 1) + " dB", top.toNearestInt(),
               juce::Justification::centredLeft);
    g.setColour(clipped ? tok::danger : tok::track);
    g.fillRoundedRectangle(led.reduced(1.f, 1.5f), 2.f);
    g.setColour(clipped ? juce::Colours::black : tok::textSecondary);
    g.setFont(fBadge());
    g.drawText("CLIP", led.toNearestInt(), juce::Justification::centred);
    b.removeFromTop(3.f);
    auto draw = [&](juce::Rectangle<float> rc, float peak) {
        g.setColour(tok::track);
        g.fillRoundedRectangle(rc, 2.f);
        const float db = 20.f * std::log10(peak + 1e-5f);
        const float u = juce::jlimit(0.f, 1.f, (db + 60.f) / 60.f);
        g.setColour(db > -1.f ? tok::danger : (db > -9.f ? tok::amber : tok::accent));
        g.fillRoundedRectangle(rc.withWidth(rc.getWidth() * u), 2.f);
        const float hu = juce::jlimit(0.f, 1.f, (hdb + 60.f) / 60.f); // peak-hold tick
        g.setColour(tok::textPrimary);
        g.fillRect(rc.getX() + rc.getWidth() * hu - 1.f, rc.getY(), 1.5f, rc.getHeight());
    };
    const float h = (b.getHeight() - 2.f) * 0.5f;
    draw(b.withHeight(h), l);
    draw(b.withTrimmedTop(h + 2.f), r);
}

} // namespace chui
