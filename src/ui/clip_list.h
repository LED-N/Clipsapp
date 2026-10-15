#pragma once
#include <QListView>
#include <QStandardItemModel>
#include <QStyledItemDelegate>
#include <QUrl>
class QLineEdit;
namespace clipsapp {
class ThumbnailCache;
namespace ClipRole {
enum { Path = Qt::UserRole + 1, Key, Header, Name, Detail, Game, Bytes, Modified, Favorite, Fresh, FitOk, FitText, Limit, ThumbPath, Failed };
}
// Starts a copy-only drag of real files towards another application.
bool startFileDrag(QWidget* source, const QList<QUrl>& urls);
QList<QUrl> existingFileUrls(const QStringList& paths);

// Exports real file URLs to the operating system rather than Qt model cells.
class ClipListModel final : public QStandardItemModel {
public:
    using QStandardItemModel::QStandardItemModel;
    QStringList mimeTypes() const override;
    QMimeData* mimeData(const QModelIndexList& indexes) const override;
    Qt::DropActions supportedDragActions() const override { return Qt::CopyAction; }
    Qt::DropActions supportedDropActions() const override { return Qt::CopyAction; }
};

class ClipListView;
class ClipRowDelegate : public QStyledItemDelegate {
public:
    enum Hit { Nothing, Check, Edit, Share, More, Copy, Play };
    ClipRowDelegate(ClipListView* view, ThumbnailCache* cache);
    virtual Hit hitTest(const QRect& row, const QModelIndex& index, const QPoint& position) const = 0;
    virtual QRect nameRect(const QRect& row, const QModelIndex& index) const = 0;
protected:
    void paintBackground(QPainter* painter, const QRectF& row, const QStyleOptionViewItem& option) const;
    // Returns false while the preview is still being read.
    bool paintThumbnail(QPainter* painter, const QRectF& rect, const QModelIndex& index, double radius, bool duration) const;
    ClipListView* m_view; ThumbnailCache* m_cache;
};
class LibraryRowDelegate final : public ClipRowDelegate {
public:
    using ClipRowDelegate::ClipRowDelegate;
    void setShowGame(bool show) { m_showGame = show; }
    // Pictures rather than clips: their size instead of the length and the tracks, and a copy of the image instead of a Discord copy.
    void setImages(bool images) { m_images = images; }
    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override;
    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override;
    Hit hitTest(const QRect& row, const QModelIndex& index, const QPoint& position) const override;
    QRect nameRect(const QRect& row, const QModelIndex& index) const override;
private:
    struct Layout { QRectF row, check, thumb, play, text, size, time, edit, share, more; bool compact = false; };
    Layout layout(const QRect& item) const;
    bool m_showGame = true, m_images = false;
};
class ShareRowDelegate final : public ClipRowDelegate {
public:
    using ClipRowDelegate::ClipRowDelegate;
    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override;
    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override;
    Hit hitTest(const QRect& row, const QModelIndex& index, const QPoint& position) const override;
    QRect nameRect(const QRect& row, const QModelIndex& index) const override;
private:
    struct Layout { QRectF row, check, thumb, text, size, date, copy, more; };
    Layout layout(const QRect& item) const;
};

class ClipListView final : public QListView {
    Q_OBJECT
public:
    explicit ClipListView(QWidget* parent = nullptr);
    ClipListModel* clips() const { return m_model; }
    void setRowDelegate(ClipRowDelegate* delegate);
    QStringList selectedPaths() const;
    void selectPaths(const QStringList& paths);
    int rowOf(const QString& path) const;
    QString pathAt(const QModelIndex& index) const { return index.data(ClipRole::Path).toString(); }
    QPoint hoverPosition() const { return m_hover; }
    bool keyboardFocus() const { return m_keyboard; }
    void beginRename(const QString& path);
    QMimeData* dragPayload() const;
signals:
    void openRequested(const QString& path);
    void actionRequested(int hit, const QString& path, const QPoint& globalPosition);
    void menuRequested(const QString& path, const QPoint& globalPosition);
    void renameRequested(const QString& path, const QString& name);
    void fileDragStarted(int count, int unavailable);
    void fileDragUnavailable();
protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;
    void startDrag(Qt::DropActions supportedActions) override;
    bool eventFilter(QObject* watched, QEvent* event) override;
private:
    ClipRowDelegate::Hit hitAt(const QPoint& position, QModelIndex* index = nullptr) const;
    void finishRename(bool commit);
    ClipListModel* m_model; ClipRowDelegate* m_rows = nullptr; QPoint m_hover{-1, -1}; ClipRowDelegate::Hit m_pressed = ClipRowDelegate::Nothing; QPersistentModelIndex m_pressedIndex;
    QLineEdit* m_editor = nullptr; QString m_editing; bool m_keyboard = false;
};
}
