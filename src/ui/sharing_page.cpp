#include "sharing_page.h"
#include "clip_list.h"
#include "discord_batch.h"
#include "thumbnail_cache.h"
#include "widgets.h"
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLocale>
#include <QMenu>
#include <QScrollBar>
#include <QShortcut>
#include <QStackedWidget>
#include <QVBoxLayout>
#include <algorithm>

namespace clipsapp {
namespace {
struct Preset { qint64 bytes; const char* name; };
const Preset presets[]{{20000000, "sans Nitro"}, {50000000, "Nitro Basic"}, {100000000, "serveur niveau 3"}, {1000000000, "Nitro"}};
QString firstLine(const QString& text) { return text.section('\n', 0, 0).trimmed(); }
}
QString SharingPage::limitName(qint64 bytes) { for (const auto& preset : presets) if (preset.bytes == bytes) return QString::fromUtf8(preset.name); return QStringLiteral("personnalisée"); }
SharingPage::SharingPage(ThumbnailCache* cache, DiscordBatch* batch, QWidget* parent) : QWidget(parent), m_cache(cache), m_batch(batch) {
    auto* layout = new QVBoxLayout(this); layout->setContentsMargins(0, 0, 0, 0); layout->setSpacing(0);
    auto* head = new QHBoxLayout; head->setContentsMargins(24, 20, 24, 12); head->setSpacing(10);
    auto* titles = new QVBoxLayout; titles->setSpacing(2); titles->addWidget(label(QStringLiteral("Partage Discord"), "title"));
    titles->addWidget(label(QStringLiteral("Glissez une copie dans une conversation Discord, ou copiez-la puis collez-la avec Ctrl V."), "t2", true)); head->addLayout(titles, 1);
    auto* folder = new Button(QStringLiteral("folder"), QStringLiteral("Ouvrir le dossier")); head->addWidget(folder, 0, Qt::AlignTop);
    m_limitButton = new Button(QString()); m_limitButton->setObjectName("share-limit"); m_limitButton->setTrailingIcon(QStringLiteral("down")); m_limitButton->setSpread(true); head->addWidget(m_limitButton, 0, Qt::AlignTop);
    layout->addLayout(head);

    m_progress = new QWidget; auto* progress = new QVBoxLayout(m_progress); progress->setContentsMargins(24, 0, 24, 14); progress->setSpacing(8); progress->addWidget(caption(QStringLiteral("En préparation")));
    auto* job = card(); auto* jobLayout = new QHBoxLayout(job); jobLayout->setContentsMargins(14, 12, 14, 12); jobLayout->setSpacing(14);
    jobLayout->addWidget(new IconBox(QStringLiteral("share"), tk::info, tk::infoSoft));
    auto* jobTexts = new QVBoxLayout; jobTexts->setSpacing(6); auto* jobLine = new QHBoxLayout; jobLine->setSpacing(12);
    m_jobTitle = label({}); m_jobTitle->setTextFormat(Qt::RichText); m_jobTitle->setObjectName("share-job-title"); jobLine->addWidget(m_jobTitle, 1); m_jobFigures = monoLabel({}, 12, tk::text2); jobLine->addWidget(m_jobFigures);
    jobTexts->addLayout(jobLine); m_jobBar = new Bar; m_jobBar->setColors(tk::info); jobTexts->addWidget(m_jobBar); jobLayout->addLayout(jobTexts, 1);
    m_readyTag = new Tag({}, Tag::Ok); m_failedTag = new Tag({}, Tag::Err); jobLayout->addWidget(m_readyTag); jobLayout->addWidget(m_failedTag);
    auto* cancel = new Button(QStringLiteral("Annuler le lot"), Button::Ghost, Button::Small); cancel->setObjectName("share-cancel-batch"); jobLayout->addWidget(cancel); progress->addWidget(job); layout->addWidget(m_progress);

    m_listHost = new QWidget; auto* host = new QVBoxLayout(m_listHost); host->setContentsMargins(14, 0, 4, 0); host->setSpacing(0);
    m_readyCaption = caption(QStringLiteral("Prêtes à envoyer")); m_readyCaption->setContentsMargins(10, 0, 0, 6); host->addWidget(m_readyCaption);
    m_stack = new QStackedWidget; host->addWidget(m_stack, 1);
    m_list = new ClipListView; m_list->setObjectName("discord-library"); m_list->setRowDelegate(new ShareRowDelegate(m_list, cache)); m_stack->addWidget(m_list);
    auto* empty = new QWidget; auto* emptyLayout = new QVBoxLayout(empty); emptyLayout->setSpacing(8); emptyLayout->addStretch();
    auto* emptyTitle = label(QStringLiteral("Aucune copie pour l'instant")); emptyTitle->setFont(uiFont(20, QFont::Bold)); emptyTitle->setAlignment(Qt::AlignCenter); emptyLayout->addWidget(emptyTitle);
    auto* emptyText = label(QStringLiteral("Depuis vos clips, choisissez « Préparer pour Discord ». La copie compressée arrive ici, prête à glisser dans une conversation."), "t2", true);
    emptyText->setFont(uiFont(14)); emptyText->setAlignment(Qt::AlignCenter); emptyText->setFixedWidth(480); emptyLayout->addLayout(centered(emptyText)); emptyLayout->addStretch(); m_stack->addWidget(empty);
    m_failed = new QWidget; auto* failed = new QVBoxLayout(m_failed); failed->setContentsMargins(0, 12, 10, 8); failed->setSpacing(2);
    auto* failedCaption = caption(QStringLiteral("À reprendre")); failedCaption->setContentsMargins(10, 0, 0, 4); failed->addWidget(failedCaption);
    m_failedRows = new QVBoxLayout; m_failedRows->setSpacing(2); failed->addLayout(m_failedRows); host->addWidget(m_failed); m_failed->hide(); layout->addWidget(m_listHost, 1);

    m_bar = card("bar"); m_bar->setParent(this); m_bar->setFixedHeight(56); m_bar->hide();
    auto* bar = new QHBoxLayout(m_bar); bar->setContentsMargins(16, 0, 10, 0); bar->setSpacing(8);
    auto* barTexts = new QVBoxLayout; barTexts->setSpacing(0); m_barTitle = label({}, "bold"); m_barDetail = label({}, "small");
    auto* dragLine = new QHBoxLayout; dragLine->setSpacing(4); dragLine->addWidget(new IconLabel(QStringLiteral("grip"), tk::text2, 14)); dragLine->addWidget(m_barDetail, 1);
    barTexts->addStretch(); barTexts->addWidget(m_barTitle); barTexts->addLayout(dragLine); barTexts->addStretch(); bar->addLayout(barTexts); bar->addStretch();
    m_copy = new Button(QStringLiteral("copy"), QStringLiteral("Copier les fichiers"), Button::Primary); m_copy->setObjectName("copy-discord-files"); bar->addWidget(m_copy);
    m_reveal = new Button(QStringLiteral("folder"), QStringLiteral("Afficher")); bar->addWidget(m_reveal);
    m_trash = new Button(QStringLiteral("trash"), QStringLiteral("Supprimer"), Button::Danger); m_trash->setObjectName("delete-discord-files"); bar->addWidget(m_trash);
    auto* divider = separator(true); divider->setFixedHeight(28); bar->addSpacing(4); bar->addWidget(divider); bar->addSpacing(4);
    auto* clear = new Button(QStringLiteral("x"), QString(), Button::Ghost); clear->setToolTip(QStringLiteral("Tout désélectionner (Échap)")); clear->setAccessibleName(QStringLiteral("Tout désélectionner")); bar->addWidget(clear);

    connect(folder, &QPushButton::clicked, this, &SharingPage::openFolderRequested);
    connect(cancel, &QPushButton::clicked, m_batch, &DiscordBatch::cancel);
    connect(m_limitButton, &QPushButton::clicked, this, [this] {
        auto* menu = popupMenu(this); menu->setAttribute(Qt::WA_DeleteOnClose); menu->setMinimumWidth(m_limitButton->width());
        for (const auto& preset : presets) {
            const auto bytes = preset.bytes; auto* action = menu->addAction(bytes == m_limit ? icon(QStringLiteral("check"), tk::accent) : QIcon(iconPixmap(QString(), tk::text, 16)), formatLimit(bytes) + QStringLiteral(" · ") + QString::fromUtf8(preset.name));
            connect(action, &QAction::triggered, this, [this, bytes] { emit limitChosen(bytes); });
        }
        menu->popup(m_limitButton->mapToGlobal(QPoint(0, m_limitButton->height() + 4)));
    });
    connect(m_batch, &DiscordBatch::changed, this, &SharingPage::refreshBatch);
    connect(m_batch, &DiscordBatch::runningChanged, this, &SharingPage::refreshBatch);
    connect(m_list->selectionModel(), &QItemSelectionModel::selectionChanged, this, [this] { updateSelection(); });
    connect(m_list, &ClipListView::menuRequested, this, &SharingPage::showMenu);
    connect(m_list, &ClipListView::actionRequested, this, [this](int hit, const QString& path, const QPoint& position) {
        if (hit == ClipRowDelegate::Copy) emit copyRequested({path});
        else if (hit == ClipRowDelegate::More) { if (!selectedPaths().contains(path)) m_list->selectPaths({path}); showMenu(path, position); }
    });
    connect(clear, &QPushButton::clicked, m_list, &QAbstractItemView::clearSelection);
    connect(m_copy, &QPushButton::clicked, this, [this] { emit copyRequested(selectedPaths()); });
    connect(m_reveal, &QPushButton::clicked, this, [this] { const auto paths = selectedPaths(); if (!paths.isEmpty()) emit revealRequested(paths.first()); });
    connect(m_trash, &QPushButton::clicked, this, [this] { emit trashRequested(selectedPaths()); });
    connect(cache, &ThumbnailCache::ready, m_list->viewport(), qOverload<>(&QWidget::update));
    const auto shortcut = [this](const QKeySequence& keys, auto&& action) { auto* result = new QShortcut(keys, m_list); result->setContext(Qt::WidgetWithChildrenShortcut); connect(result, &QShortcut::activated, this, action); };
    shortcut(QKeySequence::Delete, [this] { const auto paths = selectedPaths(); if (!paths.isEmpty() && m_trash->isEnabled()) emit trashRequested(paths); });
    shortcut(QKeySequence::Copy, [this] { const auto paths = selectedPaths(); if (!paths.isEmpty()) emit copyRequested(paths); });
    shortcut(Qt::Key_Escape, [this] { m_list->clearSelection(); });
    setCopies({}, m_limit); refreshBatch();
}
QStringList SharingPage::selectedPaths() const { return m_list->selectedPaths(); }
void SharingPage::setBusy(bool trashing) { m_trashing = trashing; updateSelection(); }
void SharingPage::setCopies(const QList<ShareCopy>& copies, qint64 limitBytes) {
    m_copies = copies; m_limit = limitBytes; const auto selected = selectedPaths(); const int scroll = m_list->verticalScrollBar()->value(); auto* model = m_list->clips();
    m_limitButton->setText(QStringLiteral("Limite : %1 · %2").arg(formatLimit(limitBytes), limitName(limitBytes)));
    m_list->setUpdatesEnabled(false);
    { QSignalBlocker blocker(m_list->selectionModel()); model->clear();
      for (const auto& copy : copies) {
          auto* row = new QStandardItem; row->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsDragEnabled); const auto& file = copy.file;
          row->setData(file.path, ClipRole::Path); row->setData(QFileInfo(file.path).fileName(), ClipRole::Name); row->setData(file.size, ClipRole::Bytes); row->setData(file.modified, ClipRole::Modified); row->setData(file.path, Qt::ToolTipRole);
          const QFileInfo source(copy.link.source); const bool known = !copy.link.source.isEmpty();
          row->setData(known ? QStringLiteral("depuis ") + source.completeBaseName() : QStringLiteral("Copie de partage"), ClipRole::Detail);
          if (known && source.isFile()) { row->setData(source.absoluteFilePath(), ClipRole::ThumbPath); row->setData(ThumbnailCache::keyFor(source.absoluteFilePath(), source.size(), source.lastModified()), ClipRole::Key); }
          // The limit the copy was made for; for an older copy, the smallest usual limit it fits in.
          qint64 limit = copy.link.limitBytes; if (limit <= 0) { limit = presets[3].bytes; for (int index = 3; index >= 0; --index) if (file.size <= presets[index].bytes) limit = presets[index].bytes; }
          row->setData(limit, ClipRole::Limit); model->appendRow(row);
      } }
    m_list->selectPaths(selected); m_list->verticalScrollBar()->setValue(scroll); m_list->setUpdatesEnabled(true);
    m_readyCaption->setText(QStringLiteral("PRÊTES À ENVOYER  %1").arg(copies.size())); m_readyCaption->setVisible(!copies.isEmpty());
    m_stack->setCurrentIndex(copies.isEmpty() && !m_batch->running() ? 1 : 0); updateSelection();
}
void SharingPage::refreshBatch() {
    const auto& jobs = m_batch->jobs(); const bool running = m_batch->running(); m_progress->setVisible(running);
    if (running) {
        const int index = m_batch->current(), size = m_batch->batchSize(); int position = 0;
        for (int job = 0; job < jobs.size() && job <= index; ++job) if (jobs[job].batch == m_batch->batch()) ++position;
        const auto name = index >= 0 && index < jobs.size() ? jobs[index].name : QString(); const auto stage = index >= 0 && index < jobs.size() ? jobs[index].stage : QString();
        m_jobTitle->setText(QStringLiteral("<b style='font-weight:600'>%1</b> <span style='color:#A1AABA'>· clip %2 sur %3%4</span>").arg(name.toHtmlEscaped()).arg(std::max(1, position)).arg(size).arg(stage.isEmpty() ? QString() : QStringLiteral(" · ") + stage.toHtmlEscaped()));
        const int percent = m_batch->percent(), remaining = m_batch->remainingSeconds(); m_jobBar->setValue(percent / 100.0);
        m_jobFigures->setText(remaining >= 0 ? QStringLiteral("%1 % · ≈ %2 s").arg(percent).arg(remaining) : QStringLiteral("%1 %").arg(percent));
        const int ready = m_batch->count(ShareJob::Ready), failed = m_batch->count(ShareJob::Failed);
        m_readyTag->setText(ready > 1 ? QStringLiteral("%1 prêtes").arg(ready) : QStringLiteral("1 prête")); m_readyTag->setVisible(ready > 0);
        m_failedTag->setText(failed > 1 ? QStringLiteral("%1 échecs").arg(failed) : QStringLiteral("1 échec")); m_failedTag->setVisible(failed > 0);
    }
    // Failed copies wait here for "Réessayer" or "Retirer". Rebuilt only when the set changes.
    QString signature; for (int index = 0; index < jobs.size(); ++index) if (jobs[index].state == ShareJob::Failed) signature += QString::number(index) + '|' + jobs[index].source + '|' + jobs[index].error + '\n';
    if (signature != m_failedSignature) {
        m_failedSignature = signature;
        while (auto* child = m_failedRows->takeAt(0)) { if (child->widget()) child->widget()->deleteLater(); delete child; }
        int shown = 0;
        for (int index = 0; index < jobs.size() && shown < 4; ++index) {
            if (jobs[index].state != ShareJob::Failed) continue;
            ++shown; auto* row = new QWidget; row->setObjectName("share-failed-row"); row->setFixedHeight(60); auto* line = new QHBoxLayout(row); line->setContentsMargins(42, 0, 4, 0); line->setSpacing(14);
            auto* mark = new IconBox(QStringLiteral("warn"), tk::danger, tk::dangerSoft); mark->setFixedSize(64, 36); line->addWidget(mark);
            auto* texts = new QVBoxLayout; texts->setSpacing(0); texts->addStretch(); texts->addWidget(label(jobs[index].name, "strong")); auto* reason = label(firstLine(jobs[index].error), "danger"); reason->setToolTip(jobs[index].error); texts->addWidget(reason); texts->addStretch(); line->addLayout(texts, 1);
            auto* retry = new Button(QStringLiteral("refresh"), QStringLiteral("Réessayer"), Button::Secondary, Button::Small); auto* dismiss = new Button(QStringLiteral("Retirer"), Button::Ghost, Button::Small); line->addWidget(retry); line->addWidget(dismiss);
            connect(retry, &QPushButton::clicked, m_batch, [this, index] { m_batch->retry(index); }); connect(dismiss, &QPushButton::clicked, m_batch, [this, index] { m_batch->remove(index); });
            m_failedRows->addWidget(row);
        }
        m_failed->setVisible(shown > 0);
    }
    m_stack->setCurrentIndex(m_copies.isEmpty() && !running && signature.isEmpty() ? 1 : 0);
}
void SharingPage::showMenu(const QString& path, const QPoint& globalPosition) {
    const auto paths = selectedPaths(); if (paths.isEmpty()) return;
    QString source; for (const auto& copy : m_copies) if (copy.file.path == path) source = copy.link.source;
    auto* menu = popupMenu(this); menu->setAttribute(Qt::WA_DeleteOnClose); menu->setMinimumWidth(264);
    const auto add = [&](const QString& iconName, const QString& text, bool enabled, auto&& action) { auto* result = menu->addAction(icon(iconName, iconName == QLatin1String("trash") ? tk::danger : tk::text2), text); result->setEnabled(enabled); connect(result, &QAction::triggered, this, action); };
    add(QStringLiteral("copy"), (paths.size() > 1 ? QStringLiteral("Copier les fichiers") : QStringLiteral("Copier le fichier")) + QStringLiteral("\tCtrl C"), true, [this, paths] { emit copyRequested(paths); });
    // A copy does not open in the editor, but it can be watched as it will look once sent.
    add(QStringLiteral("expand"), QStringLiteral("Lire en plein écran"), paths.size() == 1 && !m_trashing, [this, path] {
        QStringList playlist; for (const auto& copy : m_copies) playlist.append(copy.file.path);
        const int index = int(playlist.indexOf(path)); if (index >= 0) emit viewRequested(playlist, index);
    });
    add(QStringLiteral("folder"), QStringLiteral("Afficher dans le dossier"), paths.size() == 1, [this, path] { emit revealRequested(path); });
    add(QStringLiteral("cut"), QStringLiteral("Ouvrir l'original dans l'éditeur"), paths.size() == 1 && QFileInfo(source).isFile() && !m_trashing, [this, source] { emit openOriginalRequested(source); });
    menu->addSeparator();
    add(QStringLiteral("trash"), QStringLiteral("Supprimer la copie\tSuppr"), !m_trashing && !m_batch->running(), [this, paths] { emit trashRequested(paths); });
    menu->popup(globalPosition);
}
void SharingPage::updateSelection() {
    const auto paths = selectedPaths(); qint64 bytes = 0; for (const auto& copy : m_copies) if (paths.contains(copy.file.path)) bytes += copy.file.size;
    const bool visible = !paths.isEmpty(), changed = visible != m_bar->isVisible();
    m_barTitle->setText((paths.size() > 1 ? QStringLiteral("%1 copies sélectionnées · ").arg(paths.size()) : QStringLiteral("1 copie sélectionnée · ")) + formatBytes(bytes));
    m_barDetail->setText(paths.size() > 1 ? QStringLiteral("Glissez-les ensemble dans Discord") : QStringLiteral("Glissez-la dans Discord"));
    m_trash->setEnabled(!m_trashing && !m_batch->running()); m_list->setDragEnabled(!m_trashing);
    if (changed) {
        m_bar->setVisible(visible); static_cast<QVBoxLayout*>(m_listHost->layout())->setContentsMargins(14, 0, 4, visible ? 84 : 0);
        if (visible) { m_bar->setGeometry(20, height() - 16 - 56, width() - 40, 56); m_bar->raise(); }
        emit selectionBarVisible(visible);
    }
}
void SharingPage::resizeEvent(QResizeEvent* event) { QWidget::resizeEvent(event); m_bar->setGeometry(20, height() - 16 - 56, width() - 40, 56); }
}
