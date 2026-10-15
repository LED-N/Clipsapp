#pragma once
#include "config.h"

namespace clipsapp {
// Where the settings live when no folder is given: one folder named after the application, beside
// those of the other programs (AppData\Local\Clipsapp on Windows).
QString defaultConfigDirectory();
// Versions up to 0.8.2 kept them one folder deeper, in "Clipsapp\Clipsapp". What is found there comes
// up into this folder, without replacing anything already in it; the old folder goes once empty.
void adoptEarlierSettings(const QString& earlier, const QString& directory);
class ConfigStore {
public:
    explicit ConfigStore(QString directory);
    QString filePath() const;
    bool load(AppConfig& config, QString& error) const;
    bool save(const AppConfig& config, QString& error) const;
private:
    QString m_directory;
};
}

