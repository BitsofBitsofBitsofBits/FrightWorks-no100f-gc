// Simulated monitor layouts for the Fullscreen monitor labels. tools/launcher-layout-tests checks the launcher against
// the same cases and expected text: change both together.
#include "monitor_labels.hpp"

#include <cstdio>
#include <string>
#include <vector>

using aurora::window::monitor_labels;
using aurora::window::MonitorLabelInput;

namespace {
int g_failures = 0;

MonitorLabelInput Monitor(const char* name, int x, int y, int w, int h, bool primary = false) {
    return {name, x, y, x + w, y + h, primary};
}

void Check(const char* name, const std::vector<MonitorLabelInput>& layout, const std::vector<std::string>& expected) {
    const auto labels = monitor_labels(layout);
    if (labels == expected) {
        return;
    }
    ++g_failures;
    std::fprintf(stderr, "FAIL %s\n", name);
    for (size_t i = 0; i < labels.size(); ++i) {
        std::fprintf(stderr, "  got '%s', want '%s'\n", labels[i].c_str(), i < expected.size() ? expected[i].c_str() : "");
    }
}
} // namespace

int main() {
    Check("no clash: names and places differ",
          {Monitor("XB271HU", 0, 0, 2560, 1440, true), Monitor("G215HV", 640, -1080, 1920, 1080),
           Monitor("VS248", 2560, 0, 1920, 1080), Monitor("VS248", 2560, -1080, 1920, 1080)},
          {"XB271HU (2560x1440, main)", "G215HV (1920x1080, above)", "VS248 (1920x1080, right)",
           "VS248 (1920x1080, above right)"});
    Check("two matching to the right: numbered left to right, whatever the list order",
          {Monitor("Main", 0, 0, 2560, 1440, true), Monitor("VS248", 4480, 0, 1920, 1080),
           Monitor("VS248", 2560, 0, 1920, 1080)},
          {"Main (2560x1440, main)", "VS248 (1920x1080, right 2)", "VS248 (1920x1080, right 1)"});
    Check("same left edge: numbered top to bottom",
          {Monitor("Main", 0, 0, 2560, 1440, true), Monitor("VS248", 2560, 980, 1920, 1080),
           Monitor("VS248", 2560, -100, 1920, 1080)},
          {"Main (2560x1440, main)", "VS248 (1920x1080, right 2)", "VS248 (1920x1080, right 1)"});
    Check("each clash counts on its own",
          {Monitor("Main", 0, 0, 2560, 1440, true), Monitor("VS248", -1920, 0, 1920, 1080),
           Monitor("VS248", -3840, 0, 1920, 1080), Monitor("VS248", 2560, 0, 1920, 1080),
           Monitor("VS248", 4480, 0, 1920, 1080)},
          {"Main (2560x1440, main)", "VS248 (1920x1080, left 2)", "VS248 (1920x1080, left 1)",
           "VS248 (1920x1080, right 1)", "VS248 (1920x1080, right 2)"});
    Check("same place but another size or name: no number",
          {Monitor("Main", 0, 0, 2560, 1440, true), Monitor("VS248", 2560, 0, 1920, 1080),
           Monitor("VS248", 4480, 0, 1280, 1024), Monitor("G215HV", 5760, 0, 1920, 1080)},
          {"Main (2560x1440, main)", "VS248 (1920x1080, right)", "VS248 (1280x1024, right)", "G215HV (1920x1080, right)"});
    Check("below and above left",
          {Monitor("Main", 0, 0, 2560, 1440, true), Monitor("VS248", 0, 1440, 1920, 1080),
           Monitor("VS248", -1920, -1080, 1920, 1080)},
          {"Main (2560x1440, main)", "VS248 (1920x1080, below)", "VS248 (1920x1080, above left)"});
    Check("overlapping the main monitor: no place word, number only on a clash",
          {Monitor("Main", 0, 0, 2560, 1440, true), Monitor("VS248", 100, 100, 1920, 1080),
           Monitor("VS248", 200, 100, 1920, 1080), Monitor("G215HV", 300, 100, 1920, 1080)},
          {"Main (2560x1440, main)", "VS248 (1920x1080, 1)", "VS248 (1920x1080, 2)", "G215HV (1920x1080)"});
    if (g_failures == 0) {
        std::puts("monitor labels: all cases pass");
    }
    return g_failures == 0 ? 0 : 1;
}
