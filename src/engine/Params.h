#pragma once
// Central parameter registry. Every parameter is stored normalized 0..1 (that is what hosts
// automate and what presets store). Engine code maps to real units with normToReal().
#include <string>
#include <vector>

namespace chiller {

enum class Curve : int { Lin, Log, Disc, Toggle };

struct ParamDef {
    std::string id;     // stable id used in state/presets, e.g. "max_cut"
    std::string name;   // display name, e.g. "Max Cut"
    float min = 0.f, max = 1.f, def = 0.f; // real units; def in real units
    Curve curve = Curve::Lin;
    std::vector<std::string> choices;      // for Disc params with named steps (min=0,max=n-1)
    const char* unit = "";
};

namespace pid {
enum Id : int {
    // detect
    threshold, amount, focus, maxCut, rangeLow, rangeHigh,
    // speed
    attack, release, speedTilt,
    // protect
    transientPass, harmonicProtect, budget,
    // stereo
    stereoMode, link,
    // output
    mix, trim, output, delta, autoGain,
    // global
    mode, bypass,
    count
};
} // namespace pid

// Parameter-state version, bump when ids change meaning.
constexpr int kStateVersion = 1;

const std::vector<ParamDef>& paramDefs();              // size == pid::count
int findParam(const std::string& id);                  // -1 if missing

float normToReal(const ParamDef& d, float norm);
float realToNorm(const ParamDef& d, float real);
inline float normToReal(int id, float norm) { return normToReal(paramDefs()[(size_t) id], norm); }
inline float realToNorm(int id, float real) { return realToNorm(paramDefs()[(size_t) id], real); }

// Plain container for a full parameter state (normalized).
struct ParamState {
    std::vector<float> v;
    ParamState();
    float& operator[](int i) { return v[(size_t) i]; }
    float operator[](int i) const { return v[(size_t) i]; }
    void setReal(int id, float real) { v[(size_t) id] = realToNorm(id, real); }
    float real(int id) const { return normToReal(id, v[(size_t) id]); }
};

} // namespace chiller
