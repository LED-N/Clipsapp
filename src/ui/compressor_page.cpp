#include "compressor_page.h"
#include "compress_batch.h"
#include "thumbnail_cache.h"
#include "widgets.h"
#include <QFileInfo>
#include <QHBoxLayout>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QScrollArea>
#include <QVBoxLayout>
#include <algorithm>

namespace clipsapp {
namespace {
struct Preset { qint64 bytes; const char* name; };
const Preset presets[]{{20000000, "sans Nitro"}, {50000000, "Nitro Basic"}, {100000000, "serveur niveau 3"}, {1000000000, "Nitro"}};
class PartialBox final : public QWidget {
public:
    PartialBox() { setFixedSize(18, 18); }
protected:
    void paintEvent(QPaintEvent*) override { QPainter painter(this); Toggle::paintBox(painter, rect(), true, true); }
};
class Thumb final : public QWidget {
public:
    Thumb(ThumbnailCache* cache, const ClipEntry& clip) : m_cache(cache), m_path(clip.path), m_key(ThumbnailCache::keyFor(clip.path, clip.size, clip.modified)) { setFixedSize(64, 36); connect(cache, &ThumbnailCache::ready, this, qOverload<>(&QWidget::update)); }
protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this); painter.setRenderHint(QPainter::Antialiasing); QPainterPath rounded; rounded.addRoundedRect(rect(), 4, 4); painter.setClipPath(rounded); painter.fillRect(rect(), tk::raised);
        const auto* preview = m_cache->preview(m_key); const auto* meta = m_cache->meta(m_key);
        if (!preview) m_cache->request(m_key, m_path);
        if (preview && !preview->image.isNull()) { painter.setRenderHint(QPainter::SmoothPixmapTransform); const auto shown = QSizeF(preview->image.size()).scaled(QSizeF(size()), Qt::KeepAspectRatio); painter.drawImage(QRectF((width() - shown.width()) / 2, (height() - shown.height()) / 2, shown.width(), shown.height()), preview->image); }
        else paintIcon(painter, QStringLiteral("film"), QRectF(width() / 2.0 - 8, height() / 2.0 - 8, 16, 16), tk::text3);
    }
private:
    ThumbnailCache* m_cache; QString m_path, m_key;
};
}
QString CompressorPage::limitName(qint64 bytes) { for (const auto& preset : presets) if (preset.bytes == bytes) return QString::fromUtf8(preset.name); return QStringLiteral("personnalisée"); }
CompressorPage::CompressorPage(ThumbnailCache* cache, CompressBatch* batch, QWidget* parent) : QWidget(parent), m_cache(cache), m_batch(batch) {
    setObjectName("compressor-page");
    auto* outer = new QVBoxLayout(this); outer->setContentsMargins(0, 0, 0, 0); outer->setSpacing(0);
    auto* content = new QWidget; auto* layout = new QVBoxLayout(content); m_content = layout; layout->setContentsMargins(24, 20, 24, 24); layout->setSpacing(16);
    auto* scroll = new QScrollArea; scroll->setWidgetResizable(true); scroll->setFrameShape(QFrame::NoFrame); scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff); scroll->setWidget(content); outer->addWidget(scroll);
    auto* titles = new QVBoxLayout; titles->setSpacing(2); titles->addWidget(label(QStringLiteral("Compresseur"), "title"));
    titles->addWidget(label(QStringLiteral("Allégez vos clips : gagnez de la place sans perte visible, ou visez la taille d'envoi de Discord."), "t2", true)); layout->addLayout(titles);

    // A library compressing behind the window says where it is.
    auto* running = card(); m_running = running; running->setObjectName("compressor-running"); auto* runningLayout = new QHBoxLayout(running); runningLayout->setContentsMargins(14, 12, 14, 12); runningLayout->setSpacing(14);
    runningLayout->addWidget(new IconBox(QStringLiteral("compress"), tk::accent, tk::accentSoft));
    auto* runningTexts = new QVBoxLayout; runningTexts->setSpacing(6); auto* runningLine = new QHBoxLayout; m_runningTitle = label({}, "strong"); runningLine->addWidget(m_runningTitle, 1); m_runningDetail = monoLabel({}, 12, tk::text2); runningLine->addWidget(m_runningDetail);
    runningTexts->addLayout(runningLine); m_runningBar = new Bar; runningTexts->addWidget(m_runningBar); runningLayout->addLayout(runningTexts, 1);
    auto* show = new Button(QStringLiteral("Voir"), Button::Secondary, Button::Small); show->setObjectName("compressor-running-show"); runningLayout->addWidget(show);
    auto* stop = new Button(QStringLiteral("Arrêter"), Button::Ghost, Button::Small); stop->setObjectName("compressor-running-stop"); stop->setTextColor(tk::danger); runningLayout->addWidget(stop);
    layout->addWidget(running); running->hide();

    auto* goals = new QHBoxLayout; goals->setSpacing(12);
    const auto goalCard = [](const QString& iconName, const QColor& color, const QColor& soft, const QString& title, const QString& text, QVBoxLayout*& body) {
        auto* frame = card(); auto* layout = new QVBoxLayout(frame); layout->setContentsMargins(18, 16, 18, 16); layout->setSpacing(10);
        auto* head = new QHBoxLayout; head->setSpacing(12); head->addWidget(new IconBox(iconName, color, soft)); head->addWidget(label(title, "h2"), 1); layout->addLayout(head);
        layout->addWidget(label(text, "t2", true)); body = layout; return frame;
    };
    QVBoxLayout* body;
    auto* space = goalCard(QStringLiteral("compress"), tk::accent, tk::accentSoft, QStringLiteral("Gagner de la place"),
        QStringLiteral("La vidéo est réencodée en HEVC ou en AV1, à qualité constante ; le son et ses pistes restent tels quels. Pour un clip, vous comparez avant et après."), body);
    m_spaceFigures = monoLabel({}, 12, tk::text2); body->addWidget(m_spaceFigures); body->addStretch();
    m_library = new Button(QStringLiteral("compress"), QStringLiteral("Compresser toute la bibliothèque…"), Button::Primary); m_library->setObjectName("compress-library"); body->addWidget(m_library, 0, Qt::AlignLeft);
    body->addWidget(label(QStringLiteral("Ou sélectionnez des clips dans la bibliothèque, puis « Compresser »."), "small3", true));
    auto* discord = goalCard(QStringLiteral("share"), tk::info, tk::infoSoft, QStringLiteral("Envoyer sur Discord"),
        QStringLiteral("Une copie à la taille de votre limite, à glisser directement dans une conversation. Elle est temporaire : rien ne s'accumule sur le disque."), body);
    m_discordFigures = monoLabel({}, 12, tk::text2); body->addWidget(m_discordFigures); body->addStretch();
    m_limitButton = new Button(QString()); m_limitButton->setObjectName("compress-limit"); m_limitButton->setTrailingIcon(QStringLiteral("down")); m_limitButton->setSpread(true); body->addWidget(m_limitButton, 0, Qt::AlignLeft);
    body->addWidget(label(QStringLiteral("La bibliothèque marque chaque clip « Prêt pour Discord » ou « Plus de … » selon cette limite."), "small3", true));
    goals->addWidget(space, 1); goals->addWidget(discord, 1); layout->addLayout(goals);

    m_oldCopies = new Banner(Banner::Info, QStringLiteral("folder")); m_oldCopies->setObjectName("compressor-old-copies"); m_oldCopies->hide(); layout->addWidget(m_oldCopies);
    auto* openOld = new Button(QStringLiteral("Ouvrir le dossier"), Button::Secondary, Button::Small); auto* deleteOld = new Button(QStringLiteral("trash"), QStringLiteral("Supprimer…"), Button::Danger, Button::Small); deleteOld->setObjectName("compressor-old-delete");
    m_oldCopies->actions()->addWidget(openOld); m_oldCopies->actions()->addWidget(deleteOld);

    m_heaviestHost = new QWidget; auto* heaviestLayout = new QVBoxLayout(m_heaviestHost); heaviestLayout->setContentsMargins(0, 4, 0, 0); heaviestLayout->setSpacing(8);
    // The heaviest clips, ticked like those of the library to compress several at once.
    auto* heaviestHead = new QHBoxLayout; heaviestHead->addWidget(caption(QStringLiteral("Les plus lourds")), 1);
    m_checkAll = new Toggle(QStringLiteral("Tout cocher"), Toggle::Box); m_checkAll->setObjectName("heaviest-check-all"); m_checkAll->setFont(uiFont(12)); heaviestHead->addWidget(m_checkAll); heaviestLayout->addLayout(heaviestHead);
    auto* well = card("well"); well->setObjectName("compressor-heaviest"); m_heaviest = new QVBoxLayout(well); m_heaviest->setContentsMargins(1, 4, 1, 4); m_heaviest->setSpacing(0); heaviestLayout->addWidget(well);
    m_compressedNote = label({}, "small3", true); m_compressedNote->setObjectName("heaviest-compressed-note"); m_compressedNote->hide(); heaviestLayout->addWidget(m_compressedNote);
    // The bar of what is ticked floats at the bottom of the page, over the list, as in the library: its actions stay in reach wherever the list is scrolled.
    auto* selection = card("bar"); m_selection = selection; selection->setParent(this); selection->setObjectName("heaviest-selection"); selection->setFixedHeight(56); selection->hide(); auto* bar = new QHBoxLayout(selection); bar->setContentsMargins(16, 0, 10, 0); bar->setSpacing(8); bar->addWidget(new PartialBox);
    auto* barTexts = new QVBoxLayout; barTexts->setSpacing(0); barTexts->setContentsMargins(4, 0, 0, 0); barTexts->addStretch(); m_selectionTitle = label({}, "bold"); m_selectionTitle->setObjectName("heaviest-selection-count"); barTexts->addWidget(m_selectionTitle); m_selectionDetail = label({}, "small"); barTexts->addWidget(m_selectionDetail); barTexts->addStretch(); bar->addLayout(barTexts, 1);
    auto* roomSelected = new Button(QStringLiteral("compress"), QStringLiteral("Gagner de la place")); roomSelected->setObjectName("heaviest-compress"); bar->addWidget(roomSelected);
    auto* shareSelected = new Button(QStringLiteral("share"), QStringLiteral("Compresser pour Discord")); shareSelected->setObjectName("heaviest-discord"); bar->addWidget(shareSelected);
    auto* divider = separator(true); divider->setFixedHeight(28); bar->addSpacing(4); bar->addWidget(divider); bar->addSpacing(4);
    auto* uncheck = new Button(QStringLiteral("x"), QString(), Button::Ghost); uncheck->setToolTip(QStringLiteral("Tout décocher")); uncheck->setAccessibleName(uncheck->toolTip()); bar->addWidget(uncheck);
    m_selectionActions = {roomSelected, shareSelected};
    connect(roomSelected, &QPushButton::clicked, this, [this] { emit compressRequested(checkedPaths(), Space); });
    connect(shareSelected, &QPushButton::clicked, this, [this] { emit compressRequested(checkedPaths(), Discord); });
    connect(uncheck, &QPushButton::clicked, this, [this] { setChecked({}); });
    connect(m_checkAll, &QCheckBox::clicked, this, [this](bool all) { QSet<QString> paths; if (all) for (const auto& shown : m_shown) paths.insert(shown); setChecked(paths); });
    layout->addWidget(m_heaviestHost);
    m_empty = label(QStringLiteral("Aucun clip dans la bibliothèque pour l'instant : les clips sauvegardés apparaîtront ici, les plus lourds en premier."), "t2", true); layout->addWidget(m_empty);
    layout->addStretch();

    connect(m_library, &QPushButton::clicked, this, &CompressorPage::libraryRequested);
    connect(show, &QPushButton::clicked, this, &CompressorPage::batchRequested);
    connect(stop, &QPushButton::clicked, m_batch, &CompressBatch::cancel);
    connect(openOld, &QPushButton::clicked, this, &CompressorPage::openOldCopiesRequested);
    connect(deleteOld, &QPushButton::clicked, this, &CompressorPage::deleteOldCopiesRequested);
    connect(m_limitButton, &QPushButton::clicked, this, [this] {
        auto* menu = popupMenu(this); menu->setAttribute(Qt::WA_DeleteOnClose); menu->setMinimumWidth(m_limitButton->width());
        for (const auto& preset : presets) {
            const auto bytes = preset.bytes; auto* action = menu->addAction(bytes == m_limit ? icon(QStringLiteral("check"), tk::accent) : QIcon(iconPixmap(QString(), tk::text, 16)), formatLimit(bytes) + QStringLiteral(" · ") + QString::fromUtf8(preset.name));
            connect(action, &QAction::triggered, this, [this, bytes] { emit limitChosen(bytes); });
        }
        menu->popup(m_limitButton->mapToGlobal(QPoint(0, m_limitButton->height() + 4)));
    });
    connect(m_batch, &CompressBatch::changed, this, &CompressorPage::refreshBatch);
    connect(m_batch, &CompressBatch::runningChanged, this, &CompressorPage::refreshBatch);
    // A clip already compressed by Clipsapp is not offered again: a second pass would lose a little more for little room.
    connect(m_cache, &ThumbnailCache::marksReady, this, [this] { refreshFigures(); refreshHeaviest(); });
    setClips({}, m_limit); refreshBatch();
}
void CompressorPage::setBusy(bool busy) { m_busy = busy; m_library->setEnabled(!busy && !m_clips.isEmpty()); m_heaviestSignature.clear(); refreshHeaviest(); }
void CompressorPage::setClips(const QList<ClipEntry>& clips, qint64 limitBytes) {
    m_clips = clips; m_limit = limitBytes; int over = 0; for (const auto& clip : clips) over += clip.size > limitBytes;
    QList<QPair<QString, QString>> keys; for (const auto& clip : clips) keys.append({ThumbnailCache::keyFor(clip.path, clip.size, clip.modified), clip.path});
    m_cache->requestMarks(keys); refreshFigures();
    m_discordFigures->setText(over ? QStringLiteral("%1 clip%2 dépasse%3 %4").arg(over).arg(over > 1 ? "s" : "", over > 1 ? "nt" : "").arg(formatLimit(limitBytes)) : QStringLiteral("Tous les clips tiennent dans %1").arg(formatLimit(limitBytes)));
    m_limitButton->setText(QStringLiteral("Limite : %1 · %2").arg(formatLimit(limitBytes), limitName(limitBytes)));
    m_library->setEnabled(!m_busy && !clips.isEmpty());
    refreshHeaviest();
}
bool CompressorPage::compressed(const ClipEntry& clip) const { return m_cache->compressed(ThumbnailCache::keyFor(clip.path, clip.size, clip.modified)).value_or(false); }
void CompressorPage::refreshFigures() {
    qint64 total = 0; int done = 0; for (const auto& clip : std::as_const(m_clips)) { total += clip.size; done += compressed(clip); }
    const int count = int(m_clips.size());
    m_spaceFigures->setText(!count ? QStringLiteral("Bibliothèque vide") : done ? QStringLiteral("%1 clip%2 · %3 · %4 déjà compressé%5").arg(count).arg(count > 1 ? "s" : "").arg(formatBytes(total)).arg(done).arg(done > 1 ? "s" : "")
                                    : QStringLiteral("%1 clip%2 · %3 dans la bibliothèque").arg(count).arg(count > 1 ? "s" : "").arg(formatBytes(total)));
}
void CompressorPage::setOldCopies(int count, qint64 bytes) {
    m_oldCopies->setTexts(count > 1 ? QStringLiteral("%1 anciennes copies Discord · %2").arg(count).arg(formatBytes(bytes)) : QStringLiteral("Une ancienne copie Discord · %1").arg(formatBytes(bytes)),
        QStringLiteral("Elles datent d'avant le Compresseur, dans le dossier « Partage Discord ». Les copies pour Discord sont maintenant temporaires : celles-ci peuvent partir."));
    m_oldCopies->setVisible(count > 0);
}
void CompressorPage::refreshBatch() {
    const bool running = m_batch->running() && !m_batch->options().review;
    m_running->setVisible(running); if (!running) return;
    const auto& jobs = m_batch->jobs(); int done = 0; for (const auto& job : jobs) done += job.state != CompressJob::Pending && job.state != CompressJob::Running;
    m_runningTitle->setText(QStringLiteral("Compression de %1 clips · %2 %").arg(jobs.size()).arg(m_batch->percent()));
    m_runningDetail->setText(QStringLiteral("%1 sur %2 · %3 gagnés").arg(std::min<int>(done + 1, int(jobs.size()))).arg(jobs.size()).arg(formatBytes(m_batch->savedBytes())));
    m_runningBar->setValue(m_batch->percent() / 100.0);
}
void CompressorPage::refreshHeaviest() {
    QList<ClipEntry> sorted; int hidden = 0; for (const auto& clip : std::as_const(m_clips)) { if (compressed(clip)) ++hidden; else sorted.append(clip); }
    std::sort(sorted.begin(), sorted.end(), [](const ClipEntry& left, const ClipEntry& right) { return left.size > right.size; }); sorted = sorted.mid(0, 12);
    m_compressedNote->setText(hidden > 1 ? QStringLiteral("%1 clips déjà compressés par Clipsapp ne sont pas proposés ici.").arg(hidden) : QStringLiteral("Un clip déjà compressé par Clipsapp n'est pas proposé ici."));
    m_compressedNote->setVisible(hidden > 0 && !sorted.isEmpty());
    m_empty->setText(m_clips.isEmpty() ? QStringLiteral("Aucun clip dans la bibliothèque pour l'instant : les clips sauvegardés apparaîtront ici, les plus lourds en premier.") : QStringLiteral("Tous vos clips ont déjà été compressés par Clipsapp : il n'y a plus rien à proposer ici."));
    m_empty->setVisible(sorted.isEmpty()); m_heaviestHost->setVisible(!sorted.isEmpty());
    QString signature = QString::number(m_limit) + (m_busy ? "b" : ""); for (const auto& clip : sorted) signature += clip.path + QString::number(clip.size);
    if (signature == m_heaviestSignature) return;
    m_heaviestSignature = signature;
    while (auto* child = m_heaviest->takeAt(0)) { if (child->widget()) { child->widget()->hide(); child->widget()->deleteLater(); } delete child; }
    m_shown.clear(); m_checks.clear(); for (const auto& clip : sorted) m_shown.append(clip.path);
    // What is no longer among them is no longer ticked.
    for (auto it = m_checked.begin(); it != m_checked.end();) if (m_shown.contains(*it)) ++it; else it = m_checked.erase(it);
    for (int index = 0; index < sorted.size(); ++index) {
        const auto& clip = sorted[index];
        if (index) { auto* line = separator(); setThemedStyleSheet(line, QStringLiteral("background: {lineSoft};")); m_heaviest->addWidget(line); }
        auto* row = new QWidget; row->setFixedHeight(56); row->setProperty("path", clip.path); row->setCursor(Qt::PointingHandCursor); row->installEventFilter(this);
        auto* line = new QHBoxLayout(row); line->setContentsMargins(12, 0, 12, 0); line->setSpacing(12);
        auto* check = new Toggle({}, Toggle::Box); check->setObjectName(QStringLiteral("heaviest-check-%1").arg(index)); check->setChecked(m_checked.contains(clip.path)); line->addWidget(check); m_checks.append(check);
        connect(check, &QCheckBox::clicked, this, [this, path = clip.path](bool on) { auto paths = m_checked; if (on) paths.insert(path); else paths.remove(path); setChecked(paths); });
        line->addWidget(new Thumb(m_cache, clip));
        auto* texts = new QVBoxLayout; texts->setSpacing(0); texts->addStretch(); texts->addWidget(new ElideLabel(clip.name, "strong", Qt::ElideMiddle)); texts->addWidget(new ElideLabel(clip.game, "small")); texts->addStretch(); line->addLayout(texts, 1);
        line->addWidget(monoLabel(formatBytes(clip.size), 12, tk::text));
        const bool fits = clip.size <= m_limit; auto* fit = new Tag(fits ? QStringLiteral("Prêt pour Discord") : QStringLiteral("Plus de %1").arg(formatLimit(m_limit)), fits ? Tag::Ok : Tag::Neutral); line->addWidget(fit);
        auto* room = new Button(QStringLiteral("compress"), QStringLiteral("Gagner de la place"), Button::Secondary, Button::Small); room->setObjectName(QStringLiteral("compress-space-%1").arg(index)); room->setEnabled(!m_busy); line->addWidget(room);
        auto* share = new Button(QStringLiteral("share"), QString(), Button::Ghost, Button::Small); share->setToolTip(QStringLiteral("Compresser pour Discord")); share->setAccessibleName(share->toolTip()); share->setEnabled(!m_busy); line->addWidget(share);
        const auto path = clip.path;
        connect(room, &QPushButton::clicked, this, [this, path] { emit compressRequested({path}, Space); });
        connect(share, &QPushButton::clicked, this, [this, path] { emit compressRequested({path}, Discord); });
        m_heaviest->addWidget(row);
    }
    updateSelection();
}
QStringList CompressorPage::checkedPaths() const { QStringList paths; for (const auto& path : m_shown) if (m_checked.contains(path)) paths.append(path); return paths; }
void CompressorPage::setChecked(const QSet<QString>& paths) {
    m_checked = paths; for (int index = 0; index < m_checks.size() && index < m_shown.size(); ++index) m_checks[index]->setChecked(m_checked.contains(m_shown[index]));
    updateSelection();
}
void CompressorPage::updateSelection() {
    const auto paths = checkedPaths(); qint64 bytes = 0; for (const auto& clip : m_clips) if (paths.contains(clip.path)) bytes += clip.size;
    m_selectionTitle->setText(paths.size() > 1 ? QStringLiteral("%1 clips cochés").arg(paths.size()) : QStringLiteral("1 clip coché"));
    m_selectionDetail->setText(QStringLiteral("%1 à compresser").arg(formatBytes(bytes)));
    for (auto* action : m_selectionActions) action->setEnabled(!m_busy);
    if (const bool visible = !paths.isEmpty(); visible == m_selection->isHidden()) {
        m_selection->setVisible(visible); m_content->setContentsMargins(24, 20, 24, visible ? 24 + 72 : 24);
        if (visible) { placeSelection(); m_selection->raise(); }
        emit selectionBarVisible(visible);
    }
    m_checkAll->setChecked(!m_shown.isEmpty() && paths.size() == m_shown.size()); m_checkAll->setVisible(!m_shown.isEmpty());
}
// A click on a row, beside its buttons, ticks it as its box does.
bool CompressorPage::eventFilter(QObject* watched, QEvent* event) {
    if (event->type() == QEvent::MouseButtonRelease && static_cast<QMouseEvent*>(event)->button() == Qt::LeftButton) {
        if (const auto path = watched->property("path").toString(); !path.isEmpty()) { auto paths = m_checked; if (!paths.remove(path)) paths.insert(path); setChecked(paths); return true; }
    }
    return QWidget::eventFilter(watched, event);
}
bool CompressorPage::selectionShown() const { return !m_selection->isHidden(); }
void CompressorPage::placeSelection() { m_selection->setGeometry(20, height() - 16 - 56, width() - 40, 56); }
void CompressorPage::resizeEvent(QResizeEvent* event) { QWidget::resizeEvent(event); placeSelection(); }
}
