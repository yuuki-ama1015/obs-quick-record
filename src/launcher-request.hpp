#pragma once
#include <QFile>
#include <QFileInfo>
#include <QDateTime>

// A request can open the selector only. It cannot select a target or start recording.
inline bool consumeLauncherRequest(const QString &path, qint64 pid)
{
    QFile file(path);
    if (!file.exists()) return false;
    const qint64 age = QFileInfo(file).lastModified().msecsTo(QDateTime::currentDateTime());
    if (age < 0 || age > 60000 || file.size() > 32) { file.remove(); return false; }
    if (!file.open(QIODevice::ReadOnly)) return false;
    bool valid = false;
    const qint64 requestedPid = file.readAll().trimmed().toLongLong(&valid);
    file.close();
    if (!valid) { file.remove(); return false; }
    if (requestedPid != pid) return false;
    return file.remove(); // Exactly once, even if the next timer tick occurs immediately.
}
