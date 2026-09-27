#include "region-selector.hpp"
#include <algorithm>
CaptureTarget RegionSelector::between(QPoint start, QPoint end, const std::vector<MonitorInfo> &monitors)
{
    CaptureTarget result;
    result.kind = CaptureKind::Region;
    // Half-open physical pixel edges: a drag from 0 to 800 selects exactly 800 pixels.
    result.physical = QRect(std::min(start.x(), end.x()), std::min(start.y(), end.y()),
                           std::abs(end.x() - start.x()), std::abs(end.y() - start.y()));
    for (const auto &monitor : monitors) if (monitor.physical.contains(result.physical)) {
        result.monitor = monitor;
        result.title = monitor.device;
        break;
    }
    return result;
}
QRectF RegionSelector::toLogical(const QRect &physical, const QRect &monitor, const QSize &logicalSize)
{
    if (monitor.isEmpty()) return {};
    return QRectF((physical.x() - monitor.x()) * double(logicalSize.width()) / monitor.width(),
                  (physical.y() - monitor.y()) * double(logicalSize.height()) / monitor.height(),
                  physical.width() * double(logicalSize.width()) / monitor.width(),
                  physical.height() * double(logicalSize.height()) / monitor.height());
}
