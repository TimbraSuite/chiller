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

} // namespace chiller
