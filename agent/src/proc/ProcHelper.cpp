#include "ProcHelper.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QString>

#include <sys/stat.h>
#include <pwd.h>

namespace ProcHelper {

uid_t statUid(const QString &path)
{
    struct stat st;
    if (::stat(path.toLocal8Bit().constData(), &st) == 0)
        return st.st_uid;
    return (uid_t)-1;
}

int findPidByOpenFile(const QString &targetPath)
{
    const QStringList pids = QDir("/proc").entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &pidStr : pids) {
        bool ok;
        pidStr.toInt(&ok);
        if (!ok) continue;

        QDirIterator it(QString("/proc/%1/fd").arg(pidStr),
                        QDir::Files | QDir::System);
        while (it.hasNext()) {
            it.next();
            if (QFile::symLinkTarget(it.filePath()) == targetPath)
                return pidStr.toInt();
        }
    }
    return -1;
}

uid_t uidOfPid(int pid)
{
    QFile f(QString("/proc/%1/status").arg(pid));
    if (!f.open(QIODevice::ReadOnly)) return (uid_t)-1;

    for (const QByteArray &line : f.readAll().split('\n')) {
        if (line.startsWith("Uid:")) {
            const QList<QByteArray> parts = line.split('\t');
            if (parts.size() >= 2)
                return (uid_t)parts[1].trimmed().toUInt();
        }
    }
    return (uid_t)-1;
}

QString uidToUsername(uid_t uid)
{
    char buf[1024];
    struct passwd pw, *result = nullptr;
    if (getpwuid_r(uid, &pw, buf, sizeof(buf), &result) == 0 && result)
        return QString::fromUtf8(result->pw_name);
    return QString::number((uint)uid);
}

QString pidToProcessName(int pid)
{
    QFile f(QString("/proc/%1/comm").arg(pid));
    if (f.open(QIODevice::ReadOnly))
        return QString::fromUtf8(f.readAll()).trimmed();
    return "unknown";
}

} // namespace ProcHelper
