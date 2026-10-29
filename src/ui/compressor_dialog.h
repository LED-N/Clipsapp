#pragma once
#include "compress_batch.h"
#include "discord_batch.h"
#include "widgets.h"
#include <QFutureWatcher>
#include <QPointer>
#include <memory>
#include <optional>
class QDoubleSpinBox;
class QTemporaryDir;
class QVBoxLayout;
namespace clipsapp {
class CompareView;
class ThumbnailCache;
// One dialog for every compression. First what for: to save room, the picture kept, or to send a
// copy on Discord, at the size of its limits. Then the work, and what it gives:
// - for Discord, copies to drag into a conversation, which disappear when the dialog closes;
// - for one clip saving room, the two versions side by side, and the choice to replace the original;
// - for several, or the whole library, each checked clip replacing its original as it goes, which
//   may go on in the background.
class CompressorDialog final : public ModalDialog {
    Q_OBJECT
public:
    enum Goal { Choose, Space, Discord };
    struct Setup {
        QStringList paths; Goal goal = Choose;
        // The whole library: only room is saved there.
        bool library = false;
        // The clip open in the editor, as it is edited, for a Discord copy of the edit.
        std::optional<ClipInfo> editedInfo; std::optional<ClipEdit> edited; bool editedChanges = false;
        // The listening level of the comparison, the one of the full-screen player.
        int volume = 100;
        // A replay or a recording runs: the compression shares the graphics card and the processor with it.
        bool capturing = false;
    };
    CompressorDialog(DiscordBatch* discord, CompressBatch* space, ThumbnailCache* cache, const Setup& setup, const ShareOptions& shareDefaults, const CompressSettings& spaceDefaults, QWidget* parent);
    ~CompressorDialog() override;
    // Échap: stops what would be lost (a Discord copy, the comparison), lets a library go on behind.
    void reject() override;
signals:
    void preferencesChosen(qint64 bytes, const QString& codec, bool autoResize);
    void spaceSettingsChosen(const CompressSettings& settings);
    void copyRequested(const QStringList& paths);
    void volumeChanged(int percent);
private:
    enum Stage { Options, DiscordWork, SpaceWork, Compare, SpaceBatch };
    void buildOptions();
    QWidget* buildSpaceOptions();
    QWidget* buildDiscordOptions();
    void buildDiscordWork();
    void buildSpaceWork();
    void buildCompare();
    void buildBatch();
    void chooseGoal(Goal goal);
    void showStage(Stage stage);
    void begin();
    qint64 limitBytes() const;
    QString codec() const;
    CompressSettings spaceSettings() const;
    void updateEstimates();
    void updateAgain();
    QStringList spacePaths() const;
    void refreshDiscord();
    void refreshSpace();
    void refreshBatch();
    // The engines belong to the window, which may let go of them before this dialog when it closes.
    QPointer<DiscordBatch> m_discord; QPointer<CompressBatch> m_space; ThumbnailCache* m_cache; Setup m_setup; ShareOptions m_shareDefaults; CompressSettings m_spaceDefaults;
    Goal m_goal = Choose; Stage m_stage = Options; bool m_replacing = false, m_replaced = false;
    std::shared_ptr<QTemporaryDir> m_temporary;
    QList<DiscordPrepared> m_infos; QFutureWatcher<QList<DiscordPrepared>> m_probe;
    QVBoxLayout* m_stages; Bar* m_overall; Button* m_start; Button* m_cancel;
    // What for.
    Tile* m_spaceTile; Tile* m_discordTile; QList<QWidget*> m_goalPages; QWidget* m_goalHint;
    // Saving room.
    Segmented* m_level; QLabel* m_levelHint; QList<QPushButton*> m_engines; QCheckBox* m_permanent = nullptr; QLabel* m_permanentWarning = nullptr; QList<Tag*> m_againTags; Banner* m_againWarning = nullptr; QCheckBox* m_again = nullptr;
    // Discord.
    QList<Tile*> m_limits; QDoubleSpinBox* m_limit; QWidget* m_customRow; QList<QPushButton*> m_codecs; QCheckBox* m_resize;
    QList<QLabel*> m_estimateTexts; QList<Tag*> m_estimateTags; QLabel* m_estimateWarning;
    QVBoxLayout* m_jobs; QWidget* m_jobScroll = nullptr; QList<QWidget*> m_jobRows; int m_jobBatch = -1; QWidget* m_dragAll;
    // One clip saving room, then its comparison.
    IconBox* m_workMark; QWidget* m_workProgress; QLabel* m_workPercent; QLabel* m_workEta; QLabel* m_workStage; Bar* m_workBar;
    CompareView* m_compare = nullptr; Button* m_keep = nullptr; Button* m_replace = nullptr;
    // Several clips, or the library.
    ElideLabel* m_batchName; ElideLabel* m_batchStage; QLabel* m_batchFigures; Bar* m_batchBar; QVBoxLayout* m_batchRows; QWidget* m_batchScroll; int m_batchShown = 0;
    Button* m_stop; Button* m_background; Button* m_close; Button* m_details;
};
// What a compression gave, clip by clip: before, after, the room gained, or why a clip kept its original.
void showCompressionReport(QWidget* parent, const QList<CompressJob>& jobs, bool permanent);
}
