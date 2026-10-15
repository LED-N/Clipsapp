#pragma once
#include <QImage>
#include <QTimer>
#include <QWidget>
class QLabel;
namespace clipsapp {
class Button;
class ElideLabel;
// The screenshots full screen, one after the other: the arrows of the keyboard, or those on the
// sides, go to the previous and the next one in the order of the list.
class ImageViewer final : public QWidget {
    Q_OBJECT
public:
    explicit ImageViewer(QWidget* owner);
    void open(const QStringList& playlist, int index);
    void finish();
    QString currentPath() const { return m_playlist.value(m_index); }
    int index() const { return m_index; }
    QImage image() const { return m_image; }
signals:
    void viewed(const QString& path);
    void editRequested(const QString& path);
    void copyRequested(const QString& path);
    void closed(const QString& path);
protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void closeEvent(QCloseEvent* event) override;
private:
    void load(int index);
    void jump(int index);
    void wake();
    void setControlsVisible(bool visible);
    QSize deviceSize() const;
    QWidget* m_owner; QStringList m_playlist; int m_index = -1, m_wheel = 0; bool m_open = false;
    QImage m_image; QSize m_original; QString m_message; QTimer m_hide;
    QWidget* m_top; ElideLabel* m_title; QLabel* m_count; QLabel* m_size; Button* m_previous; Button* m_next;
};
}
