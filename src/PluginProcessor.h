#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_dsp/juce_dsp.h>
#include "dsp/Engine.h"
#include "engine/Params.h"
#include "engine/Presets.h"
#include <array>
#include <atomic>
#include <vector>

// ---- shared text formatting (used by host parameter text and the editor) ----
// real value with unit / choice name / On-Off. `pretty` is for the UI only (e.g. "1/6 oct"); host text stays decimal.
juce::String chFormat(int paramId, float norm, bool pretty = false);

class ChillerProcessor : public juce::AudioProcessor {
public:
    ChillerProcessor();
    ~ChillerProcessor() override = default;

    // AudioProcessor
    const juce::String getName() const override { return "Chiller"; }
    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& l) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;

    // ---- parameters ----
    juce::AudioProcessorValueTreeState apvts;
    juce::RangedAudioParameter* getParam(int id) const { return params[(size_t) id]; }
    float getRaw(int id) const { return raw[(size_t) id]->load(std::memory_order_relaxed); }   // normalized
    float getReal(int id) const { return chiller::normToReal(id, getRaw(id)); }

    // ---- UI data (message thread) ----
    juce::String presetName { "Init" };
    int presetIndex = 0;
    void readUi(chiller::dsp::UiFrame& f) { engine.readUi(f); }
    std::atomic<float> peakL { 0.f }, peakR { 0.f };    // output sample peaks (decaying)
    std::atomic<float> cpuLoad { 0.f };                 // fraction of the buffer time spent in processBlock
    std::atomic<int> hostPlaying { -1 };                // transport: 1 playing, 0 stopped, -1 unknown (no play head, e.g. Standalone)
    double getLatencyMs() const { return currentSampleRate > 0.0 ? 1000.0 * getLatencySamples() / currentSampleRate : 0.0; }
    double currentSampleRate = 48000.0;

    // ---- parameter-state helpers (message thread) ----
    chiller::ParamState captureState() const;
    void applyState(const chiller::ParamState&);        // sets every parameter as a host gesture
    void setParamReal(int id, float real);              // one parameter, as a gesture
    void loadPreset(int index);
    // undo / redo of user edits: controls call historyCommit() when a gesture ends
    void historyCommit();
    bool canUndo() const { return histIdx > 0; }
    bool canRedo() const { return histIdx + 1 < (int) hist.size(); }
    void undo();
    void redo();
    // A/B compare
    int abActive() const { return abSlot; }
    void selectAB(int slot);
    void copyToOtherAB();

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void applyMode(chiller::dsp::Mode m);

    chiller::dsp::Engine engine;
    chiller::dsp::DelayLine dryDelay;
    std::array<juce::RangedAudioParameter*, (size_t) chiller::pid::count> params {};
    std::array<std::atomic<float>*, (size_t) chiller::pid::count> raw {};
    chiller::dsp::Mode activeMode = chiller::dsp::Mode::Mix;
    int maxBlock = 512;
    juce::AudioBuffer<float> wet;
    juce::SmoothedValue<float> sMix, sTrim, sOutput, sBypass, sDelta, sAuto;
    float eDry = 0.f, eWet = 0.f, eCoef = 0.001f;
    std::vector<chiller::ParamState> hist;
    int histIdx = 0;
    chiller::ParamState abState[2];
    bool abValid[2] = { true, false };
    int abSlot = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ChillerProcessor)
};
