#include "screenshot_editor.h"
#include "video_canvas.h"
#include "widgets.h"
#include "core/clip_library.h"
#include <QDir>
#include <QFileInfo>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QImageReader>
#include <QKeyEvent>
#include <QPainter>
#include <QPushButton>
#include <QLineEdit>
#include <QLocale>
#include <QScrollArea>
#include <QShortcut>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>
#include <algorithm>
#include <cmath>
namespace clipsapp {
namespace {
// A colour to draw with: a round swatch, ringed when chosen.
class ColorDot final : public QPushButton {
public:
    explicit ColorDot(const QColor& color) : m_color(color) { setCheckable(true); setFixedSize(26, 26); setCursor(Qt::PointingHandCursor); }
    QColor color() const { return m_color; }
protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this); painter.setRenderHint(QPainter::Antialiasing);
        if (isChecked()) { painter.setPen(QPen(tk::accent, 2)); painter.setBrush(Qt::NoBrush); painter.drawEllipse(QRectF(rect()).adjusted(1, 1, -1, -1)); }
        painter.setPen(QPen(QColor(255, 255, 255, 60), 1)); painter.setBrush(m_color); painter.drawEllipse(QRectF(rect()).adjusted(5, 5, -5, -5));
    }
private:
    QColor m_color;
};
const char* const ratioNames[]{"Entier", "16:9", "9:16", "1:1", "Libre"};
const double ratios[]{0, 16.0 / 9.0, 9.0 / 16.0, 1.0, 0};
QVBoxLayout* section(QVBoxLayout* parent, const QString& title, const QString& iconName) {
    auto* layout = new QVBoxLayout; layout->setSpacing(10); auto* head = new QHBoxLayout; head->addWidget(caption(title)); head->addStretch();
    head->addWidget(new IconLabel(iconName, tk::text3)); layout->addLayout(head); parent->addLayout(layout); return layout;
}
}
QString ScreenshotEditor::copyPath(const QString& original, const QString& word) {
    const QFileInfo file(original); const auto base = file.completeBaseName();
    for (int number = 1; number < 1000; ++number) {
        const auto name = number == 1 ? QStringLiteral("%1 (%2).png").arg(base, word) : QStringLiteral("%1 (%2 %3).png").arg(base, word).arg(number);
        if (!file.dir().exists(name)) return file.dir().filePath(name);
    }
    return file.dir().filePath(capturedFileName(base, QStringLiteral("png")));
}
ScreenshotEditor::ScreenshotEditor(QWidget* parent) : QWidget(parent), m_saving(new QFutureWatcher<QString>(this)) {
    setObjectName("screenshotEditor");
    setThemedStyleSheet(this, QStringLiteral("QWidget#screenshotInspector { background: {rail}; }"));
    auto* root = new QVBoxLayout(this); root->setContentsMargins(0, 0, 0, 0); root->setSpacing(0);
    // The header of the clip editor, with what a picture needs.
    auto* header = new QWidget; header->setFixedHeight(52); auto* top = new QHBoxLayout(header); top->setContentsMargins(16, 0, 16, 0); top->setSpacing(10);
    m_back = new Button(QStringLiteral("back"), QStringLiteral("Screenshots"), Button::Ghost); m_back->setObjectName("screenshot-back"); m_back->setPadding(10); top->addWidget(m_back);
    auto* divider = separator(true); divider->setFixedHeight(24); top->addWidget(divider);
    m_title = new ElideLabel({}, "clip", Qt::ElideMiddle); m_title->setObjectName("screenshot-title"); m_title->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed); m_title->setMaximumWidth(360); top->addWidget(m_title);
    m_titleEdit = new QLineEdit; m_titleEdit->setObjectName("screenshot-rename"); m_titleEdit->setMaxLength(120); m_titleEdit->setFixedWidth(300); m_titleEdit->hide(); m_titleEdit->installEventFilter(this); top->addWidget(m_titleEdit);
    m_rename = new Button(QStringLiteral("edit"), QString(), Button::Ghost, Button::Small); m_rename->setObjectName("screenshot-rename-button"); m_rename->setToolTip(QStringLiteral("Renommer le screenshot (F2)")); m_rename->setAccessibleName(QStringLiteral("Renommer le screenshot")); top->addWidget(m_rename);
    m_gameTag = new Tag; m_gameTag->setIconName(QStringLiteral("tag")); m_gameTag->hide(); top->addWidget(m_gameTag); top->addStretch(1);
    m_reveal = new Button(QStringLiteral("folder"), QString(), Button::Ghost); m_reveal->setObjectName("screenshot-reveal"); m_reveal->setToolTip(QStringLiteral("Afficher dans le dossier")); m_reveal->setAccessibleName(m_reveal->toolTip()); top->addWidget(m_reveal);
    m_copy = new Button(QStringLiteral("copy"), QStringLiteral("Copier l'image")); m_copy->setObjectName("screenshot-copy"); m_copy->setToolTip(QStringLiteral("Pour la coller dans Discord ou ailleurs (Ctrl C)"));
    m_save = new Button(QStringLiteral("save"), QStringLiteral("Enregistrer le recadrage"), Button::Primary); m_save->setObjectName("screenshot-save");
    root->addWidget(header); root->addWidget(separator());

    auto* workspace = new QHBoxLayout; workspace->setSpacing(0);
    auto* center = new QVBoxLayout; center->setContentsMargins(16, 16, 16, 16); center->setSpacing(12);
    m_banner = new Banner(Banner::Ok, QStringLiteral("ok")); m_banner->setObjectName("screenshot-banner"); m_banner->hide(); center->addWidget(m_banner);
    auto* openSaved = new Button(QStringLiteral("Ouvrir la nouvelle image"), Button::Secondary, Button::Small); openSaved->setObjectName("open-cropped");
    auto* trashOriginal = new Button(QStringLiteral("Mettre l'original à la corbeille…"), Button::Ghost, Button::Small); trashOriginal->setObjectName("trash-original-screenshot");
    auto* closeBanner = new Button(QStringLiteral("x"), QString(), Button::Ghost, Button::Small); closeBanner->setAccessibleName(QStringLiteral("Fermer"));
    for (auto* button : {openSaved, trashOriginal, closeBanner}) m_banner->actions()->addWidget(button);
    m_canvas = new VideoCanvas; m_canvas->setObjectName("screenshot-canvas"); center->addWidget(m_canvas, 1);
    m_mode = new Segmented; m_mode->setObjectName("screenshot-mode"); m_mode->setDark(true); m_mode->addOption(QStringLiteral("Cadrer"), QStringLiteral("crop")); m_mode->addOption(QStringLiteral("Dessiner"), QStringLiteral("edit")); m_mode->addOption(QStringLiteral("Résultat"), QStringLiteral("eye"));
    // What is drawn lies over the picture, below the buttons that sit on it.
    m_markup = new MarkupLayer(m_canvas);
    auto* overlay = new QWidget; auto* overlayLayout = new QHBoxLayout(overlay); overlayLayout->setContentsMargins(0, 0, 0, 0); overlayLayout->addWidget(m_mode); m_canvas->setOverlay(overlay); m_markup->keepUnder(overlay);
    m_mode->setCurrentIndex(2); m_canvas->setCropTool(false); m_canvas->setResultPreview(true);
    workspace->addLayout(center, 1); workspace->addWidget(separator(true));

    auto* scroll = new QScrollArea; scroll->setWidgetResizable(true); scroll->setFrameShape(QFrame::NoFrame); scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff); scroll->setFixedWidth(288); setThemedStyleSheet(scroll, QStringLiteral("QScrollArea { background: {rail}; }"));
    auto* inspector = new QWidget; inspector->setObjectName("screenshotInspector"); auto* layout = new QVBoxLayout(inspector); layout->setContentsMargins(18, 16, 18, 16); layout->setSpacing(18);
    auto* crop = section(layout, QStringLiteral("Cadrage"), QStringLiteral("crop"));
    auto* ratioRow = new QHBoxLayout; ratioRow->setSpacing(6);
    const QSizeF shapes[]{{26, 15}, {24, 14}, {11, 19}, {16, 16}, {22, 15}}; const Tile::Shape kinds[]{Tile::Filled, Tile::Outline, Tile::Outline, Tile::Outline, Tile::Dashed};
    for (int index = 0; index < 5; ++index) {
        auto* tile = new Tile(QString::fromLatin1(ratioNames[index])); tile->setObjectName(QStringLiteral("screenshot-ratio-%1").arg(index)); tile->setCentered(true); tile->setRaised(true); tile->setShape(shapes[index], kinds[index]); tile->setFixedTileHeight(58);
        tile->setChecked(index == 0); ratioRow->addWidget(tile); m_ratios.append(tile); connect(tile, &QPushButton::clicked, this, [this, index] { chooseRatio(index); });
    }
    crop->addLayout(ratioRow); crop->addWidget(label(QStringLiteral("Tracez ou glissez le cadre sur l'image."), "hint", true));
    m_cropSize = label({}, "small"); m_cropSize->setObjectName("screenshot-crop-size"); m_cropSize->setFont(monoFont(12));
    auto* resetCrop = new Button(QStringLiteral("undo"), QStringLiteral("Image entière"), Button::Ghost, Button::Small); resetCrop->setObjectName("screenshot-crop-reset"); resetCrop->setPadding(6);
    auto* cropLine = new QHBoxLayout; cropLine->addWidget(m_cropSize, 1); cropLine->addWidget(resetCrop); crop->addLayout(cropLine);
    layout->addWidget(separator());
    // Simple marks on the picture: a stroke, an arrow, a frame, an ellipse, in a few colours and thicknesses.
    auto* draw = section(layout, QStringLiteral("Dessin"), QStringLiteral("edit"));
    auto* toolRow = new QHBoxLayout; toolRow->setSpacing(6);
    const QStringList toolNames{QStringLiteral("Pinceau"), QStringLiteral("Flèche"), QStringLiteral("Cadre"), QStringLiteral("Ellipse")}, toolIcons{QStringLiteral("edit"), QStringLiteral("arrow"), QStringLiteral("rect"), QStringLiteral("circle")};
    for (int index = 0; index < toolNames.size(); ++index) {
        auto* tile = new Tile(toolNames[index]); tile->setObjectName(QStringLiteral("markup-tool-%1").arg(index)); tile->setIconName(toolIcons[index]); tile->setCentered(true); tile->setRaised(true); tile->setFixedTileHeight(58);
        toolRow->addWidget(tile); m_tools.append(tile); connect(tile, &QPushButton::clicked, this, [this, index] { chooseTool(index); });
    }
    draw->addLayout(toolRow);
    auto* colorRow = new QHBoxLayout; colorRow->setSpacing(8);
    const QList<QColor> colors{QColor(0xFF, 0x3B, 0x30), QColor(0xFF, 0xCC, 0x00), QColor(0x34, 0xC7, 0x59), QColor(0x0A, 0x84, 0xFF), QColor(Qt::white), QColor(0x10, 0x12, 0x16)};
    const QStringList colorNames{QStringLiteral("Rouge"), QStringLiteral("Jaune"), QStringLiteral("Vert"), QStringLiteral("Bleu"), QStringLiteral("Blanc"), QStringLiteral("Noir")};
    for (int index = 0; index < colors.size(); ++index) {
        auto* dot = new ColorDot(colors[index]); dot->setObjectName(QStringLiteral("markup-color-%1").arg(index)); dot->setToolTip(colorNames[index]); dot->setAccessibleName(QStringLiteral("Couleur : ") + colorNames[index]);
        colorRow->addWidget(dot); m_colors.append(dot); connect(dot, &QPushButton::clicked, this, [this, index] { chooseColor(index); });
    }
    colorRow->addStretch(); draw->addLayout(colorRow);
    m_thickness = new Segmented; m_thickness->setObjectName("markup-thickness"); m_thickness->addOption(QStringLiteral("Fin")); m_thickness->addOption(QStringLiteral("Moyen")); m_thickness->addOption(QStringLiteral("Épais")); m_thickness->setCurrentIndex(1); draw->addWidget(m_thickness);
    auto* drawActions = new QHBoxLayout;
    m_undo = new Button(QStringLiteral("undo"), QStringLiteral("Annuler"), Button::Ghost, Button::Small); m_undo->setObjectName("markup-undo"); m_undo->setPadding(6); m_undo->setToolTip(QStringLiteral("Retirer le dernier tracé (Ctrl Z)"));
    m_clearMarks = new Button(QStringLiteral("x"), QStringLiteral("Tout effacer"), Button::Ghost, Button::Small); m_clearMarks->setObjectName("markup-clear"); m_clearMarks->setPadding(6);
    drawActions->addWidget(m_undo); drawActions->addStretch(); drawActions->addWidget(m_clearMarks); draw->addLayout(drawActions);
    draw->addWidget(label(QStringLiteral("Le cadrage et le dessin sont enregistrés dans une nouvelle image, à côté de l'originale, qui ne change pas."), "hint", true)); layout->addWidget(separator());
    auto* origin = section(layout, QStringLiteral("Image d'origine"), QStringLiteral("image"));
    auto* facts = new QGridLayout; facts->setHorizontalSpacing(10); facts->setVerticalSpacing(6); facts->setColumnMinimumWidth(0, 86); facts->setColumnStretch(1, 1);
    const QStringList names{QStringLiteral("Dimensions"), QStringLiteral("Taille"), QStringLiteral("Prise le"), QStringLiteral("Format"), QStringLiteral("Fichier")};
    for (int index = 0; index < names.size(); ++index) {
        facts->addWidget(label(names[index], "small"), index, 0);
        auto* value = new ElideLabel({}, "small", Qt::ElideMiddle); value->setObjectName(QStringLiteral("screenshot-fact-%1").arg(index)); facts->addWidget(value, index, 1); m_facts.append(value);
    }
    origin->addLayout(facts); layout->addStretch();
    // At the bottom of the side, as in the clip editor: what is kept, then what goes.
    m_trash = new Button(QStringLiteral("trash"), QStringLiteral("Mettre à la corbeille"), Button::Danger); m_trash->setObjectName("screenshot-trash"); m_trash->setToolTip(QStringLiteral("Raccourci : Suppr")); m_trash->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed); 
    auto* actions = new QVBoxLayout; actions->setSpacing(8);
    for (auto* action : {m_save, m_copy, m_trash}) { action->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed); actions->addWidget(action); }
    layout->addLayout(actions);
    scroll->setWidget(inspector); workspace->addWidget(scroll); root->addLayout(workspace, 1);

    connect(m_back, &QPushButton::clicked, this, &ScreenshotEditor::backRequested);
    connect(m_mode, &Segmented::currentChanged, this, [this](int index) { m_canvas->setCropTool(index == 0); m_canvas->setResultPreview(index == 2); m_markup->setDrawing(index == 1); m_markup->update(); });
    connect(m_markup, &MarkupLayer::changed, this, [this] { updateActions(); });
    connect(m_thickness, &Segmented::currentChanged, this, [this] { m_markup->setStrokeWidth(strokeWidth()); });
    connect(m_undo, &QPushButton::clicked, this, [this] { m_markup->undo(); });
    connect(m_clearMarks, &QPushButton::clicked, this, [this] { m_markup->clear(); });
    chooseTool(0); chooseColor(0);
    connect(m_canvas, &VideoCanvas::cropSelected, this, [this](QRect crop) { if (m_ratio == 0) m_ratio = 4; setCrop(crop); });
    connect(resetCrop, &QPushButton::clicked, this, [this] { chooseRatio(0); });
    connect(m_rename, &QPushButton::clicked, this, [this] {
        if (m_path.isEmpty()) return;
        m_titleEdit->setText(QFileInfo(m_path).completeBaseName()); m_title->hide(); m_rename->hide(); m_titleEdit->show(); m_titleEdit->selectAll(); m_titleEdit->setFocus();
    });
    connect(m_titleEdit, &QLineEdit::returnPressed, this, [this] { finishRename(true); });
    connect(m_copy, &QPushButton::clicked, this, [this] { if (!m_path.isEmpty()) emit copyRequested({m_path}); });
    connect(m_reveal, &QPushButton::clicked, this, [this] { if (!m_path.isEmpty()) emit revealRequested(m_path); });
    connect(m_trash, &QPushButton::clicked, this, [this] { if (!m_path.isEmpty() && !saving()) emit deleteRequested(m_path, false); });
    connect(m_save, &QPushButton::clicked, this, &ScreenshotEditor::saveCrop);
    connect(openSaved, &QPushButton::clicked, this, [this] { if (!m_savedPath.isEmpty()) emit openRequested(m_savedPath); });
    connect(trashOriginal, &QPushButton::clicked, this, [this] { if (!m_path.isEmpty()) emit deleteRequested(m_path, false); });
    connect(closeBanner, &QPushButton::clicked, m_banner, &QWidget::hide);
    connect(m_saving, &QFutureWatcher<QString>::finished, this, [this] {
        const auto result = m_saving->result(); updateActions();
        if (result.startsWith(QLatin1Char('!'))) { emit failed(QStringLiteral("Le recadrage n'a pas été enregistré"), result.mid(1)); return; }
        m_savedPath = result; m_banner->setTexts(QStringLiteral("Copie enregistrée"), QFileInfo(result).fileName()); m_banner->show();
        // What is drawn now lives in the copy: the original starts clean again.
        m_drafts.remove(m_path); m_markup->setMarks({});
        emit saved(result, m_path);
    });
    // The keys of the editor work wherever the focus is in its window while it is shown: a button clicked
    // never takes them away, and a field being typed in keeps the keys it writes.
    const auto shortcut = [this](const QKeySequence& keys, auto&& action) { auto* result = new QShortcut(keys, this); result->setContext(Qt::WindowShortcut); connect(result, &QShortcut::activated, this, action); };
    shortcut(Qt::Key_F2, [this] { if (m_rename->isVisible()) m_rename->click(); });
    shortcut(QKeySequence::Undo, [this] { m_markup->undo(); });
    shortcut(QKeySequence::Delete, [this] { m_trash->click(); });
    shortcut(QKeySequence::Copy, [this] { m_copy->click(); });
    shortcut(Qt::Key_Escape, [this] { if (m_titleEdit->isVisible()) finishRename(false); else m_back->click(); });
    updateActions();
}
void ScreenshotEditor::openImage(QString path) {
    if (!m_path.isEmpty()) { if (m_markup->marks().isEmpty()) m_drafts.remove(m_path); else m_drafts.insert(m_path, m_markup->marks()); }
    m_path = path; m_savedPath.clear(); m_banner->hide(); finishRename(false); m_markup->setMarks(m_drafts.value(path));
    QImageReader reader(path); reader.setAutoTransform(true); m_size = reader.size();
    // Shown at most at the width of a large screen: a 4K picture is cropped in its own pixels all the same.
    if (m_size.isValid() && m_size.width() > 2560) reader.setScaledSize(m_size.scaled(2560, 2560, Qt::KeepAspectRatio));
    const auto image = reader.read(); m_loaded = !image.isNull(); if (!m_loaded) m_size = {};
    const QFileInfo file(path); m_title->setFullText(file.completeBaseName());
    m_canvas->setFrame(image, m_size); m_ratio = 0; setCrop(QRect(QPoint(), m_size));
    const QLocale french(QLocale::French, QLocale::France);
    const QStringList values{m_loaded ? QStringLiteral("%1 × %2").arg(m_size.width()).arg(m_size.height()) : QStringLiteral("Image illisible"), formatBytes(file.size()), french.toString(file.lastModified(), QStringLiteral("d MMMM yyyy, HH:mm")), file.suffix().toUpper(), file.fileName()};
    for (int index = 0; index < m_facts.size(); ++index) static_cast<ElideLabel*>(m_facts[index])->setFullText(values[index]);
    m_mode->setCurrentIndex(2); m_markup->setStrokeWidth(strokeWidth()); updateActions();
    if (!m_loaded) emit failed(QStringLiteral("Image illisible"), reader.errorString());
}
QList<Mark> ScreenshotEditor::marks() const { return m_markup->marks(); }
void ScreenshotEditor::setMarks(const QList<Mark>& marks) { m_markup->setMarks(marks); updateActions(); }
void ScreenshotEditor::chooseTool(int index) {
    // Picking a tool is starting to draw.
    for (int tool = 0; tool < m_tools.size(); ++tool) m_tools[tool]->setChecked(tool == index);
    m_markup->setTool(Mark::Kind(index)); if (m_loaded && m_mode->currentIndex() != 1) m_mode->setCurrentIndex(1);
}
void ScreenshotEditor::chooseColor(int index) {
    for (int dot = 0; dot < m_colors.size(); ++dot) m_colors[dot]->setChecked(dot == index);
    m_markup->setColor(static_cast<ColorDot*>(m_colors[index])->color());
}
// A line reads the same on any picture: its thickness follows the width of the picture.
double ScreenshotEditor::strokeWidth() const {
    const double base = std::max(2.0, m_size.width() / 640.0), factors[]{1.5, 3, 6};
    return base * factors[std::clamp(m_thickness->currentIndex(), 0, 2)];
}
void ScreenshotEditor::setGame(const QString& game) { m_game = game; m_gameTag->setText(game); m_gameTag->setVisible(!game.isEmpty()); }
void ScreenshotEditor::moveImage(const QString& from, const QString& to) {
    if (from == m_savedPath) m_savedPath = to;
    if (from != m_path) return;
    m_path = to; m_title->setFullText(QFileInfo(to).completeBaseName()); static_cast<ElideLabel*>(m_facts.last())->setFullText(QFileInfo(to).fileName());
}
void ScreenshotEditor::forgetImage(const QString& path) { if (path == m_savedPath) { m_savedPath.clear(); m_banner->hide(); } if (path == m_path) { m_path.clear(); m_loaded = false; updateActions(); } }
void ScreenshotEditor::chooseRatio(int index) {
    if (!m_loaded) return;
    m_ratio = index; m_canvas->setAspect(ratios[index]);
    if (index == 0) { setCrop(QRect(QPoint(), m_size)); return; }
    if (index < 4) {
        int width = m_size.width(), height = qRound(width / ratios[index]); if (height > m_size.height()) { height = m_size.height(); width = qRound(height * ratios[index]); }
        const auto centre = m_crop == QRect(QPoint(), m_size) ? QPoint(m_size.width() / 2, m_size.height() / 2) : m_crop.center();
        setCrop({std::clamp(centre.x() - width / 2, 0, m_size.width() - width), std::clamp(centre.y() - height / 2, 0, m_size.height() - height), width, height});
    } else setCrop(m_crop);
    m_mode->setCurrentIndex(0);
}
void ScreenshotEditor::setCrop(QRect crop) {
    m_crop = crop.intersected(QRect(QPoint(), m_size)); if (m_crop.isEmpty()) m_crop = QRect(QPoint(), m_size);
    const bool whole = m_crop == QRect(QPoint(), m_size);
    if (whole && m_ratio != 4) m_ratio = 0;
    for (int tile = 0; tile < m_ratios.size(); ++tile) m_ratios[tile]->setChecked(tile == m_ratio);
    m_canvas->setAspect(ratios[m_ratio]); m_canvas->setFreeFrame(m_ratio == 4); m_canvas->setCrop(m_crop); m_markup->update();
    m_cropSize->setText(m_size.isValid() ? QStringLiteral("%1 × %2 px").arg(m_crop.width()).arg(m_crop.height()) : QString());
    updateActions();
}
void ScreenshotEditor::saveCrop() {
    const auto marks = m_markup->marks();
    if (!m_loaded || saving() || (m_crop == QRect(QPoint(), m_size) && marks.isEmpty())) return;
    const auto source = m_path, target = copyPath(m_path, marks.isEmpty() ? QStringLiteral("recadré") : QStringLiteral("annoté")); const auto crop = m_crop;
    // Read again in full and written away from the interface: a 4K picture takes a moment to compress.
    m_saving->setFuture(QtConcurrent::run([source, target, crop, marks] {
        QImageReader reader(source); reader.setAutoTransform(true); auto image = reader.read();
        if (image.isNull()) return QStringLiteral("!") + reader.errorString();
        // The marks were drawn in the pixels of this very picture: they land where they were seen.
        if (!marks.isEmpty()) { image = image.convertToFormat(QImage::Format_RGB32); QPainter painter(&image); paintMarks(painter, marks); }
        return image.copy(crop).save(target, "PNG") ? target : QStringLiteral("!Impossible d'écrire ") + QDir::toNativeSeparators(target);
    }));
    updateActions();
}
void ScreenshotEditor::finishRename(bool commit) {
    if (!m_titleEdit->isVisible()) return;
    const auto name = m_titleEdit->text().trimmed(); m_titleEdit->hide(); m_title->show(); m_rename->show();
    if (commit && !name.isEmpty() && name != QFileInfo(m_path).completeBaseName()) emit renameRequested(m_path, name);
}
bool ScreenshotEditor::eventFilter(QObject* watched, QEvent* event) {
    if (watched == m_titleEdit && event->type() == QEvent::KeyPress && static_cast<QKeyEvent*>(event)->key() == Qt::Key_Escape) { finishRename(false); return true; }
    if (watched == m_titleEdit && event->type() == QEvent::FocusOut) finishRename(true);
    return QWidget::eventFilter(watched, event);
}
void ScreenshotEditor::updateActions() {
    const bool ready = m_loaded && !saving(), drawn = !m_markup->marks().isEmpty(), changed = ready && (m_crop != QRect(QPoint(), m_size) || drawn);
    m_save->setEnabled(changed); m_save->setText(saving() ? QStringLiteral("Enregistrement…") : QStringLiteral("Enregistrer une copie"));
    m_save->setToolTip(changed ? QStringLiteral("Enregistre l'image cadrée et dessinée comme une nouvelle image, à côté de l'originale") : QStringLiteral("Cadrez ou dessinez sur l'image pour en enregistrer une copie"));
    m_undo->setEnabled(ready && drawn); m_clearMarks->setEnabled(ready && drawn); for (auto* tool : m_tools) tool->setEnabled(ready);
    for (auto* button : {m_copy, m_reveal, m_trash, m_rename}) button->setEnabled(!m_path.isEmpty() && !saving());
    m_canvas->setEnabled(ready); for (auto* tile : m_ratios) tile->setEnabled(ready);
}
}
