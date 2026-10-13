#pragma once
#include <QString>
namespace clipsapp {
QString safeGameFolder(QString name);
QString gameNameFromApplication(const QString& executable, const QString& title, const QString& product = {});
}
