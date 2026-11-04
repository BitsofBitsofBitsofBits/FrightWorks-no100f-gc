#pragma once

#include <algorithm>
#include <numeric>
#include <string>
#include <vector>

namespace aurora::window {
struct MonitorLabelInput {
  std::string name;
  int left = 0;
  int top = 0;
  int right = 0;
  int bottom = 0;
  bool primary = false;
};

// "VS248 (1920x1080, above right)": name, size and place relative to the main monitor. Monitors whose labels would
// match get a number, left to right, then top to bottom: "VS248 (1920x1080, right 2)". The launcher makes the same text
// (Monitors.cs Labels); runtime/tests/monitor_labels_tests.cpp and tools/launcher-layout-tests check the same cases.
inline std::vector<std::string> monitor_labels(const std::vector<MonitorLabelInput>& monitors) {
  MonitorLabelInput mainMonitor;
  for (const auto& monitor : monitors) {
    if (monitor.primary) {
      mainMonitor = monitor;
    }
  }
  std::vector<std::string> bases;
  std::vector<std::string> places;
  for (const auto& m : monitors) {
    std::string place = "main";
    if (!m.primary) {
      const std::string vertical = m.bottom <= mainMonitor.top ? "above" : m.top >= mainMonitor.bottom ? "below" : "";
      const std::string horizontal = m.right <= mainMonitor.left ? "left" : m.left >= mainMonitor.right ? "right" : "";
      place = vertical + (!vertical.empty() && !horizontal.empty() ? " " : "") + horizontal;
    }
    places.push_back(place);
    bases.push_back(m.name + " (" + std::to_string(m.right - m.left) + "x" + std::to_string(m.bottom - m.top));
  }

  std::vector<size_t> byPosition(monitors.size());
  std::iota(byPosition.begin(), byPosition.end(), size_t{0});
  std::stable_sort(byPosition.begin(), byPosition.end(), [&](size_t a, size_t b) {
    return monitors[a].left != monitors[b].left ? monitors[a].left < monitors[b].left : monitors[a].top < monitors[b].top;
  });
  std::vector<std::string> labels(monitors.size());
  for (const size_t i : byPosition) {
    int sameLabel = 0;
    int number = 0;
    for (const size_t other : byPosition) {
      if (bases[other] == bases[i] && places[other] == places[i]) {
        ++sameLabel;
        if (other == i) {
          number = sameLabel;
        }
      }
    }
    std::string tail = places[i];
    if (sameLabel > 1) {
      tail += (tail.empty() ? "" : " ") + std::to_string(number);
    }
    labels[i] = bases[i] + (tail.empty() ? "" : ", " + tail) + ")";
  }
  return labels;
}
} // namespace aurora::window
