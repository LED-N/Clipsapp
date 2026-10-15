#include "config_store.h"
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QFileInfo>
#include <QSaveFile>
#include <QStandardPaths>
#include <utility>

namespace clipsapp {
QString defaultConfigDirectory() { return QDir(QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)).filePath(QStringLiteral("Clipsapp")); }
void adoptEarlierSettings(const QString& earlier, const QString& directory) {
    const QDir from(earlier);
    if (earlier.isEmpty() || !from.exists() || QDir::cleanPath(from.absolutePath()) == QDir::cleanPath(QDir(directory).absolutePath())) return;
    if (!QDir().mkpath(directory)) return;
    for (const auto& entry : from.entryInfoList(QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot)) {
        const auto target = QDir(directory).filePath(entry.fileName());
        // Interrupted halfway, the move goes on at the next launch; what is already there stays.
        if (!QFileInfo::exists(target)) QDir().rename(entry.absoluteFilePath(), target);
    }
    from.rmdir(from.absolutePath());
}
ConfigStore::ConfigStore(QString directory) : m_directory(std::move(directory)) {}
QString ConfigStore::filePath() const { return QDir(m_directory).filePath("settings.json"); }
bool ConfigStore::load(AppConfig& config, QString& error) const {
    QFile file(filePath());
    if (!file.exists()) { config = AppConfig::defaults(); error.clear(); return true; }
    if (!file.open(QIODevice::ReadOnly)) { error = file.errorString(); return false; }
    if (file.size() > 1024 * 1024) { error = QStringLiteral("Le fichier de configuration est trop volumineux."); return false; }
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) { error = QStringLiteral("Configuration illisible : ") + parseError.errorString(); return false; }
    return AppConfig::fromJson(document.object(), config, error);
}
bool ConfigStore::save(const AppConfig& config, QString& error) const {
    const auto errors = config.validationErrors();
    if (!errors.isEmpty()) { error = errors.join('\n'); return false; }
    if (!QDir().mkpath(m_directory)) { error = QStringLiteral("Impossible de créer le dossier de configuration."); return false; }
    QSaveFile file(filePath());
    if (!file.open(QIODevice::WriteOnly)) { error = file.errorString(); return false; }
    const auto bytes = QJsonDocument(config.toJson()).toJson(QJsonDocument::Indented);
    if (file.write(bytes) != bytes.size() || !file.commit()) { error = file.errorString(); return false; }
    error.clear(); return true;
}
}

