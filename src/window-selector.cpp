#include "window-selector.hpp"
#include <windows.h>
#include <dwmapi.h>
namespace {
QString escaped(QString value) { return value.replace("#", "#22").replace(":", "#3A"); }
bool eligible(HWND window)
{
    DWORD pid = 0, cloaked = 0;
    GetWindowThreadProcessId(window, &pid);
    if (!IsWindowVisible(window) || IsIconic(window) || pid == GetCurrentProcessId() ||
        GetWindowLongPtrW(window, GWL_EXSTYLE) & WS_EX_TOOLWINDOW) return false;
    if (SUCCEEDED(DwmGetWindowAttribute(window, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) && cloaked) return false;
    return GetWindowTextLengthW(window) > 0;
}
CaptureTarget describe(HWND window, const std::vector<MonitorInfo> &monitors)
{
    CaptureTarget target;
    if (!eligible(window)) return target;
    wchar_t title[4096]{}, className[256]{}, path[32768]{};
    DWORD pid = 0, length = static_cast<DWORD>(std::size(path));
    GetWindowThreadProcessId(window, &pid);
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process) return target;
    const bool found = QueryFullProcessImageNameW(process, 0, path, &length);
    CloseHandle(process);
    if (!found) return target;
    GetWindowTextW(window, title, static_cast<int>(std::size(title)));
    GetClassNameW(window, className, static_cast<int>(std::size(className)));
    RECT rect{};
    if (FAILED(DwmGetWindowAttribute(window, DWMWA_EXTENDED_FRAME_BOUNDS, &rect, sizeof(rect))))
        if (!GetWindowRect(window, &rect)) return target;
    target.physical = {rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top};
    for (const auto &monitor : monitors) if (monitor.physical.intersects(target.physical)) { target.monitor = monitor; break; }
    target.kind = CaptureKind::Window;
    target.title = QString::fromWCharArray(title);
    target.windowValue = WindowSelector::encode(target.title, QString::fromWCharArray(className), QString::fromWCharArray(path).section('\\', -1));
    target.windowHandle = reinterpret_cast<quintptr>(window);
    target.processId = pid;
    return target;
}
}
QString WindowSelector::encode(const QString &title, const QString &windowClass, const QString &exe)
{
    return escaped(title) + ":" + escaped(windowClass) + ":" + escaped(exe);
}
CaptureTarget WindowSelector::at(QPoint point, const std::vector<MonitorInfo> &monitors)
{
    struct Search { QPoint point; const std::vector<MonitorInfo> &monitors; CaptureTarget result; } search{point, monitors, {}};
    // EnumWindows is z-ordered and lets us skip our full-desktop overlay and all OBS windows.
    EnumWindows([](HWND window, LPARAM data) -> BOOL {
        auto &search = *reinterpret_cast<Search *>(data);
        if (!eligible(window)) return TRUE;
        RECT rect{};
        if (FAILED(DwmGetWindowAttribute(window, DWMWA_EXTENDED_FRAME_BOUNDS, &rect, sizeof(rect))))
            if (!GetWindowRect(window, &rect)) return TRUE;
        POINT point{search.point.x(), search.point.y()};
        if (!PtInRect(&rect, point)) return TRUE;
        search.result = describe(window, search.monitors);
        return !search.result.valid();
    }, reinterpret_cast<LPARAM>(&search));
    return search.result;
}
bool WindowSelector::stillValid(const CaptureTarget &target)
{
    auto current = describe(reinterpret_cast<HWND>(target.windowHandle), {target.monitor});
    return current.valid() && current.processId == target.processId && current.windowValue == target.windowValue;
}
