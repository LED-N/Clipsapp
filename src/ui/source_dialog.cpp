#include "source_dialog.h"
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QListWidget>
#include <QPainter>
#include <QSet>
#include <QStyledItemDelegate>
#include <QVBoxLayout>

namespace clipsapp {
namespace {
const SourceKind kinds[]{SourceKind::Microphone, SourceKind::Application, SourceKind::ApplicationMix, SourceKind::Output};
class ExclusionDelegate final : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    QSize sizeHint(const QStyleOptionViewItem&, const QModelIndex&) const override { return {200, 34}; }
    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override {
        painter->save(); const QRectF row(option.rect);
        if (index.row() > 0) painter->fillRect(QRectF(row.left(), row.top(), row.width(), 1), tk::lineSoft);
        Toggle::paintBox(*painter, QRectF(row.left() + 12, row.center().y() - 9, 18, 18), index.data(Qt::CheckStateRole).toInt() == Qt::Checked);
        painter->setFont(uiFont(13)); painter->setPen(tk::text); painter->drawText(row.adjusted(40, 0, -12, 0), Qt::AlignVCenter | Qt::AlignLeft, QFontMetrics(painter->font()).elidedText(index.data(Qt::DisplayRole).toString(), Qt::ElideMiddle, int(row.width() - 140)));
        const auto note = index.data(Qt::UserRole + 1).toString();
        if (!note.isEmpty()) { painter->setFont(uiFont(12)); painter->setPen(tk::text3); painter->drawText(row.adjusted(40, 0, -12, 0), Qt::AlignVCenter | Qt::AlignRight, note); }
        painter->restore();
    }
};
QWidget* chip(const QString& text) { auto* tag = new Tag(text, Tag::Neutral); tag->setIconName(QStringLiteral("lock")); return tag; }
}
SourceDialog::SourceDialog(const SourceCatalog& catalog, const RecordingProfile& profile, const AudioSource* existing, QWidget* parent)
    : ModalDialog(parent, 560), m_catalog(catalog), m_original(existing ? *existing : AudioSource{}), m_newTrackId(newId()) {
    if (m_original.id.isEmpty()) m_original.id = newId();
    setObjectName("source-dialog");
    setHeading(existing ? QStringLiteral("Modifier la source « %1 »").arg(existing->name) : QStringLiteral("Ajouter une source audio"), QStringLiteral("Ce que la source écoute, et la piste où elle est rangée."));
    body()->setSpacing(16);
    auto* listen = new QVBoxLayout; listen->setSpacing(8); listen->addWidget(label(QStringLiteral("Écouter"), "lbl"));
    auto* kindRow = new QHBoxLayout; kindRow->setSpacing(8);
    const QStringList titles{QStringLiteral("Un micro"), QStringLiteral("Une application"), QStringLiteral("Tout, sauf exclusions"), QStringLiteral("Une sortie audio")}; const QStringList icons{QStringLiteral("mic"), QStringLiteral("app"), QStringLiteral("apps"), QStringLiteral("phones")};
    for (int index = 0; index < 4; ++index) {
        auto* tile = new Tile(titles[index]); tile->setObjectName(QStringLiteral("source-kind-%1").arg(index)); tile->setIconName(icons[index]); tile->setFixedTileHeight(84); tile->setAutoExclusive(true); tile->setChecked(kinds[index] == m_original.kind);
        kindRow->addWidget(tile); m_kinds.append(tile); connect(tile, &QPushButton::clicked, this, [this] { populateTargets(); });
    }
    listen->addLayout(kindRow); body()->addLayout(listen);
    const auto group = [](const QString& title, QWidget* widget, QLabel** caption = nullptr) { auto* layout = new QVBoxLayout; layout->setSpacing(6); auto* text = label(title, "lbl"); if (caption) *caption = text; layout->addWidget(text); layout->addWidget(widget); return layout; };
    auto* grid = new QGridLayout; grid->setHorizontalSpacing(12); grid->setVerticalSpacing(12); grid->setColumnStretch(0, 1); grid->setColumnStretch(1, 1);
    m_name = new QLineEdit(m_original.name); m_name->setObjectName("source-name"); m_name->setPlaceholderText(QStringLiteral("Ex. Jeu, Micro, Discussion…")); grid->addLayout(group(QStringLiteral("Nom de la source"), m_name), 0, 0);
    m_track = new ChoiceBox; m_track->setObjectName("source-track");
    for (int index = 0; index < profile.tracks.size(); ++index) m_track->addItem(QIcon(iconPixmap(QStringLiteral("stop"), tk::track(index), 16, 2)), QStringLiteral("Piste %1 · %2").arg(index + 1).arg(profile.tracks[index].name), profile.tracks[index].id);
    m_track->addItem(icon(QStringLiteral("plus")), QStringLiteral("Nouvelle piste séparée"), m_newTrackId);
    m_track->setCurrentIndex(existing ? m_track->findData(existing->trackId) : m_track->count() - 1); grid->addLayout(group(QStringLiteral("Rangée dans la piste"), m_track), 0, 1);
    m_targetRow = new QWidget; auto* targetLayout = new QHBoxLayout(m_targetRow); targetLayout->setContentsMargins(0, 0, 0, 0); targetLayout->setSpacing(8);
    m_target = new ChoiceBox; m_target->setObjectName("source-target"); m_target->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon); m_target->setMinimumContentsLength(12);
    m_browse = new Button(QStringLiteral("Parcourir…")); targetLayout->addWidget(m_target, 1); targetLayout->addWidget(m_browse);
    auto* targetHost = new QWidget; auto* targetGroup = group(QStringLiteral("Périphérique"), m_targetRow, &m_targetLabel); targetGroup->setContentsMargins(0, 0, 0, 0); targetHost->setLayout(targetGroup); grid->addWidget(targetHost, 1, 0);
    m_newTrackRow = new QWidget; m_newTrack = new QLineEdit; m_newTrack->setObjectName("source-new-track"); m_newTrack->setPlaceholderText(QStringLiteral("Par défaut : le nom de la source"));
    auto* newTrackGroup = group(QStringLiteral("Nom de la nouvelle piste"), m_newTrack); newTrackGroup->setContentsMargins(0, 0, 0, 0); m_newTrackRow->setLayout(newTrackGroup); grid->addWidget(m_newTrackRow, 1, 1);
    m_targetRow = targetHost; body()->addLayout(grid);

    auto* panel = card("panel"); m_exclusionsPanel = panel; auto* exclusionLayout = new QVBoxLayout(panel); exclusionLayout->setContentsMargins(14, 14, 14, 14); exclusionLayout->setSpacing(10);
    exclusionLayout->addWidget(label(QStringLiteral("Exclusions"), "bold"));
    m_autoExclusions = new Toggle(QStringLiteral("Exclure les applications déjà écoutées par une autre source")); m_autoExclusions->setObjectName("exclude-separate-applications"); m_autoExclusions->setChecked(m_original.excludeSeparateApplications); exclusionLayout->addWidget(m_autoExclusions);
    auto* chips = new QWidget; auto* chipLayout = new QHBoxLayout(chips); chipLayout->setContentsMargins(46, 0, 0, 0); chipLayout->setSpacing(6); int shown = 0;
    for (const auto& source : profile.sources) if (source.enabled && source.kind == SourceKind::Application && source.id != m_original.id && !source.targetId.isEmpty() && shown++ < 4) chipLayout->addWidget(chip(QFileInfo(source.targetId).fileName()));
    chipLayout->addStretch(); chips->setVisible(shown > 0 && m_autoExclusions->isChecked()); exclusionLayout->addWidget(chips);
    connect(m_autoExclusions, &QCheckBox::toggled, chips, [chips, shown](bool on) { chips->setVisible(on && shown > 0); });
    exclusionLayout->addWidget(label(QStringLiteral("Exclure aussi"), "lbl"));
    m_exclusions = new QListWidget; m_exclusions->setObjectName("application-exclusions"); m_exclusions->setItemDelegate(new ExclusionDelegate(m_exclusions)); m_exclusions->setFixedHeight(104); m_exclusions->setSelectionMode(QAbstractItemView::NoSelection); m_exclusions->setFocusPolicy(Qt::NoFocus);
    m_exclusions->setStyleSheet(QStringLiteral("QListWidget { background: #0F1217; border: 1px solid #242A34; border-radius: 8px; }")); exclusionLayout->addWidget(m_exclusions);
    QSet<QString> seen, active; for (const auto& target : m_catalog.applications) active.insert(QDir::fromNativeSeparators(target.id).toCaseFolded());
    const auto addExclusion = [this, &seen, &active](const QString& path, const QString& name, bool checkedByDefault = false) {
        const auto key = QDir::fromNativeSeparators(path).toCaseFolded(); if (path.isEmpty() || seen.contains(key)) return; seen.insert(key);
        auto* item = new QListWidgetItem(name.isEmpty() ? QFileInfo(path).fileName() : name, m_exclusions); item->setData(Qt::UserRole, path); item->setToolTip(path); item->setFlags(Qt::ItemIsEnabled);
        item->setData(Qt::UserRole + 1, active.contains(key) ? QStringLiteral("joue du son") : QStringLiteral("arrêtée"));
        bool checked = checkedByDefault; for (const auto& excluded : m_original.excludedApplications) if (QDir::fromNativeSeparators(excluded).compare(QDir::fromNativeSeparators(path), Qt::CaseInsensitive) == 0) checked = true;
        item->setCheckState(checked ? Qt::Checked : Qt::Unchecked);
    };
    for (const auto& target : m_catalog.applications) addExclusion(target.id, target.label);
    for (const auto& source : profile.sources) if (source.kind == SourceKind::Application) addExclusion(source.targetId, source.name + " · " + QFileInfo(source.targetId).fileName());
    for (const auto& path : m_original.excludedApplications) addExclusion(path, {});
    connect(m_exclusions, &QListWidget::itemClicked, this, [](QListWidgetItem* item) { item->setCheckState(item->checkState() == Qt::Checked ? Qt::Unchecked : Qt::Checked); });
    auto* browseExclusion = new Button(QStringLiteral("plus"), QStringLiteral("Choisir un programme…"), Button::Ghost, Button::Small); browseExclusion->setPadding(4); exclusionLayout->addWidget(browseExclusion, 0, Qt::AlignLeft);
    connect(browseExclusion, &QPushButton::clicked, this, [this] {
        const auto path = QFileDialog::getOpenFileName(this, QStringLiteral("Exclure une application"), {}, QStringLiteral("Applications Windows (*.exe)")); if (path.isEmpty()) return;
        for (int row = 0; row < m_exclusions->count(); ++row) if (m_exclusions->item(row)->data(Qt::UserRole).toString().compare(path, Qt::CaseInsensitive) == 0) { m_exclusions->item(row)->setCheckState(Qt::Checked); return; }
        auto* item = new QListWidgetItem(QFileInfo(path).fileName(), m_exclusions); item->setData(Qt::UserRole, path); item->setToolTip(path); item->setFlags(Qt::ItemIsEnabled); item->setCheckState(Qt::Checked); m_exclusions->scrollToItem(item);
    });
    body()->addWidget(panel);
    m_enabled = new Toggle(QStringLiteral("Inclure cette source dans les clips")); m_enabled->setObjectName("source-enabled"); m_enabled->setChecked(m_original.enabled); body()->addWidget(m_enabled);
    auto* noteRow = new QHBoxLayout; noteRow->setSpacing(8); noteRow->addWidget(new IconLabel(QStringLiteral("info"), tk::text2, 14), 0, Qt::AlignTop); m_note = label({}, "hint", true); noteRow->addWidget(m_note, 1); body()->addLayout(noteRow);
    m_error = label({}, "danger", true); m_error->setObjectName("source-error"); m_error->hide(); body()->addWidget(m_error);
    auto* cancel = new Button(QStringLiteral("Annuler")); auto* save = new Button(QStringLiteral("check"), QStringLiteral("Enregistrer la source"), Button::Primary); save->setObjectName("source-save"); save->setDefault(true);
    footer()->addWidget(cancel); footer()->addWidget(save);
    connect(save, &QPushButton::clicked, this, [this] { validateAndAccept(); }); connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(m_track, &QComboBox::currentIndexChanged, this, [this] { m_newTrackRow->setVisible(m_track->currentData().toString() == m_newTrackId); });
    connect(m_browse, &QPushButton::clicked, this, [this] {
        const auto path = QFileDialog::getOpenFileName(this, QStringLiteral("Choisir l'application"), {}, QStringLiteral("Applications Windows (*.exe)"));
        if (path.isEmpty()) return;
        m_target->addItem(QFileInfo(path).fileName(), QDir::toNativeSeparators(path)); m_target->setItemData(m_target->count() - 1, QFileInfo(path).fileName(), Qt::UserRole + 1); m_target->setCurrentIndex(m_target->count() - 1);
        if (m_name->text().trimmed().isEmpty()) m_name->setText(QFileInfo(path).completeBaseName());
    });
    populateTargets(); m_newTrackRow->setVisible(m_track->currentData().toString() == m_newTrackId);
}
SourceKind SourceDialog::kind() const { for (int index = 0; index < m_kinds.size(); ++index) if (m_kinds[index]->isChecked()) return kinds[index]; return m_original.kind; }
void SourceDialog::populateTargets() {
    m_target->clear(); m_error->hide();
    const auto current = kind();
    m_exclusionsPanel->setVisible(current == SourceKind::ApplicationMix); m_targetRow->setVisible(current != SourceKind::ApplicationMix);
    if (current == SourceKind::ApplicationMix) {
        m_target->addItem(QStringLiteral("Toutes les applications sauf exclusions"), "@applications");
        m_target->setItemData(0, QStringLiteral("Toutes les applications sauf exclusions"), Qt::UserRole + 1);
        m_note->setText(QStringLiteral("Idéal pour le son du jeu : tout ce qui joue du son, sauf ce que vous excluez. Le jeu est pris en compte dès qu'il émet du son, même lancé après le replay. Clipsapp est toujours exclue.")); adjustSize(); return;
    }
    m_targetLabel->setText(current == SourceKind::Application ? QStringLiteral("Application") : QStringLiteral("Périphérique"));
    m_target->addItem(QStringLiteral("Choisir une source…"), QString());
    if (current != SourceKind::Application) {
        m_target->addItem(QStringLiteral("Suivre le périphérique par défaut"), "@default");
        m_target->setItemData(m_target->count() - 1, QStringLiteral("Suivre le périphérique par défaut"), Qt::UserRole + 1);
    }
    for (const auto& target : m_catalog.audioTargets(current)) {
        m_target->addItem(target.label, target.id); m_target->setItemData(m_target->count() - 1, target.label, Qt::UserRole + 1);
    }
    if (current == m_original.kind && !m_original.targetId.isEmpty()) {
        int index = m_target->findData(m_original.targetId);
        if (index < 0) {
            m_target->addItem(m_original.targetLabel + QStringLiteral(" (indisponible actuellement)"), m_original.targetId); index = m_target->count() - 1;
            m_target->setItemData(index, m_original.targetLabel, Qt::UserRole + 1);
        }
        m_target->setCurrentIndex(index);
    }
#ifdef Q_OS_WIN
    m_browse->setVisible(current == SourceKind::Application);
#else
    m_browse->setVisible(false);
#endif
    m_note->setText(current == SourceKind::Application
        ? QStringLiteral("Les applications apparaissent lorsqu'elles jouent du son. Plusieurs sources rangées dans la même piste sont mélangées pour de bon.")
        : current == SourceKind::Output
        ? QStringLiteral("Une sortie contient le son de plusieurs applications. L'ajouter à côté de ces mêmes applications peut doubler leur son.")
        : QStringLiteral("Choisissez un microphone précis pour le garder, ou suivez le périphérique par défaut du système."));
    adjustSize();
}
AudioSource SourceDialog::source() const {
    AudioSource value = m_original;
    value.name = m_name->text().trimmed(); value.kind = kind();
    value.targetId = m_target->currentData().toString(); value.targetLabel = value.targetId.isEmpty() ? QString() : m_target->currentData(Qt::UserRole + 1).toString();
    value.trackId = m_track->currentData().toString(); value.enabled = m_enabled->isChecked();
    value.excludedApplications.clear(); value.excludeSeparateApplications = m_autoExclusions->isChecked();
    for (int row = 0; row < m_exclusions->count(); ++row) if (m_exclusions->item(row)->checkState() == Qt::Checked) value.excludedApplications.append(m_exclusions->item(row)->data(Qt::UserRole).toString());
    return value;
}
AudioTrack SourceDialog::newTrack() const {
    if (m_track->currentData().toString() != m_newTrackId) return {};
    const auto name = m_newTrack->text().trimmed(); return {m_newTrackId, name.isEmpty() ? m_name->text().trimmed() : name};
}
void SourceDialog::validateAndAccept() {
    const auto value = source();
    if (value.name.isEmpty() || (value.enabled && value.targetId.isEmpty())) {
        m_error->setText(QStringLiteral("Donnez un nom à la source et choisissez ce qu'elle écoute, ou désactivez-la pour la régler plus tard.")); m_error->show(); adjustSize(); return;
    }
    accept();
}
}
