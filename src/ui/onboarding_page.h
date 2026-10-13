#pragma once
#include "core/config.h"
#include "platform/source_catalog.h"
#include <QWidget>
class QComboBox;
class QHBoxLayout;
class QKeySequenceEdit;
class QLabel;
namespace clipsapp {
class Button;
class Keycaps;
class Tile;
// First launch: four choices in one column, then the replay can run.
class OnboardingPage final : public QWidget {
    Q_OBJECT
public:
    explicit OnboardingPage(AppConfig& config, QWidget* parent = nullptr);
    void setCatalog(const SourceCatalog& catalog);
    void refresh();
    int step() const { return m_step; }
signals:
    void chooseFolderRequested();
    void finished(bool startReplay);
private:
    RecordingProfile& profile();
    void go(int step);
    AppConfig& m_config; SourceCatalog m_catalog; int m_step = 0;
    QLabel* m_progress; QList<QWidget*> m_steps; QList<QLabel*> m_summaries; QList<QWidget*> m_bodies; QList<Button*> m_changes;
    QLabel* m_folder; QHBoxLayout* m_screens; QList<Tile*> m_targets; QComboBox* m_windows; Button* m_targetNext;
    Keycaps* m_caps; Keycaps* m_capsSummary; QKeySequenceEdit* m_hotkey; Button* m_start;
};
}
