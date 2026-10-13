#pragma once
#include "config.h"

namespace clipsapp {
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

