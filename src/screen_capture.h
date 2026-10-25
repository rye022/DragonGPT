#pragma once
#include <QObject>
#include <QImage>
#include <QTimer>
#include <QDateTime>
#include <QJsonObject>
#include <QPointer>
#include <QWidget>
#include <QProcess>
#include "context_engine.h"
namespace dragon {
struct CaptureTarget {quintptr handle=0;QString title,process;QRect bounds;bool fullscreen=false;};
class ScreenCapture final:public QObject {
    Q_OBJECT
public:
    explicit ScreenCapture(const QString &settingsFile,QObject *parent=nullptr);
    ~ScreenCapture() override;
    bool enabled() const{return enabled_;}
    bool busy() const{return busy_;}
    void configure(bool enabled,bool contextEnabled,const QStringList &blocked);
    QStringList blocked() const{return blocked_;}
    ContextEngine &context(){return context_;}
    const ContextEngine &context() const{return context_;}
    CaptureTarget lastTarget() const{return last_;}
    QVector<CaptureTarget> windows() const;
    void poll();
    void capture(quintptr handle=0,int monitor=-1);
    void cancel();
    static QString denied(const CaptureTarget &target,const QStringList &blocked);
    static CaptureTarget inspect(quintptr handle);
signals:
    void ready(const QImage &image,const QJsonObject &metadata);
    void failed(const QString &reason);
    void status(const QString &text);
private:
    QString file_;bool enabled_=false,busy_=false;
    QStringList blocked_;
    ContextEngine context_;
    QTimer tracker_;
    CaptureTarget last_;
    quint64 epoch_=0;
    QVector<QPointer<QWidget>> hidden_;
    void restore();
    QProcess nativeCapture_;
    QTimer nativeDeadline_;
    QByteArray nativeOutput_;
};
}
