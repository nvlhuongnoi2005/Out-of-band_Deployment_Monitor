#pragma once

#include "IFileWatcher.h"
#include <QMap>
#include <QSocketNotifier>
#include <sys/inotify.h>
#include <sys/types.h>

class InotifyWatcher : public IFileWatcher
{
    Q_OBJECT
public:
    explicit InotifyWatcher(QObject *parent = nullptr);
    ~InotifyWatcher() override;

    bool start(const QStringList &directories) override;
    void stop() override;
    bool isRunning() const override;

private slots:
    void onInotifyEvent();

private:
    void      addWatchRecursive(const QString &dirPath);
    bool      addWatch(const QString &dirPath);
    void      processEvent(const struct inotify_event *e);
    EventType maskToEventType(uint32_t mask) const;

    // Helpers lấy thông tin user/process từ /proc
    static uid_t   statUid(const QString &path);
    static int     findPidByOpenFile(const QString &path);
    static QString uidToUsername(uid_t uid);
    static QString pidToProcessName(int pid);
    static uid_t   uidOfPid(int pid);

    int                m_fd      = -1;
    bool               m_running = false;
    QSocketNotifier   *m_notifier = nullptr;
    QMap<int, QString> m_wdToPath;  // watch descriptor → đường dẫn thư mục
};
