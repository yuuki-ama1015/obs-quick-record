#include "region-selector.hpp"
#include "monitor-selector.hpp"
#include "window-selector.hpp"
#include <QGuiApplication>
#include <QImage>
#include <QPainter>
#include <cassert>
#include <iostream>

// Compare a partial repaint with an independent full repaint, including fractional
// logical coordinates on a physical-pixel backing store. Old borders must vanish.
static void checkRepaint(const QRect &before, const QRect &after, double dpi)
{
    const QRect monitor(-600, -300, 600, 300);
    const QSize size(qRound(600 / dpi), qRound(300 / dpi));
    auto draw = [&](QImage &image, const QRect &selected, const QRegion &damage) {
        QPainter p(&image);
        p.setClipRegion(damage);
        p.setCompositionMode(QPainter::CompositionMode_Source);
        p.fillRect(QRect(QPoint(0, 0), size), QColor(0, 0, 0, 95));
        if (!selected.isEmpty()) {
            const auto rect = RegionSelector::toLogical(selected, monitor, size);
            p.fillRect(rect, QColor(0, 0, 0, 1));
            p.setCompositionMode(QPainter::CompositionMode_SourceOver);
            p.setPen(QPen(QColor("#ff5252"), 3));
            p.drawRect(rect.adjusted(1, 1, -1, -1));
        }
    };
    const QRegion full(QRect(QPoint(0, 0), size));
    QImage partial(QSize(qRound(size.width() * dpi), qRound(size.height() * dpi)), QImage::Format_ARGB32_Premultiplied);
    partial.setDevicePixelRatio(dpi);
    partial.fill(Qt::transparent);
    draw(partial, before, full);
    const auto damage = RegionSelector::selectionDamage(before, after, monitor, size);
    draw(partial, after, damage);
    QImage expected(partial.size(), partial.format());
    expected.setDevicePixelRatio(dpi);
    expected.fill(Qt::transparent);
    draw(expected, after, full);
    assert(partial == expected);
}
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
        const auto changedFill = QRegion(logical.toAlignedRect()).xored(QRegion(RegionSelector::toLogical(moved, monitor, size).toAlignedRect()));
        assert(changedFill.subtracted(damage).isEmpty());
        assert(!damage.contains(logical.center().toPoint())); // The unchanged interior stays cached.
        qint64 dirtyPixels = 0;
        for (const auto &rect : damage) dirtyPixels += qint64(rect.width()) * rect.height();
        assert(dirtyPixels < qint64(logical.width() * logical.height()) / 4);
        assert(damage.boundingRect() != QRect(QPoint(0, 0), size));
        assert(RegionSelector::selectionDamage(selected, selected, monitor, size).isEmpty());
        assert(RegionSelector::selectionDamage({}, selected, monitor, size) ==
               RegionSelector::selectionDamage(selected, {}, monitor, size)); // Cancel restores the old area.
        const QRect before(-530, -250, 420, 200);
        for (const QRect &after : {before, before.translated(1, 1), before.translated(60, 30),
                                  before.adjusted(0, 0, -150, -80), before.adjusted(-30, -20, 20, 10),
                                  QRect(-700, -350, 250, 180), QRect(-50, -50, 150, 100), QRect(-410, -160, 2, 2), QRect{}}) {
            checkRepaint(before, after, dpi);
            checkRepaint(after, before, dpi);
        }
    }
    assert(RegionSelector::selectionDamage({100, 100, 50, 50}, {120, 100, 50, 50}, left.physical, QSize(2560, 1440)).isEmpty());
    const auto clipped = RegionSelector::selectionDamage({}, {-20, -20, 100, 100}, right.physical, QSize(3840, 2160));
    assert(clipped.boundingRect().topLeft() == QPoint(0, 0));
    std::cout << "physical coordinates, reverse drag, cross-monitor rejection, 100/125/150/200% passed\n";
}
