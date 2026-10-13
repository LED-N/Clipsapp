#pragma once
#include "core/clip_library.h"
#include <QFrame>
#include <QWidget>
class QLabel;
class QStackedWidget;
class QVBoxLayout;
namespace clipsapp {
class Bar;
class Button;
class ClipListView;
class DiscordBatch;
class Tag;
class ThumbnailCache;
struct ShareCopy { ClipEntry file; ShareLink link; };
// The compressed copies, ready to drag or paste into Discord, and the batch that makes them.
class SharingPage final : public QWidget {
    Q_OBJECT
public:
    SharingPage(ThumbnailCache* cache, DiscordBatch* batch, QWidget* parent = nullptr);
    void setCopies(const QList<ShareCopy>& copies, qint64 limitBytes);
    void setBusy(bool trashing);
    QStringList selectedPaths() const;
    ClipListView* list() const { return m_list; }
    static QString limitName(qint64 bytes);
signals:
    void copyRequested(const QStringList& paths);
    void revealRequested(const QString& path);
    void trashRequested(const QStringList& paths);
    void openOriginalRequested(const QString& path);
    void viewRequested(const QStringList& playlist, int index);
    void openFolderRequested();
    void limitChosen(qint64 bytes);
    void selectionBarVisible(bool visible);
protected:
    void resizeEvent(QResizeEvent* event) override;
private:
    void refreshBatch();
    void updateSelection();
    void showMenu(const QString& path, const QPoint& globalPosition);
    ThumbnailCache* m_cache; DiscordBatch* m_batch; QList<ShareCopy> m_copies; qint64 m_limit = 20000000; bool m_trashing = false;
    Button* m_limitButton; QWidget* m_progress; QLabel* m_jobTitle; QLabel* m_jobFigures; Bar* m_jobBar; Tag* m_readyTag; Tag* m_failedTag;
    QLabel* m_readyCaption; QStackedWidget* m_stack; ClipListView* m_list; QWidget* m_failed; QVBoxLayout* m_failedRows; QString m_failedSignature;
    QFrame* m_bar; QLabel* m_barTitle; QLabel* m_barDetail; Button* m_copy; Button* m_reveal; Button* m_trash; QWidget* m_listHost;
};
}
