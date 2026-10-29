#include "delete_prompt.h"
#include "widgets.h"
#include "core/clip_library.h"
#include <QFileInfo>
#include <QHBoxLayout>
#include <QScrollArea>
#include <QVBoxLayout>
#include <algorithm>
namespace clipsapp {
Deletion confirmTrash(QWidget* parent, const QStringList& paths, bool copies, const QString& note) {
    ModalDialog dialog(parent, 480); dialog.setObjectName("delete-clips-prompt");
    QStringList names; for (const auto& path : paths) names.append(QFileInfo(path).fileName()); dialog.setProperty("names", names);
    const bool images = std::all_of(paths.cbegin(), paths.cend(), [](const QString& path) { return isImageFile(path); });
    const bool recordings = !images && std::all_of(paths.cbegin(), paths.cend(), [](const QString& path) { return QFileInfo(path).completeBaseName().contains(QStringLiteral("-Enregistrement-")); });
    const auto count = paths.size(); const QString what = copies ? (count > 1 ? QStringLiteral("%1 copies").arg(count) : QStringLiteral("cette copie")) : images ? (count > 1 ? QStringLiteral("%1 screenshots").arg(count) : QStringLiteral("ce screenshot"))
        : recordings ? (count > 1 ? QStringLiteral("%1 enregistrements").arg(count) : QStringLiteral("cet enregistrement")) : (count > 1 ? QStringLiteral("%1 clips").arg(count) : QStringLiteral("ce clip"));
    const auto trashTitle = QStringLiteral("Mettre %1 à la corbeille ?").arg(what), trashText = (note.isEmpty() ? QString() : note + QLatin1Char(' ')) + (count > 1 ? QStringLiteral("Vous pourrez les récupérer depuis la corbeille du système.") : QStringLiteral("Vous pourrez le récupérer depuis la corbeille du système."));
    dialog.setHeading(trashTitle, trashText, new IconBox(QStringLiteral("trash"), tk::danger, tk::dangerSoft), false);
    dialog.body()->setSpacing(10);
    auto* well = card("well"); auto* wellLayout = new QVBoxLayout(well); wellLayout->setContentsMargins(1, 6, 1, 6);
    auto* rows = new QWidget; auto* rowsLayout = new QVBoxLayout(rows); rowsLayout->setContentsMargins(0, 0, 0, 0); rowsLayout->setSpacing(0);
    for (const auto& path : paths) {
        const QFileInfo file(path); auto* line = new QHBoxLayout; line->setContentsMargins(12, 5, 12, 5); line->setSpacing(12);
        line->addWidget(new ElideLabel(file.completeBaseName(), nullptr, Qt::ElideMiddle), 1); line->addWidget(monoLabel(formatBytes(file.size()), 12, tk::text2)); rowsLayout->addLayout(line);
    }
    auto* scroll = new QScrollArea; scroll->setWidgetResizable(true); scroll->setWidget(rows); scroll->setFrameShape(QFrame::NoFrame); scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setFixedHeight(int(std::min<qsizetype>(6, count) * 30)); wellLayout->addWidget(scroll); dialog.body()->addWidget(well);
    auto* permanent = new Toggle(QStringLiteral("Supprimer définitivement, sans passer par la corbeille"), Toggle::Box); permanent->setObjectName("trash-permanent"); dialog.body()->addWidget(permanent);
    auto* warning = label(count > 1 ? QStringLiteral("Irréversible : ces fichiers ne pourront pas être récupérés.") : QStringLiteral("Irréversible : ce fichier ne pourra pas être récupéré."), "danger", true); warning->setContentsMargins(26, 0, 0, 0); warning->hide(); dialog.body()->addWidget(warning);
    auto* keep = new Button(copies ? (count > 1 ? QStringLiteral("Garder les copies") : QStringLiteral("Garder la copie")) : images ? (count > 1 ? QStringLiteral("Garder les screenshots") : QStringLiteral("Garder le screenshot"))
        : recordings ? (count > 1 ? QStringLiteral("Garder les enregistrements") : QStringLiteral("Garder l'enregistrement")) : (count > 1 ? QStringLiteral("Garder les clips") : QStringLiteral("Garder le clip"))); keep->setObjectName("trash-keep");
    auto* confirm = new Button(QStringLiteral("trash"), QStringLiteral("Mettre à la corbeille"), Button::DangerSolid); confirm->setObjectName("trash-confirm"); confirm->setAutoDefault(false);
    dialog.footer()->addWidget(keep); dialog.footer()->addWidget(confirm); keep->setDefault(true);
    QObject::connect(permanent, &QCheckBox::toggled, &dialog, [&dialog, confirm, warning, trashTitle, trashText, what, note](bool forGood) {
        confirm->setText(forGood ? QStringLiteral("Supprimer définitivement") : QStringLiteral("Mettre à la corbeille")); warning->setVisible(forGood);
        dialog.setHeadingTexts(forGood ? QStringLiteral("Supprimer %1 définitivement ?").arg(what) : trashTitle, forGood ? (note.isEmpty() ? QString() : note + QLatin1Char(' ')) + QStringLiteral("Sans passer par la corbeille du système.") : trashText); dialog.refit();
    });
    QObject::connect(keep, &QPushButton::clicked, &dialog, &QDialog::reject); QObject::connect(confirm, &QPushButton::clicked, &dialog, &QDialog::accept);
    if (dialog.exec() != QDialog::Accepted) return Deletion::Kept;
    return permanent->isChecked() ? Deletion::Permanent : Deletion::Trash;
}
}
