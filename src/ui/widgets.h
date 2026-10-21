#pragma once
#include "theme.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDoubleSpinBox>
#include <QFrame>
#include <QKeySequence>
#include <QLabel>
#include <QList>
#include <QPointer>
#include <QPushButton>
#include <functional>
class QHBoxLayout;
class QMenu;
class QTimer;
class QVBoxLayout;

namespace clipsapp {
QLabel* label(const QString& text, const char* role = nullptr, bool wrap = false);
QLabel* caption(const QString& text);
QLabel* monoLabel(const QString& text, int pixels = 12, const QColor& color = tk::text, int weight = QFont::Normal);
QFrame* card(const char* kind = "true");
QFrame* separator(bool vertical = false);
// Centres a widget in a row. A wrapped label keeps its full height this way, which an alignment flag would not give it.
QHBoxLayout* centered(QWidget* widget);
QMenu* popupMenu(QWidget* parent);
// Tests run the window off screen; its dialogs and menus must follow it there.
void followVisibility(QWidget* child, const QWidget* parent);

class Button : public QPushButton {
public:
    enum Variant { Secondary, Primary, Ghost, Danger, DangerSolid, Segment, Tool };
    enum Size { Small, Medium, Large };
    explicit Button(const QString& text = {}, Variant variant = Secondary, Size size = Medium, QWidget* parent = nullptr);
    Button(const QString& iconName, const QString& text, Variant variant = Secondary, Size size = Medium, QWidget* parent = nullptr);
    void setVariant(Variant variant) { m_variant = variant; update(); }
    void setIconName(const QString& name) { m_icon = name; updateGeometry(); update(); }
    void setTrailingIcon(const QString& name) { m_trailing = name; updateGeometry(); update(); }
    void setKbd(const QString& keys) { m_kbd = keys; updateGeometry(); update(); }
    void setTextColor(const QColor& color) { m_color = color; update(); }
    // Label on the left, trailing icon on the right, as a list opener.
    void setSpread(bool spread) { m_spread = spread; update(); }
    void setPadding(int padding) { m_padding = padding; updateGeometry(); update(); }
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;
    static int heightOf(Size size) { return size == Small ? 28 : size == Large ? 40 : 32; }
protected:
    void paintEvent(QPaintEvent*) override;
    void focusInEvent(QFocusEvent* event) override;
    bool event(QEvent* event) override;
private:
    QFont textFont() const;
    Variant m_variant; Size m_size; QString m_icon, m_trailing, m_kbd; QColor m_color; bool m_spread = false, m_keyboardFocus = false; int m_padding = -1;
};
QSize kbdSize(const QString& keys);
void paintKbd(QPainter& painter, const QRectF& rect, const QString& keys, const QColor& color);

class Tag : public QWidget {
public:
    enum Variant { Neutral, Ok, Warn, Err, Info, Dark };
    explicit Tag(const QString& text = {}, Variant variant = Neutral, QWidget* parent = nullptr);
    void set(const QString& text, Variant variant) { m_text = text; m_variant = variant; updateGeometry(); update(); }
    void setText(const QString& text) { set(text, m_variant); }
    void setIconName(const QString& name) { m_icon = name; updateGeometry(); update(); }
    QString text() const { return m_text; }
    Variant variant() const { return m_variant; }
    QSize sizeHint() const override { return measure(m_text, !m_icon.isEmpty()); }
    static QSize measure(const QString& text, bool icon = false);
    static void paint(QPainter& painter, const QRectF& rect, const QString& text, Variant variant, const QString& icon = {});
protected:
    void paintEvent(QPaintEvent*) override;
private:
    QString m_text, m_icon; Variant m_variant;
};

// A check box drawn as a switch (a setting applied at once) or as a box (a choice in a list).
class Toggle : public QCheckBox {
public:
    enum Kind { Switch, Box };
    explicit Toggle(const QString& text = {}, Kind kind = Switch, QWidget* parent = nullptr);
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override { return sizeHint(); }
    static void paintBox(QPainter& painter, const QRectF& rect, bool checked, bool partial = false);
protected:
    void paintEvent(QPaintEvent*) override;
    bool hitButton(const QPoint& position) const override { return rect().contains(position); }
private:
    Kind m_kind;
};
class RadioRow : public QPushButton {
public:
    RadioRow(const QString& title, const QString& detail, QWidget* parent = nullptr);
    QSize sizeHint() const override { return {200, 38}; }
protected:
    void paintEvent(QPaintEvent*) override;
private:
    QString m_title, m_detail;
};

// A selectable card: quality presets, ratios, limits, source kinds, screens.
class Tile : public QPushButton {
public:
    enum Shape { NoShape, Outline, Filled, Dashed };
    explicit Tile(const QString& title, const QString& subtitle = {}, QWidget* parent = nullptr);
    void setTitle(const QString& title) { m_title = title; update(); }
    void setSubtitle(const QString& subtitle) { m_subtitle = subtitle; updateGeometry(); update(); }
    void setIconName(const QString& name) { m_icon = name; updateGeometry(); update(); }
    void setShape(QSizeF size, Shape shape) { m_shapeSize = size; m_shape = shape; update(); }
    void setPicture(const QString& iconName, bool dashed) { m_picture = true; m_pictureIcon = iconName; m_pictureDashed = dashed; updateGeometry(); update(); }
    void setCentered(bool centered) { m_centered = centered; update(); }
    void setTitlePixels(int pixels) { m_titlePixels = pixels; updateGeometry(); update(); }
    void setRaised(bool raised) { m_raised = raised; update(); }
    void setMutedTitle(bool muted) { m_mutedTitle = muted; update(); }
    void setFixedTileHeight(int height) { m_height = height; updateGeometry(); }
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override { return {40, sizeHint().height()}; }
protected:
    void paintEvent(QPaintEvent*) override;
private:
    QString m_title, m_subtitle, m_icon, m_pictureIcon; QSizeF m_shapeSize; Shape m_shape = NoShape;
    bool m_centered = false, m_picture = false, m_pictureDashed = false, m_raised = false, m_mutedTitle = false; int m_titlePixels = 13, m_height = 0;
};

class Segmented : public QWidget {
    Q_OBJECT
public:
    explicit Segmented(QWidget* parent = nullptr);
    void addOption(const QString& text, const QString& iconName = {});
    void setOptionText(int index, const QString& text);
    int currentIndex() const { return m_current; }
    void setCurrentIndex(int index);
    void setDark(bool dark) { m_dark = dark; update(); }
signals:
    void currentChanged(int index);
protected:
    void paintEvent(QPaintEvent*) override;
private:
    QHBoxLayout* m_layout; QList<Button*> m_buttons; int m_current = -1; bool m_dark = false;
};

// Gauge, progress and size meter share one shape.
class Bar : public QWidget {
public:
    explicit Bar(QWidget* parent = nullptr, int height = 6);
    void setValue(double value);
    // An invalid color is the theme's, read when the bar is painted: the accent (or its gradient) and its track.
    void setColors(const QColor& fill, const QColor& track = QColor()) { m_fill = fill; m_track = track; update(); }
    void setMarker(double position) { m_marker = position; update(); }
    double value() const { return m_value; }
protected:
    void paintEvent(QPaintEvent*) override;
private:
    double m_value = 0, m_marker = -1; QColor m_fill, m_track;
};

class Keycaps : public QWidget {
public:
    explicit Keycaps(QWidget* parent = nullptr, bool large = false);
    void setSequence(const QKeySequence& sequence);
    QStringList keys() const { return m_keys; }
    QSize sizeHint() const override;
    static QStringList keyNames(const QKeySequence& sequence);
protected:
    void paintEvent(QPaintEvent*) override;
private:
    QStringList m_keys; bool m_large;
};

class IconBox : public QWidget {
public:
    IconBox(const QString& iconName, const QColor& color = tk::text2, const QColor& background = tk::raised, int size = 32, QWidget* parent = nullptr);
    void set(const QString& iconName, const QColor& color, const QColor& background) { m_icon = iconName; m_color = color; m_background = background; update(); }
    void setRound(bool round) { m_round = round; update(); }
protected:
    void paintEvent(QPaintEvent*) override;
private:
    QString m_icon; QColor m_color, m_background; bool m_round = false;
};
// A bare icon that keeps its place in a layout.
class IconLabel : public QWidget {
public:
    IconLabel(const QString& iconName, const QColor& color = tk::text2, int size = 16, QWidget* parent = nullptr);
    void set(const QString& iconName, const QColor& color) { m_icon = iconName; m_color = color; update(); }
protected:
    void paintEvent(QPaintEvent*) override;
private:
    QString m_icon; QColor m_color;
};

// One line of text that gives way with an ellipsis instead of widening its row.
class ElideLabel : public QLabel {
public:
    explicit ElideLabel(const QString& text = {}, const char* role = nullptr, Qt::TextElideMode mode = Qt::ElideRight, QWidget* parent = nullptr);
    void setFullText(const QString& text) { m_full = text; setToolTip(text); updateGeometry(); update(); }
    QString fullText() const { return m_full; }
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override { return {12, QLabel::minimumSizeHint().height()}; }
protected:
    void paintEvent(QPaintEvent*) override;
private:
    QString m_full; Qt::TextElideMode m_mode;
};
// A dashed call to add something: quiet until hovered.
class DashedButton : public QPushButton {
public:
    DashedButton(const QString& iconName, const QString& text, QWidget* parent = nullptr);
    QSize sizeHint() const override { return {200, 44}; }
protected:
    void paintEvent(QPaintEvent*) override;
private:
    QString m_icon;
};
class TabButton : public QPushButton {
public:
    TabButton(const QString& iconName, const QString& text, QWidget* parent = nullptr);
    QSize sizeHint() const override;
protected:
    void paintEvent(QPaintEvent*) override;
private:
    QString m_icon;
};

class ChoiceBox : public QComboBox {
public:
    explicit ChoiceBox(QWidget* parent = nullptr);
protected:
    void paintEvent(QPaintEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
};
// Seconds, shown and typed as mm:ss.zzz.
class TimeField : public QDoubleSpinBox {
public:
    explicit TimeField(QWidget* parent = nullptr);
    QString textFromValue(double value) const override;
    double valueFromText(const QString& text) const override;
    QValidator::State validate(QString& input, int& position) const override;
};
QString timeText(qint64 milliseconds);

class Banner : public QFrame {
public:
    enum Kind { Ok, Info, Warn, Err };
    explicit Banner(Kind kind, const QString& iconName, QWidget* parent = nullptr);
    void setTexts(const QString& title, const QString& detail);
    QHBoxLayout* actions() const { return m_actions; }
private:
    QLabel* m_title; QLabel* m_detail; QHBoxLayout* m_actions;
};

// A frameless panel over a flat scrim. The safe button takes the focus by default.
class ModalDialog : public QDialog {
public:
    explicit ModalDialog(QWidget* parent, int width = 480);
    void setHeading(const QString& title, const QString& subtitle = {}, QWidget* leading = nullptr, bool closeButton = true);
    void setHeadingTexts(const QString& title, const QString& subtitle);
    // Fits the panel to its content again and keeps it centred on its window.
    void refit();
    // Without the scrim, at the top right of its window: what it changes stays in sight behind it.
    void setAside() { m_aside = true; }
    QVBoxLayout* headerLayout() const { return m_header; }
    QVBoxLayout* body() const { return m_body; }
    QHBoxLayout* footer() const { return m_footer; }
    QHBoxLayout* footerLeft() const { return m_footerLeft; }
protected:
    void paintEvent(QPaintEvent*) override;
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;
private:
    QVBoxLayout* m_header; QVBoxLayout* m_body; QHBoxLayout* m_footer; QHBoxLayout* m_footerLeft; QHBoxLayout* m_headingRow;
    QLabel* m_title = nullptr; QLabel* m_subtitle = nullptr; QPointer<QWidget> m_scrim; bool m_aside = false;
};

// One question, one field. Returns false when the user backs out.
bool askText(QWidget* parent, const QString& title, const QString& subtitle, const QString& fieldLabel, QString& value, const QString& acceptText);

struct ToastAction { QString text; std::function<void()> run; };
// Notifications stack bottom right, three at most. They replace the status bar.
class ToastHost : public QObject {
public:
    enum Kind { Ok, Info, Warn, Error };
    explicit ToastHost(QWidget* area);
    void show(Kind kind, const QString& title, const QString& text = {}, QList<ToastAction> actions = {}, const QString& iconName = {});
    void setBottomInset(int inset) { m_inset = inset; layout(); }
protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
private:
    void layout();
    QWidget* m_area; QList<QPointer<QWidget>> m_toasts; int m_inset = 20;
};
}
