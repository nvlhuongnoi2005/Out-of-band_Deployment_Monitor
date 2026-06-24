#pragma once

#include "FileEvent.h"

// Interface thuần — Agent chỉ biết đến cái này, không biết HTTP hay gì khác
// Cho phép swap EventReporter (HTTP) → KafkaReporter, FileReporter, v.v.
// mà không sửa Agent
class IEventReporter
{
public:
    virtual ~IEventReporter() = default;
    virtual void enqueue(const FileEvent &event) = 0;
    virtual int  pendingCount() const = 0;
};
