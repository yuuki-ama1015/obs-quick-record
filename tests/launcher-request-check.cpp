#include "launcher-request.hpp"
#include "launcher-request-watcher.hpp"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QSaveFile>
#include <QDir>
#include <windows.h>
#include <cassert>
#include <QApplication>
#include <QCloseEvent>
#include <QEventLoop>
#include "launcher-session.hpp"
#include "foreground-safety.hpp"
class MainWindow : public QWidget {
public:
    int closes = 0;
    bool cleaned = true;
    void closeEvent(QCloseEvent *event) override { assert(cleaned); ++closes; event->accept(); }
};
int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    app.setQuitOnLastWindowClosed(false);
    QTemporaryDir dir;
    assert(dir.isValid());
    const auto path = dir.filePath("request.txt");
    auto write = [&](const QByteArray &data) { QFile file(path); assert(file.open(QIODevice::WriteOnly)); assert(file.write(data) == data.size()); };
    assert(!consumeLauncherRequest(path, 42));
    write("42");
    assert(!consumeLauncherRequest(path, 41));
    assert(consumeLauncherRequest(path, 42));
    assert(!consumeLauncherRequest(path, 42));
    write("invalid");
    assert(!consumeLauncherRequest(path, 42) && !QFile::exists(path));
    bool autoExit = true;
    write("42");
    assert(consumeLauncherRequest(path, 42, &autoExit) && !autoExit);
    write("42\nexit-after-capture");
    assert(!consumeLauncherRequest(path, 41, &autoExit) && !autoExit);
    assert(consumeLauncherRequest(path, 42, &autoExit) && autoExit);
    write("42\nunknown");
    assert(!consumeLauncherRequest(path, 42, &autoExit) && !autoExit);
    write("42");
    QFile file(path); assert(file.open(QIODevice::ReadWrite));
    assert(file.setFileTime(QDateTime::currentDateTime().addSecs(-61), QFileDevice::FileModificationTime)); file.close();
    assert(!consumeLauncherRequest(path, 42) && !QFile::exists(path));
    {
        int received = 0;
        bool exitAfter = false;
        LauncherRequestWatcher receiver(nullptr, [&](bool value) { ++received; exitAfter = value; });
        auto pump = [](int milliseconds) {
            QEventLoop loop; QTimer::singleShot(milliseconds, &loop, &QEventLoop::quit); loop.exec();
        };
        auto until = [&](auto condition) {
            QElapsedTimer elapsed; elapsed.start();
            while (!condition() && elapsed.elapsed() < 3000) pump(20);
            assert(condition());
        };
        auto publish = [](const QString &target, const QByteArray &data) {
            QSaveFile request(target); assert(request.open(QIODevice::WriteOnly));
            assert(request.write(data) == data.size()); assert(request.commit());
        };
        auto polling = [&] {
            for (auto *timer : receiver.findChildren<QTimer *>())
                if (!timer->isSingleShot() && timer->isActive()) return true;
            return false;
        };
        auto watching = [&] {
            for (auto *watcher : receiver.findChildren<QFileSystemWatcher *>())
                if (!watcher->directories().isEmpty()) return true;
            return false;
        };
        // A request published before registration is consumed on the first GUI turn.
        publish(path, "42\nexit-after-capture");
        receiver.start(path, 42);
        until([&] { return received == 1; });
        assert(exitAfter && !QFile::exists(path) && !watching() && !polling());
        assert(receiver.findChildren<QFileSystemWatcher *>().isEmpty());

        receiver.start(path, 42);
        pump(100);
        assert(watching() && !polling()); // No 500ms idle polling.
        publish(path, "41");
        pump(100);
        assert(received == 1 && QFile::exists(path)); // A different OBS PID cannot open selection.
        publish(path, "42"); // Atomic replacement must remain observable.
        until([&] { return received == 2; });
        assert(!exitAfter && !QFile::exists(path));
        publish(path, "42");
        pump(100);
        assert(received == 2 && QFile::exists(path)); // One request per startup window.
        assert(QFile::remove(path));

        // A sharing lock can disappear without any file modification notification.
        publish(path, "42");
        HANDLE lock = CreateFileW(path.toStdWString().c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        assert(lock != INVALID_HANDLE_VALUE);
        receiver.start(path, 42);
        pump(100);
        assert(received == 2 && polling());
        CloseHandle(lock);
        until([&] { return received == 3; });
        assert(!polling());

        receiver.start(path, 42);
        receiver.stop(); // Also cancels the queued initial check.
        publish(path, "42");
        pump(100);
        assert(received == 3 && QFile::exists(path) && !watching());
        assert(QFile::remove(path));

        receiver.start(path, 42, 80);
        pump(150);
        publish(path, "42");
        pump(100);
        assert(received == 3 && QFile::exists(path) && !watching() && !polling());
        assert(receiver.findChildren<QFileSystemWatcher *>().isEmpty());
        assert(QFile::remove(path));

        const auto nested = dir.filePath("recreated");
        const auto nestedRequest = nested + "/request.txt";
        receiver.start(nestedRequest, 42); // Missing directory uses the retry fallback.
        assert(polling());
        assert(QDir().mkpath(nested));
        until([&] { return !polling(); }); // Re-register notifications when possible.
        assert(watching());
        assert(QDir().rmdir(nested));
        until([&] { return polling(); }); // Deleting the watched directory must not strand the request.
        assert(QDir().mkpath(nested));
        publish(nestedRequest, "42");
        until([&] { return received == 4; });
        assert(!QFile::exists(nestedRequest) && !watching() && !polling());
    }
    auto wait = [] {
        QEventLoop loop; QTimer::singleShot(650, &loop, &QEventLoop::quit); loop.exec();
    };
    MainWindow main;
    bool busy = false;
    LauncherSession session([&] { return busy; });
    session.complete(); wait(); assert(main.closes == 0); // Already-running OBS is never owned.
    session.claim(&main);
    main.cleaned = false; busy = true;
    session.complete(); wait(); assert(main.closes == 0); // Output/preparation still active.
    main.cleaned = true; busy = false;
    wait(); assert(main.closes == 1); // STOPPED cleanup or selection cancel has completed.
    session.claim(&main); session.complete(); session.resume(); wait(); assert(main.closes == 1);
    session.complete(); session.retain(); wait(); assert(main.closes == 1); // External output/manual recording.
    session.claim(&main); main.show(); main.hide(); session.complete(); wait(); assert(main.closes == 1); // User opened OBS.
    main.show(); session.claim(&main); main.hide(); session.complete(); wait(); assert(main.closes == 1);
    session.claim(&main); session.complete(); session.retain(); wait(); assert(main.closes == 1); // Shutdown cancels queued close.
    busy = true; session.claim(&main); session.complete(); wait(); assert(main.closes == 1);
    busy = false; wait(); assert(main.closes == 2); // A startup/settings dialog delays initial ownership, never loses it.
    {
        MainWindow hidden;
        LauncherSession activation([] { return false; });
        activation.claim(&hidden);
        QEvent activated(QEvent::WindowActivate);
        QCoreApplication::sendEvent(&hidden, &activated);
        activation.complete(); wait();
        assert(hidden.closes == 1); // Hidden owner activation must not cancel automatic exit.
    }
    {
        QWidget foreground;
        const void *mainHandle = reinterpret_cast<const void *>(1);
        const void *otherHandle = reinterpret_cast<const void *>(2);
        assert(!isMainWindowForeground(nullptr, mainHandle, mainHandle));
        assert(!isMainWindowForeground(&foreground, mainHandle, mainHandle));
        foreground.show();
        assert(isMainWindowForeground(&foreground, mainHandle, mainHandle));
        assert(!isMainWindowForeground(&foreground, otherHandle, mainHandle));
        assert(!isMainWindowForeground(&foreground, nullptr, nullptr));
        foreground.showMinimized();
        assert(!isMainWindowForeground(&foreground, mainHandle, mainHandle));
        foreground.showNormal();
        assert(isMainWindowForeground(&foreground, mainHandle, mainHandle));
        foreground.hide();
        assert(!isMainWindowForeground(&foreground, mainHandle, mainHandle));
    }
}
