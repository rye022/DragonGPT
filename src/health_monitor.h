#pragma once
#include <QObject>
#include <QDateTime>
#include <QFutureWatcher>
#include <QTimer>
#include <QVector>
namespace dragon {
struct HealthSample {
    QDateTime time;
    double cpu=-1,memory=-1,gpu=-1,gpuTemperature=-1;
    quint64 memoryTotal=0,memoryUsed=0,totalTicks=0,idleTicks=0;
    qint64 idleSeconds=-1;
    QString gpuSource="最忙引擎";
};
class HealthMonitor final:public QObject{
    Q_OBJECT
public:
    explicit HealthMonitor(QObject *parent=nullptr);
    ~HealthMonitor() override;
    void start();
    void stop();
    void sample();
    void ingest(HealthSample sample);
    HealthSample latest() const{return latest_;}
    QVector<HealthSample> trend() const{return trend_;}
    QString overview() const;
signals:
    void changed();
    void sustainedLoad(const QString &advice);
    void activeSeconds(int seconds,QDate date);
private:
    QTimer timer_;
    QFutureWatcher<HealthSample> watcher_;
    HealthSample latest_,previousRaw_;
    QVector<HealthSample> trend_;
    QDateTime highSince_,lastAlert_;
    static HealthSample readSystem();
};
}
