#pragma once
#include <QString>
#include <sys/types.h>

namespace ProcHelper {

uid_t   statUid(const QString &path);
int     findPidByOpenFile(const QString &path);
uid_t   uidOfPid(int pid);
QString uidToUsername(uid_t uid);
QString pidToProcessName(int pid);

} // namespace ProcHelper
