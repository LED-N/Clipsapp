#include "whats_new.h"
#include <QComboBox>
#include <QDesktopServices>
#include <QFile>
#include <QHBoxLayout>
#include <QPainter>
#include <QRegularExpression>
#include <QScrollArea>
#include <QScrollBar>
#include <QUrl>
#include <QVBoxLayout>
#include <algorithm>
namespace clipsapp {
QString inlineMarkdown(const QString& text) {
    QString html = text.toHtmlEscaped();
    // French spacing: a colon or a guillemet never starts or ends a line alone.
    for (const auto& [from, to] : std::initializer_list<std::pair<const char*, const char*>>{{"« ", "«&nbsp;"}, {" »", "&nbsp;»"}, {" :", "&nbsp;:"}, {" ;", "&nbsp;;"}, {" ?", "&nbsp;?"}, {" !", "&nbsp;!"}})
        html.replace(QString::fromUtf8(from), QString::fromUtf8(to));
    static const QRegularExpression code(QStringLiteral("`([^`]+)`")), strong(QStringLiteral("\\*\\*(.+?)\\*\\*")), link(QStringLiteral("\\[([^\\]]+)\\]\\(([^)\\s]+)\\)"));
    html.replace(code, QStringLiteral("<span style=\"font-family: Consolas, monospace; background-color: #1F242D; color: #E9EDF3;\">&nbsp;\\1&nbsp;</span>"));
    html.replace(strong, QStringLiteral("<b style=\"color: #E9EDF3;\">\\1</b>"));
    html.replace(link, QStringLiteral("<a style=\"color: #6FE3B4; text-decoration: none;\" href=\"\\2\">\\1</a>"));
    return html;
}
QList<NoteSection> parseReleaseNotes(const QString& markdown) {
    QList<NoteSection> sections; NoteSection current; bool inItem = false;
    static const QRegularExpression version(QStringLiteral("^v?\\d+(\\.\\d+)*$")), aloneBold(QStringLiteral("^\\*\\*([^*]+)\\*\\*:?$")), lead(QStringLiteral("^\\*\\*([^*]+)\\*\\*\\s*:\\s*(.*)$"));
    const auto close = [&] { if (!current.title.isEmpty() || !current.paragraphs.isEmpty() || !current.items.isEmpty()) sections << current; current = {}; inItem = false; };
    const auto open = [&](const QString& title) { close(); current.title = title; };
    for (const auto& raw : markdown.split('\n')) {
        const auto line = QString(raw).remove('\r'); const auto trimmed = line.trimmed();
        if (trimmed.isEmpty()) { inItem = false; continue; }
        if (trimmed.startsWith('#')) {
            // The heading of the release itself ("## 0.8.3") says nothing the dialog's title does not.
            const auto title = trimmed.mid(trimmed.indexOf(QRegularExpression(QStringLiteral("[^#]")))).trimmed();
            if (!version.match(title).hasMatch() && !title.startsWith(QStringLiteral("Clipsapp"))) open(title);
            continue;
        }
        if (const auto bold = aloneBold.match(trimmed); bold.hasMatch()) { open(bold.captured(1).trimmed()); continue; }
        const int indent = int(line.indexOf(QRegularExpression(QStringLiteral("\\S"))));
        if (trimmed.startsWith(QStringLiteral("- ")) || trimmed.startsWith(QStringLiteral("* "))) {
            const auto text = trimmed.mid(2).trimmed();
            if (indent >= 2 && !current.items.isEmpty()) { current.items.last().details << text; continue; }
            NoteItem item;
            if (const auto match = lead.match(text); match.hasMatch() && !match.captured(2).isEmpty()) {
                item.lead = match.captured(1).trimmed(); item.text = match.captured(2).trimmed(); item.afterLead = item.text;
                if (!item.text.isEmpty()) item.text[0] = item.text[0].toUpper();
            } else item.text = text;
            static const QRegularExpression place(QStringLiteral("\\s*(?:\\*\\*)?(Réglages\\s*→[^*.]*)(?:\\*\\*)?\\.?\\s*$"));
            if (const auto found = place.match(item.text); found.hasMatch() && found.capturedStart() > 0) { item.where = found.captured(1).trimmed(); item.text = item.text.left(found.capturedStart()).trimmed(); }
            if (const auto found = place.match(item.afterLead); found.hasMatch() && found.capturedStart() > 0) item.afterLead = item.afterLead.left(found.capturedStart()).trimmed();
            current.items << item; inItem = true; continue;
        }
        // A line that follows an item without a blank line continues it.
        if (inItem && !current.items.isEmpty()) {
            auto& item = current.items.last(); (item.details.isEmpty() ? item.text : item.details.last()) += ' ' + trimmed;
            if (item.details.isEmpty() && !item.afterLead.isEmpty()) item.afterLead += ' ' + trimmed;
            continue;
        }
        current.paragraphs << trimmed;
    }
    close(); return sections;
}
QList<ReleaseNotes> changelogReleases(const QString& markdown) {
    QList<ReleaseNotes> releases; static const QRegularExpression heading(QStringLiteral("^##\\s+v?(\\d+(?:\\.\\d+)*)\\s*$"));
    for (const auto& raw : markdown.split('\n')) {
        const auto line = QString(raw).remove('\r');
        if (const auto match = heading.match(line.trimmed()); match.hasMatch()) { releases.append({match.captured(1), {}}); continue; }
        if (!releases.isEmpty()) releases.last().notes += line + '\n';
    }
    for (auto& release : releases) release.notes = release.notes.trimmed();
    return releases;
}
QList<ReleaseNotes> bundledChangelog() {
    QFile file(QStringLiteral(":/CHANGELOG.md")); if (!file.open(QIODevice::ReadOnly)) return {};
    return changelogReleases(QString::fromUtf8(file.readAll()));
}

namespace {
struct Look { const char* icon; QColor color, background; };
Look lookOf(const QString& title) {
    const auto lower = title.toLower();
    if (lower.contains(QStringLiteral("nouveaut"))) return {"spark", tk::accent, tk::accentSoft};
    if (lower.contains(QStringLiteral("amélior"))) return {"up", tk::info, tk::infoSoft};
    if (lower.contains(QStringLiteral("correct")) || lower.contains(QStringLiteral("retrait"))) return {"check", tk::text2, tk::raised};
    return {"info", tk::text2, tk::raised};
}
QLabel* richLabel(const QString& html, const char* role) {
    auto* text = label({}, role, true); text->setTextFormat(Qt::RichText); text->setText(html); text->setOpenExternalLinks(true);
    text->setTextInteractionFlags(Qt::LinksAccessibleByMouse); text->setAlignment(Qt::AlignTop | Qt::AlignLeft); return text;
}
class Dot final : public QWidget {
public:
    explicit Dot(const QColor& color) : m_color(color) { setFixedSize(14, 20); }
protected:
    void paintEvent(QPaintEvent*) override { QPainter painter(this); painter.setRenderHint(QPainter::Antialiasing); painter.setPen(Qt::NoPen); painter.setBrush(m_color); painter.drawEllipse(QPointF(5, 10), 3, 3); }
private:
    QColor m_color;
};
constexpr int dialogWidth = 620;
// The icon of a new feature, from its name.
QString featureIcon(const QString& lead) {
    const auto name = lead.toLower();
    if (name.contains(QStringLiteral("enregistr"))) return QStringLiteral("rec");
    if (name.contains(QStringLiteral("thème"))) return QStringLiteral("star");
    if (name.contains(QStringLiteral("screenshot"))) return QStringLiteral("camera");
    if (name.contains(QStringLiteral("buffer")) || name.contains(QStringLiteral("disque"))) return QStringLiteral("save");
    if (name.contains(QStringLiteral("volume")) || name.contains(QStringLiteral("son"))) return QStringLiteral("speaker");
    return QStringLiteral("spark");
}
// A new feature stands out: a card of its own, its name large, a sentence or two, and where to find it.
QWidget* featureCard(const NoteItem& item) {
    auto* card = new QFrame; card->setObjectName("whats-new-feature");
    card->setStyleSheet(QStringLiteral("QFrame#whats-new-feature { background: %1; border: 1px solid %2; border-radius: 10px; }").arg(tk::mix(tk::surface, tk::accent, 0.05).name(), tk::mix(tk::line, tk::accent, 0.35).name()));
    auto* row = new QHBoxLayout(card); row->setContentsMargins(14, 14, 16, 14); row->setSpacing(14);
    row->addWidget(new IconBox(featureIcon(item.lead), tk::accent, tk::accentSoft, 36), 0, Qt::AlignTop);
    auto* texts = new QVBoxLayout; texts->setSpacing(4);
    auto* title = richLabel(inlineMarkdown(item.lead), nullptr); title->setFont(uiFont(16, QFont::DemiBold)); texts->addWidget(title);
    auto* text = richLabel(inlineMarkdown(item.text), "t2"); texts->addWidget(text);
    for (const auto& detail : item.details) texts->addWidget(richLabel(QStringLiteral("– ") + inlineMarkdown(detail), "t2"));
    if (!item.where.isEmpty()) {
        auto* place = new QLabel(item.where); place->setObjectName("whats-new-where"); place->setFont(uiFont(12, QFont::DemiBold));
        place->setStyleSheet(QStringLiteral("QLabel { background: %1; color: %2; border-radius: 5px; padding: 3px 8px; }").arg(tk::chip.name(), tk::text2.name()));
        auto* line = new QHBoxLayout; line->setContentsMargins(0, 6, 0, 0); line->addWidget(place); line->addStretch(); texts->addLayout(line);
    }
    row->addLayout(texts, 1); return card;
}
QWidget* notesView(const QString& markdown) {
    auto* content = new QWidget; content->setObjectName("whats-new-notes"); auto* layout = new QVBoxLayout(content); layout->setContentsMargins(22, 4, 22, 20); layout->setSpacing(0);
    const auto sections = parseReleaseNotes(markdown); bool first = true;
    for (const auto& section : sections) {
        if (!first) { layout->addSpacing(18); layout->addWidget(separator()); layout->addSpacing(18); } first = false;
        const auto look = lookOf(section.title); const bool features = QLatin1String(look.icon) == QLatin1String("spark");
        if (!section.title.isEmpty()) {
            auto* heading = new QHBoxLayout; heading->setSpacing(10);
            heading->addWidget(new IconBox(QString::fromLatin1(look.icon), look.color, look.background, 26)); auto* title = label(section.title); title->setFont(uiFont(15, QFont::DemiBold)); heading->addWidget(title); heading->addStretch();
            layout->addLayout(heading); layout->addSpacing(14);
        }
        for (const auto& paragraph : section.paragraphs) { layout->addWidget(richLabel(inlineMarkdown(paragraph), "t2")); layout->addSpacing(10); }
        for (int index = 0; index < section.items.size(); ++index) {
            const auto& item = section.items[index];
            // The new features in cards; improvements and corrections in a short list, one line each.
            if (features && !item.lead.isEmpty()) { layout->addWidget(featureCard(item)); if (index + 1 < section.items.size()) layout->addSpacing(10); continue; }
            if (!features) {
                auto* row = new QHBoxLayout; row->setSpacing(8); row->setContentsMargins(4, 0, 0, 0); row->addWidget(new Dot(look.color), 0, Qt::AlignTop);
                QString html = item.lead.isEmpty() ? inlineMarkdown(item.text) : QStringLiteral("<b style=\"color: %1;\">%2</b>&nbsp;: %3").arg(tk::text.name(), inlineMarkdown(item.lead), inlineMarkdown(item.afterLead.isEmpty() ? item.text : item.afterLead));
                for (const auto& detail : item.details) html += QStringLiteral("<br>– ") + inlineMarkdown(detail);
                auto* text = richLabel(html, "t2");
                row->addWidget(text, 1); layout->addLayout(row);
                if (index + 1 < section.items.size()) layout->addSpacing(8);
                continue;
            }
            auto* row = new QHBoxLayout; row->setSpacing(8); row->setContentsMargins(4, 0, 0, 0); row->addWidget(new Dot(look.color), 0, Qt::AlignTop);
            auto* texts = new QVBoxLayout; texts->setSpacing(3);
            if (!item.lead.isEmpty()) { auto* lead = richLabel(inlineMarkdown(item.lead), nullptr); lead->setFont(uiFont(14, QFont::DemiBold)); texts->addWidget(lead); texts->addWidget(richLabel(inlineMarkdown(item.text), "t2")); }
            else texts->addWidget(richLabel(inlineMarkdown(item.text), nullptr));
            for (const auto& detail : item.details) {
                auto* line = new QHBoxLayout; line->setSpacing(6); line->setContentsMargins(0, 4, 0, 0);
                auto* dash = label(QStringLiteral("–"), "t3"); dash->setAlignment(Qt::AlignTop); line->addWidget(dash, 0, Qt::AlignTop); line->addWidget(richLabel(inlineMarkdown(detail), "t2"), 1); texts->addLayout(line);
            }
            row->addLayout(texts, 1); layout->addLayout(row);
            if (index + 1 < section.items.size()) layout->addSpacing(14);
        }
    }
    layout->addStretch(); return content;
}
}

WhatsNewDialog::WhatsNewDialog(QWidget* parent, const QList<ReleaseNotes>& releases, int shown, const QString& installedVersion, const QString& previousVersion, const QString& repository)
    : ModalDialog(parent, dialogWidth), m_releases(releases), m_installed(installedVersion), m_previous(previousVersion), m_repository(repository) {
    setObjectName("whats-new");
    setHeading(QString(), QString(), new IconBox(QStringLiteral("spark"), tk::accent, tk::accentSoft, 36));
    if (m_previous.isEmpty() && m_releases.size() > 1) {
        // The journal: one version at a time, the others in a list, newest first.
        auto* row = new QHBoxLayout; row->setSpacing(10); row->addWidget(label(QStringLiteral("Version"), "t2"));
        m_versions = new ChoiceBox; m_versions->setObjectName("whats-new-versions"); m_versions->setMinimumWidth(170);
        for (const auto& release : m_releases) m_versions->addItem(release.version == m_installed ? QStringLiteral("%1 · installée").arg(release.version) : release.version);
        row->addWidget(m_versions); row->addStretch(); headerLayout()->addLayout(row);
        connect(m_versions, &QComboBox::currentIndexChanged, this, [this](int index) { showRelease(index); });
    }
    body()->setContentsMargins(0, 8, 0, 0);
    // The notes scroll inside the card; the heading and the buttons stay in view.
    m_scroll = new QScrollArea; m_scroll->setObjectName("whats-new-scroll"); m_scroll->setWidgetResizable(true); m_scroll->setFrameShape(QFrame::NoFrame); m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scroll->setStyleSheet(QStringLiteral("QScrollArea, QScrollArea > QWidget > QWidget { background: transparent; }")); body()->addWidget(m_scroll);
    m_page = new Button(QStringLiteral("open"), QStringLiteral("Voir sur GitHub"), Button::Ghost); m_page->setObjectName("whats-new-page"); footerLeft()->addWidget(m_page);
    auto* done = new Button(m_previous.isEmpty() ? QStringLiteral("Fermer") : QStringLiteral("C'est parti"), Button::Primary); done->setObjectName("whats-new-close"); footer()->addWidget(done);
    connect(m_page, &QPushButton::clicked, this, [this] { if (m_shown >= 0) QDesktopServices::openUrl(QUrl(QStringLiteral("https://github.com/%1/releases/tag/v%2").arg(m_repository, m_releases[m_shown].version))); });
    connect(done, &QPushButton::clicked, this, &QDialog::accept);
    showRelease(std::clamp(shown, 0, int(m_releases.size()) - 1));
    // The journal keeps its height from one version to the next: only the notes move.
    const int room = parent ? parent->window()->height() - (m_versions ? 280 : 230) : 560, tallest = std::max(160, std::min(room, 600));
    if (m_versions) m_scroll->setFixedHeight(tallest);
    else { auto* content = m_scroll->widget(); content->setFixedWidth(dialogWidth - 2); const int wanted = content->sizeHint().height(); content->setMinimumWidth(0); content->setMaximumWidth(QWIDGETSIZE_MAX); m_scroll->setFixedHeight(std::clamp(wanted, 160, tallest)); }
    done->setDefault(true); done->setFocus(); adjustSize();
}
void WhatsNewDialog::showRelease(int index) {
    if (index < 0 || index >= m_releases.size()) return;
    m_shown = index; const auto& release = m_releases[index];
    QString subtitle;
    if (!m_previous.isEmpty()) subtitle = QStringLiteral("Mise à jour installée, à la place de la version %1.").arg(m_previous);
    else if (release.version == m_installed) subtitle = QStringLiteral("C'est la version que vous utilisez.");
    else subtitle = QStringLiteral("Une version précédente. Vous utilisez la %1.").arg(m_installed);
    setHeadingTexts(QStringLiteral("Quoi de neuf dans Clipsapp %1").arg(release.version), subtitle);
    if (m_versions && m_versions->currentIndex() != index) { const QSignalBlocker quiet(m_versions); m_versions->setCurrentIndex(index); }
    m_scroll->setWidget(notesView(release.notes)); m_scroll->verticalScrollBar()->setValue(0);
    m_page->setVisible(!m_repository.isEmpty());
}
}
