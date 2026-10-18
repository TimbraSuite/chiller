#include "TestMain.h"
#include "engine/Params.h"
#include "engine/Presets.h"
#include <set>

using namespace chiller;

CH_TEST(params_table_size_and_unique_ids)
{
    const auto& d = paramDefs();
    CH_CHECK((int) d.size() == pid::count);
    std::set<std::string> ids;
    for (const auto& p : d) {
        CH_CHECK(!p.id.empty());
        CH_CHECK(!p.name.empty());
        CH_CHECK(ids.insert(p.id).second);
        CH_CHECK(findParam(p.id) >= 0);
    }
    CH_CHECK(findParam("does_not_exist") == -1);
}

CH_TEST(params_defaults_within_range)
{
    for (const auto& p : paramDefs()) {
        CH_CHECK(p.min < p.max);
        CH_CHECK(p.def >= p.min && p.def <= p.max);
        if (p.curve == Curve::Log) CH_CHECK(p.min > 0.f);
        if (p.curve == Curve::Disc) CH_CHECK((int) p.choices.size() == (int) (p.max - p.min) + 1 || p.choices.empty());
        if (!p.choices.empty()) CH_CHECK(p.def >= 0.f && p.def <= (float) p.choices.size() - 1.f);
        const float n = realToNorm(p, p.def);
        CH_CHECK(n >= 0.f && n <= 1.f);
        CH_CHECK_NEAR(normToReal(p, n), p.def, 1e-4 * std::max(1.f, std::abs(p.def)));
    }
}

CH_TEST(params_planned_values)
{
    auto d = [](int id) -> const ParamDef& { return paramDefs()[(size_t) id]; };
    CH_CHECK_NEAR(d(pid::threshold).def, 6.0, 1e-6);
    CH_CHECK_NEAR(d(pid::amount).def, 50.0, 1e-6);
    CH_CHECK_NEAR(d(pid::focus).def, 1.0 / 6.0, 1e-6);
    CH_CHECK_NEAR(d(pid::focus).min, 1.0 / 24.0, 1e-6);
    CH_CHECK_NEAR(d(pid::attack).def, 10.0, 1e-6);
    CH_CHECK_NEAR(d(pid::release).def, 120.0, 1e-6);
    CH_CHECK_NEAR(d(pid::maxCut).def, 12.0, 1e-6);
    CH_CHECK_NEAR(d(pid::rangeLow).def, 100.0, 1e-6);
    CH_CHECK_NEAR(d(pid::rangeHigh).def, 18000.0, 1e-6);
    CH_CHECK_NEAR(d(pid::mix).def, 100.0, 1e-6);
    CH_CHECK_NEAR(d(pid::trim).min, -24.0, 1e-6);
    CH_CHECK_NEAR(d(pid::output).max, 24.0, 1e-6);
    CH_CHECK(d(pid::mode).choices.size() == 3 && d(pid::mode).def == 1.f && d(pid::mode).choices[0] == "Live (low latency)");
    CH_CHECK(d(pid::scKey).curve == Curve::Toggle && d(pid::scKey).def == 0.f);
    CH_CHECK_NEAR(d(pid::budget).def, 6.0, 1e-6);
    CH_CHECK_NEAR(d(pid::threshold).max, 24.0, 1e-6);
    CH_CHECK_NEAR(d(pid::maxCut).max, 30.0, 1e-6);
    CH_CHECK(d(pid::stereoMode).choices.size() == 2);
}

CH_TEST(params_state_roundtrip)
{
    ParamState s;
    s.setReal(pid::release, 250.f);
    CH_CHECK_NEAR(s.real(pid::release), 250.0, 0.01);
    ParamState def;
    CH_CHECK_NEAR(def.real(pid::attack), 10.0, 1e-3);
}

CH_TEST(params_bands)
{
    for (int b = 0; b < pid::kBands; ++b) {
        const std::string n = std::to_string(b + 1);
        CH_CHECK(findParam("band" + n + "_on") == pid::band(b, pid::bandOn));
        CH_CHECK(findParam("band" + n + "_freq") == pid::band(b, pid::bandFreq));
        CH_CHECK(findParam("band" + n + "_depth") == pid::band(b, pid::bandDepth));
        CH_CHECK(findParam("band" + n + "_width") == pid::band(b, pid::bandWidth));
        ParamState s;
        CH_CHECK(s.real(pid::band(b, pid::bandOn)) == 0.f);
        CH_CHECK_NEAR(s.real(pid::band(b, pid::bandDepth)), 0.0, 1e-4);
        CH_CHECK_NEAR(s.real(pid::band(b, pid::bandFreq)), kBandDefaultHz[b], 0.01 * kBandDefaultHz[b]);
    }
    CH_CHECK(pid::band(pid::kBands - 1, pid::bandWidth) == pid::count - 1);
    CH_CHECK(!pid::isBandParam(pid::bypass) && pid::isBandParam(pid::bandBase));
}

CH_TEST(preset_values_roundtrip_and_keep_globals)
{
    ParamState a;
    a.setReal(pid::threshold, 9.f);
    a.setReal(pid::band(2, pid::bandOn), 1.f);
    a.setReal(pid::band(2, pid::bandDepth), -60.f);
    a.setReal(pid::band(2, pid::bandFreq), 3200.f);
    a.setReal(pid::mix, 40.f);                                  // global: not part of a preset
    const auto vals = presetValues(a);
    for (const auto& [id, v] : vals) CH_CHECK(isPresetParam(id));
    ParamState b;
    b.setReal(pid::mix, 70.f);
    b.setReal(pid::amount, 90.f);                               // owned, not in a: back to the preset's value
    applyPresetValues(b, vals);
    CH_CHECK_NEAR(b.real(pid::threshold), 9.0, 1e-3);
    CH_CHECK_NEAR(b.real(pid::band(2, pid::bandDepth)), -60.0, 1e-3);
    CH_CHECK_NEAR(b.real(pid::band(2, pid::bandFreq)), 3200.0, 0.5);
    CH_CHECK(b.real(pid::band(2, pid::bandOn)) == 1.f);
    CH_CHECK_NEAR(b.real(pid::amount), 50.0, 1e-3);
    CH_CHECK_NEAR(b.real(pid::mix), 70.0, 1e-3);                // kept
    // out-of-range values are clamped, unknown and global ids ignored
    ParamState c;
    applyPresetValues(c, { { pid::threshold, 99.f }, { pid::output, 12.f }, { -1, 3.f }, { pid::count + 5, 1.f } });
    CH_CHECK_NEAR(c.real(pid::threshold), 24.0, 1e-3);
    CH_CHECK_NEAR(c.real(pid::output), 0.0, 1e-3);
}
