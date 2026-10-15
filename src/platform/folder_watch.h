#pragma once
#include <QObject>
#include <QStringList>
#include <memory>

namespace clipsapp {
// Follows a folder and everything under it, without holding its sub-folders: they can still be
// renamed or removed from the Explorer while they are followed.
class FolderWatcher final : public QObject {
    Q_OBJECT
public:
    explicit FolderWatcher(QObject* parent = nullptr);
    ~FolderWatcher() override;
    // An empty path stops. The same folder again is kept, unless it could not be followed: then it is tried again.
    void watch(const QString& directory);
    bool watching() const;
signals:
    // What changed, relative to the folder and written with "/". An empty list means the system lost
    // count, or the folder itself went away: anything in it may have changed.
    void changed(const QStringList& paths);
private:
    struct Worker;
    std::unique_ptr<Worker> m_worker;
    QString m_directory;
};
}
