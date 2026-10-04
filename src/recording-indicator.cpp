#include "recording-indicator.hpp"
#include <QGuiApplication>
#include <windows.h>
#include <obs-module.h>
RecordingIndicator::RecordingIndicator() : QLabel(nullptr, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::WindowDoesNotAcceptFocus)
{
    setAttribute(Qt::WA_ShowWithoutActivating);
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setStyleSheet("QLabel { color: #ff6060; background: #202020; padding: 8px 14px; border-radius: 6px; font-size: 16px; }");
    connect(&timer, &QTimer::timeout, this, [this] {
        const auto seconds = elapsed.elapsed() / 1000;
        setText(QString::fromUtf8("● REC  %1:%2").arg(seconds / 60, 2, 10, QChar('0')).arg(seconds % 60, 2, 10, QChar('0')));
        adjustSize();
    });
}
void RecordingIndicator::start(const MonitorInfo &monitor)
{
    elapsed.start();
    setText(QString::fromUtf8("● REC  00:00"));
    adjustSize();
    auto *screen = monitor.screen ? monitor.screen.data() : QGuiApplication::primaryScreen();
    if (!screen) return;
    auto area = screen->availableGeometry(); // Qt logical pixels.
    move(area.right() - width() - 16, area.top() + 16);
    show();
    if (!SetWindowDisplayAffinity(reinterpret_cast<HWND>(winId()), WDA_EXCLUDEFROMCAPTURE))
        blog(LOG_WARNING, "OBS Quick Record: recording indicator capture exclusion unavailable; disable indicator if visible in recordings");
    timer.start(1000);
}
void RecordingIndicator::stop() { timer.stop(); hide(); }
