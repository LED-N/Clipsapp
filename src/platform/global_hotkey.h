#pragma once
#include <QObject>
#include <QAbstractNativeEventFilter>
#include <QKeySequence>

namespace clipsapp {
class GlobalHotkey final : public QObject, public QAbstractNativeEventFilter {
    Q_OBJECT
public:
    // Each shortcut of the application has its own identifier for Windows.
    explicit GlobalHotkey(QObject* parent = nullptr, int id = 0x4c43);
    ~GlobalHotkey() override;
    bool bind(const QKeySequence& sequence, QString& error);
    void unbind();
    bool nativeEventFilter(const QByteArray& eventType, void* message, qintptr* result) override;
signals:
    void activated();
private:
    bool m_bound = false; int m_id;
};
}
