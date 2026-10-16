#include "TestMain.h"
#include <cstring>
int main(int argc, char** argv)
{
    const char* filter = argc > 1 ? argv[1] : nullptr;
    int run = 0;
    for (auto& c : chtest::registry()) {
        if (filter && !std::strstr(c.name, filter)) continue;
        int before = chtest::failures();
        std::printf("[ RUN  ] %s\n", c.name);
        c.fn();
        ++run;
        std::printf(chtest::failures() == before ? "[  OK  ] %s\n" : "[FAILED] %s\n", c.name);
    }
    std::printf("\n%d tests, %d failed checks\n", run, chtest::failures());
    return chtest::failures() == 0 ? 0 : 1;
}
