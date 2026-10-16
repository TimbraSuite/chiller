#pragma once
// Minimal test harness. Usage in src/tests/test_<area>.cpp:
//   CH_TEST(osc_saw_is_bandlimited) { ...; CH_CHECK(cond); CH_CHECK_NEAR(a, b, tol); }
// Run: chiller_tests [substring-filter]
#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace chtest {
struct Case { const char* name; std::function<void()> fn; };
inline std::vector<Case>& registry() { static std::vector<Case> r; return r; }
inline int& failures() { static int f = 0; return f; }
struct Reg { Reg(const char* n, std::function<void()> f) { registry().push_back({ n, std::move(f) }); } };
inline void fail(const char* file, int line, const std::string& msg)
{ ++failures(); std::printf("    FAIL %s:%d: %s\n", file, line, msg.c_str()); }
} // namespace chtest

#define CH_TEST(name) static void name(); static chtest::Reg reg_##name(#name, name); static void name()
#define CH_CHECK(c) do { if (!(c)) chtest::fail(__FILE__, __LINE__, #c); } while (0)
#define CH_CHECK_NEAR(a, b, tol) do { double _a = (a), _b = (b); if (!(std::fabs(_a - _b) <= (tol))) \
    chtest::fail(__FILE__, __LINE__, std::string(#a " ~= " #b "  got ") + std::to_string(_a) + " vs " + std::to_string(_b)); } while (0)
#define CH_INFO(...) std::printf("    " __VA_ARGS__)
