#pragma once
#include "core/clip_library.h"
#include <QSet>
#include <QWidget>
class QCheckBox;
class QFrame;
class QLabel;
class QVBoxLayout;
namespace clipsapp {
class Banner;
class Bar;
class Button;
class CompressBatch;
class ThumbnailCache;
// Where the compressor lives: the two things it does, the whole library at once, the heaviest clips
// first, and a library compressing in the background.
class CompressorPage final : public QWidget {
    Q_OBJECT
public:
    enum Goal { Space = 1, Discord = 2 };
    CompressorPage(ThumbnailCache* cache, CompressBatch* batch, QWidget* parent = nullptr);
    void setClips(const QList<ClipEntry>& clips, qint64 limitBytes);
    // The copies the versions before 0.8.5 kept in « Partage Discord », offered for cleaning.
    void setOldCopies(int count, qint64 bytes);
    void setBusy(bool busy);
    bool selectionShown() const;
    static QString limitName(qint64 bytes);
signals:
    void compressRequested(const QStringList& paths, int goal);
    void libraryRequested();
    void batchRequested();
    void limitChosen(qint64 bytes);
    void openOldCopiesRequested();
    void deleteOldCopiesRequested();
    void selectionBarVisible(bool visible);
protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
private:
    void refreshBatch();
    void refreshHeaviest();
    void refreshFigures();
    bool compressed(const ClipEntry& clip) const;
    QStringList checkedPaths() const;
    void setChecked(const QSet<QString>& paths);
    void updateSelection();
    void placeSelection();
    ThumbnailCache* m_cache; CompressBatch* m_batch; QList<ClipEntry> m_clips; qint64 m_limit = 20000000; bool m_busy = false;
    QWidget* m_running; QLabel* m_runningTitle; QLabel* m_runningDetail; Bar* m_runningBar;
    QLabel* m_spaceFigures; Button* m_library; QLabel* m_discordFigures; Button* m_limitButton;
    QWidget* m_heaviestHost; QVBoxLayout* m_heaviest; QString m_heaviestSignature; QLabel* m_empty; Banner* m_oldCopies;
    // The heaviest shown, those ticked, and the bar that acts on them.
    QStringList m_shown; QSet<QString> m_checked; QList<QCheckBox*> m_checks; QCheckBox* m_checkAll; QFrame* m_selection; QLabel* m_compressedNote; QLabel* m_selectionTitle; QLabel* m_selectionDetail; QList<QWidget*> m_selectionActions; QVBoxLayout* m_content;
};
}
