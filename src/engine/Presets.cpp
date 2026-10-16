#include "Presets.h"

namespace chiller {

const std::vector<Preset>& factoryPresets()
{
    using namespace pid;
    static const std::vector<Preset> presets = {
        { "Init", {} },
        { "Vocal De-Harsh",
          { { threshold, 4.5f }, { amount, 68.f }, { focus, 1.f / 6.f }, { attack, 3.f }, { release, 70.f }, { speedTilt, 0.25f },
            { harmonicProtect, 75.f }, { transientPass, 55.f }, { maxCut, 8.f }, { budget, 4.f }, { rangeLow, 120.f }, { rangeHigh, 16000.f } } },
        { "Bright Acoustic Guitar",
          { { threshold, 5.f }, { amount, 55.f }, { focus, 1.f / 9.f }, { attack, 6.f }, { release, 110.f }, { speedTilt, 0.1f },
            { harmonicProtect, 60.f }, { transientPass, 35.f }, { maxCut, 7.f }, { budget, 3.5f }, { rangeLow, 150.f } } },
        { "Drum Bus Tame",
          { { threshold, 6.f }, { amount, 45.f }, { focus, 1.f / 4.f }, { attack, 1.5f }, { release, 40.f }, { speedTilt, 0.35f },
            { harmonicProtect, 20.f }, { transientPass, 80.f }, { maxCut, 6.f }, { budget, 3.f } } },
        { "Mix Bus Gentle",
          { { threshold, 6.5f }, { amount, 35.f }, { focus, 1.f / 3.f }, { attack, 12.f }, { release, 160.f },
            { harmonicProtect, 70.f }, { transientPass, 50.f }, { maxCut, 4.f }, { budget, 2.f } } },
        { "Full Surgery",
          { { threshold, 2.7f }, { amount, 90.f }, { focus, 1.f / 12.f }, { attack, 2.f }, { release, 60.f }, { speedTilt, -0.2f },
            { harmonicProtect, 40.f }, { transientPass, 25.f }, { maxCut, 16.f }, { budget, 8.f } } },
    };
    return presets;
}

void applyPreset(ParamState& state, int index)
{
    const auto& ps = factoryPresets();
    if (index < 0 || index >= (int) ps.size()) return;
    ParamState def;
    for (int i = 0; i < pid::count; ++i) {
        if (i == pid::mode || i == pid::bypass || i == pid::stereoMode || i == pid::scKey || i == pid::delta ||
            i == pid::mix || i == pid::trim || i == pid::output || i == pid::autoGain) continue;
        state[i] = def[i];
    }
    for (const auto& [id, v] : ps[(size_t) index].values) state.setReal(id, v);
}

} // namespace chiller
