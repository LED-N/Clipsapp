#pragma once
#include <QFile>
#include <QMutex>
#include <QString>
#include <QStringList>

namespace clipsapp {
// The application's own journal, on this PC only: what it did and what went wrong. Two files at
// most, the current one and the one before, each kept under a size; nothing in them leaves the
// computer by itself.
class LogFile {
public:
    explicit LogFile(QString directory, qint64 maxBytes = 1024 * 1024);
    QString directory() const { return m_directory; }
    QString currentPath() const;
    QString previousPath() const;
    // One line, dated. The same message repeated is written once, with how many times it came.
    void write(const QString& level, const QString& message);
    // The last lines written, the older file first.
    QStringList tail(int lines);
private:
    void append(const QString& line);
    void flushRepeats();
    QString m_directory;
    qint64 m_maxBytes;
    QMutex m_mutex;
    QFile m_file;
    QString m_lastMessage;
    int m_repeats = 0;
};
// From now on, every message of the application and of Qt also goes to this journal.
void installAppLog(const QString& directory);
// The journal installed, or nothing in tests and before installAppLog.
LogFile* appLog();
// The user's own folder replaced by "~", in both ways of writing paths: what is shared with
// someone else does not carry the name of the account.
QString withoutHome(QString text);
}
