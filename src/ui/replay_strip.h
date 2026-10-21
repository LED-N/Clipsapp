#pragma once
#include <QFrame>
class QLabel;
namespace clipsapp {
class Bar;
class Button;
struct ReplayView {
    enum State { Idle, Starting, Running, Stopping, Error };
    State state = Idle;
    QString profileSummary, liveSummary, warning, warningTitle, error, hotkey;
    double seconds = 0, capacity = 60;
    qint64 bytes = 0;
    // The buffer is kept in files rather than in memory.
    bool onDisk = false;
    bool saving = false, canStart = true, canSave = false, canFixWarning = false;
    QString blockedReason;
    // "state" is that of the capture, which serves the replay ("replay"), a recording, or both.
    bool replay = true;
    // A recording under way (or asked while the capture starts), its length and weight, and its key.
    bool recording = false, canRecord = true; double recordingSeconds = 0; qint64 recordingBytes = 0; QString recordHotkey;
};
// Always visible at the top of the window, editor included: the state of the replay
// is the most important thing the application has to say.
class ReplayStrip final : public QFrame {
    Q_OBJECT
public:
    explicit ReplayStrip(QWidget* parent = nullptr);
    void apply(const ReplayView& view);
    // As narrow as the window may be: narrower than the full strip, which then turns compact.
    QSize minimumSizeHint() const override { return {900, 60}; }
signals:
    void startRequested();
    void stopRequested();
    void saveRequested();
    void recordRequested();
    void fullScreenRequested();
    void settingsRequested();
protected:
    void resizeEvent(QResizeEvent* event) override;
private:
    void refresh();
    void fill();
    ReplayView m_view; bool m_compact = false;
    QWidget* m_status; QWidget* m_texts; QWidget* m_recordStatus; QWidget* m_recordTexts; QLabel* m_recordBytes; Bar* m_gauge; QLabel* m_seconds; QLabel* m_bytes; Button* m_fix; Button* m_record; Button* m_primary; Button* m_secondary;
};
}
