#pragma once

#include <QObject>
#include <QString>
#include "config/AgentConfig.h"
#include "FileEvent.h"

class IFileWatcher;

class Agent : public QObject
{
    Q_OBJECT
public:
    explicit Agent(const QString &configPath, QObject *parent = nullptr);
    ~Agent() override;

    bool start();
    void stop();

private slots:
    void onFileEvent(const FileEvent &event);
    void onWatcherError(const QString &message);

private:
    AgentConfig   m_config;
    IFileWatcher *m_watcher = nullptr;
};
