#pragma once
#include "core/config.h"
#include "platform/source_catalog.h"
#include <QWidget>
class QCheckBox;
class QComboBox;
class QHBoxLayout;
class QKeySequenceEdit;
class QLabel;
namespace clipsapp {
class Button;
class Keycaps;
class QualityTile;
class Segmented;
class Tile;
// First launch: five choices in one column, then the replay can run.
class OnboardingPage final : public QWidget {
    Q_OBJECT
public:
    explicit OnboardingPage(AppConfig& config, QWidget* parent = nullptr);
    void setCatalog(const SourceCatalog& catalog);
    void refresh();
    int step() const { return m_step; }
    // Whether this copy keeps its own shortcut in the Start menu, and whether the user wants it.
    void setStartMenu(bool managed);
    bool startMenuWanted() const;
signals:
    void chooseFolderRequested();
    void finished(bool startReplay);
private:
    RecordingProfile& profile();
    void go(int step);
    void offerScreenSize();
    AppConfig& m_config; SourceCatalog m_catalog; int m_step = 0;
    QLabel* m_progress; QList<QWidget*> m_steps; QList<QLabel*> m_summaries; QList<QWidget*> m_bodies; QList<Button*> m_changes;
    QLabel* m_folder; QHBoxLayout* m_screens; QList<Tile*> m_targets; QComboBox* m_windows; Button* m_targetNext;
    Keycaps* m_caps; Keycaps* m_capsSummary; QKeySequenceEdit* m_hotkey; Button* m_start; QCheckBox* m_startMenu;
    QList<QualityTile*> m_sizes; Segmented* m_rates; QList<QualityTile*> m_presets; QLabel* m_qualityHint; bool m_qualityChosen = false, m_refreshing = false;
};
}
