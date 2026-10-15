#include "image_viewer.h"
#include "widgets.h"
#include <QCloseEvent>
#include <QFileInfo>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QImageReader>
#include <QKeyEvent>
#include <QPainter>
#include <QScreen>
#include <QWheelEvent>
#include <algorithm>
namespace clipsapp {
ImageViewer::ImageViewer(QWidget* owner) : QWidget(owner, Qt::Window | Qt::FramelessWindowHint), m_owner(owner) {
    setObjectName("image-viewer"); setWindowTitle(QStringLiteral("Clipsapp")); setWindowModality(Qt::ApplicationModal); setAttribute(Qt::WA_OpaquePaintEvent);
    setFocusPolicy(Qt::StrongFocus); setMouseTracking(true); followVisibility(this, owner);
    setStyleSheet(QStringLiteral("QFrame#viewerBar { background: rgba(12, 14, 18, 224); border: none; }"));
    // The bar of the clip viewer, with what a picture offers: the editor and a copy for Discord.
    auto* top = new QFrame(this); top->setObjectName("viewerBar"); m_top = top; auto* layout = new QHBoxLayout(top); layout->setContentsMargins(20, 0, 12, 0); layout->setSpacing(12);
    m_title = new ElideLabel({}, "clip", Qt::ElideMiddle); m_title->setObjectName("image-viewer-title"); m_title->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed); layout->addWidget(m_title);
    m_count = label({}, "small"); m_count->setObjectName("image-viewer-count"); layout->addWidget(m_count);
    m_size = label({}, "small3"); m_size->setObjectName("image-viewer-size"); layout->addWidget(m_size); layout->addStretch();
    layout->addWidget(label(QStringLiteral("← → pour changer d'image · Échap pour quitter"), "small3"));
    auto* copy = new Button(QStringLiteral("copy"), QStringLiteral("Copier"), Button::Ghost); copy->setObjectName("image-viewer-copy"); copy->setFocusPolicy(Qt::NoFocus); copy->setToolTip(QStringLiteral("Copier l'image (Ctrl C)")); layout->addWidget(copy);
    auto* edit = new Button(QStringLiteral("crop"), QStringLiteral("Éditer"), Button::Ghost); edit->setObjectName("image-viewer-edit"); edit->setFocusPolicy(Qt::NoFocus); edit->setToolTip(QStringLiteral("Ouvrir dans l'éditeur (Entrée)")); layout->addWidget(edit);
    auto* close = new Button(QStringLiteral("x"), QString(), Button::Ghost); close->setObjectName("image-viewer-close"); close->setFocusPolicy(Qt::NoFocus); close->setToolTip(QStringLiteral("Quitter le plein écran (Échap)")); close->setAccessibleName(QStringLiteral("Quitter le plein écran")); layout->addWidget(close);
    // On each side, the way to the neighbouring pictures.
    m_previous = new Button(QStringLiteral("prev"), QString(), Button::Secondary); m_previous->setParent(this); m_previous->setObjectName("image-viewer-previous"); m_previous->setFixedSize(44, 44); m_previous->setFocusPolicy(Qt::NoFocus); m_previous->setToolTip(QStringLiteral("Image précédente (←)")); m_previous->setAccessibleName(QStringLiteral("Image précédente"));
    m_next = new Button(QStringLiteral("next"), QString(), Button::Secondary); m_next->setParent(this); m_next->setObjectName("image-viewer-next"); m_next->setFixedSize(44, 44); m_next->setFocusPolicy(Qt::NoFocus); m_next->setToolTip(QStringLiteral("Image suivante (→)")); m_next->setAccessibleName(QStringLiteral("Image suivante"));
    m_hide.setSingleShot(true); m_hide.setInterval(2500);
    connect(&m_hide, &QTimer::timeout, this, [this] { if (!m_top->underMouse() && !m_previous->underMouse() && !m_next->underMouse()) setControlsVisible(false); });
    connect(close, &QPushButton::clicked, this, [this] { finish(); });
    connect(copy, &QPushButton::clicked, this, [this] { if (m_index >= 0) emit copyRequested(currentPath()); });
    connect(edit, &QPushButton::clicked, this, [this] { if (m_index < 0) return; const auto path = currentPath(); finish(); emit editRequested(path); });
    connect(m_previous, &QPushButton::clicked, this, [this] { jump(m_index - 1); wake(); });
    connect(m_next, &QPushButton::clicked, this, [this] { jump(m_index + 1); wake(); });
}
void ImageViewer::open(const QStringList& playlist, int index) {
    if (playlist.isEmpty() || index < 0 || index >= playlist.size()) return;
    m_playlist = playlist; m_open = true;
    if (testAttribute(Qt::WA_DontShowOnScreen) || property("windowed").toBool()) show();
    else {
        // The screen the application is on, whole: no frame, no title bar.
        auto* screen = m_owner ? m_owner->screen() : QGuiApplication::primaryScreen();
        if (screen) setGeometry(screen->geometry());
        showFullScreen();
    }
    raise(); activateWindow(); setFocus(); load(index); wake();
}
QSize ImageViewer::deviceSize() const { const double ratio = devicePixelRatioF(); return QSize(qRound(width() * ratio), qRound(height() * ratio)).expandedTo({1, 1}); }
void ImageViewer::load(int index) {
    m_index = index; m_message.clear(); const auto path = currentPath();
    // Read at the size of the screen at most: a 4K picture on a 1080p screen is never decoded whole.
    QImageReader reader(path); reader.setAutoTransform(true); m_original = reader.size();
    if (m_original.isValid() && (m_original.width() > deviceSize().width() || m_original.height() > deviceSize().height())) reader.setScaledSize(m_original.scaled(deviceSize(), Qt::KeepAspectRatio));
    m_image = reader.read(); if (m_image.isNull()) m_message = QStringLiteral("Image illisible : ") + reader.errorString();
    m_title->setFullText(QFileInfo(path).completeBaseName());
    m_count->setText(QStringLiteral("%1 sur %2").arg(index + 1).arg(m_playlist.size())); m_count->setVisible(m_playlist.size() > 1);
    m_size->setText(m_original.isValid() ? QStringLiteral("%1 × %2").arg(m_original.width()).arg(m_original.height()) : QString());
    m_previous->setVisible(m_playlist.size() > 1 && m_top->isVisible()); m_next->setVisible(m_playlist.size() > 1 && m_top->isVisible());
    m_previous->setEnabled(index > 0); m_next->setEnabled(index + 1 < m_playlist.size());
    update(); if (!m_image.isNull()) emit viewed(path);
}
void ImageViewer::jump(int index) { if (index < 0 || index >= m_playlist.size() || index == m_index) return; load(index); }
void ImageViewer::finish() {
    if (!m_open) return;
    m_open = false; const auto path = currentPath(); m_hide.stop(); m_image = {}; m_index = -1; hide(); setCursor(Qt::ArrowCursor);
    emit closed(path);
}
void ImageViewer::wake() { setControlsVisible(true); m_hide.start(); }
void ImageViewer::setControlsVisible(bool visible) {
    const bool sides = visible && m_playlist.size() > 1;
    if (m_top->isVisible() == visible && m_previous->isVisible() == sides) return;
    m_top->setVisible(visible); m_previous->setVisible(sides); m_next->setVisible(sides); setCursor(visible ? Qt::ArrowCursor : Qt::BlankCursor);
}
void ImageViewer::resizeEvent(QResizeEvent*) {
    m_top->setGeometry(0, 0, width(), 56);
    m_previous->move(20, (height() - m_previous->height()) / 2); m_next->move(width() - 20 - m_next->width(), (height() - m_next->height()) / 2);
    // Opened on another screen, the picture is read again for its pixels.
    if (m_open && m_index >= 0 && !m_image.isNull() && m_original.isValid() && m_image.size() != m_original && m_image.size() != m_original.scaled(deviceSize(), Qt::KeepAspectRatio)) load(m_index);
}
void ImageViewer::paintEvent(QPaintEvent*) {
    QPainter painter(this); painter.fillRect(rect(), Qt::black);
    if (m_image.isNull()) { painter.setPen(m_message.isEmpty() ? tk::text3 : tk::danger); painter.setFont(uiFont(14)); painter.drawText(rect().adjusted(40, 40, -40, -40), Qt::AlignCenter | Qt::TextWordWrap, m_message.isEmpty() ? QStringLiteral("Chargement…") : m_message); return; }
    // In pixels of the screen: a picture no larger than the screen is shown as it is, never enlarged.
    const auto device = deviceSize(); const QSize shown = m_image.width() > device.width() || m_image.height() > device.height() ? m_image.size().scaled(device, Qt::KeepAspectRatio) : m_image.size();
    painter.scale(1 / devicePixelRatioF(), 1 / devicePixelRatioF()); painter.setRenderHint(QPainter::SmoothPixmapTransform);
    painter.drawImage(QRect(QPoint((device.width() - shown.width()) / 2, (device.height() - shown.height()) / 2), shown), m_image);
}
void ImageViewer::keyPressEvent(QKeyEvent* event) {
    wake();
    switch (event->key()) {
    case Qt::Key_Escape: case Qt::Key_F: case Qt::Key_F11: case Qt::Key_Space: finish(); break;
    case Qt::Key_Left: case Qt::Key_Up: case Qt::Key_PageUp: jump(m_index - 1); break;
    case Qt::Key_Right: case Qt::Key_Down: case Qt::Key_PageDown: jump(m_index + 1); break;
    case Qt::Key_Home: jump(0); break;
    case Qt::Key_End: jump(int(m_playlist.size()) - 1); break;
    case Qt::Key_Return: case Qt::Key_Enter: if (m_index >= 0) { const auto path = currentPath(); finish(); emit editRequested(path); } break;
    case Qt::Key_C: if (event->modifiers() & Qt::ControlModifier && m_index >= 0) emit copyRequested(currentPath()); else QWidget::keyPressEvent(event); break;
    default: QWidget::keyPressEvent(event);
    }
}
void ImageViewer::mouseMoveEvent(QMouseEvent* event) { wake(); QWidget::mouseMoveEvent(event); }
void ImageViewer::mouseDoubleClickEvent(QMouseEvent* event) { if (event->button() == Qt::LeftButton && !childAt(event->position().toPoint())) finish(); }
// The wheel goes through the pictures, one a notch.
void ImageViewer::wheelEvent(QWheelEvent* event) {
    m_wheel += event->angleDelta().y(); const int notches = m_wheel / 120; m_wheel -= notches * 120; event->accept(); wake();
    if (notches) jump(std::clamp(m_index - notches, 0, int(m_playlist.size()) - 1));
}
void ImageViewer::closeEvent(QCloseEvent* event) { finish(); event->accept(); }
}
