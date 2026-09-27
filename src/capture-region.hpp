#pragma once
#include <QRect>
#include <QString>
#include <QPointer>
#include <QScreen>
enum class CaptureKind { Region, Window, Monitor };
struct MonitorInfo {
    QString device, id;
    QRect physical; // Windows physical desktop pixels, including negative origins.
    QPointer<QScreen> screen;
    int index = -1;
};
struct CaptureRegion {
    int x = 0, y = 0, width = 0, height = 0, monitorIndex = -1;
};
struct CaptureTarget {
    CaptureKind kind = CaptureKind::Monitor;
    MonitorInfo monitor;
    QRect physical;
    QString title, windowValue;
    quintptr windowHandle = 0;
    quint32 processId = 0;
    bool valid() const { return physical.width() >= 2 && physical.height() >= 2 && !monitor.id.isEmpty(); }
};
