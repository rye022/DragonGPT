#pragma once
#include <QObject>
#include <QAbstractNativeEventFilter>
#include <QElapsedTimer>

namespace dragon {
// A bare Q is never registered or consumed. Only a double tap while the
// Windows desktop itself has focus is recognized. Editors/games receive Q.
class DesktopHotkey final : public QObject, public QAbstractNativeEventFilter {
    Q_OBJECT
public:
    explicit DesktopHotkey(QObject *parent=nullptr);
    ~DesktopHotkey() override;
    QString configure(const QString &sequence);
    QString sequence() const{return sequence_;}
    bool nativeEventFilter(const QByteArray &,void *,qintptr *) override;
    void observeQ(bool down,bool allowed,qint64 milliseconds);
signals:
    void activated();
private:
    QString sequence_;
    int id_=0;
    void *hook_=nullptr;
    bool registered_=false,down_=false;
    qint64 lastTap_=-10000;
};
}
