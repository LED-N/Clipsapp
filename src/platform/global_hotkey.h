#pragma once
#include <QObject>
#include <QAbstractNativeEventFilter>
#include <QKeySequence>

namespace clipsapp {
class GlobalHotkey final : public QObject, public QAbstractNativeEventFilter {
    Q_OBJECT
public:
    explicit GlobalHotkey(QObject* parent = nullptr);
    ~GlobalHotkey() override;
    bool bind(const QKeySequence& sequence, QString& error);
    void unbind();
    bool nativeEventFilter(const QByteArray& eventType, void* message, qintptr* result) override;
signals:
    void activated();
private:
    bool m_bound = false;
};
}
