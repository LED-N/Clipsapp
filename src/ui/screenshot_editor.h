#pragma once
#include "image_markup.h"
#include <QFutureWatcher>
#include <QHash>
#include <QImage>
#include <QWidget>
class QLabel;
class QLineEdit;
class QPushButton;
namespace clipsapp {
class Banner;
class Button;
class ElideLabel;
class Segmented;
class Tag;
class Tile;
class VideoCanvas;
class MarkupLayer;
// A screenshot seen large: cropped to a new picture, renamed, copied, or put in the trash. The
// original stays as it is, like a clip after an export.
class ScreenshotEditor final : public QWidget {
    Q_OBJECT
public:
    explicit ScreenshotEditor(QWidget* parent = nullptr);
    void openImage(const QString& path);
    QString imagePath() const { return m_path; }
    QSize imageSize() const { return m_size; }
    QRect crop() const { return m_crop; }
    // What is drawn on the picture, in its own pixels.
    QList<Mark> marks() const;
    void setMarks(const QList<Mark>& marks);
    bool saving() const { return m_saving->isRunning(); }
    void setGame(const QString& game);
    // The picture shown was renamed, or is gone.
    void moveImage(const QString& from, const QString& to);
    void forgetImage(const QString& path);
    // The copy written beside the original: "name (recadré).png", or "name (annoté).png" once something is drawn on it.
    static QString copyPath(const QString& original, const QString& word);
    static QString croppedPath(const QString& original) { return copyPath(original, QStringLiteral("recadré")); }
signals:
    void backRequested();
    void renameRequested(const QString& path, const QString& name);
    void deleteRequested(const QString& path, bool confirmed);
    void copyRequested(const QStringList& paths);
    void revealRequested(const QString& path);
    void saved(const QString& path, const QString& original);
    void openRequested(const QString& path);
    void failed(const QString& title, const QString& detail);
protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
private:
    void chooseRatio(int index);
    void chooseTool(int index);
    void chooseColor(int index);
    double strokeWidth() const;
    void setCrop(QRect crop);
    void saveCrop();
    void finishRename(bool commit);
    void updateActions();
    QString m_path, m_game, m_savedPath; QSize m_size; QRect m_crop; int m_ratio = 0; bool m_loaded = false;
    QFutureWatcher<QString>* m_saving;
    Button* m_back; ElideLabel* m_title; QLineEdit* m_titleEdit; Button* m_rename; Tag* m_gameTag; Button* m_copy; Button* m_reveal; Button* m_trash; Button* m_save;
    Banner* m_banner; VideoCanvas* m_canvas; Segmented* m_mode; QList<Tile*> m_ratios; QLabel* m_cropSize; QList<QLabel*> m_facts;
    MarkupLayer* m_markup; QList<Tile*> m_tools; QList<QPushButton*> m_colors; Segmented* m_thickness; Button* m_undo; Button* m_clearMarks;
    // What was drawn on each picture opened in this session, kept when another is opened.
    QHash<QString, QList<Mark>> m_drafts;
};
}
