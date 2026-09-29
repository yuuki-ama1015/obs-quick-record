#include "monitor-selector.hpp"
#include <QGuiApplication>
#ifdef _WIN32
#include <windows.h>
#endif
std::vector<MonitorInfo> MonitorSelector::enumerate()
{
    std::vector<MonitorInfo> result;
#ifdef _WIN32
    const auto previous = SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    EnumDisplayMonitors(nullptr, nullptr, [](HMONITOR handle, HDC, LPRECT, LPARAM data) -> BOOL {
        auto &list = *reinterpret_cast<std::vector<MonitorInfo> *>(data);
        MONITORINFOEXW info{};
        info.cbSize = sizeof(info);
        if (!GetMonitorInfoW(handle, &info)) return TRUE;
        DISPLAY_DEVICEW device{};
        device.cb = sizeof(device);
        if (!EnumDisplayDevicesW(info.szDevice, 0, &device, EDD_GET_DEVICE_INTERFACE_NAME)) return TRUE;
        MonitorInfo monitor;
        monitor.device = QString::fromWCharArray(info.szDevice);
        monitor.id = QString::fromWCharArray(device.DeviceID);
        monitor.physical = QRect(info.rcMonitor.left, info.rcMonitor.top,
            info.rcMonitor.right - info.rcMonitor.left, info.rcMonitor.bottom - info.rcMonitor.top);
        monitor.index = static_cast<int>(list.size());
        // QScreen::name() is a friendly label on Windows, not \\.\DISPLAYn.
        // Qt scales screen sizes for DPI but preserves their physical origins.
        for (auto *screen : QGuiApplication::screens())
            if (screen->geometry().topLeft() == monitor.physical.topLeft()) monitor.screen = screen;
        if (monitor.screen) list.push_back(monitor);
        return TRUE;
    }, reinterpret_cast<LPARAM>(&result));
    if (previous) SetThreadDpiAwarenessContext(previous);
#endif
    return result;
}
QPoint MonitorSelector::cursor()
{
    POINT point{};
    GetPhysicalCursorPos(&point);
    return {point.x, point.y};
}
CaptureTarget MonitorSelector::at(const QPoint &point, const std::vector<MonitorInfo> &monitors)
{
    for (const auto &monitor : monitors) if (monitor.physical.contains(point)) {
        CaptureTarget target;
        target.kind = CaptureKind::Monitor;
        target.monitor = monitor;
        target.physical = monitor.physical;
        target.title = monitor.device;
        return target;
    }
    return {};
}
bool MonitorSelector::stillValid(const MonitorInfo &monitor)
{
    for (const auto &current : enumerate())
        if (current.id == monitor.id && current.physical == monitor.physical) return true;
    return false;
}
