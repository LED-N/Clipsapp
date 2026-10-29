#pragma once
#include "media/clip_player.h"
#include "media/clip_exporter.h"
#include "media/clip_analysis.h"
#include <QElapsedTimer>
#include <QFutureWatcher>
#include <QHash>
#include <QPointer>
#include <QThreadPool>
#include <QTimer>
#include <QWidget>
class QAbstractButton;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSlider;
class QHBoxLayout;
class QSpinBox;
class QScrollBar;
class QVBoxLayout;
namespace clipsapp {
class Banner;
class Bar;
class Button;
class ClipTimeline;
class ElideLabel;
class IconBox;
class ModalDialog;
class Segmented;
class Tag;
class Tile;
class VideoCanvas;
class ClipEditor final : public QWidget {
    Q_OBJECT
public:
    explicit ClipEditor(QWidget* parent = nullptr);
    ~ClipEditor() override;
    void openClip(const QString& path);
    void suspend();
    void release();
    QString clipPath() const { return m_path; }
    ClipEdit currentEdit() const { return m_edit; }
    ClipInfo clipInfo() const { return m_info; }
    bool ready() const { return m_initialized; }
    void setGame(const QString& game);
    void forgetClip(const QString& path);
    // Keeps the session's work on a clip when its file is renamed.
    void moveClip(const QString& from, const QString& to);
    QList<QFuture<ClipAnalysis>> pendingAnalyses() const;
    bool exportTo(const QString& path, QString& error);
    void noteHighQualityExport(const QString& original, const ClipEdit& edit);
    void setSharingBusy(bool busy);
    bool exporting() const { return m_exporter.running(); }
    // Changed by the user, beyond the usual levels it opened with.
    bool touched() const;
    // The usual level of each track: where a clip starts, and what "Garder ces volumes" replaces.
    void setDefaultMix(const TrackMix& mix);
    // The edit of the last export, for the mix to learn from.
    ClipEdit exportedEdit() const { return m_exportEdit; }
    // For the full-screen viewer: it shows the current result from where the editor is, then hands the position back.
    qint64 position() const { return m_player.snapshot().positionMs; }
    void pausePlayback() { if (m_player.snapshot().playing) m_player.setPlaying(false); }
    void showPosition(qint64 milliseconds) { if (m_initialized) m_player.seek(milliseconds); }
signals:
    void fullScreenRequested();
    void backRequested();
    void exported(const QString& path, const QString& original);
    void deleteRequested(const QString& path, bool confirmed);
    void discordRequested(const ClipInfo& info, const ClipEdit& edit);
    void renameRequested(const QString& path, const QString& name);
    void openRequested(const QString& path);
    void failed(const QString& title, const QString& detail);
    // The levels of this clip are to become the usual ones.
    void mixChosen(const TrackMix& mix);
    // The image at this moment of the clip, framed as the crop says, to keep among the screenshots.
    void screenshotRequested(const QString& path, qint64 positionMs, const QRect& crop);
protected:
    void resizeEvent(QResizeEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;
private:
    QWidget* buildHeader();
    QWidget* buildInspector();
    void updatePlayer();
    void applyEdits();
    void updateCrop(QRect crop);
    void chooseRatio(int index);
    void exportClip();
    void beginAnalysis();
    void stopAnalysis();
    void updateActions();
    void updateSummary();
    void removeTrack(int stream);
    void restoreTracks();
    void stepFrames(int frames);
    void finishRename(bool commit);
    void showExportDialog(const QString& name);
    void showExportResult(const QString& path);
    void closeExportDialog();
    bool hasExportChanges() const;
    ClipPlayer m_player;
    ClipExporter m_exporter;
    ClipInfo m_info;
    ClipEdit m_edit;
    // The file as it is, the same with each track at its usual level, the last export, the one running.
    ClipEdit m_originalEdit, m_usualEdit, m_baseline, m_exportEdit;
    TrackMix m_mix;
    TrackMix chosenMix() const;
    QSize previewBox() const;
    QTimer m_previewSize; QElapsedTimer m_refresh; bool m_seenPlaying = false, m_seenLoaded = false;
    QHash<QString, ClipEdit> m_drafts;
    QHash<QString, ClipEdit> m_exports;
    QString m_path, m_game;
    QString m_exportSource, m_exportedPath;
    QElapsedTimer m_exportClock;
    bool m_updating = false, m_initialized = false, m_compact = false;
    bool m_sharingBusy = false;
    int m_ratio = 0;
    QFutureWatcher<ClipAnalysis>* m_analysis = nullptr;
    QThreadPool m_analysisPool;
    QList<QFuture<ClipAnalysis>> m_analysisTasks;
    std::shared_ptr<std::atomic_bool> m_analysisStop;
    QHash<QString, ClipAnalysis> m_analysisCache;
    QString m_analysisKey;
    QWidget* m_header; Button* m_back; ElideLabel* m_title; QLineEdit* m_titleEdit; Button* m_rename; Tag* m_gameTag; Tag* m_stateTag; QLabel* m_hint;
    Button* m_discord; Button* m_export;
    QPointer<ModalDialog> m_exportDialog; IconBox* m_exportMark = nullptr; QWidget* m_exportProgress = nullptr; QLabel* m_exportPercent = nullptr; QLabel* m_exportEta = nullptr; Bar* m_exportBar = nullptr;
    Banner* m_banner; Banner* m_problem; Button* m_reload; qint64 m_reloadAt = 0;
    VideoCanvas* m_canvas; Segmented* m_mode; Button* m_fullScreen; Button* m_screenshot;
    QVBoxLayout* m_center; QWidget* m_transport; Button* m_play; QLabel* m_time; QLabel* m_total; QLabel* m_selection; QLabel* m_selectionCaption; Button* m_startHere; Button* m_endHere;
    ClipTimeline* m_timeline; QWidget* m_zoomControls; QSlider* m_zoom; QLabel* m_zoomLabel; QScrollBar* m_pan; QLabel* m_zoomHint;
    QList<QSlider*> m_volumes; QList<QAbstractButton*> m_mutes;
    QWidget* m_inspector; QWidget* m_controls; QDoubleSpinBox* m_start; QDoubleSpinBox* m_end; QLabel* m_kept;
    QList<QVBoxLayout*> m_sections; QLabel* m_originCaption; Button* m_detailsToggle; QWidget* m_facts;
    QList<Tile*> m_ratios; QHBoxLayout* m_ratioRow; QWidget* m_precise; Button* m_preciseToggle; QSpinBox* m_x; QSpinBox* m_y; QSpinBox* m_width; QSpinBox* m_height;
    Segmented* m_audioMode; QLabel* m_audioSummary; Button* m_restoreTracks; Button* m_keepMix; QList<QLabel*> m_details; ElideLabel* m_fileName; Button* m_delete;
};
}
