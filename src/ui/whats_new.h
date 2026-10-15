#pragma once
#include "widgets.h"
#include <QList>
#include <QStringList>
class QComboBox;
class QScrollArea;
namespace clipsapp {
// The notes of a release, as the changelog writes them: sections in bold or as headings, items as bullets.
struct NoteItem {
    // "**Screenshots** : text" gives a lead and a text; an item without such a lead has its text only.
    QString lead, text;
    QStringList details;
};
struct NoteSection { QString title; QStringList paragraphs; QList<NoteItem> items; };
QList<NoteSection> parseReleaseNotes(const QString& markdown);
// Inline markdown (bold, code, links) as the rich text of a label.
QString inlineMarkdown(const QString& text);

// A changelog cut into its versions, newest first: each "## 0.8.3" opens one.
struct ReleaseNotes { QString version, notes; };
QList<ReleaseNotes> changelogReleases(const QString& markdown);
// CHANGELOG.md, as built into the application.
QList<ReleaseNotes> bundledChangelog();

// What a version brings, in a readable form. Given the version it replaced, the dialog follows an update
// and shows that version alone; without it, it is the journal, open on one version with the others a choice away.
class WhatsNewDialog : public ModalDialog {
public:
    WhatsNewDialog(QWidget* parent, const QList<ReleaseNotes>& releases, int shown, const QString& installedVersion, const QString& previousVersion = {}, const QString& repository = {});
    int shown() const { return m_shown; }
    void showRelease(int index);
private:
    QList<ReleaseNotes> m_releases; QString m_installed, m_previous, m_repository; int m_shown = -1;
    QScrollArea* m_scroll = nullptr; QComboBox* m_versions = nullptr; QPushButton* m_page = nullptr;
};
}
