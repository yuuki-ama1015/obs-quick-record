#pragma once
#include "capture-region.hpp"
#include <vector>
namespace MonitorSelector {
std::vector<MonitorInfo> enumerate();
QPoint cursor(); // Windows physical pixels.
CaptureTarget at(const QPoint &point, const std::vector<MonitorInfo> &monitors);
bool stillValid(const MonitorInfo &monitor);
}
