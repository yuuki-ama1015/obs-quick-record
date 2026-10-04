#pragma once
#include <QEvent>
#include <QPointer>
#include <QTimer>
#include <QWidget>
#include <functional>

// Ownership is process-local, never saved in settings or inferred from minimization.
class LauncherSession : public QObject {
public:
    explicit LauncherSession(std::function<bool()> busy) : busy(std::move(busy))
    {
        connect(&timer, &QTimer::timeout, this, [this] {
            if (!window) { retain(); return; }
            if (window->isVisible()) { retain(); return; }
            if (this->busy()) return;
            auto *main = window.data();
            retain(); // One normal close request; OBS owns prompts/remux/shutdown.
            main->close();
        });
    }
    ~LauncherSession() override { retain(); }
    void claim(QWidget *main)
    {
        if (!main || main->isVisible()) return;
        window = main;
        main->installEventFilter(this);
    }
    void complete() { if (window) timer.start(500); }
    void resume() { timer.stop(); }
    void retain()
    {
        timer.stop();
        if (window) window->removeEventFilter(this);
        window = nullptr;
    }
private:
    bool eventFilter(QObject *object, QEvent *event) override
    {
        if (object == window && (event->type() == QEvent::Show || event->type() == QEvent::WindowActivate)) retain();
        return false;
    }
    std::function<bool()> busy;
    QPointer<QWidget> window;
    QTimer timer;
};
