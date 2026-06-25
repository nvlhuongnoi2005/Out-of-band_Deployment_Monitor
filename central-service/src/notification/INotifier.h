#pragma once
#include <QString>

class INotifier {
public:
    virtual ~INotifier() = default;
    virtual void notify(const QString &subject, const QString &body) = 0;
};
