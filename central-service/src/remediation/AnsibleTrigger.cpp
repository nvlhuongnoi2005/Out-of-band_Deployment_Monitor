#include "AnsibleTrigger.h"

#include <QProcess>
#include <QStringList>
#include <QDebug>
#include <thread>

AnsibleTrigger::AnsibleTrigger(const QString &playbookPath,
                                const QString &inventoryPath)
    : m_playbookPath(playbookPath), m_inventoryPath(inventoryPath)
{}

void AnsibleTrigger::trigger(const QString &server,  const QString &project,
                              const QString &path,    const QString &eventType)
{
    const QString playbook  = m_playbookPath;
    const QString inventory = m_inventoryPath;

    const QString extraVars =
        QString("drift_server=%1 drift_project=%2 drift_path=%3 drift_event=%4")
            .arg(server, project, path, eventType);

    qInfo() << "[Ansible] Queuing remediation for server=" << server << "path=" << path;

    // Detach so the HTTP response is not delayed while ansible-playbook runs
    std::thread([playbook, inventory, server, extraVars]() {
        QProcess proc;
        proc.start("ansible-playbook", {
            playbook,
            "-i", inventory,
            "--limit", server,
            "-e", extraVars
        });

        if (!proc.waitForFinished(120000)) {
            qWarning() << "[Ansible] Playbook timeout for" << server;
            proc.kill();
        } else if (proc.exitCode() != 0) {
            qWarning() << "[Ansible] Playbook failed (exit=" << proc.exitCode() << "):"
                       << proc.readAllStandardError().simplified();
        } else {
            qInfo() << "[Ansible] Remediation complete for" << server;
        }
    }).detach();
}
