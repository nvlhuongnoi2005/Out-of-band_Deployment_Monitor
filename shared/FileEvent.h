#pragma once

#include <QString>
#include <QDateTime>

enum class EventType {
    CREATE,
    MODIFY,
    DELETE,
    ATTRIB,
    MOVED_FROM,
    MOVED_TO,
    UNKNOWN
};

inline QString eventTypeToString(EventType type)
{
    switch (type) {
    case EventType::CREATE:     return "CREATE";
    case EventType::MODIFY:     return "MODIFY";
    case EventType::DELETE:     return "DELETE";
    case EventType::ATTRIB:     return "ATTRIB";
    case EventType::MOVED_FROM: return "MOVED_FROM";
    case EventType::MOVED_TO:   return "MOVED_TO";
    default:                    return "UNKNOWN";
    }
}

struct FileEvent {
    QString   eventId;
    QString   agentId;
    QString   server;
    QString   project;
    QString   path;
    EventType eventType = EventType::UNKNOWN;
    QDateTime timestamp;

    // user info — lấy từ /proc khi dùng inotify, từ kernel khi dùng eBPF
    int     uid         = -1;
    QString username    = "unknown";
    int     pid         = -1;
    QString processName = "unknown";
};
