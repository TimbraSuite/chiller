#include "TestMain.h"
#include "engine/Params.h"
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
    CH_CHECK_NEAR(d(pid::rangeLow).def, 40.0, 1e-6);
    CH_CHECK_NEAR(d(pid::rangeHigh).def, 18000.0, 1e-6);
    CH_CHECK_NEAR(d(pid::mix).def, 100.0, 1e-6);
    CH_CHECK_NEAR(d(pid::trim).min, -24.0, 1e-6);
    CH_CHECK_NEAR(d(pid::output).max, 24.0, 1e-6);
    CH_CHECK(d(pid::mode).choices.size() == 3 && d(pid::mode).def == 1.f);
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
