#pragma once
#include "core/config.h"
#include "platform/source_catalog.h"
#include <QKeySequence>
#include <QWidget>
class QCheckBox;
class QComboBox;
class QKeySequenceEdit;
class QLabel;
class QLineEdit;
class QSlider;
class QSpinBox;
class QStackedWidget;
class QVBoxLayout;
namespace clipsapp {
class Banner;
class Bar;
class Button;
class Keycaps;
class Tag;
class Tile;
// Three tabs over one profile. Nothing is applied before "Enregistrer"; while the
// replay runs the page says so and offers to stop it, rather than greying out silently.
class SettingsPage final : public QWidget {
    Q_OBJECT
public:
    SettingsPage(AppConfig& config, const QString& settingsPath, QWidget* parent = nullptr);
    void refreshCatalog();
    const SourceCatalog& catalog() const { return m_catalog; }
    void load();
    void collect();
    void setSaved(const AppConfig& saved);
    void setLocked(bool locked);
    void showTab(int index);
    int currentTab() const;
    QKeySequence hotkey() const;
    bool setFps(int fps);
    void setFolder(const QString& path);
    // Whether this build follows a repository for its updates, and the version it is.
    void setUpdates(bool followed, const QString& version);
    // Whether this copy keeps its own shortcut in the Start menu.
    void setStartMenu(bool managed);
    // Whether this copy can start with the session of the system.
    void setLoginStart(bool managed);
    // A game whose launch starts the replay, in "games" mode. The same game twice is kept once.
    void addReplayGame(const QString& executable);
    RecordingProfile& profile();
signals:
    void checkUpdatesRequested();
    void copyDiagnosticRequested();
    void edited();
    void saveRequested();
    void revertRequested();
    void stopReplayRequested();
    void chooseFolderRequested();
    void openSettingsFolderRequested();
    void notice(const QString& title, const QString& text);
private:
    QWidget* buildCaptureTab();
    QWidget* buildAudioTab();
    QWidget* buildGeneralTab(const QString& settingsPath);
    void changed();
    void populateVideoTargets();
    void populateSources();
    void editSource(int index);
    void removeSource(int index);
    void pruneTracks();
    void duplicateProfile();
    void updateEstimate();
    void updateQuality();
    void updateHotkey();
    void refreshDirty();
    void populateGames();
    void populateGameMixes();
    void chooseGame();
    AppConfig& m_config; AppConfig m_saved; SourceCatalog m_catalog; bool m_loading = true, m_locked = false;
    QComboBox* m_profiles; Button* m_duplicate; QList<QWidget*> m_tabButtons; QStackedWidget* m_tabs; Banner* m_lock; QWidget* m_saveBar; QLabel* m_dirtyTitle; QLabel* m_dirtyDetail; Button* m_save;
    QComboBox* m_videoTarget; QLineEdit* m_gameName; QCheckBox* m_detectGame; QList<Tile*> m_presets; Button* m_advancedToggle; QWidget* m_advanced;
    QComboBox* m_resolution; QComboBox* m_fps; QComboBox* m_codec; QComboBox* m_encoder; QSpinBox* m_bitrate; QCheckBox* m_cursor;
    QComboBox* m_rateControl; QComboBox* m_quality; QComboBox* m_effort; QLabel* m_bitrateLabel; QLabel* m_qualityLabel;
    QSlider* m_durationSlider; QSpinBox* m_duration; QComboBox* m_memory; QWidget* m_estimateCalm; QLabel* m_estimateCalmText; Bar* m_estimateCalmBar;
    QWidget* m_estimateWarn; QLabel* m_estimateWarnTitle; QLabel* m_estimateWarnLimit; Bar* m_estimateWarnBar; QLabel* m_estimateWarnText;
    Keycaps* m_hotkeyCaps; QKeySequenceEdit* m_hotkey; Button* m_hotkeyChange; QLabel* m_hotkeyHint;
    Tag* m_captureTag; Tag* m_qualityTag; Tag* m_replayTag;
    QVBoxLayout* m_lanes; QLabel* m_sourceNotes; QLineEdit* m_folder; QCheckBox* m_savedSound; QCheckBox* m_savedNotify; QWidget* m_soundOptions; QComboBox* m_sound; QSlider* m_soundVolume; QLabel* m_soundPercent;
    QCheckBox* m_shotSound; QWidget* m_shotSoundOptions; QComboBox* m_shotSoundChoice; QSlider* m_shotVolume; QLabel* m_shotPercent; Keycaps* m_shotCaps; QKeySequenceEdit* m_shotKey; Button* m_shotChange; QLabel* m_shotHint; QVBoxLayout* m_gameMixes; QWidget* m_gameMixHost;
    QWidget* m_updateSection; QCheckBox* m_checkUpdates; QLabel* m_version; QCheckBox* m_closeToTray; QCheckBox* m_startMenu;
    QCheckBox* m_loginStart; QWidget* m_loginHint; QComboBox* m_autoReplay; QLabel* m_autoHint; QWidget* m_gamesHost; QVBoxLayout* m_games; QLabel* m_gamesEmpty;
};
}
