#include "theme_editor.h"
#include "widgets.h"
#include <QApplication>
#include <QClipboard>
#include <QFrame>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainterPath>
#include <QPushButton>
#include <algorithm>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QPainter>
#include <QVBoxLayout>

namespace clipsapp {
namespace {
// The shades of one hue: more vivid to the right, darker downward.
class ShadeSquare final : public QWidget {
public:
    explicit ShadeSquare(QWidget* parent = nullptr) : QWidget(parent) { setFixedSize(260, 170); setCursor(Qt::CrossCursor); }
    void set(double hue, double saturation, double value) { m_hue = hue; m_saturation = saturation; m_value = value; update(); }
    std::function<void(double, double)> moved;
protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this); painter.setRenderHint(QPainter::Antialiasing); const QRectF box = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
        QPainterPath shape; shape.addRoundedRect(box, 6, 6); painter.setClipPath(shape);
        QLinearGradient across(box.topLeft(), box.topRight()); across.setColorAt(0, Qt::white); across.setColorAt(1, QColor::fromHsvF(m_hue, 1, 1)); painter.fillRect(box, across);
        QLinearGradient down(box.topLeft(), box.bottomLeft()); down.setColorAt(0, QColor(0, 0, 0, 0)); down.setColorAt(1, Qt::black); painter.fillRect(box, down);
        painter.setClipping(false); painter.setPen(QPen(tk::lineStrong, 1)); painter.setBrush(Qt::NoBrush); painter.drawRoundedRect(box, 6, 6);
        const QPointF mark(box.left() + m_saturation * box.width(), box.top() + (1 - m_value) * box.height());
        painter.setPen(QPen(Qt::black, 3)); painter.drawEllipse(mark, 6, 6); painter.setPen(QPen(Qt::white, 1.6)); painter.drawEllipse(mark, 6, 6);
    }
    void mousePressEvent(QMouseEvent* event) override { pick(event->position()); }
    void mouseMoveEvent(QMouseEvent* event) override { if (event->buttons() & Qt::LeftButton) pick(event->position()); }
private:
    void pick(const QPointF& at) { if (moved) moved(std::clamp(at.x() / width(), 0.0, 1.0), std::clamp(1 - at.y() / height(), 0.0, 1.0)); }
    double m_hue = 0, m_saturation = 1, m_value = 1;
};
// Every hue, from red back to red.
class HueBar final : public QWidget {
public:
    explicit HueBar(QWidget* parent = nullptr) : QWidget(parent) { setFixedSize(260, 16); setCursor(Qt::PointingHandCursor); }
    void set(double hue) { m_hue = hue; update(); }
    std::function<void(double)> moved;
protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this); painter.setRenderHint(QPainter::Antialiasing); const QRectF box = QRectF(rect()).adjusted(0.5, 2.5, -0.5, -2.5);
        QLinearGradient hues(box.topLeft(), box.topRight()); for (int step = 0; step <= 6; ++step) hues.setColorAt(step / 6.0, QColor::fromHsvF(std::min(0.9999, step / 6.0), 1, 1));
        painter.setPen(QPen(tk::lineStrong, 1)); painter.setBrush(hues); painter.drawRoundedRect(box, 5, 5);
        const double x = box.left() + m_hue * box.width(); painter.setPen(QPen(Qt::black, 3)); painter.setBrush(Qt::NoBrush); painter.drawRoundedRect(QRectF(x - 3, 0.5, 6, height() - 1), 2, 2);
        painter.setPen(QPen(Qt::white, 1.5)); painter.drawRoundedRect(QRectF(x - 3, 0.5, 6, height() - 1), 2, 2);
    }
    void mousePressEvent(QMouseEvent* event) override { pick(event->position()); }
    void mouseMoveEvent(QMouseEvent* event) override { if (event->buttons() & Qt::LeftButton) pick(event->position()); }
private:
    void pick(const QPointF& at) { if (moved) moved(std::clamp(at.x() / width(), 0.0, 0.9999)); }
    double m_hue = 0;
};
// The picker of the application, in French and in its colors: shades of a hue, the hue, the code,
// and a few colors that suit a theme. Each color tried is given at once.
class ColorPicker final : public ModalDialog {
public:
    ColorPicker(const QColor& color, const QString& title, QWidget* parent) : ModalDialog(parent, 470), m_before(color) {
        setObjectName("color-picker"); setAside(); setHeading(title, QStringLiteral("Chaque couleur essayée s'affiche aussitôt dans Clipsapp."));
        auto* row = new QHBoxLayout; row->setSpacing(18);
        auto* left = new QVBoxLayout; left->setSpacing(10); m_square = new ShadeSquare; m_hueBar = new HueBar; left->addWidget(m_square); left->addWidget(m_hueBar); row->addLayout(left);
        auto* right = new QVBoxLayout; right->setSpacing(10);
        m_sample = new QFrame; m_sample->setFixedSize(130, 56); right->addWidget(m_sample);
        right->addWidget(label(QStringLiteral("Code"), "lbl")); m_code = new QLineEdit; m_code->setObjectName("color-code"); m_code->setFixedWidth(130); m_code->setFont(monoFont(13)); m_code->setMaxLength(7); right->addWidget(m_code);
        right->addWidget(label(QStringLiteral("Suggestions"), "lbl"));
        auto* suggestions = new QGridLayout; suggestions->setSpacing(6);
        const unsigned picks[]{0x6FE3B4, 0xA98BFF, 0xD48BFF, 0x5EA8FF, 0xF5B24A, 0xFF7EB6, 0xF2777C, 0x7BD4E6, 0x0C0E12, 0x0F0B17, 0x090E18, 0xEEEAF7};
        int index = 0; for (const unsigned pick : picks) {
            auto* swatch = new QPushButton; swatch->setFixedSize(18, 18); swatch->setCursor(Qt::PointingHandCursor); swatch->setToolTip(tk::rgb(pick).name().toUpper());
            swatch->setStyleSheet(QStringLiteral("QPushButton { background: %1; border: 1px solid %2; border-radius: 4px; } QPushButton:hover { border-color: %3; }").arg(tk::rgb(pick).name(), tk::lineStrong.name(), tk::text.name()));
            connect(swatch, &QPushButton::clicked, this, [this, pick] { setColor(tk::rgb(pick)); });
            suggestions->addWidget(swatch, index / 6, index % 6); ++index;
        }
        right->addLayout(suggestions); right->addStretch(); row->addLayout(right); body()->addLayout(row);
        auto* cancel = new Button(QStringLiteral("Annuler")); auto* choose = new Button(QStringLiteral("check"), QStringLiteral("Choisir"), Button::Primary); choose->setObjectName("color-choose"); choose->setDefault(true);
        footer()->addWidget(cancel); footer()->addWidget(choose);
        connect(cancel, &QPushButton::clicked, this, &QDialog::reject); connect(choose, &QPushButton::clicked, this, &QDialog::accept);
        m_square->moved = [this](double saturation, double value) { setColor(QColor::fromHsvF(m_hue, saturation, value)); };
        m_hueBar->moved = [this](double hue) { m_hue = hue; setColor(QColor::fromHsvF(hue, std::max(0.0, double(m_color.hsvSaturationF())), std::max(0.0, double(m_color.valueF()))), false); };
        connect(m_code, &QLineEdit::textEdited, this, [this](QString text) { if (!text.startsWith('#')) text.prepend('#'); if (QColor color(text); text.size() == 7 && color.isValid()) setColor(color, true, false); });
        m_hue = std::max(0.0, double(color.hsvHueF())); setColor(color);
    }
    QColor color() const { return m_color; }
    QColor before() const { return m_before; }
    std::function<void(const QColor&)> tried;
private:
    void setColor(const QColor& color, bool takeHue = true, bool writeCode = true) {
        m_color = color.toRgb(); if (takeHue && color.hsvHueF() >= 0) m_hue = color.hsvHueF();
        m_square->set(m_hue, m_color.hsvSaturationF(), m_color.valueF()); m_hueBar->set(m_hue);
        m_sample->setStyleSheet(QStringLiteral("QFrame { background: %1; border: 1px solid %2; border-radius: 8px; }").arg(m_color.name(), tk::lineStrong.name()));
        if (writeCode) m_code->setText(m_color.name().toUpper());
        if (tried) tried(m_color);
    }
    QColor m_before, m_color; double m_hue = 0;
    ShadeSquare* m_square; HueBar* m_hueBar; QFrame* m_sample; QLineEdit* m_code;
};
}
// A color to pick: its sample and its code. While the picker is open, every color tried is shown in
// the whole window; cancelled, the one before comes back.
class ColorSwatch final : public QPushButton {
public:
    explicit ColorSwatch(const QString& title, QWidget* parent = nullptr) : QPushButton(parent), m_title(title) {
        setCursor(Qt::PointingHandCursor); setFixedSize(132, 34); setFocusPolicy(Qt::StrongFocus); setAccessibleName(title);
        connect(this, &QPushButton::clicked, this, [this] { pick(); });
    }
    void setColor(const QColor& color) { m_color = color; setToolTip(QStringLiteral("%1 : %2").arg(m_title, color.name().toUpper())); update(); }
    QColor color() const { return m_color; }
    std::function<void(const QColor&)> picked;
protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this); painter.setRenderHint(QPainter::Antialiasing);
        const bool off = !isEnabled(), over = underMouse() && !off;
        painter.setPen(QPen(over ? tk::lineHover : tk::lineStrong, 1)); painter.setBrush(off ? tk::header : tk::field);
        painter.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 6, 6);
        painter.setOpacity(off ? 0.35 : 1); painter.setPen(QPen(tk::lineHover, 1)); painter.setBrush(m_color); painter.drawRoundedRect(QRectF(7.5, 7.5, 19, 19), 4, 4);
        painter.setFont(monoFont(12)); painter.setPen(off ? tk::textOff : tk::text); painter.drawText(QRectF(34, 0, width() - 40, height()), Qt::AlignVCenter | Qt::AlignLeft, m_color.name().toUpper());
    }
private:
    void pick() {
        const QColor before = m_color;
        ColorPicker dialog(m_color, m_title, window()); dialog.tried = [this](const QColor& color) { if (picked) picked(color); };
        if (dialog.exec() == QDialog::Accepted) { if (picked) picked(dialog.color()); }
        else if (picked) picked(before);
    }
    QString m_title; QColor m_color;
};
// A theme to start from, drawn in its own colors.
class PresetTile final : public QPushButton {
public:
    PresetTile(const ThemePreset& preset, QWidget* parent = nullptr) : QPushButton(parent), m_preset(preset) {
        setCheckable(true); setCursor(Qt::PointingHandCursor); setFixedSize(124, 70); setObjectName(QStringLiteral("theme-preset-") + preset.id); setAccessibleName(preset.name); setToolTip(preset.name);
    }
    const ThemePreset& preset() const { return m_preset; }
protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this); painter.setRenderHint(QPainter::Antialiasing); const auto& colors = m_preset.colors; const QRectF box = QRectF(rect()).adjusted(1, 1, -1, -1);
        QLinearGradient ground(box.topLeft(), box.bottomRight()); ground.setColorAt(0, colors.background); ground.setColorAt(1, colors.windowGradient ? colors.windowEnd : colors.background);
        painter.setPen(QPen(isChecked() ? tk::accent : underMouse() ? tk::lineHover : tk::line, isChecked() ? 2 : 1)); painter.setBrush(ground); painter.drawRoundedRect(box, 8, 8);
        QLinearGradient accent(QPointF(12, 0), QPointF(52, 0)); accent.setColorAt(0, colors.accent); accent.setColorAt(1, colors.accentGradient ? colors.accentEnd : colors.accent);
        painter.setPen(Qt::NoPen); painter.setBrush(accent); painter.drawRoundedRect(QRectF(12, 14, 40, 10), 5, 5);
        painter.setBrush(tk::mix(colors.background, colors.text, 0.12)); painter.drawRoundedRect(QRectF(58, 14, 30, 10), 5, 5);
        painter.setFont(uiFont(12, QFont::DemiBold)); painter.setPen(colors.text); painter.drawText(QRectF(12, 34, width() - 20, 24), Qt::AlignVCenter | Qt::AlignLeft, m_preset.name);
    }
private:
    ThemePreset m_preset;
};
ThemeEditor::ThemeEditor(QWidget* parent) : QWidget(parent) {
    setObjectName("theme-editor");
    auto* layout = new QVBoxLayout(this); layout->setContentsMargins(0, 0, 0, 0); layout->setSpacing(14);
    layout->addWidget(label(QStringLiteral("Partir d'un thème"), "lbl"));
    auto* presets = new QHBoxLayout; presets->setSpacing(10);
    for (const auto& preset : themePresets()) {
        auto* tile = new PresetTile(preset); m_presets.append(tile); presets->addWidget(tile);
        connect(tile, &QPushButton::clicked, this, [this, tile] { edit([tile](ThemeColors& colors) { colors = tile->preset().colors; }); });
    }
    presets->addStretch(); layout->addLayout(presets);

    layout->addWidget(label(QStringLiteral("Personnaliser"), "lbl"));
    auto* grid = new QGridLayout; grid->setHorizontalSpacing(14); grid->setVerticalSpacing(10); grid->setColumnStretch(3, 1);
    const auto swatch = [this](const QString& title, const char* name, const std::function<void(ThemeColors&, const QColor&)>& set) {
        auto* result = new ColorSwatch(title); result->setObjectName(QLatin1String(name));
        result->picked = [this, set](const QColor& color) { edit([&set, &color](ThemeColors& colors) { set(colors, color); }); };
        return result;
    };
    m_background = swatch(QStringLiteral("Fond"), "theme-background", [](ThemeColors& colors, const QColor& color) { colors.background = color; if (!colors.windowGradient) colors.windowEnd = color; });
    m_accent = swatch(QStringLiteral("Couleur principale"), "theme-accent", [](ThemeColors& colors, const QColor& color) { colors.accent = color; if (!colors.accentGradient) colors.accentEnd = color; });
    m_text = swatch(QStringLiteral("Texte"), "theme-text", [](ThemeColors& colors, const QColor& color) { colors.text = color; });
    m_windowEnd = swatch(QStringLiteral("Second fond"), "theme-window-end", [](ThemeColors& colors, const QColor& color) { colors.windowEnd = color; });
    m_accentEnd = swatch(QStringLiteral("Seconde couleur principale"), "theme-accent-end", [](ThemeColors& colors, const QColor& color) { colors.accentEnd = color; });
    m_windowGradient = new Toggle(QStringLiteral("Dégradé du fond")); m_windowGradient->setObjectName("theme-window-gradient");
    m_accentGradient = new Toggle(QStringLiteral("Dégradé des boutons")); m_accentGradient->setObjectName("theme-accent-gradient");
    m_angle = new ChoiceBox; m_angle->setObjectName("theme-angle"); m_angle->setFixedWidth(190);
    for (const auto& [text, angle] : std::initializer_list<std::pair<const char*, int>>{{"→  Vers la droite", 0}, {"↘  Vers le bas à droite", 45}, {"↓  Vers le bas", 90}, {"↙  Vers le bas à gauche", 135}, {"←  Vers la gauche", 180}, {"↖  Vers le haut à gauche", 225}, {"↑  Vers le haut", 270}, {"↗  Vers le haut à droite", 315}})
        m_angle->addItem(QString::fromUtf8(text), angle);
    const auto row = [grid](int line, const QString& title, QWidget* first, QWidget* second = nullptr, QWidget* third = nullptr) {
        grid->addWidget(label(title), line, 0); grid->addWidget(first, line, 1);
        if (second) grid->addWidget(second, line, 2);
        if (third) grid->addWidget(third, line, 3, Qt::AlignLeft);
    };
    row(0, QStringLiteral("Fond"), m_background, m_windowGradient, m_windowEnd);
    row(1, QStringLiteral("Direction du dégradé"), m_angle);
    row(2, QStringLiteral("Couleur principale"), m_accent, m_accentGradient, m_accentEnd);
    row(3, QStringLiteral("Texte"), m_text);
    layout->addLayout(grid);
    m_darkNote = label(QStringLiteral("Les fonds clairs ne sont pas encore proposés : un fond trop clair est assombri, un texte trop sombre éclairci, pour que tout reste lisible."), "hint", true);
    layout->addWidget(m_darkNote);
    connect(m_windowGradient, &QCheckBox::toggled, this, [this](bool on) { edit([on](ThemeColors& colors) { colors.windowGradient = on; }); });
    connect(m_accentGradient, &QCheckBox::toggled, this, [this](bool on) { edit([on](ThemeColors& colors) { colors.accentGradient = on; }); });
    connect(m_angle, &QComboBox::currentIndexChanged, this, [this] { edit([this](ThemeColors& colors) { colors.angle = m_angle->currentData().toInt(); }); });

    // A theme travels as one line of text: copied here, pasted by a friend in his own Clipsapp.
    auto* actions = new QHBoxLayout; actions->setSpacing(8);
    auto* copy = new Button(QStringLiteral("copy"), QStringLiteral("Copier le thème"), Button::Secondary, Button::Small); copy->setObjectName("theme-copy");
    auto* paste = new Button(QStringLiteral("plus"), QStringLiteral("Coller un thème"), Button::Secondary, Button::Small); paste->setObjectName("theme-paste");
    auto* reset = new Button(QStringLiteral("undo"), QStringLiteral("Revenir au thème Clipsapp"), Button::Ghost, Button::Small); reset->setObjectName("theme-reset");
    actions->addWidget(copy); actions->addWidget(paste); actions->addStretch(); actions->addWidget(reset); layout->addLayout(actions);
    layout->addWidget(label(QStringLiteral("« Copier le thème » met un code dans le presse-papiers, à envoyer à un ami : dans son Clipsapp, « Coller un thème » le reprend tel quel."), "hint", true));
    connect(copy, &QPushButton::clicked, this, [this] {
        QApplication::clipboard()->setText(themeCode(m_colors));
        emit notice(QStringLiteral("Thème copié"), QStringLiteral("Collez le code à un ami : « Coller un thème » le reprend dans son Clipsapp."));
    });
    connect(paste, &QPushButton::clicked, this, [this] {
        ThemeColors read;
        if (!readThemeCode(QApplication::clipboard()->text(), read)) { emit notice(QStringLiteral("Aucun thème à coller"), QStringLiteral("Copiez d'abord un code de thème, qui commence par « clipsapp-theme- »."));  return; }
        edit([&read](ThemeColors& colors) { colors = read; });
        emit notice(QStringLiteral("Thème collé"), QStringLiteral("Enregistrez les réglages pour le garder."));
    });
    connect(reset, &QPushButton::clicked, this, [this] { edit([](ThemeColors& colors) { colors = ThemeColors{}; }); });
    display(m_colors);
}
void ThemeEditor::setTheme(const ThemeColors& colors) { m_colors = colors; display(colors); }
void ThemeEditor::edit(const std::function<void(ThemeColors&)>& change) {
    if (m_loading) return;
    auto colors = m_colors; change(colors); colors = darkTheme(colors);
    if (colors == m_colors) { display(colors); return; }
    m_colors = colors; display(colors); emit changed(colors);
}
void ThemeEditor::display(const ThemeColors& colors) {
    m_loading = true;
    m_background->setColor(colors.background); m_accent->setColor(colors.accent); m_text->setColor(colors.text);
    m_windowEnd->setColor(colors.windowEnd); m_accentEnd->setColor(colors.accentEnd);
    m_windowGradient->setChecked(colors.windowGradient); m_accentGradient->setChecked(colors.accentGradient);
    m_windowEnd->setEnabled(colors.windowGradient); m_accentEnd->setEnabled(colors.accentGradient); m_angle->setEnabled(colors.windowGradient);
    int angle = m_angle->findData(colors.angle);
    if (angle < 0) { m_angle->addItem(QStringLiteral("%1°").arg(colors.angle), colors.angle); angle = m_angle->count() - 1; }
    m_angle->setCurrentIndex(angle);
    for (auto* tile : m_presets) tile->setChecked(tile->preset().colors == colors);
    m_loading = false;
}
}
