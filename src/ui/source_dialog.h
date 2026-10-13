#pragma once
#include "widgets.h"
#include "core/config.h"
#include "platform/source_catalog.h"
class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;

namespace clipsapp {
class SourceDialog final : public ModalDialog {
public:
    SourceDialog(const SourceCatalog& catalog, const RecordingProfile& profile, const AudioSource* existing, QWidget* parent);
    AudioSource source() const;
    AudioTrack newTrack() const;
private:
    SourceKind kind() const;
    void populateTargets();
    void validateAndAccept();
    SourceCatalog m_catalog;
    AudioSource m_original;
    QList<Tile*> m_kinds;
    QLineEdit* m_name;
    QComboBox* m_target;
    QLabel* m_targetLabel;
    QWidget* m_targetRow;
    QComboBox* m_track;
    QWidget* m_newTrackRow;
    QLineEdit* m_newTrack;
    QCheckBox* m_enabled;
    QPushButton* m_browse;
    QWidget* m_exclusionsPanel;
    QListWidget* m_exclusions;
    QCheckBox* m_autoExclusions;
    QLabel* m_note;
    QLabel* m_error;
    QString m_newTrackId;
};
}
