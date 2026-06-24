#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include "FileEvent.h"

// Strategy pattern interface — cho phép swap inotify <-> eBPF
// mà không ảnh hưởng đến Agent hay EventQueue
class IFileWatcher : public QObject
{
    Q_OBJECT
public:
    explicit IFileWatcher(QObject *parent = nullptr) : QObject(parent) {}
    virtual ~IFileWatcher() = default;

    virtual bool start(const QStringList &directories) = 0;
    virtual void stop() = 0;
    virtual bool isRunning() const = 0;

signals:
    void fileEventDetected(const FileEvent &event);
    void errorOccurred(const QString &message);
};
