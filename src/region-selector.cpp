#include "region-selector.hpp"
#include <algorithm>
QRegion RegionSelector::selectionDamage(const QRect &previous, const QRect &current, const QRect &monitor, const QSize &logicalSize)
{
    if (previous == current) return {};
    const QRect surface(QPoint(0, 0), logicalSize);
    auto area = [&](const QRect &physical) {
        // Local logical pixels, including margin for the 3px selection outline.
        return physical.isEmpty() ? QRegion{} : QRegion(toLogical(physical, monitor, logicalSize)
            .toAlignedRect().adjusted(-4, -4, 4, 4).intersected(surface));
    };
    return area(previous).united(area(current));
}
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
QPoint RegionSelector::toPhysical(const QPointF &logical, const QRect &monitor, const QSize &logicalSize)
{
    if (monitor.isEmpty() || logicalSize.isEmpty()) return {};
    return {monitor.x() + qRound(logical.x() * monitor.width() / logicalSize.width()),
            monitor.y() + qRound(logical.y() * monitor.height() / logicalSize.height())};
}
