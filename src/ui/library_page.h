#pragma once
#include "core/clip_library.h"
#include <QFrame>
#include <QKeySequence>
#include <QWidget>
class QLabel;
class QLineEdit;
class QListWidget;
class QStackedWidget;
namespace clipsapp {
class Button;
class ClipListView;
class Keycaps;
class LibraryRowDelegate;
class ThumbnailCache;
struct LibraryItem { ClipEntry clip; bool favorite = false, fresh = false, hasCopy = false; };

// Type a game, pick a suggestion, press Enter. Files are never moved.
class ClassifyPopup final : public QFrame {
    Q_OBJECT
public:
    explicit ClassifyPopup(QWidget* parent);
    // The popup grows upwards from its anchor, the top right corner of what opened it.
    void open(int clipCount, const QList<QPair<QString, int>>& games, const QString& current, const QPoint& anchor);
signals:
    void chosen(const QString& game);
protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void hideEvent(QHideEvent* event) override;
private:
    void refill();
    void accept();
    QLabel* m_title; QLineEdit* m_field; QListWidget* m_list; QList<QPair<QString, int>> m_games; QPoint m_anchor; bool m_images = false;
};

class LibraryPage final : public QWidget {
    Q_OBJECT
public:
    enum Filter { All, Favorites, Game };
    enum Sort { Recent, Oldest, ByName, Largest };
    // The same page lists the clips, or the screenshots.
    enum Kind { Clips, Screenshots };
    explicit LibraryPage(ThumbnailCache* cache, Kind kind = Clips, QWidget* parent = nullptr);
    Kind kind() const { return m_kind; }
    // What the empty list of screenshots shows: the shortcut that takes one.
    void setScreenshotHint(const QKeySequence& hotkey);
    void setItems(QList<LibraryItem> items, qint64 limitBytes);
    void setFilter(Filter filter, const QString& game = {});
    void setLibraryPath(const QString& path) { if (path == m_path) return; m_path = path; refresh(); }
    void setLoading(bool loading) { if (loading == m_loading) return; m_loading = loading; refresh(); }
    void setBusy(bool trashing, bool sharing);
    void setReplayHint(const QKeySequence& hotkey, int seconds, bool canStart, bool running);
    QStringList selectedPaths() const;
    void select(const QStringList& paths);
    void clearSelection();
    void focusSearch();
    void rename(const QString& path);
    void classify();
    ClipListView* list() const { return m_list; }
signals:
    void openRequested(const QString& path);
    // Full-screen viewing of one clip, with the listed clips as the playlist to walk through.
    void viewRequested(const QStringList& playlist, int index);
    void shareRequested(const QStringList& paths);
    void trashRequested(const QStringList& paths);
    void classifyRequested(const QStringList& paths, const QString& game);
    void favoriteRequested(const QStringList& paths, bool favorite);
    void renameRequested(const QString& path, const QString& name);
    void copyRequested(const QStringList& paths);
    void revealRequested(const QString& path);
    void startReplayRequested();
    void chooseFolderRequested();
    void selectionBarVisible(bool visible);
protected:
    void resizeEvent(QResizeEvent* event) override;
private:
    void refresh();
    void updateSelection();
    void showMenu(const QString& path, const QPoint& globalPosition);
    void view(const QString& path);
    void toggleFavorite();
    const LibraryItem* item(const QString& path) const;
    Kind m_kind; ThumbnailCache* m_cache; QList<LibraryItem> m_items; qint64 m_limit = 20000000; Filter m_filter = All; QString m_game, m_path; Sort m_sort = Recent;
    bool m_loading = false, m_trashing = false, m_sharing = false; QString m_sortName = QStringLiteral("Plus récents");
    QLabel* m_title; QLabel* m_subtitle; QWidget* m_search; QLineEdit* m_searchField; Button* m_sortButton;
    QStackedWidget* m_stack; ClipListView* m_list; LibraryRowDelegate* m_rows; QWidget* m_empty; QWidget* m_noMatch; Keycaps* m_keys; QLabel* m_emptyText; Button* m_startReplay;
    QFrame* m_bar; QLabel* m_barTitle; QLabel* m_barDetail; Button* m_classify; Button* m_share; Button* m_trash; ClassifyPopup* m_popup; QWidget* m_listHost;
};
}
