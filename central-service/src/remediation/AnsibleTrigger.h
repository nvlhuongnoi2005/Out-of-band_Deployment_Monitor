#pragma once
#include <QString>

// FR-12: Trigger an Ansible playbook asynchronously when UNAUTHORIZED_DRIFT is detected.
// Runs in a detached thread so it does not block the HTTP handler.
class AnsibleTrigger {
public:
    explicit AnsibleTrigger(const QString &playbookPath,
                             const QString &inventoryPath);

    void trigger(const QString &server,  const QString &project,
                 const QString &path,    const QString &eventType);

private:
    QString m_playbookPath;
    QString m_inventoryPath;
};
