#pragma once
#include <QRegion>
#include "capture-region.hpp"
#include <vector>
namespace RegionSelector {
QRegion selectionDamage(const QRect &previous, const QRect &current, const QRect &monitor, const QSize &logicalSize);
CaptureTarget between(QPoint start, QPoint end, const std::vector<MonitorInfo> &monitors);
QRectF toLogical(const QRect &physical, const QRect &monitor, const QSize &logicalSize);
QPoint toPhysical(const QPointF &logical, const QRect &monitor, const QSize &logicalSize);
}
