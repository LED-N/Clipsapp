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
                item.lead = match.captured(1).trimmed(); item.text = match.captured(2).trimmed();
                if (!item.text.isEmpty()) item.text[0] = item.text[0].toUpper();
            } else item.text = text;
            current.items << item; inItem = true; continue;
        }
        // A line that follows an item without a blank line continues it.
        if (inItem && !current.items.isEmpty()) { auto& item = current.items.last(); (item.details.isEmpty() ? item.text : item.details.last()) += ' ' + trimmed; continue; }
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
QWidget* notesView(const QString& markdown) {
    auto* content = new QWidget; content->setObjectName("whats-new-notes"); auto* layout = new QVBoxLayout(content); layout->setContentsMargins(22, 4, 22, 20); layout->setSpacing(0);
    const auto sections = parseReleaseNotes(markdown); bool first = true;
    for (const auto& section : sections) {
        if (!first) { layout->addSpacing(18); layout->addWidget(separator()); layout->addSpacing(18); } first = false;
        const auto look = lookOf(section.title);
        if (!section.title.isEmpty()) {
            auto* heading = new QHBoxLayout; heading->setSpacing(10);
            heading->addWidget(new IconBox(QString::fromLatin1(look.icon), look.color, look.background, 26)); auto* title = label(section.title); title->setFont(uiFont(15, QFont::DemiBold)); heading->addWidget(title); heading->addStretch();
            layout->addLayout(heading); layout->addSpacing(14);
        }
        for (const auto& paragraph : section.paragraphs) { layout->addWidget(richLabel(inlineMarkdown(paragraph), "t2")); layout->addSpacing(10); }
        for (int index = 0; index < section.items.size(); ++index) {
            const auto& item = section.items[index];
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
