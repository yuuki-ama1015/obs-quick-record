#include "launcher-request.hpp"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <cassert>
int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
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
    write("42");
    QFile file(path); assert(file.open(QIODevice::ReadWrite));
    assert(file.setFileTime(QDateTime::currentDateTime().addSecs(-61), QFileDevice::FileModificationTime)); file.close();
    assert(!consumeLauncherRequest(path, 42) && !QFile::exists(path));
}
