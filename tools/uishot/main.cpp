// Developer tool: renders the Chiller editor to a PNG without a host (software renderer).
//   chiller_uishot out.png [scale]
// It feeds the processor a synthetic signal (noise + a few resonant tones) so the spectrum has content.
#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "ui/Controls.h"
#include <cstdio>
#include <random>

int main(int argc, char** argv)
{
    const juce::ScopedJuceInitialiser_GUI init;
    const juce::File out = argc > 1 ? juce::File::getCurrentWorkingDirectory().getChildFile(argv[1]) : juce::File("chiller-ui.png");
    const float scale = argc > 2 ? (float) std::atof(argv[2]) : 1.f;

    ChillerProcessor proc;
    proc.prepareToPlay(48000.0, 512);
    juce::AudioBuffer<float> buf(2, 512);
    juce::MidiBuffer midi;
    std::mt19937 rng(1);
    std::normal_distribution<float> nd(0.f, 0.05f);
    double ph = 0.0;
    for (int b = 0; b < 40; ++b) {
        for (int i = 0; i < 512; ++i, ph += 1.0) {
            // pink-ish noise via one-pole lowpass + resonances at 220 Hz, 2.8 kHz, 6.1 kHz
            static float lp = 0.f;
            lp += 0.2f * (nd(rng) - lp);
            const float s = lp * 2.f + 0.12f * std::sin(2.0 * 3.14159265 * 220.0 * ph / 48000.0)
                          + 0.09f * std::sin(2.0 * 3.14159265 * 2800.0 * ph / 48000.0)
                          + 0.05f * std::sin(2.0 * 3.14159265 * 6100.0 * ph / 48000.0);
            buf.setSample(0, i, s); buf.setSample(1, i, s * 0.9f);
        }
        proc.processBlock(buf, midi);
    }
    std::unique_ptr<juce::AudioProcessorEditor> ed(proc.createEditor());
    ed->setSize((int) (ChillerEditor::kW * scale), (int) (ChillerEditor::kH * scale));
    chui::Tickable::forceShowing() = true;   // no window peer offscreen
    for (int i = 0; i < 6; ++i) {
        proc.processBlock(buf, midi);
        chui::Tickable::tickAll();
    }
    auto img = ed->createComponentSnapshot(ed->getLocalBounds(), true, 1.0f);
    out.deleteFile();
    juce::FileOutputStream fos(out);
    if (!fos.openedOk()) { std::fprintf(stderr, "cannot write %s\n", out.getFullPathName().toRawUTF8()); return 1; }
    juce::PNGImageFormat().writeImageToStream(img, fos);
    std::printf("wrote %s (%dx%d)\n", out.getFullPathName().toRawUTF8(), img.getWidth(), img.getHeight());
    return 0;
}
