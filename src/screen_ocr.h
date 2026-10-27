#pragma once
#include <QObject>
#include <QProcess>
#include <QTimer>
#include <QImage>
namespace dragon {
class ScreenOcr final:public QObject {
    Q_OBJECT
public:
    explicit ScreenOcr(QObject *parent=nullptr);
    ~ScreenOcr() override;
    bool busy() const{return process_.state()!=QProcess::NotRunning;}
    void start(const QImage &image);
    void cancel();
signals:
    void ready(const QString &text);
    void failed(const QString &reason);
private:
    QProcess process_;
    QTimer deadline_;
    QByteArray output_;
};
}
