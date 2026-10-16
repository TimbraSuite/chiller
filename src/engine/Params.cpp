#include "Params.h"
#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace chiller {

namespace {
std::vector<ParamDef> buildDefs()
{
    std::vector<ParamDef> d((size_t) pid::count);
    auto set = [&](int idx, std::string id, std::string name, float mn, float mx, float def,
                   Curve c = Curve::Lin, const char* unit = "", std::vector<std::string> choices = {}) {
        auto& p = d[(size_t) idx];
        p.id = std::move(id); p.name = std::move(name); p.min = mn; p.max = mx; p.def = def;
        p.curve = c; p.unit = unit; p.choices = std::move(choices);
    };
    auto choice = [&](int idx, std::string id, std::string name, std::vector<std::string> ch, int def) {
        const float n = (float) ch.size() - 1.f;
        set(idx, std::move(id), std::move(name), 0.f, n, (float) def, Curve::Disc, "", std::move(ch));
    };
    auto toggle = [&](int idx, std::string id, std::string name, bool def) {
        set(idx, std::move(id), std::move(name), 0.f, 1.f, def ? 1.f : 0.f, Curve::Toggle);
    };

    using namespace pid;
    // ---- detect ----
    set(threshold, "threshold", "Threshold", 0.f, 24.f, 6.f, Curve::Lin, "dB");
    set(amount, "amount", "Amount", 0.f, 100.f, 50.f, Curve::Lin, "%");
    set(focus, "focus", "Focus", 1.f / 24.f, 1.f, 1.f / 6.f, Curve::Log, "oct");
    set(maxCut, "max_cut", "Max Cut", 0.f, 30.f, 12.f, Curve::Lin, "dB");
    set(rangeLow, "range_low", "Range Low", 20.f, 20000.f, 100.f, Curve::Log, "Hz");
    set(rangeHigh, "range_high", "Range High", 20.f, 20000.f, 18000.f, Curve::Log, "Hz");
    // ---- speed ----
    set(attack, "attack", "Attack", 1.f, 100.f, 10.f, Curve::Log, "ms");
    set(release, "release", "Release", 10.f, 500.f, 120.f, Curve::Log, "ms");
    set(speedTilt, "speed_tilt", "Speed Tilt", -1.f, 1.f, 0.f);
    // ---- protect ----
    set(transientPass, "transient_pass", "Transient Pass", 0.f, 100.f, 0.f, Curve::Lin, "%");
    set(harmonicProtect, "harmonic_protect", "Harmonic Protect", 0.f, 100.f, 0.f, Curve::Lin, "%");
    set(budget, "budget", "Budget", 0.f, 12.f, 6.f, Curve::Lin, "dB");
    // ---- stereo ----
    choice(stereoMode, "stereo_mode", "Stereo Mode", { "L/R", "M/S" }, 0);
    set(link, "link", "Link", 0.f, 100.f, 100.f, Curve::Lin, "%");
    toggle(scKey, "sc_key", "Sidechain Key", false);
    // ---- output ----
    set(mix, "mix", "Mix", 0.f, 100.f, 100.f, Curve::Lin, "%");
    set(trim, "trim", "Trim", -24.f, 24.f, 0.f, Curve::Lin, "dB");
    set(output, "output", "Output", -24.f, 24.f, 0.f, Curve::Lin, "dB");
    toggle(delta, "delta", "Delta", false);
    toggle(autoGain, "auto_gain", "Auto Gain", false);
    // ---- global ----
    choice(mode, "mode", "Mode", { "Live (low latency)", "Mix", "Master" }, 1);
    toggle(bypass, "bypass", "Bypass", false);
    return d;
}
} // namespace

const std::vector<ParamDef>& paramDefs()
{
    static const std::vector<ParamDef> defs = buildDefs();
    return defs;
}

int findParam(const std::string& id)
{
    static const std::unordered_map<std::string, int> map = [] {
        std::unordered_map<std::string, int> m;
        const auto& d = paramDefs();
        for (size_t i = 0; i < d.size(); ++i) m[d[i].id] = (int) i;
        return m;
    }();
    auto it = map.find(id);
    return it == map.end() ? -1 : it->second;
}

float normToReal(const ParamDef& d, float x)
{
    x = std::clamp(x, 0.f, 1.f);
    switch (d.curve) {
        case Curve::Log:    return d.min * std::pow(d.max / d.min, x);
        case Curve::Disc:   return std::round(d.min + x * (d.max - d.min));
        case Curve::Toggle: return x >= 0.5f ? 1.f : 0.f;
        default:            return d.min + x * (d.max - d.min);
    }
}

float realToNorm(const ParamDef& d, float v)
{
    if (d.max == d.min) return 0.f;
    if (d.curve == Curve::Log)
        return std::clamp(std::log(std::max(v, d.min) / d.min) / std::log(d.max / d.min), 0.f, 1.f);
    return std::clamp((v - d.min) / (d.max - d.min), 0.f, 1.f);
}

ParamState::ParamState()
{
    const auto& d = paramDefs();
    v.resize(d.size());
    for (size_t i = 0; i < d.size(); ++i) v[i] = realToNorm(d[i], d[i].def);
}

} // namespace chiller
