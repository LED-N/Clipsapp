#include "app_log.h"
#include <QDateTime>
#include <QDir>
#include <QMutexLocker>
#include <algorithm>
#include <cstring>
#include <utility>

namespace clipsapp {
namespace {
LogFile* installed = nullptr;
QtMessageHandler previous = nullptr;
QString levelName(QtMsgType type) {
    switch (type) {
    case QtDebugMsg: return QStringLiteral("DEBUG");
    case QtInfoMsg: return QStringLiteral("INFO");
    case QtWarningMsg: return QStringLiteral("WARN");
    case QtCriticalMsg: return QStringLiteral("ERROR");
    case QtFatalMsg: return QStringLiteral("FATAL");
    }
    return QStringLiteral("INFO");
}
void handle(QtMsgType type, const QMessageLogContext& context, const QString& message) {
    if (installed) {
        // Qt's own categories say where a message comes from; the application's go without.
        const bool named = context.category && std::strcmp(context.category, "default") != 0;
        installed->write(levelName(type), named ? QStringLiteral("%1: %2").arg(QString::fromUtf8(context.category), message) : message);
    }
    if (previous) previous(type, context, message);
}
}
LogFile::LogFile(QString directory, qint64 maxBytes) : m_directory(std::move(directory)), m_maxBytes(maxBytes) {}
QString LogFile::currentPath() const { return QDir(m_directory).filePath(QStringLiteral("clipsapp.log")); }
QString LogFile::previousPath() const { return QDir(m_directory).filePath(QStringLiteral("clipsapp.1.log")); }
void LogFile::write(const QString& level, const QString& message) {
    QMutexLocker lock(&m_mutex);
    const auto key = level + ' ' + message;
    if (key == m_lastMessage) { ++m_repeats; return; }
    flushRepeats(); m_lastMessage = key;
    auto text = message; text.replace('\n', QStringLiteral("\n                              "));
    append(QStringLiteral("%1  %2  %3").arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz")), level.leftJustified(5), text));
}
void LogFile::flushRepeats() {
    if (!m_repeats) return;
    const int repeats = std::exchange(m_repeats, 0);
    append(QStringLiteral("%1  %2  (message précédent répété %3 fois)").arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz")), QStringLiteral("     ")).arg(repeats));
}
void LogFile::append(const QString& line) {
    const auto bytes = line.toUtf8() + '\n';
    if (!m_file.isOpen()) {
        if (!QDir().mkpath(m_directory)) return;
        m_file.setFileName(currentPath()); if (!m_file.open(QIODevice::WriteOnly | QIODevice::Append)) return;
    }
    // The current file becomes the previous one, and the previous one goes.
    if (m_file.size() > 0 && m_file.size() + bytes.size() > m_maxBytes) {
        m_file.close(); QFile::remove(previousPath()); QFile::rename(currentPath(), previousPath());
        m_file.setFileName(currentPath()); if (!m_file.open(QIODevice::WriteOnly | QIODevice::Append)) return;
    }
    m_file.write(bytes); m_file.flush();
}
QStringList LogFile::tail(int lines) {
    QMutexLocker lock(&m_mutex); flushRepeats();
    QStringList result;
    for (const auto& path : {previousPath(), currentPath()}) {
        QFile file(path); if (!file.open(QIODevice::ReadOnly)) continue;
        result += QString::fromUtf8(file.readAll()).split('\n', Qt::SkipEmptyParts);
    }
    return result.mid(std::max<qsizetype>(0, result.size() - lines));
}
void installAppLog(const QString& directory) {
    if (installed) return;
    // Left in place until the very end: Qt may still have something to say as the program closes.
    installed = new LogFile(directory);
    previous = qInstallMessageHandler(handle);
}
LogFile* appLog() { return installed; }
QString withoutHome(QString text) {
    const auto home = QDir::homePath();
    if (home.isEmpty() || home == QStringLiteral("/")) return text;
#ifdef Q_OS_WIN
    const auto sensitivity = Qt::CaseInsensitive;
#else
    const auto sensitivity = Qt::CaseSensitive;
#endif
    text.replace(home, QStringLiteral("~"), sensitivity); text.replace(QDir::toNativeSeparators(home), QStringLiteral("~"), sensitivity);
    return text;
}
}
