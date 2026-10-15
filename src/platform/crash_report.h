#pragma once
#include <QString>
namespace clipsapp {
// A crash leaves a small report in this folder: where each thread of the program was, not the
// memory of the replay. The five most recent are kept.
void installCrashReports(const QString& directory);
// The newest report written since this was last asked, or nothing. Asking marks it as seen.
QString unseenCrashReport(const QString& directory);
}
