#pragma once

#include "IFileWatcher.h"
#include <QProcess>
#include <QTemporaryFile>
#include <QByteArray>

// FR-10: eBPF-based file watcher using bpftrace as the backend.
// Advantages over inotify:
//   - Captures uid/pid AT SYSCALL ENTRY — no /proc race for short-lived processes
//   - Works correctly for DELETE (the deleting process is still in-kernel when captured)
//   - Operates below the VFS layer — harder to bypass than inotify
//
// Requirements:
//   - bpftrace installed: apt install bpftrace  (or dnf/pacman equivalent)
//   - Root privileges or CAP_BPF + CAP_PERFMON
//
// Select at runtime: oob-agent --watcher ebpf
class EbpfWatcher : public IFileWatcher
{
    Q_OBJECT
public:
    explicit EbpfWatcher(QObject *parent = nullptr);
    ~EbpfWatcher() override;

    bool start(const QStringList &directories) override;
    void stop() override;
    bool isRunning() const override;

    // Returns true if bpftrace is found in PATH.
    static bool isAvailable();

private slots:
    void onReadyRead();
    void onStderrReady();
    void onProcessFinished(int exitCode, QProcess::ExitStatus status);

private:
    QString buildScript(const QStringList &dirs) const;
    void    parseLine(const QByteArray &line);

    QProcess       *m_proc    = nullptr;
    QTemporaryFile  m_script;
    bool            m_running = false;
    QByteArray      m_buf;
};
