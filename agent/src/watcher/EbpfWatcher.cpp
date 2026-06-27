#include "EbpfWatcher.h"
#include "FileEvent.h"
#include "proc/ProcHelper.h"

#include <QUuid>
#include <QDateTime>
#include <QProcess>
#include <QProcessEnvironment>
#include <QDir>
#include <QDebug>

// ─── Constructor / Destructor ─────────────────────────────────────────────────

EbpfWatcher::EbpfWatcher(QObject *parent)
    : IFileWatcher(parent)
{
    m_script.setFileTemplate(QDir::tempPath() + "/oob-ebpf-XXXXXX.bt");
    m_script.setAutoRemove(true);
}

EbpfWatcher::~EbpfWatcher()
{
    stop();
}

// ─── Static availability check ───────────────────────────────────────────────

bool EbpfWatcher::isAvailable()
{
    QProcess p;
    p.start("bpftrace", {"--version"});
    return p.waitForFinished(3000) && p.exitCode() == 0;
}

// ─── Lifecycle ────────────────────────────────────────────────────────────────

bool EbpfWatcher::start(const QStringList &directories)
{
    if (directories.isEmpty()) {
        emit errorOccurred("[eBPF] No watch directories specified");
        return false;
    }

    // Write the generated bpftrace script to a temp file
    if (!m_script.open()) {
        emit errorOccurred("[eBPF] Cannot create script temp file");
        return false;
    }
    m_script.write(buildScript(directories).toUtf8());
    m_script.flush();
    const QString scriptPath = m_script.fileName();

    m_proc = new QProcess(this);

    // BPFTRACE_STRLEN: max path length captured in kernel; 256 covers typical paths
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert("BPFTRACE_STRLEN", "256");
    m_proc->setProcessEnvironment(env);

    connect(m_proc, &QProcess::readyReadStandardOutput,
            this,   &EbpfWatcher::onReadyRead);
    connect(m_proc, &QProcess::readyReadStandardError,
            this,   &EbpfWatcher::onStderrReady);
    connect(m_proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this,   &EbpfWatcher::onProcessFinished);

    m_proc->start("bpftrace", {scriptPath});
    if (!m_proc->waitForStarted(5000)) {
        emit errorOccurred("[eBPF] bpftrace failed to start: " + m_proc->errorString());
        delete m_proc;
        m_proc = nullptr;
        return false;
    }

    m_running = true;
    qInfo() << "[eBPF] bpftrace started, watching" << directories.size()
            << "director(ies) via eBPF tracepoints";
    return true;
}

void EbpfWatcher::stop()
{
    if (!m_running) return;
    m_running = false;

    if (m_proc) {
        m_proc->terminate();
        if (!m_proc->waitForFinished(3000))
            m_proc->kill();
        m_proc->deleteLater();
        m_proc = nullptr;
    }

    m_buf.clear();
    qInfo() << "[eBPF] Stopped.";
}

bool EbpfWatcher::isRunning() const { return m_running; }

// ─── Script generation ────────────────────────────────────────────────────────

QString EbpfWatcher::buildScript(const QStringList &dirs) const
{
    // Generate strncmp conditions for each watched directory
    auto makeFilter = [](const QString &var, const QStringList &dirs) -> QString {
        QStringList conds;
        for (const QString &d : dirs) {
            // Trim trailing slash so both /foo and /foo/bar match
            const QString clean = d.endsWith('/') ? d.chopped(1) : d;
            conds << QString("strncmp(%1, \"%2\", %3) == 0")
                       .arg(var).arg(clean).arg(clean.length());
        }
        return conds.join(" ||\n       ");
    };

    const QString pathFilter = makeFilter("$path", dirs);
    const QString oldFilter  = makeFilter("$old",  dirs);

    // Template uses uppercase placeholders to avoid conflict with bpftrace % format specs
    QString script = R"SCRIPT(
BEGIN { printf("READY\n"); }

// ─── openat: file create/modify ───────────────────────────────────────────────
tracepoint:syscalls:sys_enter_openat
{
  $path = str(args->filename);
  if (PATH_FILTER) {
    // O_WRONLY=1, O_RDWR=2, O_CREAT=64
    if ((args->flags & 3) != 0) {
      if ((args->flags & 64) != 0) {
        printf("CREATE|%s|%d|%s|%d\n", $path, uid, comm, pid);
      } else {
        printf("MODIFY|%s|%d|%s|%d\n", $path, uid, comm, pid);
      }
    }
  }
}

// ─── unlinkat: file delete ────────────────────────────────────────────────────
tracepoint:syscalls:sys_enter_unlinkat
{
  $path = str(args->pathname);
  if (PATH_FILTER) {
    printf("DELETE|%s|%d|%s|%d\n", $path, uid, comm, pid);
  }
}

// ─── renameat2: file move ─────────────────────────────────────────────────────
tracepoint:syscalls:sys_enter_renameat2
{
  $old = str(args->oldname);
  if (OLD_FILTER) {
    $new = str(args->newname);
    printf("MOVED_FROM|%s|%d|%s|%d\n", $old, uid, comm, pid);
    printf("MOVED_TO|%s|%d|%s|%d\n", $new, uid, comm, pid);
  }
}

// ─── fchmodat: permission change ─────────────────────────────────────────────
tracepoint:syscalls:sys_enter_fchmodat
{
  $path = str(args->filename);
  if (PATH_FILTER) {
    printf("ATTRIB|%s|%d|%s|%d\n", $path, uid, comm, pid);
  }
}
)SCRIPT";

    script.replace("PATH_FILTER", pathFilter);
    script.replace("OLD_FILTER",  oldFilter);
    return script;
}

// ─── Output parsing ───────────────────────────────────────────────────────────

void EbpfWatcher::onReadyRead()
{
    m_buf += m_proc->readAllStandardOutput();

    int pos;
    while ((pos = m_buf.indexOf('\n')) != -1) {
        const QByteArray line = m_buf.left(pos).trimmed();
        m_buf = m_buf.mid(pos + 1);

        if (line == "READY") {
            qInfo() << "[eBPF] Probes attached, monitoring active";
        } else if (!line.isEmpty()) {
            parseLine(line);
        }
    }
}

void EbpfWatcher::onStderrReady()
{
    // bpftrace prints "Attaching N probes..." and warnings to stderr — log as debug
    const QByteArray err = m_proc->readAllStandardError().trimmed();
    if (!err.isEmpty())
        qDebug() << "[eBPF]" << err;
}

void EbpfWatcher::onProcessFinished(int exitCode, QProcess::ExitStatus status)
{
    if (!m_running) return; // expected shutdown via stop()
    qCritical() << "[eBPF] bpftrace exited unexpectedly"
                << "(code=" << exitCode
                << "status=" << (int)status << ")";
    emit errorOccurred(QString("[eBPF] bpftrace exited unexpectedly (code=%1)").arg(exitCode));
}

void EbpfWatcher::parseLine(const QByteArray &raw)
{
    // Format: TYPE|path|uid|comm|pid
    const QList<QByteArray> parts = raw.split('|');
    if (parts.size() != 5) return;

    const QString typeName = QString::fromUtf8(parts[0]).trimmed();
    const QString path     = QString::fromUtf8(parts[1]).trimmed();
    const int     uid      = parts[2].trimmed().toInt();
    const QString comm     = QString::fromUtf8(parts[3]).trimmed();
    const int     pid      = parts[4].trimmed().toInt();

    static const QMap<QString, EventType> typeMap = {
        {"CREATE",     EventType::CREATE},
        {"MODIFY",     EventType::MODIFY},
        {"DELETE",     EventType::DELETE},
        {"ATTRIB",     EventType::ATTRIB},
        {"MOVED_FROM", EventType::MOVED_FROM},
        {"MOVED_TO",   EventType::MOVED_TO},
    };

    if (!typeMap.contains(typeName)) return;

    FileEvent ev;
    ev.eventId     = QUuid::createUuid().toString(QUuid::WithoutBraces);
    ev.path        = path;
    ev.eventType   = typeMap[typeName];
    ev.timestamp   = QDateTime::currentDateTimeUtc();
    ev.uid         = uid;
    ev.pid         = pid;
    ev.processName = comm;
    ev.username    = ProcHelper::uidToUsername(static_cast<uid_t>(uid));

    emit fileEventDetected(ev);
}
