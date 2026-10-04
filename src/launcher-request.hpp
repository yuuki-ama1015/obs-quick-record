#pragma once
#include <QFile>
#include <QFileInfo>
#include <QDateTime>

// A request can open the selector only. It cannot select a target or start recording.
inline bool consumeLauncherRequest(const QString &path, qint64 pid, bool *autoExit = nullptr)
{
    if (autoExit) *autoExit = false;
    QFile file(path);
    if (!file.exists()) return false;
    const qint64 age = QFileInfo(file).lastModified().msecsTo(QDateTime::currentDateTime());
    if (age < 0 || age > 60000 || file.size() > 64) { file.remove(); return false; }
    if (!file.open(QIODevice::ReadOnly)) return false;
    bool valid = false;
    const auto lines = file.readAll().trimmed().split('\n');
    const qint64 requestedPid = lines.first().toLongLong(&valid);
    file.close();
    if (!valid || lines.size() > 2 || (lines.size() == 2 && lines[1] != "exit-after-capture")) { file.remove(); return false; }
    if (requestedPid != pid) return false;
    if (!file.remove()) return false;
    if (autoExit) *autoExit = lines.size() == 2;
    return true; // Exactly once, even if the next timer tick occurs immediately.
}
