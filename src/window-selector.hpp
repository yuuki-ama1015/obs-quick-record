#pragma once
#include "capture-region.hpp"
#include <vector>
namespace WindowSelector {
QString encode(const QString &title, const QString &windowClass, const QString &exe);
CaptureTarget at(QPoint physicalPoint, const std::vector<MonitorInfo> &monitors);
bool stillValid(const CaptureTarget &target);
}
