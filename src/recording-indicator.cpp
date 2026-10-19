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
        setElapsedSeconds(elapsed.elapsed() / 1000);
    });
}
void RecordingIndicator::setElapsedSeconds(qint64 seconds)
{
    setText(QString::fromUtf8("● REC  %1:%2").arg(seconds / 60, 2, 10, QChar('0')).arg(seconds % 60, 2, 10, QChar('0')));
    // adjustSize() caps top-level windows to 2/3 of the screen, which can clip text.
    const auto required = sizeHint();
    if (size() != required) resize(required);
    // Keep the right edge anchored when the minute count gains another digit.
    move(availableArea.right() - width() - 16, availableArea.top() + 16);
}
void RecordingIndicator::start(const MonitorInfo &monitor)
{
    auto *screen = monitor.screen ? monitor.screen.data() : QGuiApplication::primaryScreen();
    if (!screen) return;
    availableArea = screen->availableGeometry(); // Qt logical pixels.
    elapsed.start();
    setElapsedSeconds(0);
    show();
    if (!SetWindowDisplayAffinity(reinterpret_cast<HWND>(winId()), WDA_EXCLUDEFROMCAPTURE))
        blog(LOG_WARNING, "OBS Quick Record: recording indicator capture exclusion unavailable; disable indicator if visible in recordings");
    timer.start(1000);
}
void RecordingIndicator::stop() { timer.stop(); hide(); }
