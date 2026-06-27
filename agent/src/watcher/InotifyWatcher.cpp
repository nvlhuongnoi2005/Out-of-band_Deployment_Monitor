#include "InotifyWatcher.h"
#include "proc/ProcHelper.h"
#include "FileEvent.h"

#include <QDirIterator>
#include <QUuid>
#include <QDebug>

#include <unistd.h>
#include <cerrno>
#include <cstring>

static constexpr uint32_t WATCH_MASK =
    IN_CREATE | IN_MODIFY | IN_DELETE | IN_DELETE_SELF |
    IN_ATTRIB | IN_MOVED_FROM | IN_MOVED_TO | IN_MOVE_SELF;

// ─── Lifecycle ────────────────────────────────────────────────────────────────

InotifyWatcher::InotifyWatcher(QObject *parent)
    : IFileWatcher(parent)
{}

InotifyWatcher::~InotifyWatcher()
{
    stop();
}

bool InotifyWatcher::start(const QStringList &directories)
{
    m_fd = inotify_init1(IN_NONBLOCK);
    if (m_fd < 0) {
        emit errorOccurred(QString("inotify_init1 failed: %1").arg(strerror(errno)));
        return false;
    }

    m_notifier = new QSocketNotifier(m_fd, QSocketNotifier::Read, this);
    connect(m_notifier, &QSocketNotifier::activated,
            this, &InotifyWatcher::onInotifyEvent);

    for (const QString &dir : directories)
        addWatchRecursive(dir);

    m_running = true;
    qInfo() << "[Watcher] Watching" << m_wdToPath.size()
            << "directories via inotify";
    return true;
}

void InotifyWatcher::stop()
{
    if (!m_running) return;
    m_running = false;

    if (m_notifier) {
        m_notifier->setEnabled(false);
        delete m_notifier;
        m_notifier = nullptr;
    }

    if (m_fd >= 0) {
        for (int wd : m_wdToPath.keys())
            inotify_rm_watch(m_fd, wd);
        ::close(m_fd);
        m_fd = -1;
    }
    m_wdToPath.clear();
}

bool InotifyWatcher::isRunning() const { return m_running; }

// ─── Watch management ─────────────────────────────────────────────────────────

void InotifyWatcher::addWatchRecursive(const QString &dirPath)
{
    addWatch(dirPath);
    QDirIterator it(dirPath, QDir::Dirs | QDir::NoDotAndDotDot,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        addWatch(it.filePath());
    }
}

bool InotifyWatcher::addWatch(const QString &dirPath)
{
    int wd = inotify_add_watch(m_fd,
                               dirPath.toLocal8Bit().constData(),
                               WATCH_MASK);
    if (wd < 0) {
        qWarning() << "[Watcher] Cannot watch" << dirPath << ":" << strerror(errno);
        return false;
    }
    m_wdToPath[wd] = dirPath;
    return true;
}

// ─── Event processing ─────────────────────────────────────────────────────────

void InotifyWatcher::onInotifyEvent()
{
    alignas(struct inotify_event) char buf[4096];
    while (true) {
        ssize_t len = read(m_fd, buf, sizeof(buf));
        if (len < 0) {
            if (errno == EAGAIN) break;
            emit errorOccurred(QString("inotify read: %1").arg(strerror(errno)));
            break;
        }
        if (len == 0) break;

        for (char *ptr = buf; ptr < buf + len; ) {
            const auto *e = reinterpret_cast<const inotify_event *>(ptr);
            processEvent(e);
            ptr += sizeof(inotify_event) + e->len;
        }
    }
}

void InotifyWatcher::processEvent(const struct inotify_event *e)
{
    if (e->mask & IN_IGNORED) { m_wdToPath.remove(e->wd); return; }
    if (!m_wdToPath.contains(e->wd)) return;

    const QString dirPath  = m_wdToPath[e->wd];
    const QString name     = (e->len > 0) ? QString::fromUtf8(e->name) : QString();
    const QString fullPath = name.isEmpty() ? dirPath : dirPath + "/" + name;

    if ((e->mask & IN_CREATE) && (e->mask & IN_ISDIR))
        addWatchRecursive(fullPath); // recursive: also catches pre-existing subdirs inside the new dir

    if ((e->mask & IN_ISDIR) && !(e->mask & IN_CREATE)) return;

    EventType evType = maskToEventType(e->mask);
    if (evType == EventType::UNKNOWN) return;

    FileEvent ev;
    ev.eventId   = QUuid::createUuid().toString(QUuid::WithoutBraces);
    ev.path      = fullPath;
    ev.eventType = evType;
    ev.timestamp = QDateTime::currentDateTimeUtc(); // UTC + Z suffix → ES indexes correctly

    int pid = ProcHelper::findPidByOpenFile(fullPath);
    if (pid > 0) {
        ev.pid         = pid;
        ev.processName = ProcHelper::pidToProcessName(pid);
        uid_t uid      = ProcHelper::uidOfPid(pid);
        ev.uid         = (int)uid;
        ev.username    = ProcHelper::uidToUsername(uid);
    } else {
        uid_t uid   = ProcHelper::statUid(fullPath);
        ev.uid      = (uid != (uid_t)-1) ? (int)uid : -1;
        ev.username = (uid != (uid_t)-1) ? ProcHelper::uidToUsername(uid) : "unknown";
    }

    emit fileEventDetected(ev);
}

EventType InotifyWatcher::maskToEventType(uint32_t mask) const
{
    if (mask & IN_CREATE)      return EventType::CREATE;
    if (mask & IN_MODIFY)      return EventType::MODIFY;
    if (mask & IN_DELETE)      return EventType::DELETE;
    if (mask & IN_DELETE_SELF) return EventType::DELETE;
    if (mask & IN_ATTRIB)      return EventType::ATTRIB;
    if (mask & IN_MOVED_FROM)  return EventType::MOVED_FROM;
    if (mask & IN_MOVED_TO)    return EventType::MOVED_TO;
    return EventType::UNKNOWN;
}
