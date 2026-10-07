#include "region-selector.hpp"
#include "monitor-selector.hpp"
#include "window-selector.hpp"
#include <QGuiApplication>
#include <cassert>
#include <iostream>
int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    const auto connected = MonitorSelector::enumerate();
    assert(!connected.empty());
    for (const auto &monitor : connected)
        assert(monitor.screen && monitor.screen->geometry().topLeft() == monitor.physical.topLeft());
    assert(WindowSelector::encode("a:#3A", "class:1", "test.exe") == "a#3A#223A:class#3A1:test.exe");
    MonitorInfo left; left.id = "left"; left.physical = {-2560, -240, 2560, 1440};
    MonitorInfo right; right.id = "right"; right.physical = {0, 0, 3840, 2160};
    std::vector<MonitorInfo> monitors{left, right};
    auto selection = RegionSelector::between({-200,700}, {-1000,100}, monitors);
    assert(selection.valid() && selection.physical == QRect(-1000,100,800,600));
    assert(!RegionSelector::between({-200,100}, {200,700}, monitors).valid());
    assert(!RegionSelector::between({0,0}, {0,700}, monitors).valid());
    assert(RegionSelector::between({0,0}, {3840,2160}, monitors).valid());
    for (double dpi : {1.0, 1.25, 1.5, 2.0}) {
        QRect monitor(-2400, -600, 2400, 1200), selected(-2100, -300, 1200, 600);
        auto logical = RegionSelector::toLogical(selected, monitor, QSize(int(2400/dpi), int(1200/dpi)));
        assert(logical == QRectF(300/dpi, 300/dpi, 1200/dpi, 600/dpi));
        assert(RegionSelector::toPhysical(logical.topLeft(), monitor, QSize(int(2400/dpi), int(1200/dpi))) == selected.topLeft());
        assert(RegionSelector::toPhysical(logical.bottomRight(), monitor, QSize(int(2400/dpi), int(1200/dpi))) == selected.topLeft() + QPoint(selected.width(), selected.height()));
        const QSize size(int(2400/dpi), int(1200/dpi));
        const auto moved = selected.translated(30, 20);
        const auto damage = RegionSelector::selectionDamage(selected, moved, monitor, size);
        assert(damage.contains(logical.toAlignedRect())); // Erase the old transparent hole.
        assert(damage.contains(RegionSelector::toLogical(moved, monitor, size).toAlignedRect()));
        assert(damage.boundingRect() != QRect(QPoint(0, 0), size));
        assert(RegionSelector::selectionDamage(selected, selected, monitor, size).isEmpty());
        assert(RegionSelector::selectionDamage({}, selected, monitor, size) ==
               RegionSelector::selectionDamage(selected, {}, monitor, size)); // Cancel restores the old area.
    }
    assert(RegionSelector::selectionDamage({100, 100, 50, 50}, {120, 100, 50, 50}, left.physical, QSize(2560, 1440)).isEmpty());
    const auto clipped = RegionSelector::selectionDamage({}, {-20, -20, 100, 100}, right.physical, QSize(3840, 2160));
    assert(clipped.boundingRect().topLeft() == QPoint(0, 0));
    std::cout << "physical coordinates, reverse drag, cross-monitor rejection, 100/125/150/200% passed\n";
}
