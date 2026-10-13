#pragma once
#include "discord_batch.h"
#include "widgets.h"
#include <QFutureWatcher>
class QDoubleSpinBox;
class QStackedWidget;
class QVBoxLayout;
namespace clipsapp {
class ThumbnailCache;
// Two faces of one dialog: choose a limit and see what each clip will become,
// then watch the batch. Closing it never stops the batch.
class DiscordDialog final : public ModalDialog {
    Q_OBJECT
public:
    DiscordDialog(DiscordBatch* batch, ThumbnailCache* cache, QStringList paths, const ShareOptions& defaults, QWidget* parent = nullptr,
                  std::optional<ClipInfo> editedInfo = {}, std::optional<ClipEdit> edited = {});
    ~DiscordDialog() override;
signals:
    void preferencesChosen(qint64 bytes, const QString& codec, bool autoResize);
    void showSharingRequested();
    void copyRequested(const QStringList& paths);
private:
    void buildOptions();
    void buildProgress();
    void showStage(bool progress);
    qint64 limitBytes() const;
    QString codec() const;
    void updateEstimates();
    void begin();
    void refreshJobs();
    DiscordBatch* m_batch; ThumbnailCache* m_cache; QStringList m_paths; ShareOptions m_defaults; std::optional<ClipInfo> m_editedInfo; std::optional<ClipEdit> m_edited;
    QList<DiscordPrepared> m_infos; QFutureWatcher<QList<DiscordPrepared>> m_probe;
    QStackedWidget* m_stages; QList<Tile*> m_limits; QDoubleSpinBox* m_limit; QWidget* m_customRow; QList<QPushButton*> m_codecs; QCheckBox* m_resize;
    QVBoxLayout* m_estimates; QList<QLabel*> m_estimateTexts; QList<Tag*> m_estimateTags; QLabel* m_estimateWarning;
    Bar* m_overall; QVBoxLayout* m_jobs; QWidget* m_jobScroll = nullptr; QList<QWidget*> m_jobRows; int m_jobBatch = -1;
    QWidget* m_footerNote; Button* m_cancel; Button* m_start; Button* m_cancelBatch; Button* m_background; Button* m_openSharing; bool m_progressStage = false;
};
}
