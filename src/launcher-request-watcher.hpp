#pragma once
#include "launcher-request.hpp"
#include <QElapsedTimer>
#include <QFileSystemWatcher>
#include <QObject>
#include <QTimer>
#include <functional>
#include <utility>

// GUI-thread, one-shot launch request receiver. Watch the directory because an
// atomic publish replaces the file, and the file need not exist when OBS loads.
class LauncherRequestWatcher : public QObject {
public:
    LauncherRequestWatcher(QObject *parent, std::function<void(bool)> received)
        : QObject(parent), received(std::move(received))
    {
        retry.setInterval(500);
        deadline.setSingleShot(true);
        deadline.setTimerType(Qt::PreciseTimer);
        connect(&retry, &QTimer::timeout, this, [this] { check(); });
        connect(&deadline, &QTimer::timeout, this, [this] { stop(); });
    }
    ~LauncherRequestWatcher() override { stop(); }
    void start(const QString &requestPath, qint64 processId, int timeoutMs = 60000)
    {
        stop();
        if (requestPath.isEmpty() || timeoutMs <= 0) return;
        path = requestPath;
        directory = QFileInfo(path).absolutePath();
        pid = processId;
        timeout = timeoutMs;
        waiting = true;
        changes = new QFileSystemWatcher(this);
        connect(changes, &QFileSystemWatcher::directoryChanged, this, [this] { check(); });
        elapsed.start();
        deadline.start(timeout);
        if (!watchDirectory()) retry.start();
        // Register first, then check once to close the registration/publication gap.
        QTimer::singleShot(0, this, [this] { check(); });
    }
    void stop()
    {
        waiting = false;
        retry.stop();
        deadline.stop();
        if (changes) {
            disconnect(changes, nullptr, this, nullptr);
            const auto paths = changes->directories();
            if (!paths.isEmpty()) changes->removePaths(paths);
            // May be inside its directoryChanged handler. Release the watcher and
            // its native engine after the signal returns, rather than keep it resident.
            changes->deleteLater();
            changes = nullptr;
        }
    }
private:
    bool watchDirectory()
    {
        return changes && (changes->directories().contains(directory) ||
            (QFileInfo(directory).isDir() && changes->addPath(directory)));
    }
    void check()
    {
        if (!waiting) return;
        if (elapsed.elapsed() >= timeout) { stop(); return; }
        const bool watching = watchDirectory();
        bool autoExit = false;
        if (consumeLauncherRequest(path, pid, &autoExit)) {
            stop();
            auto callback = received;
            callback(autoExit);
            return;
        }
        // A locked request may become readable without a directory notification.
        // Missing/unavailable directories and foreign-PID requests also retain retry.
        if (!watching || QFile::exists(path)) {
            if (!retry.isActive()) retry.start();
        } else retry.stop();
    }
    std::function<void(bool)> received;
    QFileSystemWatcher *changes = nullptr; // Qt parent owns it; stop schedules safe destruction.
    QTimer retry{this}, deadline{this};
    QElapsedTimer elapsed;
    QString path, directory;
    qint64 pid = 0;
    int timeout = 60000;
    bool waiting = false;
};
