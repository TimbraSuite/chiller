#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <algorithm>
#include <chrono>
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
    hist.push_back(captureState());
    abState[0] = abState[1] = hist.back();
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
    activeMode = modeFromRaw(getRaw(pid::mode));
    engine.prepare(sr, activeMode);
    dryDelay.prepare(dsp::fftSizeFor(dsp::Mode::Master, sr), 2);
    dryDelay.setDelay(engine.latencySamples());
    wet.setSize(2, maxBlock, false, true, false);
    auto init = [sr](juce::SmoothedValue<float>& s, float v, double secs = 0.02) { s.reset(sr, secs); s.setCurrentAndTargetValue(v); };
    init(sMix, getReal(pid::mix) / 100.f);
    init(sTrim, juce::Decibels::decibelsToGain(getReal(pid::trim)));
    init(sOutput, juce::Decibels::decibelsToGain(getReal(pid::output)));
    init(sBypass, getReal(pid::bypass), 0.01);
    init(sDelta, getReal(pid::delta), 0.01);
    init(sAuto, 1.f, 0.1);
    eDry = eWet = 0.f;
    eCoef = 1.f - std::exp(-1.f / (0.4f * (float) sr));
    setLatencySamples(engine.latencySamples());
}

static dsp::SuppressorParams suppressorParams(const ChillerProcessor& p)
{
    dsp::SuppressorParams s;
    s.thresholdDb = p.getReal(pid::threshold);
    s.amount = p.getReal(pid::amount) / 100.f;
    s.focusOct = p.getReal(pid::focus);
    s.maxCutDb = p.getReal(pid::maxCut);
    s.rangeLowHz = p.getReal(pid::rangeLow);
    s.rangeHighHz = p.getReal(pid::rangeHigh);
    s.attackMs = p.getReal(pid::attack);
    s.releaseMs = p.getReal(pid::release);
    s.speedTilt = p.getReal(pid::speedTilt);
    s.transientPass = p.getReal(pid::transientPass) / 100.f;
    s.harmonicProtect = p.getReal(pid::harmonicProtect) / 100.f;
    s.budgetDb = p.getReal(pid::budget);
    s.link = p.getReal(pid::link) / 100.f;
    s.displayMix = p.getReal(pid::mix) / 100.f * (1.f - p.getReal(pid::bypass));
    s.midSide = p.getReal(pid::stereoMode) >= 0.5f;
    s.scKey = p.getReal(pid::scKey) >= 0.5f;
    return s;
}

void ChillerProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const auto t0 = std::chrono::steady_clock::now();
    const int total = buffer.getNumSamples();
    if (buffer.getNumChannels() < 2 || total == 0) return;   // stereo only (see isBusesLayoutSupported)

    // transport state, so LEARN can wait for the host to start playing
    {
        int playing = -1;
        if (auto* ph = getPlayHead())
            if (const auto pos = ph->getPosition()) playing = pos->getIsPlaying() ? 1 : 0;
        hostPlaying.store(playing, std::memory_order_relaxed);
    }

    // The processor reports latency for the current mode; re-sync when the mode parameter changes.
    const auto wanted = modeFromRaw(getRaw(pid::mode));
    if (wanted != activeMode) applyMode(wanted);
    engine.setParams(suppressorParams(*this));

    // optional sidechain bus (detection only)
    const float* scIn[2] = { nullptr, nullptr };
    if (auto* bus = getBus(true, 1); bus != nullptr && bus->isEnabled() && bus->getNumberOfChannels() > 0) {
        auto scBuf = getBusBuffer(buffer, true, 1);   // a view on the host's channels, no allocation
        scIn[0] = scBuf.getReadPointer(0);
        scIn[1] = scBuf.getNumChannels() > 1 ? scBuf.getReadPointer(1) : scIn[0];
    }

    sMix.setTargetValue(getReal(pid::mix) / 100.f);
    sTrim.setTargetValue(juce::Decibels::decibelsToGain(getReal(pid::trim)));
    sOutput.setTargetValue(juce::Decibels::decibelsToGain(getReal(pid::output)));
    sBypass.setTargetValue(getReal(pid::bypass));
    const bool deltaOn = getReal(pid::delta) >= 0.5f;
    sDelta.setTargetValue(deltaOn ? 1.f : 0.f);   // Delta = (dry - wet) * mix: what the suppressor removed
    const bool autoOn = getReal(pid::autoGain) >= 0.5f;

    float pl = 0.f, pr = 0.f;
    for (int start = 0; start < total; start += maxBlock) {
        const int n = std::min(maxBlock, total - start);
        float* l = buffer.getWritePointer(0) + start;
        float* r = buffer.getWritePointer(1) + start;
        float* wl = wet.getWritePointer(0);
        float* wr = wet.getWritePointer(1);
        std::copy(l, l + n, wl);
        std::copy(r, r + n, wr);
        engine.process(wl, wr, scIn[0] ? scIn[0] + start : nullptr, scIn[1] ? scIn[1] + start : nullptr, n);

        // auto gain: compensate by the smoothed energy ratio dry/wet (only ever boosts, capped at +6 dB)
        for (int i = 0; i < n; ++i) {
            const float mx = sMix.getNextValue(), tg = sTrim.getNextValue(), og = sOutput.getNextValue(),
                        bp = sBypass.getNextValue(), dl = sDelta.getNextValue(), ag = sAuto.getNextValue();
            float eD = 0.f, eW = 0.f;
            for (int c = 0; c < 2; ++c) {
                const float in = c == 0 ? l[i] : r[i];
                const float dry = dryDelay.process(c, in);             // latency-compensated input
                const float wetS = c == 0 ? wl[i] : wr[i];
                eD += dry * dry; eW += wetS * wetS;
                const float processed = dry + (wetS * tg * ag - dry) * mx;
                const float removed = (dry - wetS) * mx;
                const float listened = processed + (removed - processed) * dl;
                float y = listened * og;
                y += (dry - y) * bp;                                    // soft bypass keeps the latency
                if (!std::isfinite(y)) y = 0.f;
                (c == 0 ? l : r)[i] = y;
                float& pk = c == 0 ? pl : pr;
                pk = std::max(pk, std::abs(y));
            }
            eDry += (eD - eDry) * eCoef;
            eWet += (eW - eWet) * eCoef;
            dryDelay.advance();
        }
        const float want = autoOn && !deltaOn ? std::clamp(std::sqrt((eDry + 1.0e-9f) / (eWet + 1.0e-9f)), 1.f, 1.995f) : 1.f;
        sAuto.setTargetValue(want);
    }
    const float decay = std::exp(-(float) total / (0.25f * (float) currentSampleRate));
    peakL.store(std::max(pl, peakL.load(std::memory_order_relaxed) * decay), std::memory_order_relaxed);
    peakR.store(std::max(pr, peakR.load(std::memory_order_relaxed) * decay), std::memory_order_relaxed);
    const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    const float inst = (float) (secs / ((double) total / currentSampleRate));
    cpuLoad.store(cpuLoad.load(std::memory_order_relaxed) * 0.9f + inst * 0.1f, std::memory_order_relaxed);
}

juce::AudioProcessorEditor* ChillerProcessor::createEditor() { return new ChillerEditor(*this); }

//==============================================================================================
// State helpers: capture / apply, presets, undo, A/B
//==============================================================================================
ParamState ChillerProcessor::captureState() const
{
    ParamState s;
    for (int i = 0; i < pid::count; ++i) s[i] = params[(size_t) i]->getValue();
    return s;
}

void ChillerProcessor::applyState(const ParamState& s)
{
    for (int i = 0; i < pid::count; ++i) {
        if (params[(size_t) i]->getValue() == s[i]) continue;
        params[(size_t) i]->beginChangeGesture();
        params[(size_t) i]->setValueNotifyingHost(s[i]);
        params[(size_t) i]->endChangeGesture();
    }
}

void ChillerProcessor::setParamReal(int id, float real)
{
    auto* p = params[(size_t) id];
    p->beginChangeGesture();
    p->setValueNotifyingHost(realToNorm(id, real));
    p->endChangeGesture();
}

void ChillerProcessor::loadPreset(int index)
{
    const int n = (int) factoryPresets().size();
    if (n == 0) return;
    index = ((index % n) + n) % n;
    auto s = captureState();
    applyPreset(s, index);
    applyState(s);
    presetIndex = index;
    presetName = factoryPresets()[(size_t) index].name;
    historyCommit();
}

void ChillerProcessor::historyCommit()
{
    auto s = captureState();
    if (!hist.empty() && hist[(size_t) histIdx].v == s.v) return;
    hist.resize((size_t) histIdx + 1);
    hist.push_back(s);
    ++histIdx;
    if (hist.size() > 200) { hist.erase(hist.begin()); --histIdx; }
}

void ChillerProcessor::undo()
{
    if (!canUndo()) return;
    historyCommit();   // make sure unsaved edits are not lost
    if (!canUndo()) return;
    applyState(hist[(size_t) --histIdx]);
}

void ChillerProcessor::redo()
{
    if (!canRedo()) return;
    applyState(hist[(size_t) ++histIdx]);
}

void ChillerProcessor::selectAB(int slot)
{
    slot = std::clamp(slot, 0, 1);
    if (slot == abSlot) return;
    abState[abSlot] = captureState();
    abValid[abSlot] = true;
    if (!abValid[slot]) { abState[slot] = abState[abSlot]; abValid[slot] = true; }
    abSlot = slot;
    applyState(abState[slot]);
    historyCommit();
}

void ChillerProcessor::copyToOtherAB()
{
    const int o = 1 - abSlot;
    abState[o] = captureState();
    abValid[o] = true;
}

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
    hist.assign(1, captureState());
    histIdx = 0;
    abState[0] = abState[1] = hist.back();
    abValid[0] = true; abValid[1] = false; abSlot = 0;
    // Tell the host every parameter may have new values (CLAP: params rescan; VST3: values changed).
    updateHostDisplay(ChangeDetails().withProgramChanged(true));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new ChillerProcessor(); }
