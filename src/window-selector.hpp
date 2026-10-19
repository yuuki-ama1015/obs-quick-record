#pragma once
#include "capture-region.hpp"
#include <vector>
namespace WindowSelector {
QString encode(const QString &title, const QString &windowClass, const QString &exe);
CaptureTarget at(QPoint physicalPoint, const std::vector<MonitorInfo> &monitors);
bool sameWindow(const CaptureTarget &target); // Cheap HWND/PID check during capture warm-up.
bool stillValid(const CaptureTarget &target);
}
