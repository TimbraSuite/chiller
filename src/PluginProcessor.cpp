#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <algorithm>
#include <cmath>

using namespace chiller;

//==============================================================================================
// Formatting helpers
//==============================================================================================
static juce::String fmtReal(float r, const juce::String& unit, bool pretty)
{
    // Decide the format on the ROUNDED value, so "99.97 Hz" prints as "100 Hz" and text -> value
    // -> text round-trips consistently.
    auto rounded = [](float v, int dec) { const float k = std::pow(10.f, (float) dec); return std::round(v * k) / k; };
    if (unit == "Hz" || unit == "ms") {
        const juce::String big = unit == "Hz" ? " kHz" : " s";
        if (std::abs(rounded(r, 0)) >= 1000.f) return juce::String(r / 1000.f, 2) + big;
        if (std::abs(rounded(r, 1)) >= 100.f) return juce::String(rounded(r, 0), 0) + " " + unit;
        return juce::String(r, 1) + " " + unit;
    }
    if (unit == "dB") return juce::String(r, 1) + " dB";
    if (unit == "%") return juce::String(r, 0) + " %";
    if (unit == "oct") {
        // Host-facing text is always decimal so text -> value -> text round-trips (clap-validator checks this).
        // The UI asks for `pretty` and gets "1/6 oct" when the value sits on a 1/n fraction.
        if (pretty) {
            if (r >= 0.999f) return "1 oct";
            const float n = 1.f / std::max(r, 1.0e-3f);
            if (std::abs(n - std::round(n)) < 0.02f) return "1/" + juce::String((int) std::round(n)) + " oct";
        }
        return juce::String(r, 3) + " oct";
    }
    return juce::String(r, 2) + (unit.isEmpty() ? "" : " " + unit);
}

// Inverse of fmtReal: understands the unit suffixes fmtReal prints ("1.20 kHz", "1/6 oct").
static float parseReal(const juce::String& text, const juce::String& unit)
{
    const auto t = text.trim().toLowerCase();
    if (unit == "oct" && t.contains("/")) {
        const float den = t.fromFirstOccurrenceOf("/", false, false).getFloatValue();
        return den > 0.f ? t.upToFirstOccurrenceOf("/", false, false).getFloatValue() / den : 0.f;
    }
    float v = t.getFloatValue();
    if (unit == "Hz" && t.endsWith("khz")) v *= 1000.f;
    if (unit == "ms" && t.endsWith("s") && !t.endsWith("ms")) v *= 1000.f;
    return v;
}

juce::String chFormat(int id, float norm, bool pretty)
{
    const auto& d = paramDefs()[(size_t) id];
    const float r = normToReal(d, norm);
    if (d.curve == Curve::Toggle) return r >= 0.5f ? "On" : "Off";
    if (d.curve == Curve::Disc && !d.choices.empty()) {
        const int i = std::clamp((int) std::lround(r), 0, (int) d.choices.size() - 1);
        return juce::String(d.choices[(size_t) i]);
    }
    if (id == pid::speedTilt) return (r > 0.005f ? "+" : "") + juce::String(r, 2);
    return fmtReal(r, juce::String(d.unit), pretty);
}

//==============================================================================================
juce::AudioProcessorValueTreeState::ParameterLayout ChillerProcessor::createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    const auto& defs = paramDefs();
    for (int i = 0; i < (int) defs.size(); ++i) {
        const auto& d = defs[(size_t) i];
        float interval = 0.f;
        if ((d.curve == Curve::Disc || d.curve == Curve::Toggle) && d.max > d.min) interval = 1.f / (d.max - d.min);
        juce::NormalisableRange<float> range(0.f, 1.f, interval);
        auto attr = juce::AudioParameterFloatAttributes()
            .withStringFromValueFunction([i](float v, int) { return chFormat(i, v); })
            .withValueFromStringFunction([i](const juce::String& t) {
                const auto& d2 = paramDefs()[(size_t) i];
                for (size_t c = 0; c < d2.choices.size(); ++c)
                    if (t.trim().equalsIgnoreCase(juce::String(d2.choices[c]))) return realToNorm(d2, (float) c);
                if (d2.curve == Curve::Toggle) return (t.trim().equalsIgnoreCase("on") || t.getIntValue() > 0) ? 1.f : 0.f;
                return realToNorm(d2, parseReal(t, juce::String(d2.unit)));
            });
        layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { d.id, 1 }, d.name, range,
                                                               realToNorm(d, d.def), attr));
    }
    return layout;
}

ChillerProcessor::ChillerProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)
                         .withInput("Sidechain", juce::AudioChannelSet::stereo(), false)),
      apvts(*this, nullptr, "CHILLER", createLayout())
{
    const auto& defs = paramDefs();
    for (int i = 0; i < pid::count; ++i) {
        params[(size_t) i] = dynamic_cast<juce::RangedAudioParameter*>(apvts.getParameter(defs[(size_t) i].id));
        raw[(size_t) i] = apvts.getRawParameterValue(defs[(size_t) i].id);
        jassert(params[(size_t) i] && raw[(size_t) i]);
    }
}

bool ChillerProcessor::isBusesLayoutSupported(const BusesLayout& l) const
{
    const auto out = l.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::stereo() || l.getMainInputChannelSet() != out) return false;
    const auto sc = l.getChannelSet(true, 1);   // optional sidechain: disabled or stereo
    return sc.isDisabled() || sc == juce::AudioChannelSet::stereo();
}

static dsp::Mode modeFromRaw(float norm)
{
    return (dsp::Mode) std::clamp((int) std::lround(normToReal(pid::mode, norm)), 0, (int) dsp::Mode::Count - 1);
}

void ChillerProcessor::applyMode(dsp::Mode m)
{
    activeMode = m;
    engine.setMode(m);
    dryDelay.reset();
    dryDelay.setDelay(engine.latencySamples());
    if (getLatencySamples() != engine.latencySamples()) setLatencySamples(engine.latencySamples());
}

void ChillerProcessor::prepareToPlay(double sr, int block)
{
    currentSampleRate = sr;
    maxBlock = std::max(block, 64);
    numChannelsPrepared = 2;   // stereo main bus; the sidechain bus is optional and unused for now
    activeMode = modeFromRaw(getRaw(pid::mode));
    engine.prepare(sr, numChannelsPrepared, activeMode);
    dryDelay.prepare(dsp::fftSizeForMode(dsp::Mode::Master), numChannelsPrepared * 2);   // trimmed + raw copies
    dryDelay.setDelay(engine.latencySamples());
    wet.setSize(numChannelsPrepared, maxBlock, false, true, false);
    trimGain.assign((size_t) maxBlock, 1.f);
    auto init = [sr](juce::SmoothedValue<float>& s, float v, double secs = 0.02) { s.reset(sr, secs); s.setCurrentAndTargetValue(v); };
    init(sMix, getReal(pid::mix) / 100.f);
    init(sTrim, juce::Decibels::decibelsToGain(getReal(pid::trim)));
    init(sOutput, juce::Decibels::decibelsToGain(getReal(pid::output)));
    init(sBypass, getReal(pid::bypass), 0.01);
    init(sDelta, getReal(pid::delta), 0.01);
    setLatencySamples(engine.latencySamples());
}

void ChillerProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int total = buffer.getNumSamples();
    // main bus only: channels 0..1. Any sidechain channels follow in the buffer and are left untouched.
    const int nch = std::min({ buffer.getNumChannels(), getMainBusNumInputChannels(), numChannelsPrepared });
    if (nch < 1 || total == 0) return;

    // The processor reports latency for the current mode; re-sync when the mode parameter changes.
    const auto wanted = modeFromRaw(getRaw(pid::mode));
    if (wanted != activeMode) applyMode(wanted);

    sMix.setTargetValue(getReal(pid::mix) / 100.f);
    sTrim.setTargetValue(juce::Decibels::decibelsToGain(getReal(pid::trim)));
    sOutput.setTargetValue(juce::Decibels::decibelsToGain(getReal(pid::output)));
    sBypass.setTargetValue(getReal(pid::bypass));
    sDelta.setTargetValue(getReal(pid::delta));   // Delta = dry - wet (what the suppressor removed)

    float pl = 0.f, pr = 0.f;
    uint32_t w = scopeWrite.load(std::memory_order_relaxed);
    for (int start = 0; start < total; start += maxBlock) {
        const int n = std::min(maxBlock, total - start);
        std::array<float*, 2> chans {}, wetPtrs {};
        for (int c = 0; c < nch; ++c) {
            chans[(size_t) c] = buffer.getWritePointer(c) + start;
            wetPtrs[(size_t) c] = wet.getWritePointer(c);
        }

        // wet path input = trimmed input
        for (int i = 0; i < n; ++i) trimGain[(size_t) i] = sTrim.getNextValue();
        for (int c = 0; c < nch; ++c)
            for (int i = 0; i < n; ++i) wetPtrs[(size_t) c][i] = chans[(size_t) c][i] * trimGain[(size_t) i];

        // scope for the spectrum display (mono input, post-trim)
        for (int i = 0; i < n; ++i) {
            float m = 0.f;
            for (int c = 0; c < nch; ++c) m += wetPtrs[(size_t) c][i];
            scope[(w + (uint32_t) i) & (kScopeSize - 1)] = m / (float) nch;
        }
        w += (uint32_t) n;

        engine.process(wetPtrs.data(), n);   // Foundation: transparent, latencySamples() late

        for (int i = 0; i < n; ++i) {
            const float mx = sMix.getNextValue(), og = sOutput.getNextValue(), bp = sBypass.getNextValue(),
                        dl = sDelta.getNextValue();
            for (int c = 0; c < nch; ++c) {
                const float in = chans[(size_t) c][i];
                const float dry = dryDelay.process(c, in * trimGain[(size_t) i]);   // latency-compensated, trimmed
                const float rawDelayed = dryDelay.process(c + nch, in);              // for soft bypass
                const float wetS = wetPtrs[(size_t) c][i];
                const float processed = dry + (wetS - dry) * mx;                     // dry/wet mix
                const float listened = processed + ((dry - wetS) - processed) * dl;  // delta listen
                float y = (listened * og) + (rawDelayed - listened * og) * bp;       // soft bypass keeps latency
                if (!std::isfinite(y)) y = 0.f;
                chans[(size_t) c][i] = y;
                float& pk = c == 0 ? pl : pr;
                pk = std::max(pk, std::abs(y));
            }
            dryDelay.advance();
        }
    }
    if (nch < 2) pr = pl;
    scopeWrite.store(w, std::memory_order_release);
    const float decay = std::exp(-(float) total / (0.25f * (float) currentSampleRate));
    peakL.store(std::max(pl, peakL.load(std::memory_order_relaxed) * decay), std::memory_order_relaxed);
    peakR.store(std::max(pr, peakR.load(std::memory_order_relaxed) * decay), std::memory_order_relaxed);
}

void ChillerProcessor::copyInputScope(float* dst, int count) const
{
    const uint32_t w = scopeWrite.load(std::memory_order_acquire);
    for (int i = 0; i < count; ++i)
        dst[i] = scope[(w - (uint32_t) count + (uint32_t) i) & (kScopeSize - 1)];
}

juce::AudioProcessorEditor* ChillerProcessor::createEditor() { return new ChillerEditor(*this); }

//==============================================================================================
// State
//==============================================================================================
void ChillerProcessor::getStateInformation(juce::MemoryBlock& dest)
{
    // Write every parameter's live value ourselves: some wrappers (e.g. CLAP) change parameters
    // without notifying APVTS listeners, which would leave a stale copy in apvts.state.
    juce::ValueTree state(apvts.state.getType());
    state.setProperty("chVersion", kStateVersion, nullptr);
    const auto& defs = paramDefs();
    for (int i = 0; i < pid::count; ++i) {
        juce::ValueTree pt("PARAM");
        pt.setProperty("id", juce::String(defs[(size_t) i].id), nullptr);
        pt.setProperty("value", params[(size_t) i]->getValue(), nullptr);
        state.appendChild(pt, nullptr);
    }
    juce::ValueTree chTree("CH");
    chTree.setProperty("preset", presetName, nullptr);
    state.appendChild(chTree, nullptr);
    if (auto xml = state.createXml()) copyXmlToBinary(*xml, dest);
}

void ChillerProcessor::setStateInformation(const void* data, int size)
{
    auto xml = getXmlFromBinary(data, size);
    if (!xml) return;
    auto tree = juce::ValueTree::fromXml(*xml);
    if (!tree.isValid()) return;
    auto chTree = tree.getChildWithName("CH");
    if (chTree.isValid()) tree.removeChild(chTree, nullptr);
    apvts.replaceState(tree);
    // ...and set each parameter explicitly, in case the APVTS cache thought it was unchanged.
    for (auto pt : tree) {
        if (!pt.hasType("PARAM")) continue;
        const int i = findParam(pt.getProperty("id").toString().toStdString());
        if (i < 0) continue;
        const float v = juce::jlimit(0.f, 1.f, (float) pt.getProperty("value", params[(size_t) i]->getValue()));
        if (params[(size_t) i]->getValue() != v) params[(size_t) i]->setValueNotifyingHost(v);
    }
    if (chTree.isValid()) presetName = chTree.getProperty("preset", "Init").toString();
    // Tell the host every parameter may have new values (CLAP: params rescan; VST3: values changed).
    updateHostDisplay(ChangeDetails().withProgramChanged(true));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new ChillerProcessor(); }
