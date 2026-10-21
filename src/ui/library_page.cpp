#include "library_page.h"
#include "clip_list.h"
#include "thumbnail_cache.h"
#include "widgets.h"
#include <QApplication>
#include <QDir>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLineEdit>
#include <QListWidget>
#include <QLocale>
#include <QMenu>
#include <QPainter>
#include <QScrollBar>
#include <QShortcut>
#include <QStackedWidget>
#include <QStyledItemDelegate>
#include <QVBoxLayout>
#include <algorithm>

namespace clipsapp {
namespace {
class SearchField final : public QWidget {
public:
    explicit SearchField(QLineEdit* field) : m_field(field) {
        setFixedHeight(32); setAttribute(Qt::WA_Hover); setCursor(Qt::IBeamCursor);
        auto* layout = new QHBoxLayout(this); layout->setContentsMargins(34, 0, 10, 0); layout->addWidget(field);
        field->setProperty("bare", true); field->installEventFilter(this); setFocusProxy(field);
        connect(field, &QLineEdit::textChanged, this, [this] { update(); });
    }
protected:
    bool eventFilter(QObject* watched, QEvent* event) override { if (event->type() == QEvent::FocusIn || event->type() == QEvent::FocusOut) update(); return QWidget::eventFilter(watched, event); }
    void mousePressEvent(QMouseEvent*) override { m_field->setFocus(Qt::MouseFocusReason); }
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this); painter.setRenderHint(QPainter::Antialiasing); const bool focus = m_field->hasFocus();
        painter.setPen(QPen(focus ? tk::accent : underMouse() ? tk::control : tk::lineStrong, focus ? 2 : 1)); painter.setBrush(tk::field);
        const double inset = focus ? 1 : 0.5; painter.drawRoundedRect(QRectF(rect()).adjusted(inset, inset, -inset, -inset), 6, 6);
        paintIcon(painter, QStringLiteral("search"), QRectF(10, 8, 16, 16), tk::text2);
        if (m_field->text().isEmpty() && !focus && width() >= 280) { const auto size = kbdSize(QStringLiteral("Ctrl F")); paintKbd(painter, QRectF(width() - 10 - size.width(), 7, size.width(), 18), QStringLiteral("Ctrl F"), tk::text3); }
    }
private:
    QLineEdit* m_field;
};
class SuggestionDelegate final : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    QSize sizeHint(const QStyleOptionViewItem&, const QModelIndex& index) const override { return {200, index.data(Qt::UserRole + 2).toInt() == 2 ? 44 : 35}; }
    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override {
        painter->save(); painter->setRenderHint(QPainter::Antialiasing); const int kind = index.data(Qt::UserRole + 2).toInt(); QRectF row = QRectF(option.rect).adjusted(0, 0, 0, -1);
        if (kind == 2) { painter->fillRect(QRectF(row.left() + 6, row.top() + 4, row.width() - 12, 1), tk::line); row.setTop(row.top() + 9); }
        if (option.state & (QStyle::State_Selected | QStyle::State_MouseOver)) { painter->setPen(Qt::NoPen); painter->setBrush(tk::selected); painter->drawRoundedRect(row, 6, 6); }
        paintIcon(*painter, kind == 0 ? QStringLiteral("tag") : kind == 1 ? QStringLiteral("plus") : QStringLiteral("undo"), QRectF(row.left() + 10, row.center().y() - 8, 16, 16), tk::text2);
        painter->setFont(uiFont(13)); painter->setPen(tk::text); const auto note = index.data(Qt::UserRole + 1).toString();
        painter->drawText(row.adjusted(36, 0, -10, 0), Qt::AlignVCenter | Qt::AlignLeft, QFontMetrics(painter->font()).elidedText(index.data(Qt::DisplayRole).toString(), Qt::ElideRight, int(row.width() - 46 - (note.isEmpty() ? 0 : 70))));
        if (!note.isEmpty()) { painter->setFont(uiFont(12)); painter->setPen(tk::text3); painter->drawText(row.adjusted(36, 0, -10, 0), Qt::AlignVCenter | Qt::AlignRight, note); }
        painter->restore();
    }
};
class KbdHint final : public QWidget {
public:
    explicit KbdHint(const QString& keys) : m_keys(keys) { setFixedSize(kbdSize(keys)); }
protected:
    void paintEvent(QPaintEvent*) override { QPainter painter(this); paintKbd(painter, rect(), m_keys, tk::text3); }
private:
    QString m_keys;
};
class PartialBox final : public QWidget {
public:
    PartialBox() { setFixedSize(18, 18); }
protected:
    void paintEvent(QPaintEvent*) override { QPainter painter(this); Toggle::paintBox(painter, rect(), true, true); }
};
QString clipCount(int count, bool images = false) { return QStringLiteral("%1 %2%3").arg(count).arg(images ? QStringLiteral("screenshot") : QStringLiteral("clip"), count > 1 ? QStringLiteral("s") : QString()); }
}

ClassifyPopup::ClassifyPopup(QWidget* parent) : QFrame(parent) {
    setObjectName("classify-popup"); setFixedWidth(316); hide();
    setStyleSheet(QStringLiteral("QFrame#classify-popup { background: #1A1F27; border: 1px solid #3B4553; border-radius: 10px; } QListWidget { background: transparent; border: none; }"));
    auto* layout = new QVBoxLayout(this); layout->setContentsMargins(1, 1, 1, 1); layout->setSpacing(0);
    auto* top = new QVBoxLayout; top->setContentsMargins(13, 13, 13, 10); top->setSpacing(10); m_title = label({}, "bold"); top->addWidget(m_title);
    m_field = new QLineEdit; m_field->setObjectName("classify-game"); m_field->setMaxLength(80); m_field->setPlaceholderText(QStringLiteral("Nom du jeu")); m_field->addAction(icon(QStringLiteral("tag")), QLineEdit::LeadingPosition);
    top->addWidget(m_field); layout->addLayout(top);
    m_list = new QListWidget; m_list->setItemDelegate(new SuggestionDelegate(m_list)); m_list->setFrameShape(QFrame::NoFrame); m_list->setMouseTracking(true); m_list->setFocusPolicy(Qt::NoFocus);
    m_list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff); m_list->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff); m_list->setContentsMargins(0, 0, 0, 0);
    auto* listHost = new QHBoxLayout; listHost->setContentsMargins(5, 0, 5, 6); listHost->addWidget(m_list); layout->addLayout(listHost);
    layout->addWidget(separator());
    auto* bottom = new QHBoxLayout; bottom->setContentsMargins(13, 10, 13, 11); bottom->setSpacing(8); bottom->addWidget(label(QStringLiteral("Les fichiers ne sont pas déplacés."), "small"), 1); bottom->addWidget(new KbdHint(QStringLiteral("Entrée")));
    layout->addLayout(bottom);
    m_field->installEventFilter(this);
    connect(m_field, &QLineEdit::textChanged, this, [this] { refill(); });
    connect(m_list, &QListWidget::itemClicked, this, [this] { accept(); });
}
void ClassifyPopup::open(int clipCount, const QList<QPair<QString, int>>& games, const QString& current, const QPoint& anchor) {
    const auto* page = qobject_cast<const LibraryPage*>(parentWidget()); m_images = page && page->kind() == LibraryPage::Screenshots;
    m_title->setText(QStringLiteral("Classer ") + ::clipsapp::clipCount(clipCount, m_images)); m_games = games; m_anchor = anchor;
    { QSignalBlocker blocker(m_field); m_field->setText(current); }
    refill(); show(); raise(); m_field->setFocus(Qt::PopupFocusReason); m_field->selectAll(); qApp->installEventFilter(this);
}
void ClassifyPopup::hideEvent(QHideEvent* event) { qApp->removeEventFilter(this); QFrame::hideEvent(event); }
void ClassifyPopup::refill() {
    const auto text = m_field->text().trimmed(); m_list->clear(); bool exact = false; int shown = 0;
    const auto add = [this](const QString& title, const QString& game, const QString& note, int kind) { auto* item = new QListWidgetItem(title, m_list); item->setData(Qt::UserRole, game); item->setData(Qt::UserRole + 1, note); item->setData(Qt::UserRole + 2, kind); };
    for (const auto& game : m_games) {
        if (game.first.compare(text, Qt::CaseInsensitive) == 0) exact = true;
        if (shown < 6 && (text.isEmpty() || game.first.contains(text, Qt::CaseInsensitive))) { add(game.first, game.first, clipCount(game.second, m_images), 0); ++shown; }
    }
    if (!text.isEmpty() && !exact) add(QStringLiteral("Créer « %1 »").arg(text), text, {}, 1);
    add(QStringLiteral("Revenir au dossier d'origine"), {}, {}, 2);
    // What is typed wins: an exact name first, otherwise the first suggestion, otherwise the new name.
    int preferred = 0; for (int row = 0; row < m_list->count(); ++row) if (m_list->item(row)->data(Qt::UserRole + 2).toInt() != 2 && m_list->item(row)->data(Qt::UserRole).toString().compare(text, Qt::CaseInsensitive) == 0) preferred = row;
    m_list->setCurrentRow(preferred);
    int listHeight = 0; for (int row = 0; row < m_list->count(); ++row) listHeight += m_list->sizeHintForRow(row);
    m_list->setFixedHeight(listHeight + 2); adjustSize(); move(m_anchor.x() - width(), m_anchor.y() - height());
}
void ClassifyPopup::accept() {
    const auto* item = m_list->currentItem(); if (!item) return;
    const auto game = item->data(Qt::UserRole + 2).toInt() == 2 ? QString() : item->data(Qt::UserRole).toString(); hide(); emit chosen(game);
}
bool ClassifyPopup::eventFilter(QObject* watched, QEvent* event) {
    if (watched == m_field && event->type() == QEvent::KeyPress) {
        const int key = static_cast<QKeyEvent*>(event)->key();
        if (key == Qt::Key_Return || key == Qt::Key_Enter) { accept(); return true; }
        if (key == Qt::Key_Escape) { hide(); return true; }
        if (key == Qt::Key_Down || key == Qt::Key_Up) { const int count = m_list->count(); if (count) m_list->setCurrentRow((m_list->currentRow() + (key == Qt::Key_Down ? 1 : count - 1)) % count); return true; }
    } else if (event->type() == QEvent::MouseButtonPress && isVisible()) {
        auto* target = qobject_cast<QWidget*>(watched);
        if (target && target != this && !isAncestorOf(target) && target->window() == window()) hide();
    }
    return QFrame::eventFilter(watched, event);
}

LibraryPage::LibraryPage(ThumbnailCache* cache, Kind kind, QWidget* parent) : QWidget(parent), m_kind(kind), m_cache(cache) {
    const bool images = kind == Screenshots;
    setObjectName(images ? "screenshots-page" : "library-page");
    auto* layout = new QVBoxLayout(this); layout->setContentsMargins(0, 0, 0, 0); layout->setSpacing(0);
    auto* head = new QHBoxLayout; head->setContentsMargins(24, 20, 24, 8); head->setSpacing(10);
    auto* titles = new QVBoxLayout; titles->setSpacing(2); m_title = label(images ? QStringLiteral("Tous les screenshots") : QStringLiteral("Tous les clips"), "title"); m_subtitle = label({}, "t2"); m_subtitle->setObjectName(images ? "screenshots-summary" : "library-summary");
    titles->addWidget(m_title); titles->addWidget(m_subtitle); head->addLayout(titles, 1);
    m_searchField = new QLineEdit; m_searchField->setObjectName(images ? "screenshots-search" : "library-search"); m_searchField->setPlaceholderText(images ? QStringLiteral("Rechercher un screenshot ou un jeu") : QStringLiteral("Rechercher un clip ou un jeu")); m_searchField->setClearButtonEnabled(false);
    m_search = new SearchField(m_searchField); m_search->setFixedWidth(300); head->addWidget(m_search);
    m_sortButton = new Button(QStringLiteral("sort"), QStringLiteral("Plus récents")); m_sortButton->setObjectName("library-sort"); m_sortButton->setTrailingIcon(QStringLiteral("down")); m_sortButton->setSpread(true); m_sortButton->setFixedWidth(156); head->addWidget(m_sortButton);
    layout->addLayout(head);

    m_listHost = new QWidget; auto* host = new QVBoxLayout(m_listHost); host->setContentsMargins(14, 0, 4, 0); host->setSpacing(0);
    m_stack = new QStackedWidget; host->addWidget(m_stack); layout->addWidget(m_listHost, 1);
    m_list = new ClipListView; m_list->setObjectName(images ? "screenshot-library" : "clip-library"); m_rows = new LibraryRowDelegate(m_list, cache); m_rows->setImages(images); m_list->setRowDelegate(m_rows); m_stack->addWidget(m_list);

    m_empty = new QWidget; auto* emptyLayout = new QVBoxLayout(m_empty); emptyLayout->setContentsMargins(24, 24, 24, 24); emptyLayout->setSpacing(0); emptyLayout->addStretch();
    m_keys = new Keycaps(nullptr, true); emptyLayout->addWidget(m_keys, 0, Qt::AlignHCenter); emptyLayout->addSpacing(24);
    auto* emptyTitle = label(images ? QStringLiteral("Aucun screenshot pour l'instant") : QStringLiteral("Aucun clip pour l'instant")); emptyTitle->setFont(uiFont(20, QFont::Bold)); emptyTitle->setAlignment(Qt::AlignCenter); emptyLayout->addWidget(emptyTitle); emptyLayout->addSpacing(8);
    m_emptyText = label({}, "t2", true); m_emptyText->setFont(uiFont(14)); m_emptyText->setAlignment(Qt::AlignCenter); m_emptyText->setFixedWidth(520); emptyLayout->addLayout(centered(m_emptyText)); emptyLayout->addSpacing(22);
    auto* emptyActions = new QHBoxLayout; emptyActions->setSpacing(8); emptyActions->addStretch();
    m_startReplay = new Button(QStringLiteral("rec"), QStringLiteral("Démarrer le replay"), Button::Primary, Button::Large); m_startReplay->setObjectName("empty-start-replay"); emptyActions->addWidget(m_startReplay);
    auto* chooseFolder = new Button(QStringLiteral("folder"), QStringLiteral("Choisir un autre dossier"), Button::Secondary, Button::Large); chooseFolder->setObjectName("empty-choose-folder"); emptyActions->addWidget(chooseFolder); emptyActions->addStretch();
    emptyLayout->addLayout(emptyActions); emptyLayout->addSpacing(18);
    auto* formats = label(images ? QStringLiteral("Les images PNG, JPEG, WebP et BMP déposées dans le dossier « Screenshots » apparaissent aussi ici.") : QStringLiteral("Les vidéos MP4, MKV, WebM, MOV et AVI déposées dans ce dossier apparaissent aussi ici."), "small3"); formats->setAlignment(Qt::AlignCenter); emptyLayout->addWidget(formats); emptyLayout->addStretch();
    m_stack->addWidget(m_empty);
    // A screenshot is taken from a game, not from here: the empty list only says how.
    if (images) { m_startReplay->hide(); chooseFolder->hide(); m_startReplay->setObjectName("screenshots-empty-start"); chooseFolder->setObjectName("screenshots-empty-folder"); m_emptyText->setText(QStringLiteral("Pendant une partie, appuyez sur ce raccourci : l'image de l'écran arrive ici, en qualité native. Le bouton Screenshot de l'éditeur y range aussi l'image d'un clip.")); }

    m_noMatch = new QWidget; auto* noMatch = new QVBoxLayout(m_noMatch); noMatch->setSpacing(12); noMatch->addStretch();
    auto* noMatchTitle = label(images ? QStringLiteral("Aucun screenshot ne correspond") : QStringLiteral("Aucun clip ne correspond")); noMatchTitle->setFont(uiFont(16, QFont::Bold)); noMatchTitle->setAlignment(Qt::AlignCenter); noMatch->addWidget(noMatchTitle);
    auto* clearSearch = new Button(QStringLiteral("Effacer la recherche")); noMatch->addWidget(clearSearch, 0, Qt::AlignHCenter); noMatch->addStretch(); m_stack->addWidget(m_noMatch);

    m_bar = card("bar"); m_bar->setParent(this); m_bar->setObjectName("library-selection-bar"); m_bar->setFixedHeight(56); m_bar->hide();
    auto* bar = new QHBoxLayout(m_bar); bar->setContentsMargins(16, 0, 10, 0); bar->setSpacing(8);
    auto* clear = new Button(QStringLiteral("x"), QString(), Button::Ghost); clear->setToolTip(QStringLiteral("Tout désélectionner (Échap)")); clear->setAccessibleName(QStringLiteral("Tout désélectionner"));
    auto* barTexts = new QVBoxLayout; barTexts->setSpacing(0); barTexts->setContentsMargins(4, 0, 0, 0); m_barTitle = label({}, "bold"); m_barTitle->setObjectName("library-selection-count"); m_barDetail = label({}, "small");
    auto* dragLine = new QHBoxLayout; dragLine->setSpacing(4); dragLine->addWidget(new IconLabel(QStringLiteral("grip"), tk::text2, 14)); dragLine->addWidget(m_barDetail, 1);
    barTexts->addStretch(); barTexts->addWidget(m_barTitle); barTexts->addLayout(dragLine); barTexts->addStretch();
    bar->addWidget(new PartialBox); bar->addLayout(barTexts); bar->addStretch();
    m_classify = new Button(QStringLiteral("tag"), QStringLiteral("Classer…")); m_classify->setObjectName("classify-clip"); bar->addWidget(m_classify);
    m_share = new Button(QStringLiteral("share"), QStringLiteral("Préparer pour Discord")); m_share->setObjectName("compress-discord-library"); bar->addWidget(m_share);
    m_trash = new Button(QStringLiteral("trash"), QStringLiteral("Corbeille"), Button::Danger); m_trash->setObjectName("delete-library-clips"); bar->addWidget(m_trash);
    // A picture needs no copy for Discord: it is dragged, or copied, as it is.
    if (images) { m_share->hide(); m_share->setObjectName("screenshots-share"); m_classify->setObjectName("classify-screenshot"); m_trash->setObjectName("delete-screenshots"); m_bar->setObjectName("screenshots-selection-bar"); m_barTitle->setObjectName("screenshots-selection-count"); m_sortButton->setObjectName("screenshots-sort"); }
    auto* divider = separator(true); divider->setFixedHeight(28); bar->addSpacing(4); bar->addWidget(divider); bar->addSpacing(4); bar->addWidget(clear);
    m_popup = new ClassifyPopup(this);

    connect(m_searchField, &QLineEdit::textChanged, this, [this] { refresh(); });
    connect(clearSearch, &QPushButton::clicked, this, [this] { m_searchField->clear(); });
    connect(m_startReplay, &QPushButton::clicked, this, &LibraryPage::startReplayRequested);
    connect(chooseFolder, &QPushButton::clicked, this, &LibraryPage::chooseFolderRequested);
    connect(m_sortButton, &QPushButton::clicked, this, [this] {
        auto* menu = popupMenu(this); menu->setAttribute(Qt::WA_DeleteOnClose); menu->setMinimumWidth(m_sortButton->width());
        const QStringList names{QStringLiteral("Plus récents"), QStringLiteral("Plus anciens"), QStringLiteral("Nom, de A à Z"), QStringLiteral("Plus lourds")};
        for (int index = 0; index < names.size(); ++index) {
            auto* action = menu->addAction(index == m_sort ? icon(QStringLiteral("check"), tk::accent) : QIcon(iconPixmap(QString(), tk::text, 16)), names[index]);
            connect(action, &QAction::triggered, this, [this, index, names] { m_sort = Sort(index); m_sortName = names[index]; m_sortButton->setText(width() < 780 ? QString() : m_sortName); m_sortButton->setToolTip(QStringLiteral("Trier : ") + m_sortName.toLower()); refresh(); });
        }
        menu->popup(m_sortButton->mapToGlobal(QPoint(0, m_sortButton->height() + 4)));
    });
    connect(m_list->selectionModel(), &QItemSelectionModel::selectionChanged, this, [this] { updateSelection(); });
    connect(m_list, &ClipListView::openRequested, this, &LibraryPage::openRequested);
    connect(m_list, &ClipListView::renameRequested, this, &LibraryPage::renameRequested);
    connect(m_list, &ClipListView::menuRequested, this, &LibraryPage::showMenu);
    connect(m_list, &ClipListView::actionRequested, this, [this](int hit, const QString& path, const QPoint& position) {
        if (hit == ClipRowDelegate::Share) { if (m_kind == Screenshots) emit copyRequested({path}); else if (!m_sharing && !m_trashing) emit shareRequested({path}); }
        else if (hit == ClipRowDelegate::Play) view(path);
        else if (hit == ClipRowDelegate::More) { if (!selectedPaths().contains(path)) m_list->selectPaths({path}); showMenu(path, position); }
    });
    connect(clear, &QPushButton::clicked, this, &LibraryPage::clearSelection);
    connect(m_classify, &QPushButton::clicked, this, &LibraryPage::classify);
    connect(m_share, &QPushButton::clicked, this, [this] { emit shareRequested(selectedPaths()); });
    connect(m_trash, &QPushButton::clicked, this, [this] { emit trashRequested(selectedPaths()); });
    connect(m_popup, &ClassifyPopup::chosen, this, [this](const QString& game) { const auto paths = selectedPaths(); if (!paths.isEmpty()) emit classifyRequested(paths, game); m_list->setFocus(); });
    connect(cache, &ThumbnailCache::ready, m_list->viewport(), qOverload<>(&QWidget::update));
    const auto shortcut = [this](const QKeySequence& keys, auto&& action) { auto* result = new QShortcut(keys, m_list); result->setContext(Qt::WidgetWithChildrenShortcut); connect(result, &QShortcut::activated, this, action); };
    shortcut(QKeySequence::Delete, [this] { const auto paths = selectedPaths(); if (!paths.isEmpty() && m_trash->isEnabled()) emit trashRequested(paths); });
    shortcut(Qt::Key_F2, [this] { const auto paths = selectedPaths(); if (paths.size() == 1) rename(paths.first()); });
    shortcut(Qt::Key_F, [this] { toggleFavorite(); });
    shortcut(QKeySequence::Copy, [this] { const auto paths = selectedPaths(); if (!paths.isEmpty()) emit copyRequested(paths); });
    shortcut(Qt::Key_Escape, [this] { clearSelection(); });
    shortcut(Qt::Key_Space, [this] { const auto paths = selectedPaths(); if (paths.size() == 1) view(paths.first()); });
    refresh();
}
void LibraryPage::view(const QString& path) {
    if (m_trashing) return;
    // The clips as they are listed, in their order: Page précédente and Page suivante walk through them.
    QStringList playlist; for (int row = 0; row < m_list->clips()->rowCount(); ++row) { const auto listed = m_list->clips()->index(row, 0).data(ClipRole::Path).toString(); if (!listed.isEmpty()) playlist.append(listed); }
    const int index = int(playlist.indexOf(path)); if (index >= 0) emit viewRequested(playlist, index);
}
const LibraryItem* LibraryPage::item(const QString& path) const { for (const auto& entry : m_items) if (entry.clip.path == path) return &entry; return nullptr; }
void LibraryPage::setItems(QList<LibraryItem> items, qint64 limitBytes) { m_items = std::move(items); m_limit = limitBytes; refresh(); }
void LibraryPage::setFilter(Filter filter, const QString& game) { if (filter == m_filter && game == m_game) return; m_filter = filter; m_game = game; m_list->clearSelection(); refresh(); m_list->scrollToTop(); }
void LibraryPage::setBusy(bool trashing, bool sharing) { m_trashing = trashing; m_sharing = sharing; updateSelection(); }
void LibraryPage::setScreenshotHint(const QKeySequence& hotkey) { if (m_kind == Screenshots) m_keys->setSequence(hotkey); }
void LibraryPage::setReplayHint(const QKeySequence& hotkey, int seconds, bool canStart, bool running) {
    if (m_kind == Screenshots) return;
    m_keys->setSequence(hotkey); m_startReplay->setEnabled(canStart && !running); m_startReplay->setText(running ? QStringLiteral("Replay actif") : QStringLiteral("Démarrer le replay"));
    m_emptyText->setText(running ? QStringLiteral("Le replay tourne. Quand il se passe quelque chose, appuyez sur le raccourci : les %1 dernières secondes arrivent ici.").arg(seconds)
                                 : QStringLiteral("Démarrez le replay avant de jouer. Quand il se passe quelque chose, appuyez sur le raccourci : les %1 dernières secondes arrivent ici.").arg(seconds));
}
QStringList LibraryPage::selectedPaths() const { return m_list->selectedPaths(); }
void LibraryPage::select(const QStringList& paths) { m_list->selectPaths(paths); }
void LibraryPage::clearSelection() { m_popup->hide(); m_list->clearSelection(); }
void LibraryPage::focusSearch() { m_searchField->setFocus(Qt::ShortcutFocusReason); m_searchField->selectAll(); }
void LibraryPage::rename(const QString& path) { m_list->beginRename(path); }
void LibraryPage::toggleFavorite() {
    const auto paths = selectedPaths(); if (paths.isEmpty()) return;
    bool all = true; for (const auto& path : paths) if (const auto* entry = item(path); !entry || !entry->favorite) all = false;
    emit favoriteRequested(paths, !all);
}
void LibraryPage::classify() {
    const auto paths = selectedPaths(); if (paths.isEmpty() || m_trashing) return;
    QHash<QString, int> counts; for (const auto& entry : m_items) if (entry.clip.game != QStringLiteral("Non classé")) ++counts[entry.clip.game];
    QList<QPair<QString, int>> games; for (auto it = counts.cbegin(); it != counts.cend(); ++it) games.append({it.key(), it.value()});
    std::sort(games.begin(), games.end(), [](const auto& left, const auto& right) { return left.second != right.second ? left.second > right.second : left.first.localeAwareCompare(right.first) < 0; });
    QString current; if (paths.size() == 1) if (const auto* entry = item(paths.first()); entry && entry->clip.game != QStringLiteral("Non classé")) current = entry->clip.game;
    m_popup->open(int(paths.size()), games, current, m_classify->mapTo(this, QPoint(m_classify->width(), 0)) + QPoint(0, m_bar->isVisible() ? -18 : m_classify->height()));
}
void LibraryPage::showMenu(const QString& path, const QPoint& globalPosition) {
    const auto paths = selectedPaths(); if (paths.isEmpty()) return;
    const bool single = paths.size() == 1, busy = m_trashing || m_sharing; const auto* entry = item(path);
    bool allFavorite = true; for (const auto& selected : paths) if (const auto* other = item(selected); !other || !other->favorite) allFavorite = false;
    auto* menu = popupMenu(this); menu->setAttribute(Qt::WA_DeleteOnClose); menu->setMinimumWidth(264);
    const auto add = [&](const QString& iconName, const QString& text, const QString& keys, bool enabled, auto&& action) {
        auto* result = menu->addAction(icon(iconName, iconName == QLatin1String("trash") ? tk::danger : tk::text2), keys.isEmpty() ? text : text + '\t' + keys); result->setEnabled(enabled); connect(result, &QAction::triggered, this, action); return result;
    };
    if (m_kind == Screenshots) {
        add(QStringLiteral("expand"), QStringLiteral("Voir en plein écran"), QStringLiteral("Espace"), single && !m_trashing, [this, path] { view(path); });
        add(QStringLiteral("crop"), QStringLiteral("Ouvrir dans l'éditeur"), QStringLiteral("Entrée"), single && !m_trashing, [this, path] { emit openRequested(path); });
        // Pasted in Discord, a single picture arrives as an image; several, as files.
        add(QStringLiteral("copy"), single ? QStringLiteral("Copier l'image") : QStringLiteral("Copier les images"), QStringLiteral("Ctrl C"), true, [this, paths] { emit copyRequested(paths); });
    } else {
        add(QStringLiteral("expand"), QStringLiteral("Lire en plein écran"), QStringLiteral("Espace"), single && !m_trashing, [this, path] { view(path); });
        add(QStringLiteral("cut"), QStringLiteral("Ouvrir dans l'éditeur"), QStringLiteral("Entrée"), single && !m_trashing, [this, path] { emit openRequested(path); });
        add(QStringLiteral("share"), QStringLiteral("Préparer pour Discord"), {}, !busy, [this, paths] { emit shareRequested(paths); });
        add(QStringLiteral("copy"), single ? QStringLiteral("Copier le fichier") : QStringLiteral("Copier les fichiers"), QStringLiteral("Ctrl C"), true, [this, paths] { emit copyRequested(paths); });
    }
    add(QStringLiteral("folder"), QStringLiteral("Afficher dans le dossier"), {}, single, [this, path] { emit revealRequested(path); });
    menu->addSeparator();
    add(QStringLiteral("edit"), QStringLiteral("Renommer"), QStringLiteral("F2"), single && !busy && entry, [this, path] { rename(path); });
    add(QStringLiteral("tag"), QStringLiteral("Classer…"), {}, !m_trashing, [this] { classify(); });
    add(QStringLiteral("star"), allFavorite ? QStringLiteral("Retirer des favoris") : QStringLiteral("Ajouter aux favoris"), QStringLiteral("F"), true, [this] { toggleFavorite(); });
    menu->addSeparator();
    add(QStringLiteral("trash"), QStringLiteral("Mettre à la corbeille"), QStringLiteral("Suppr"), !busy, [this, paths] { emit trashRequested(paths); });
    menu->popup(globalPosition);
}
void LibraryPage::refresh() {
    const bool images = m_kind == Screenshots;
    const auto needle = m_searchField->text().trimmed(); QList<const LibraryItem*> shown; qint64 bytes = 0;
    for (const auto& entry : m_items) {
        if (m_filter == Favorites && !entry.favorite) continue;
        if (m_filter == Game && entry.clip.game != m_game) continue;
        if (!needle.isEmpty() && !entry.clip.name.contains(needle, Qt::CaseInsensitive) && !entry.clip.game.contains(needle, Qt::CaseInsensitive)) continue;
        shown.append(&entry); bytes += entry.clip.size;
    }
    std::stable_sort(shown.begin(), shown.end(), [this](const LibraryItem* left, const LibraryItem* right) {
        switch (m_sort) {
        case Oldest: return left->clip.modified < right->clip.modified;
        case ByName: return left->clip.name.localeAwareCompare(right->clip.name) < 0;
        case Largest: return left->clip.size > right->clip.size;
        default: return left->clip.modified > right->clip.modified;
        }
    });
    const auto selected = selectedPaths(); const int scroll = m_list->verticalScrollBar()->value(); const auto today = QDate::currentDate(); const bool grouped = m_sort == Recent || m_sort == Oldest;
    const QLocale french(QLocale::French, QLocale::France); auto* model = m_list->clips(); m_rows->setShowGame(m_filter != Game);
    m_list->setUpdatesEnabled(false);
    { QSignalBlocker blocker(m_list->selectionModel()); model->clear(); QDate day; QStandardItem* header = nullptr; int inGroup = 0;
      const auto closeGroup = [&] { if (header) header->setData(clipCount(inGroup, images), ClipRole::Detail); };
      for (const auto* entry : shown) {
          const auto& clip = entry->clip;
          if (grouped && clip.modified.date() != day) {
              closeGroup(); day = clip.modified.date(); inGroup = 0;
              header = new QStandardItem; header->setFlags(Qt::NoItemFlags); header->setData(true, ClipRole::Header); header->setData(dateGroupTitle(day, today), ClipRole::Name); model->appendRow(header);
          }
          ++inGroup; auto* row = new QStandardItem; row->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsDragEnabled);
          row->setData(clip.path, ClipRole::Path); row->setData(clip.path, ClipRole::ThumbPath); row->setData(ThumbnailCache::keyFor(clip.path, clip.size, clip.modified), ClipRole::Key);
          row->setData(clip.name, ClipRole::Name); row->setData(clip.game, ClipRole::Game); row->setData(clip.size, ClipRole::Bytes); row->setData(clip.modified, ClipRole::Modified);
          row->setData(entry->favorite, ClipRole::Favorite); row->setData(entry->fresh, ClipRole::Fresh); row->setData(clip.path, Qt::ToolTipRole);
          row->setData(grouped ? clip.modified.toString(QStringLiteral("HH:mm")) : french.toString(clip.modified, QStringLiteral("d MMM yy HH:mm")), ClipRole::Detail);
          const bool fits = images || clip.size <= m_limit; row->setData(fits || entry->hasCopy, ClipRole::FitOk);
          row->setData(images ? QString() : fits ? QStringLiteral("Prêt pour Discord") : entry->hasCopy ? QStringLiteral("Copie Discord prête") : QStringLiteral("Plus de %1").arg(formatLimit(m_limit)), ClipRole::FitText);
          model->appendRow(row);
      }
      closeGroup(); }
    m_list->selectPaths(selected); m_list->verticalScrollBar()->setValue(scroll); m_list->setUpdatesEnabled(true);
    m_title->setText(m_filter == Favorites ? QStringLiteral("Favoris") : m_filter == Game ? m_game : images ? QStringLiteral("Tous les screenshots") : QStringLiteral("Tous les clips"));
    const bool none = m_items.isEmpty();
    m_subtitle->setText(m_loading && none ? QStringLiteral("Lecture du dossier…") : none ? (images ? QStringLiteral("Aucun screenshot dans %1") : QStringLiteral("Aucun clip dans %1")).arg(QDir::toNativeSeparators(m_path)) : clipCount(int(shown.size()), images) + QStringLiteral(" · ") + formatBytes(bytes));
    m_subtitle->setToolTip(QDir::toNativeSeparators(m_path));
    m_search->setVisible(!none); m_sortButton->setVisible(!none);
    m_stack->setCurrentWidget(none ? (m_loading ? static_cast<QWidget*>(m_list) : m_empty) : shown.isEmpty() ? m_noMatch : static_cast<QWidget*>(m_list));
    updateSelection();
}
void LibraryPage::updateSelection() {
    const auto paths = selectedPaths(); qint64 bytes = 0; for (const auto& path : paths) if (const auto* entry = item(path)) bytes += entry->clip.size;
    const bool visible = !paths.isEmpty(), changed = visible != m_bar->isVisible();
    m_barTitle->setText(paths.size() > 1 ? QStringLiteral("%1 clips sélectionnés").arg(paths.size()) : QStringLiteral("1 clip sélectionné"));
    m_barDetail->setText((paths.size() > 1 ? QStringLiteral("Glissez-les dans Discord · ") : QStringLiteral("Glissez-le dans Discord · ")) + formatBytes(bytes));
    m_classify->setEnabled(!m_trashing); m_share->setEnabled(!m_trashing && !m_sharing); m_trash->setEnabled(!m_trashing && !m_sharing);
    m_trash->setText(m_trashing ? QStringLiteral("Suppression…") : QStringLiteral("Corbeille")); m_list->setDragEnabled(!m_trashing && !m_sharing);
    if (!visible) m_popup->hide();
    if (changed) {
        m_bar->setVisible(visible); static_cast<QVBoxLayout*>(m_listHost->layout())->setContentsMargins(14, 0, 4, visible ? 84 : 0);
        if (visible) { m_bar->setGeometry(20, height() - 16 - 56, width() - 40, 56); m_bar->raise(); }
        emit selectionBarVisible(visible);
    }
}
void LibraryPage::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event); m_bar->setGeometry(20, height() - 16 - 56, width() - 40, 56); m_popup->hide();
    const bool compact = width() < 780; m_search->setFixedWidth(compact ? 220 : 300); m_sortButton->setFixedWidth(compact ? 32 : 156);
    m_sortButton->setText(compact ? QString() : m_sortName); m_sortButton->setTrailingIcon(compact ? QString() : QStringLiteral("down")); m_sortButton->setSpread(!compact);
    m_title->setFont(uiFont(compact ? 20 : 22, QFont::Bold));
}
}
