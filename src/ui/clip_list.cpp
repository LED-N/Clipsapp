#include "clip_list.h"
#include "thumbnail_cache.h"
#include "widgets.h"
#include "core/clip_library.h"
#include <QContextMenuEvent>
#include <QDrag>
#include <QFileInfo>
#include <QLineEdit>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QScrollBar>
#include <QSet>
#include <algorithm>
#ifdef Q_OS_WIN
#include <QDir>
#include <shlobj.h>
#include <shobjidl.h>
#include <dwmapi.h>
#include <uiautomationclient.h>
#include <vector>
#endif

namespace clipsapp {
namespace {
QString durationText(qint64 ms) { const auto seconds = (ms + 500) / 1000; return QString("%1:%2").arg(seconds / 60).arg(seconds % 60, 2, 10, QLatin1Char('0')); }
// The look of a small button, for rows drawn by a delegate.
void paintRowButton(QPainter* painter, const QRectF& rect, const QString& iconName, const QString& text, bool hovered, bool ghost = false) {
    painter->save(); painter->setRenderHint(QPainter::Antialiasing);
    if (ghost) { painter->setPen(Qt::NoPen); painter->setBrush(hovered ? tk::raised : QColor(Qt::transparent)); }
    else { painter->setPen(QPen(hovered ? tk::lineHover : tk::lineStrong, 1)); painter->setBrush(hovered ? tk::hover : tk::raised); }
    painter->drawRoundedRect(rect.adjusted(0.5, 0.5, -0.5, -0.5), 6, 6);
    const QColor color = ghost && !hovered ? tk::text2 : tk::text;
    if (text.isEmpty()) paintIcon(*painter, iconName, QRectF(rect.center().x() - 8, rect.center().y() - 8, 16, 16), color);
    else {
        const auto font = uiFont(12, QFont::DemiBold); const double width = 16 + 6 + QFontMetrics(font).horizontalAdvance(text); double x = rect.x() + (rect.width() - width) / 2;
        paintIcon(*painter, iconName, QRectF(x, rect.center().y() - 8, 16, 16), color); x += 22;
        painter->setFont(font); painter->setPen(color); painter->drawText(QRectF(x, rect.y(), rect.right() - x, rect.height()), Qt::AlignVCenter | Qt::AlignLeft, text);
    }
    painter->restore();
}
int rowButtonWidth(const QString& text) { return 10 + 16 + 6 + QFontMetrics(uiFont(12, QFont::DemiBold)).horizontalAdvance(text) + 10 + 2; }
QPixmap dragCard(const QWidget* source, const QList<QUrl>& urls) {
    const auto ratio = source->devicePixelRatioF(); QPixmap card(QSize(320, 58) * ratio); card.setDevicePixelRatio(ratio); card.fill(Qt::transparent);
    QPainter painter(&card); painter.setRenderHint(QPainter::Antialiasing);
    painter.setBrush(tk::raised); painter.setPen(QPen(tk::accent, 1)); painter.drawRoundedRect(QRectF(1, 1, 318, 56), 8, 8);
    painter.setFont(uiFont(13, QFont::DemiBold)); painter.setPen(tk::text);
    const auto title = urls.size() == 1 ? QFileInfo(urls.first().toLocalFile()).fileName() : QStringLiteral("%1 clips").arg(urls.size());
    painter.drawText(QRect(14, 7, 292, 22), Qt::AlignVCenter, painter.fontMetrics().elidedText(title, Qt::ElideRight, 292));
    painter.setFont(uiFont(12)); painter.setPen(tk::accent); painter.drawText(QRect(14, 30, 292, 20), Qt::AlignVCenter, QStringLiteral("Déposer dans Discord"));
    return card;
}
}
QList<QUrl> existingFileUrls(const QStringList& paths) {
    QList<QUrl> urls; QSet<QString> seen;
    for (const auto& entry : paths) {
        const QFileInfo file(entry);
        if (!file.isFile() || !file.isReadable()) continue;
        const auto path = file.absoluteFilePath(); if (seen.contains(path)) continue;
        seen.insert(path); urls.append(QUrl::fromLocalFile(path));
    }
    return urls;
}
#ifdef Q_OS_WIN
namespace {
// Windows 11 brings an application forward when a dragged file rests on its taskbar button, but not
// on every machine (not here, from the Explorer either). The drag does it itself: resting there a
// moment, the button is read through UI Automation and the window of its application comes up.
class TaskbarHover {
public:
    TaskbarHover() {
        // CUIAutomation and IUIAutomation, spelled out: MinGW does not define them.
        const CLSID automation{0xff48dba4, 0x60ef, 0x4201, {0xaa, 0x87, 0x54, 0x10, 0x3e, 0xef, 0x59, 0x4e}};
        const IID client{0x30cbe57d, 0xd9d0, 0x452a, {0xab, 0x13, 0x7a, 0xc5, 0xac, 0x48, 0x25, 0xee}};
        if (FAILED(CoCreateInstance(automation, nullptr, CLSCTX_INPROC_SERVER, client, reinterpret_cast<void**>(&s_automation)))) s_automation = nullptr;
        s_button.clear(); s_since = 0; s_done = false; m_timer = SetTimer(nullptr, 0, 120, &TaskbarHover::tick);
    }
    ~TaskbarHover() { if (m_timer) KillTimer(nullptr, m_timer); if (s_automation) s_automation->Release(); s_automation = nullptr; }
private:
    static void CALLBACK tick(HWND, UINT, UINT_PTR, DWORD) {
        POINT cursor{}; if (!s_automation || !GetCursorPos(&cursor)) return;
        wchar_t kind[64] = {}; GetClassNameW(GetAncestor(WindowFromPoint(cursor), GA_ROOT), kind, 64);
        if (wcscmp(kind, L"Shell_TrayWnd") != 0 && wcscmp(kind, L"Shell_SecondaryTrayWnd") != 0) { s_button.clear(); return; }
        IUIAutomationElement* element = nullptr; if (FAILED(s_automation->ElementFromPoint(cursor, &element)) || !element) return;
        BSTR name = nullptr, id = nullptr; element->get_CurrentName(&name); element->get_CurrentAutomationId(&id); element->Release();
        const auto text = name ? QString::fromWCharArray(name) : QString(), appId = id ? QString::fromWCharArray(id) : QString(); SysFreeString(name); SysFreeString(id);
        const auto button = appId + QLatin1Char('|') + text;
        if (button != s_button) { s_button = button; s_since = GetTickCount64(); s_done = false; return; }
        if (s_done || text.isEmpty() || GetTickCount64() - s_since < 700) return;
        s_done = true; if (const auto window = windowFor(text, appId)) bringForward(window);
    }
    // « Discord - 1 fenêtre en cours d'exécution »: the application is named first; the id of the button
    // often holds the name of its program (« Appid: com.squirrel.Discord.Discord »).
    static HWND windowFor(const QString& text, const QString& appId) {
        struct Search { QString app, id; HWND best = nullptr; int score = 0; } search{text.section(QStringLiteral(" - "), 0, 0).trimmed(), appId};
        EnumWindows([](HWND window, LPARAM data) -> BOOL {
            auto& search = *reinterpret_cast<Search*>(data);
            if (!IsWindowVisible(window) || GetWindow(window, GW_OWNER) || (GetWindowLongW(window, GWL_EXSTYLE) & WS_EX_TOOLWINDOW)) return TRUE;
            BOOL cloaked = FALSE; DwmGetWindowAttribute(window, DWMWA_CLOAKED, &cloaked, sizeof(cloaked)); if (cloaked) return TRUE;
            DWORD process = 0; GetWindowThreadProcessId(window, &process); if (process == GetCurrentProcessId()) return TRUE;
            wchar_t title[256] = {}; GetWindowTextW(window, title, 256); const auto caption = QString::fromWCharArray(title); if (caption.isEmpty()) return TRUE;
            QString program;
            if (const HANDLE handle = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, process)) {
                wchar_t path[MAX_PATH] = {}; DWORD size = MAX_PATH; if (QueryFullProcessImageNameW(handle, 0, path, &size)) program = QFileInfo(QString::fromWCharArray(path)).completeBaseName();
                CloseHandle(handle);
            }
            int score = 0;
            if (!program.isEmpty() && search.id.contains(program, Qt::CaseInsensitive)) score += 3;
            if (!program.isEmpty() && (search.app.startsWith(program, Qt::CaseInsensitive) || program.startsWith(search.app, Qt::CaseInsensitive))) score += 2;
            if (!search.app.isEmpty() && caption.contains(search.app, Qt::CaseInsensitive)) score += 1;
            // The windows come from the front: on a tie, the one seen last stays chosen.
            if (score > search.score) { search.score = score; search.best = window; }
            return TRUE;
        }, reinterpret_cast<LPARAM>(&search));
        return search.best;
    }
    static void bringForward(HWND window) {
        if (IsIconic(window)) ShowWindow(window, SW_RESTORE);
        // Once another application is in front, Windows may refuse the plain request: the switch of Alt+Tab is asked then.
        if (!SetForegroundWindow(window)) SwitchToThisWindow(window, TRUE);
    }
    inline static IUIAutomation* s_automation = nullptr; inline static QString s_button; inline static ULONGLONG s_since = 0; inline static bool s_done = false;
    UINT_PTR m_timer = 0;
};
// The drag of the Explorer itself: a shell data object with its drag image. Only this one lets the
// taskbar of Windows 11 bring forward the application hovered, Discord say, before the file is dropped.
bool shellFileDrag(QWidget* source, const QList<QUrl>& urls, const QPixmap& card) {
    std::vector<PIDLIST_ABSOLUTE> items;
    const auto release = [&items] { for (auto* item : items) CoTaskMemFree(item); };
    for (const auto& url : urls) {
        PIDLIST_ABSOLUTE item = nullptr;
        if (FAILED(SHParseDisplayName(reinterpret_cast<LPCWSTR>(QDir::toNativeSeparators(url.toLocalFile()).utf16()), nullptr, &item, 0, nullptr))) { release(); return false; }
        items.push_back(item);
    }
    IShellItemArray* array = nullptr; IDataObject* data = nullptr;
    const bool made = SUCCEEDED(SHCreateShellItemArrayFromIDLists(UINT(items.size()), const_cast<PCIDLIST_ABSOLUTE_ARRAY>(items.data()), &array))
        && SUCCEEDED(array->BindToHandler(nullptr, BHID_DataObject, IID_PPV_ARGS(&data)));
    if (array) array->Release(); release();
    if (!made) { if (data) data->Release(); return false; }
    IDragSourceHelper* helper = nullptr;
    if (SUCCEEDED(CoCreateInstance(CLSID_DragDropHelper, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&helper)))) {
        const auto image = card.toImage().convertToFormat(QImage::Format_ARGB32_Premultiplied);
        SHDRAGIMAGE picture{}; picture.sizeDragImage = {image.width(), image.height()}; picture.ptOffset = {18, 18}; picture.crColorKey = CLR_NONE;
        picture.hbmpDragImage = image.toHBITMAP();
        // On success the helper owns the picture; otherwise it is ours to delete.
        if (picture.hbmpDragImage && FAILED(helper->InitializeFromBitmap(&picture, data))) DeleteObject(picture.hbmpDragImage);
        helper->Release();
    }
    DWORD effect = DROPEFFECT_NONE; const auto window = reinterpret_cast<HWND>(source->window()->winId());
    TaskbarHover hover;
    const bool done = SUCCEEDED(SHDoDragDrop(window, data, nullptr, DROPEFFECT_COPY, &effect));
    data->Release();
    // The drag kept the release of the button for itself: the window is given it, or it would still think the button down.
    POINT cursor{}; GetCursorPos(&cursor); ScreenToClient(window, &cursor);
    PostMessageW(window, WM_LBUTTONUP, 0, MAKELPARAM(WORD(short(cursor.x)), WORD(short(cursor.y))));
    return done;
}
}
#endif
bool startFileDrag(QWidget* source, const QList<QUrl>& urls) {
    if (urls.isEmpty()) return false;
#ifdef Q_OS_WIN
    if (shellFileDrag(source, urls, dragCard(source, urls))) return true;
#endif
    auto* data = new QMimeData; data->setUrls(urls);
    auto* drag = new QDrag(source); drag->setMimeData(data); drag->setPixmap(dragCard(source, urls)); drag->setHotSpot({18, 18});
    // Copy-only native drag: no file contents in RAM, no move or source-row
    // removal, including when the target asks for a move or the user cancels.
    drag->exec(Qt::CopyAction, Qt::CopyAction);
    drag->deleteLater(); return true;
}
QStringList ClipListModel::mimeTypes() const { return {QStringLiteral("text/uri-list")}; }
QMimeData* ClipListModel::mimeData(const QModelIndexList& indexes) const {
    QSet<int> rows; for (const auto& index : indexes) rows.insert(index.row());
    auto ordered = rows.values(); std::sort(ordered.begin(), ordered.end());
    QStringList paths; for (const auto row : ordered) paths.append(index(row, 0).data(ClipRole::Path).toString());
    const auto urls = existingFileUrls(paths);
    if (urls.isEmpty()) return nullptr;
    auto* data = new QMimeData; data->setUrls(urls); return data;
}

ClipRowDelegate::ClipRowDelegate(ClipListView* view, ThumbnailCache* cache) : QStyledItemDelegate(view), m_view(view), m_cache(cache) {}
void ClipRowDelegate::paintBackground(QPainter* painter, const QRectF& row, const QStyleOptionViewItem& option) const {
    const bool selected = option.state & QStyle::State_Selected, hovered = option.state & QStyle::State_MouseOver;
    if (selected || hovered) { painter->setPen(Qt::NoPen); painter->setBrush(selected ? (hovered ? tk::mix(tk::surface, tk::accent, 0.12) : tk::mix(tk::surface, tk::accent, 0.08)) : tk::surface); painter->drawRoundedRect(row, 8, 8); }
    if (m_view->keyboardFocus() && m_view->hasFocus() && m_view->currentIndex() == option.index) { painter->setPen(QPen(tk::info, 2)); painter->setBrush(Qt::NoBrush); painter->drawRoundedRect(row.adjusted(1, 1, -1, -1), 7, 7); }
}
bool ClipRowDelegate::paintThumbnail(QPainter* painter, const QRectF& rect, const QModelIndex& index, double radius, bool duration) const {
    const auto key = index.data(ClipRole::Key).toString(), path = index.data(ClipRole::ThumbPath).toString();
    const auto* preview = key.isEmpty() ? nullptr : m_cache->preview(key); const auto* meta = key.isEmpty() ? nullptr : m_cache->meta(key);
    if (!key.isEmpty() && !preview) m_cache->request(key, path);
    painter->save(); QPainterPath rounded; rounded.addRoundedRect(rect, radius, radius); painter->setClipPath(rounded); painter->fillRect(rect, tk::raised);
    if (preview && !preview->image.isNull()) {
        const auto size = QSizeF(preview->image.size()).scaled(rect.size(), Qt::KeepAspectRatio);
        painter->setRenderHint(QPainter::SmoothPixmapTransform); painter->drawImage(QRectF(rect.center().x() - size.width() / 2, rect.center().y() - size.height() / 2, size.width(), size.height()), preview->image);
    } else paintIcon(*painter, meta && meta->gaveUp() ? QStringLiteral("warn") : QStringLiteral("film"), QRectF(rect.center().x() - 8, rect.center().y() - 8, 16, 16), tk::text3);
    painter->setClipping(false);
    if (duration && meta && meta->durationMs > 0) {
        const auto text = durationText(meta->durationMs); const auto font = monoFont(11, QFont::DemiBold); const double width = QFontMetrics(font).horizontalAdvance(text) + 10;
        const QRectF badge(rect.right() - 4 - width, rect.bottom() - 4 - 18, width, 18); painter->setPen(Qt::NoPen); painter->setBrush(tk::window); painter->drawRoundedRect(badge, 4, 4);
        painter->setFont(font); painter->setPen(tk::text); painter->drawText(badge, Qt::AlignCenter, text);
    }
    painter->restore(); return key.isEmpty() || meta;
}

LibraryRowDelegate::Layout LibraryRowDelegate::layout(const QRect& item) const {
    Layout l; l.row = QRectF(item).adjusted(0, 0, 0, -2); l.compact = item.width() < 760; const double middle = l.row.center().y();
    l.check = QRectF(l.row.left() + 10, middle - 9, 18, 18); l.thumb = QRectF(l.check.right() + 14, middle - 36, 128, 72);
    l.play = QRectF(l.thumb.center().x() - 17, middle - 17, 34, 34);
    const double timeWidth = l.compact ? 96 : 150, sizeWidth = l.compact ? 124 : 150;
    l.time = QRectF(l.row.right() - 14 - timeWidth, l.row.top(), timeWidth, l.row.height()); l.size = QRectF(l.time.left() - 14 - sizeWidth, l.row.top(), sizeWidth, l.row.height());
    l.text = QRectF(l.thumb.right() + 14, l.row.top(), std::max(0.0, l.size.left() - 14 - l.thumb.right() - 14), l.row.height());
    l.more = QRectF(l.time.right() - 28, middle - 14, 28, 28); l.share = QRectF(l.more.left() - 32, middle - 14, 28, 28);
    if (!l.compact) { const double width = rowButtonWidth(QStringLiteral("Éditer")); l.edit = QRectF(l.share.left() - 4 - width, middle - 14, width, 28); }
    return l;
}
QSize LibraryRowDelegate::sizeHint(const QStyleOptionViewItem&, const QModelIndex& index) const { return {200, index.data(ClipRole::Header).toBool() ? 40 : m_view->viewport()->width() < 760 ? 82 : 86}; }
QRect LibraryRowDelegate::nameRect(const QRect& row, const QModelIndex&) const { const auto l = layout(row); return QRectF(l.text.left() - 6, l.row.center().y() - 26, std::min(l.text.width(), 420.0), 30).toRect(); }
ClipRowDelegate::Hit LibraryRowDelegate::hitTest(const QRect& row, const QModelIndex& index, const QPoint& position) const {
    if (index.data(ClipRole::Header).toBool()) return Nothing;
    const auto l = layout(row); const QPointF point(position);
    if (l.check.adjusted(-8, -14, 8, 14).contains(point)) return Check;
    if (l.play.contains(point)) return Play;
    if (l.more.contains(point)) return More;
    if (l.share.contains(point)) return Share;
    if (!l.compact && l.edit.contains(point)) return Edit;
    return Nothing;
}
void LibraryRowDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const {
    painter->save(); painter->setRenderHint(QPainter::Antialiasing);
    if (index.data(ClipRole::Header).toBool()) {
        const auto font = uiFont(12, QFont::Bold); const auto title = index.data(ClipRole::Name).toString(); const QRectF line(option.rect.left() + 10, option.rect.top() + 16, option.rect.width() - 20, 18);
        painter->setFont(font); painter->setPen(tk::text2); painter->drawText(line, Qt::AlignVCenter | Qt::AlignLeft, title);
        painter->setFont(uiFont(12, QFont::DemiBold)); painter->setPen(tk::text3); painter->drawText(line.adjusted(QFontMetrics(font).horizontalAdvance(title) + 8, 0, 0, 0), Qt::AlignVCenter | Qt::AlignLeft, index.data(ClipRole::Detail).toString());
        painter->restore(); return;
    }
    const auto l = layout(option.rect); const bool selected = option.state & QStyle::State_Selected, hovered = option.state & QStyle::State_MouseOver;
    paintBackground(painter, l.row, option);
    if (selected || hovered || m_view->selectionModel()->hasSelection()) Toggle::paintBox(*painter, l.check, selected);
    const bool known = paintThumbnail(painter, l.thumb, index, 6, true); const auto key = index.data(ClipRole::Key).toString(); const auto* meta = m_cache->meta(key);
    const double middle = l.row.center().y();
    // On a hovered row the miniature offers to play the clip full screen. Nothing is decoded before the click.
    if (hovered && !(meta && meta->gaveUp())) {
        const bool over = l.play.contains(QPointF(m_view->hoverPosition()));
        painter->setPen(Qt::NoPen); auto disc = tk::window; disc.setAlpha(214); painter->setBrush(over ? accentBrush(l.play) : QBrush(disc)); painter->drawEllipse(l.play);
        // A picture is not played: it is seen larger.
        if (m_images) paintIcon(*painter, QStringLiteral("expand"), QRectF(l.play.center().x() - 8, l.play.center().y() - 8, 16, 16), over ? tk::onAccent : tk::text);
        else paintIcon(*painter, QStringLiteral("play"), QRectF(l.play.center().x() - 7, l.play.center().y() - 8, 16, 16), over ? tk::onAccent : tk::text);
    }
    // Name, favourite star, "Nouveau", then one line of facts.
    const auto nameFont = uiFont(14, QFont::DemiBold); const QFontMetrics nameMetrics(nameFont); const bool favorite = index.data(ClipRole::Favorite).toBool(), fresh = index.data(ClipRole::Fresh).toBool();
    const double extras = (favorite ? 22 : 0) + (fresh ? Tag::measure(QStringLiteral("Nouveau")).width() + 8 : 0);
    const auto name = nameMetrics.elidedText(index.data(ClipRole::Name).toString(), Qt::ElideMiddle, int(std::max(0.0, l.text.width() - extras)));
    painter->setFont(nameFont); painter->setPen(tk::text); painter->drawText(QRectF(l.text.left(), middle - 21, l.text.width(), 20), Qt::AlignVCenter | Qt::AlignLeft, name);
    double x = l.text.left() + nameMetrics.horizontalAdvance(name) + 8;
    if (favorite) { paintIcon(*painter, QStringLiteral("starfill"), QRectF(x, middle - 18, 14, 14), tk::warn); x += 22; }
    if (fresh) { const auto size = Tag::measure(QStringLiteral("Nouveau")); Tag::paint(*painter, QRectF(x, middle - 21, size.width(), 20), QStringLiteral("Nouveau"), Tag::Info); }
    const QRectF metaLine(l.text.left(), middle + 3, l.text.width(), 18); painter->setFont(uiFont(12));
    if (meta && meta->gaveUp()) { painter->setPen(tk::danger); painter->drawText(metaLine, Qt::AlignVCenter | Qt::AlignLeft, QStringLiteral("Impossible de lire ce fichier")); }
    else {
        QStringList facts; if (m_showGame) facts.append(index.data(ClipRole::Game).toString());
        if (meta && m_images) facts << QStringLiteral("%1 × %2").arg(meta->size.width()).arg(meta->size.height());
        else if (meta) { facts << QStringLiteral("%1p").arg(meta->size.height()) << QStringLiteral("%1 FPS").arg(qRound(meta->fps)) << (meta->tracks == 0 ? QStringLiteral("sans son") : meta->tracks == 1 ? QStringLiteral("1 piste") : QStringLiteral("%1 pistes").arg(meta->tracks)); }
        painter->setPen(tk::text2); const auto text = facts.join(QStringLiteral(" · ")); painter->drawText(metaLine, Qt::AlignVCenter | Qt::AlignLeft, QFontMetrics(painter->font()).elidedText(text, Qt::ElideRight, int(l.text.width())));
        if (!known) { painter->setPen(Qt::NoPen); painter->setBrush(tk::raised); const double after = l.text.left() + (text.isEmpty() ? 0 : QFontMetrics(painter->font()).horizontalAdvance(text) + 10); painter->drawRoundedRect(QRectF(after, middle + 7, std::min(120.0, std::max(0.0, l.text.right() - after)), 10), 4, 4); }
    }
    painter->setFont(monoFont(12)); painter->setPen(tk::text); painter->drawText(QRectF(l.size.left(), middle - 20, l.size.width(), 16), Qt::AlignVCenter | Qt::AlignLeft, formatBytes(index.data(ClipRole::Bytes).toLongLong()));
    if (const auto fit = index.data(ClipRole::FitText).toString(); !fit.isEmpty()) {
        const auto fitSize = Tag::measure(fit);
        Tag::paint(*painter, QRectF(l.size.left(), middle, std::min<double>(fitSize.width(), l.size.width()), 20), fit, index.data(ClipRole::FitOk).toBool() ? Tag::Ok : Tag::Neutral);
    }
    if (hovered) {
        const QPointF pointer(m_view->hoverPosition());
        if (!l.compact) paintRowButton(painter, l.edit, m_images ? QStringLiteral("crop") : QStringLiteral("cut"), QStringLiteral("Éditer"), l.edit.contains(pointer));
        paintRowButton(painter, l.share, m_images ? QStringLiteral("copy") : QStringLiteral("share"), {}, l.share.contains(pointer)); paintRowButton(painter, l.more, QStringLiteral("more"), {}, l.more.contains(pointer));
    } else {
        painter->setFont(monoFont(12)); painter->setPen(tk::text2);
        painter->drawText(l.time, Qt::AlignVCenter | Qt::AlignRight, index.data(ClipRole::Detail).toString());
    }
    painter->restore();
}


ClipListView::ClipListView(QWidget* parent) : QListView(parent), m_model(new ClipListModel(this)) {
    setModel(m_model); setFrameShape(QFrame::NoFrame); setSelectionMode(QAbstractItemView::ExtendedSelection); setEditTriggers(QAbstractItemView::NoEditTriggers);
    setVerticalScrollMode(QAbstractItemView::ScrollPerPixel); setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff); verticalScrollBar()->setSingleStep(28);
    setMouseTracking(true); viewport()->setAttribute(Qt::WA_Hover); setUniformItemSizes(false); setResizeMode(QListView::Adjust);
    setDragEnabled(true); setDragDropMode(QAbstractItemView::DragOnly); setDefaultDropAction(Qt::CopyAction); setAcceptDrops(false); setDropIndicatorShown(false);
    connect(verticalScrollBar(), &QScrollBar::valueChanged, this, [this] { finishRename(true); });
    connect(selectionModel(), &QItemSelectionModel::selectionChanged, this, [this] { viewport()->update(); });
}
void ClipListView::setRowDelegate(ClipRowDelegate* delegate) { m_rows = delegate; setItemDelegate(delegate); }
int ClipListView::rowOf(const QString& path) const { for (int row = 0; row < m_model->rowCount(); ++row) if (m_model->index(row, 0).data(ClipRole::Path).toString() == path) return row; return -1; }
QStringList ClipListView::selectedPaths() const {
    auto rows = selectionModel()->selectedRows(); std::sort(rows.begin(), rows.end(), [](const auto& left, const auto& right) { return left.row() < right.row(); });
    QStringList paths; for (const auto& index : rows) if (!isRowHidden(index.row()) && !index.data(ClipRole::Header).toBool()) paths.append(pathAt(index));
    return paths;
}
void ClipListView::selectPaths(const QStringList& paths) {
    const QSet<QString> wanted(paths.begin(), paths.end()); QItemSelection selection;
    for (int row = 0; row < m_model->rowCount(); ++row) { const auto index = m_model->index(row, 0); if (wanted.contains(pathAt(index))) selection.select(index, index); }
    selectionModel()->select(selection, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
}
QMimeData* ClipListView::dragPayload() const {
    const auto urls = existingFileUrls(selectedPaths()); if (urls.isEmpty()) return nullptr;
    auto* data = new QMimeData; data->setUrls(urls); return data;
}
ClipRowDelegate::Hit ClipListView::hitAt(const QPoint& position, QModelIndex* found) const {
    const auto index = indexAt(position); if (found) *found = index;
    return index.isValid() && m_rows ? m_rows->hitTest(visualRect(index), index, position) : ClipRowDelegate::Nothing;
}
void ClipListView::mousePressEvent(QMouseEvent* event) {
    m_keyboard = false; finishRename(true);
    QModelIndex index; const auto hit = event->button() == Qt::LeftButton ? hitAt(event->position().toPoint(), &index) : ClipRowDelegate::Nothing;
    if (hit == ClipRowDelegate::Nothing) { m_pressed = hit; QListView::mousePressEvent(event); return; }
    m_pressed = hit; m_pressedIndex = index; setFocus(Qt::MouseFocusReason);
    // The play mark sits on the miniature, where a drag towards Discord often starts:
    // the press selects and may drag as anywhere else on the row; only a plain click plays.
    if (hit == ClipRowDelegate::Play) { QListView::mousePressEvent(event); return; }
    if (hit == ClipRowDelegate::Check) { selectionModel()->select(index, QItemSelectionModel::Toggle | QItemSelectionModel::Rows); selectionModel()->setCurrentIndex(index, QItemSelectionModel::NoUpdate); }
    event->accept();
}
void ClipListView::mouseReleaseEvent(QMouseEvent* event) {
    if (m_pressed == ClipRowDelegate::Nothing) { QListView::mouseReleaseEvent(event); return; }
    QModelIndex index; const auto hit = hitAt(event->position().toPoint(), &index); const auto pressed = m_pressed; m_pressed = ClipRowDelegate::Nothing;
    if (pressed == ClipRowDelegate::Play) { QListView::mouseReleaseEvent(event); if (event->modifiers() & (Qt::ControlModifier | Qt::ShiftModifier)) return; }
    if (hit != pressed || index != m_pressedIndex || pressed == ClipRowDelegate::Check) return;
    const auto path = pathAt(index);
    if (pressed == ClipRowDelegate::Edit) emit openRequested(path); else emit actionRequested(pressed, path, event->globalPosition().toPoint());
}
void ClipListView::mouseMoveEvent(QMouseEvent* event) {
    const auto before = indexAt(m_hover); m_hover = event->position().toPoint(); const auto after = indexAt(m_hover);
    if (before.isValid()) update(before);
    if (after.isValid()) update(after);
    if (m_pressed == ClipRowDelegate::Nothing || m_pressed == ClipRowDelegate::Play) QListView::mouseMoveEvent(event);
}
void ClipListView::leaveEvent(QEvent* event) { const auto before = indexAt(m_hover); m_hover = {-1, -1}; if (before.isValid()) update(before); QListView::leaveEvent(event); }
void ClipListView::mouseDoubleClickEvent(QMouseEvent* event) {
    QModelIndex index; const auto hit = hitAt(event->position().toPoint(), &index);
    if (event->button() == Qt::LeftButton && index.isValid() && hit == ClipRowDelegate::Nothing && !index.data(ClipRole::Header).toBool()) emit openRequested(pathAt(index));
}
void ClipListView::keyPressEvent(QKeyEvent* event) {
    m_keyboard = true;
    if ((event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) && currentIndex().isValid() && selectionModel()->isSelected(currentIndex())) { emit openRequested(pathAt(currentIndex())); return; }
    QListView::keyPressEvent(event); viewport()->update();
}
void ClipListView::contextMenuEvent(QContextMenuEvent* event) {
    const auto index = indexAt(event->pos()); if (!index.isValid() || index.data(ClipRole::Header).toBool()) return;
    if (!selectionModel()->isSelected(index)) { selectionModel()->select(index, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows); selectionModel()->setCurrentIndex(index, QItemSelectionModel::NoUpdate); }
    emit menuRequested(pathAt(index), event->globalPos());
}
void ClipListView::startDrag(Qt::DropActions supportedActions) {
    if (!dragEnabled() || !(supportedActions & Qt::CopyAction)) return;
    const auto requested = selectedPaths(); const auto urls = existingFileUrls(requested);
    if (urls.isEmpty()) { emit fileDragUnavailable(); return; }
    emit fileDragStarted(int(urls.size()), std::max(0, int(requested.size() - urls.size())));
    startFileDrag(this, urls);
}
void ClipListView::beginRename(const QString& path) {
    finishRename(false); const int row = rowOf(path); if (row < 0 || !m_rows) return;
    const auto index = m_model->index(row, 0); scrollTo(index); m_editing = path;
    m_editor = new QLineEdit(index.data(ClipRole::Name).toString(), viewport()); m_editor->setObjectName("clip-rename"); m_editor->setMaxLength(120); m_editor->setFont(uiFont(14, QFont::DemiBold));
    m_editor->setGeometry(m_rows->nameRect(visualRect(index), index)); m_editor->installEventFilter(this); m_editor->show(); m_editor->selectAll(); m_editor->setFocus(Qt::OtherFocusReason);
    connect(m_editor, &QLineEdit::returnPressed, this, [this] { finishRename(true); });
}
void ClipListView::finishRename(bool commit) {
    if (!m_editor) return;
    auto* editor = m_editor; m_editor = nullptr; const auto path = m_editing, name = editor->text().trimmed(); m_editing.clear();
    editor->removeEventFilter(this); editor->hide(); editor->deleteLater(); setFocus(Qt::OtherFocusReason);
    if (commit && !name.isEmpty() && name != QFileInfo(path).completeBaseName()) emit renameRequested(path, name);
}
bool ClipListView::eventFilter(QObject* watched, QEvent* event) {
    if (watched == m_editor) {
        if (event->type() == QEvent::KeyPress && static_cast<QKeyEvent*>(event)->key() == Qt::Key_Escape) { finishRename(false); return true; }
        if (event->type() == QEvent::FocusOut) { finishRename(true); return false; }
    }
    return QListView::eventFilter(watched, event);
}
}
