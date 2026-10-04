#include "quick-record-overlay.hpp"
#include <QApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPushButton>
#include <QWidget>
#include <QEventLoop>
#include <cassert>
#include <iostream>

extern "C" const char *obs_module_text(const char *key) { return key; }
class PaintCounter : public QObject {
public:
    int count = 0;
    bool eventFilter(QObject *, QEvent *event) override { if (event->type() == QEvent::Paint) ++count; return false; }
};

// Exercises real overlay event routing on Qt's minimal platform. No OS key/mouse
// injection or visible desktop overlay; this does not test Windows focus delivery.
int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    app.setQuitOnLastWindowClosed(false);
    QuickRecordOverlay overlay;
    int confirmed = 0, canceled = 0, ready = 0, reset = 0, settings = 0;
    QObject::connect(&overlay, &QuickRecordOverlay::confirmed, [&] { ++confirmed; });
    QObject::connect(&overlay, &QuickRecordOverlay::canceled, [&] { ++canceled; overlay.hide(); });
    QObject::connect(&overlay, &QuickRecordOverlay::selectionReady, [&] { ++ready; });
    QObject::connect(&overlay, &QuickRecordOverlay::selectionReset, [&] { ++reset; });
    QObject::connect(&overlay, &QuickRecordOverlay::settingsRequested, [&] { ++settings; });
    overlay.open();
    QWidget *surface = nullptr;
    for (auto *widget : QApplication::topLevelWidgets())
        if (widget->isVisible() && widget->windowTitle().startsWith("Title")) surface = widget;
    assert(surface);
    PaintCounter paints;
    surface->installEventFilter(&paints);
    auto wait = [] { QEventLoop loop; QTimer::singleShot(200, &loop, &QEventLoop::quit); loop.exec(); };
    wait();
    const auto initialPaints = paints.count;
    wait();
    assert(paints.count == initialPaints); // Region idle does not repaint at 60 Hz.
    auto key = [](QWidget *receiver, int value, bool repeat = false) {
        QKeyEvent event(QEvent::KeyPress, value, Qt::NoModifier, {}, repeat);
        QApplication::sendEvent(receiver, &event);
    };
    key(surface, Qt::Key_Return);
    assert(confirmed == 0); // No selection.
    auto drag = [&] {
        const QPointF first(surface->width() / 4., surface->height() / 2.);
        const QPointF last(surface->width() * .75, surface->height() * .9);
        QMouseEvent press(QEvent::MouseButtonPress, first, surface->mapToGlobal(first),
                          Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QMouseEvent release(QEvent::MouseButtonRelease, last, surface->mapToGlobal(last),
                            Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(surface, &press);
        QApplication::sendEvent(surface, &release);
        assert(overlay.target().valid());
    };
    drag();
    assert(ready == 1 && reset == 1 && confirmed == 0);
    QWidget foreign;
    key(&foreign, Qt::Key_Return);
    key(surface, Qt::Key_Return, true);
    assert(confirmed == 0); // Unrelated windows and key auto-repeat are ignored.
    key(surface, Qt::Key_Return);
    assert(confirmed == 1);
    QPushButton *region = nullptr, *settingsButton = nullptr;
    for (auto *button : surface->findChildren<QPushButton *>()) {
        if (button->text() == "Region") region = button;
        if (button->text() == "Settings") settingsButton = button;
    }
    assert(region && settingsButton);
    region->click();
    key(surface, Qt::Key_Return);
    assert(reset == 2 && confirmed == 1); // Mode change invalidates old selection.
    drag();
    key(region, Qt::Key_Enter); // Overlay child focus accepts numpad Enter too.
    assert(ready == 2 && confirmed == 2);
    settingsButton->click();
    assert(settings == 1);
    key(surface, Qt::Key_Escape);
    assert(canceled == 1 && !surface->isVisible());
    key(surface, Qt::Key_Return);
    assert(confirmed == 2); // Hidden surfaces never accept Enter.
    QPointer<QWidget> closed = surface;
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    assert(closed.isNull()); // Hidden full-desktop surfaces release their backing storage.
    overlay.open();
    surface = nullptr;
    for (auto *widget : QApplication::topLevelWidgets())
        if (widget->isVisible() && widget->windowTitle().startsWith("Title")) surface = widget;
    assert(surface);
    surface->close(); // Alt+F4/WM_CLOSE must use the same cancellation path as Esc.
    assert(canceled == 2);
    for (auto *widget : QApplication::topLevelWidgets())
        if (widget->windowTitle().startsWith("Title")) assert(!widget->isVisible());
    overlay.open();
    bool reopened = false;
    for (auto *widget : QApplication::topLevelWidgets())
        if (widget->isVisible() && widget->windowTitle().startsWith("Title")) reopened = true;
    assert(reopened);
    std::cout << "selection confirmation, scoped Enter/Escape, repeat and mode reset passed\n";
}
