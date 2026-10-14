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
    bool saving = false, canStart = true, canSave = false, canFixWarning = false;
    QString blockedReason;
};
// Always visible at the top of the window, editor included: the state of the replay
// is the most important thing the application has to say.
class ReplayStrip final : public QFrame {
    Q_OBJECT
public:
    explicit ReplayStrip(QWidget* parent = nullptr);
    void apply(const ReplayView& view);
signals:
    void startRequested();
    void stopRequested();
    void saveRequested();
    void fullScreenRequested();
    void settingsRequested();
protected:
    void resizeEvent(QResizeEvent* event) override;
private:
    void refresh();
    ReplayView m_view; bool m_compact = false;
    QWidget* m_status; QWidget* m_texts; Bar* m_gauge; QLabel* m_seconds; QLabel* m_bytes; Button* m_fix; Button* m_primary; Button* m_secondary;
};
}
