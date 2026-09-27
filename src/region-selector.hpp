#pragma once
#include "capture-region.hpp"
#include <vector>
namespace RegionSelector {
CaptureTarget between(QPoint start, QPoint end, const std::vector<MonitorInfo> &monitors);
QRectF toLogical(const QRect &physical, const QRect &monitor, const QSize &logicalSize);
}
