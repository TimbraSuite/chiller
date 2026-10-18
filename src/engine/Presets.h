#pragma once
// Factory presets (JUCE-free). A preset lists real-unit values for the parameters it changes;
// everything else is reset to the default, except the global ones (mode, bypass, stereo mode).
#include "Params.h"
#include <vector>

namespace chiller {

struct Preset {
    const char* name;
    std::vector<std::pair<int, float>> values;   // parameter id -> real value
};

const std::vector<Preset>& factoryPresets();
// Writes the preset into `state`. Keeps mode, bypass, stereo mode, sidechain key, delta and the output
// section (mix, trim, output, auto gain) untouched.
void applyPreset(ParamState& state, int index);

// Shared by factory and user presets: which parameters a preset owns (everything except mode, bypass, stereo mode, sidechain
// key, delta and the output section), writing a preset (owned parameters to their defaults, then the listed real values,
// clamped; unknown or global ids are ignored) and reading one back (every owned parameter, real units).
bool isPresetParam(int id);
void applyPresetValues(ParamState& state, const std::vector<std::pair<int, float>>& values);
std::vector<std::pair<int, float>> presetValues(const ParamState& state);

} // namespace chiller
