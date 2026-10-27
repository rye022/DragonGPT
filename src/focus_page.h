#pragma once
#include "focus_service.h"
#include <QWidget>
#include <QAbstractNativeEventFilter>
#include <QElapsedTimer>
#include <QFutureWatcher>
#include <QTimer>
#include <functional>
class QLabel;
class QLineEdit;
class QPushButton;
namespace dragon {
class Companion;
class PetWidget;
class VoiceController;
class FocusPage final:public QWidget,public QAbstractNativeEventFilter {
    Q_OBJECT
public:
    FocusPage(Companion &companion,PetWidget &pet,VoiceController &voice,QWidget *parent=nullptr);
    ~FocusPage() override;
    bool nativeEventFilter(const QByteArray &type,void *message,qintptr *result) override;
    FocusService &service(){return service_;}
signals:
    void activityObserved(qint64 idleSeconds);
private:
    Companion &companion_;
    PetWidget &pet_;
    VoiceController &voice_;
    QElapsedTimer clock_;
    FocusService service_;
    QTimer timer_;
    QFutureWatcher<ActivitySample> probe_;
    ActivitySample activity_;
    QLabel *countdown_,*phase_,*stats_,*trend_,*water_,*monitor_,*recovery_,*voiceStatus_;
    QLineEdit *task_;
    QPushButton *start_,*pause_,*resume_,*end_,*skip_,*reset_;
    QWidget *waterActions_;
    qint64 lastProbe_=-10000,lastStats_=-10000;
    bool failed_=false;
    quintptr registeredWindow_=0;
    void refresh();
    void step();
    void settingsDialog();
    void guarded(const std::function<void()> &operation);
};
}
