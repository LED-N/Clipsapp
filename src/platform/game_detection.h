#pragma once
#include <QString>
namespace clipsapp {
// Selected windows use their executable. Screen capture observes only large
// foreground windows on that monitor, never changing the capture target.
QString detectCapturedGame(const QString& videoTarget);
}
