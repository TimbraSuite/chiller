// Developer tool: renders the Chiller editor to a PNG without a host (software renderer).
//   chiller_uishot out.png [scale] [--history] [--delta] [--key] [--mode=live|mix|master]
// It feeds the processor a synthetic signal (pink-ish noise, a harmonic tone and a few resonances) so the
// spectrum, the cut and the meters have content.
#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "ui/Controls.h"
#include <cstdio>
#include <cstring>
#include <random>

int main(int argc, char** argv)
{
    const juce::ScopedJuceInitialiser_GUI init;
    const juce::File out = argc > 1 ? juce::File::getCurrentWorkingDirectory().getChildFile(argv[1]) : juce::File("chiller-ui.png");
    float scale = 1.f;
    bool history = false, delta = false, key = false;
    int mode = 1;
    for (int i = 2; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--history")) history = true;
        else if (!std::strcmp(argv[i], "--delta")) delta = true;
        else if (!std::strcmp(argv[i], "--key")) key = true;
        else if (!std::strcmp(argv[i], "--mode=live")) mode = 0;
        else if (!std::strcmp(argv[i], "--mode=master")) mode = 2;
        else if (std::atof(argv[i]) > 0.0) scale = (float) std::atof(argv[i]);
    }

    ChillerProcessor proc;
    proc.setParamReal(chiller::pid::mode, (float) mode);
    proc.setParamReal(chiller::pid::threshold, 4.5f);
    proc.setParamReal(chiller::pid::amount, 70.f);
    proc.setParamReal(chiller::pid::harmonicProtect, 55.f);
    proc.setParamReal(chiller::pid::transientPass, 40.f);
    proc.setParamReal(chiller::pid::maxCut, 12.f);
    proc.setParamReal(chiller::pid::budget, 4.f);
    proc.setParamReal(chiller::pid::autoGain, 1.f);
    proc.setParamReal(chiller::pid::rangeLow, 28.f);
    proc.setParamReal(chiller::pid::rangeHigh, 17500.f);
    if (delta) proc.setParamReal(chiller::pid::delta, 1.f);
    if (key) proc.setParamReal(chiller::pid::scKey, 1.f);
    const int block = 512;
    proc.prepareToPlay(48000.0, block);

    juce::AudioBuffer<float> buf(4, block);   // main stereo + stereo sidechain (disabled in the plugin bus layout)
    juce::MidiBuffer midi;
    std::mt19937 rng(1);
    std::normal_distribution<float> nd(0.f, 1.f);
    float b0 = 0, b1 = 0, b2 = 0;
    double ph = 0.0;
    auto sample = [&](double t) {
        const float w = nd(rng) * 0.012f;
        b0 = 0.99765f * b0 + w * 0.0990460f; b1 = 0.96300f * b1 + w * 0.2965164f; b2 = 0.57000f * b2 + w * 1.0526913f;
        float s = b0 + b1 + b2 + w * 0.1848f;
        const double f0 = 294.0;
        for (int h = 1; h <= 18; ++h)
            s += (float) (0.05 / std::sqrt((double) h) * std::sin(6.283185307 * f0 * h * t));
        s += 0.045f * (float) std::sin(6.283185307 * 404.0 * t) + 0.035f * (float) std::sin(6.283185307 * 1250.0 * t)
           + 0.05f * (float) std::sin(6.283185307 * 3650.0 * t) + 0.02f * (float) std::sin(6.283185307 * 5800.0 * t);
        return s;
    };
    std::unique_ptr<juce::AudioProcessorEditor> ed(proc.createEditor());
    ed->setSize((int) (ChillerEditor::kW * scale), (int) (ChillerEditor::kH * scale));
    chui::Tickable::forceShowing() = true;   // no window peer offscreen
    auto* editor = dynamic_cast<ChillerEditor*>(ed.get());
    editor->explainView().showHistory(history);
    const int blocks = history ? 900 : 300;
    for (int b = 0; b < blocks; ++b) {
        buf.clear();
        for (int i = 0; i < block; ++i, ph += 1.0) {
            const float s = sample(ph / 48000.0);
            buf.setSample(0, i, s); buf.setSample(1, i, s * 0.9f);
        }
        proc.processBlock(buf, midi);
        if (b % 3 == 2) editor->tickNow();
    }
    {
        chiller::dsp::UiFrame f;
        proc.readUi(f);
        std::printf("f0 %.1f Hz, confidence %.2f, avg reduction %.2f dB, harshness %.0f -> %.0f, peaks %d\n", f.f0, f.f0Conf, f.avgReduction, f.harshIn, f.harshOut, f.numPeaks);
    }
    auto img = ed->createComponentSnapshot(ed->getLocalBounds(), true, 1.0f);
    out.deleteFile();
    juce::FileOutputStream fos(out);
    if (!fos.openedOk()) { std::fprintf(stderr, "cannot write %s\n", out.getFullPathName().toRawUTF8()); return 1; }
    juce::PNGImageFormat().writeImageToStream(img, fos);
    std::printf("wrote %s (%dx%d)\n", out.getFullPathName().toRawUTF8(), img.getWidth(), img.getHeight());
    return 0;
}
