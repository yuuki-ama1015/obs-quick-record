#include "launcher-request.hpp"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <cassert>
#include <QApplication>
#include <QCloseEvent>
#include <QEventLoop>
#include "launcher-session.hpp"
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
}
