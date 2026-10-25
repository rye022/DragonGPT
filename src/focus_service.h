#pragma once
#include "memory_store.h"
#include <QObject>
#include <QQueue>
#include <QTime>

namespace dragon {
struct WellnessSettings {
    int focusSeconds=25*60,shortSeconds=5*60,longSeconds=15*60,longEvery=4;
    bool autoNext=false,focusTts=false,waterEnabled=true,useActivity=true,deferFocus=true;
    bool waterTts=true,waterBubble=true,quietEnabled=false,preferBreak=true,silentDuringLongFocus=false;
    int waterMinutes=60,snoozeMinutes=15;
    QTime quietStart{22,0},quietEnd{8,0};
    QJsonObject json() const;
    static WellnessSettings fromJson(const QJsonObject &o);
};
struct WellnessTime { QDateTime utc; qint64 monotonicMs=0; };
struct ActivitySample {
    bool idleAvailable=false,locked=false,lockAvailable=false;
    qint64 idleMs=0,uptimeMs=-1,sessionMs=-1;
    QString source;
};
ActivitySample systemActivity();
struct CompanionNotice {
    QString id,text,lineId;
    QDateTime expires;
    int priority=0;
    bool water=false,silent=false;
};
class FocusService final:public QObject {
    Q_OBJECT
public:
    enum class Phase { Focus,ShortBreak,LongBreak };
    explicit FocusService(MemoryStore &store,WellnessTime now,QObject *parent=nullptr);
    void tick(WellnessTime now,const ActivitySample &activity,bool userBusy=false,bool doNotDisturb=false);
    void start(const QString &task,int requestedSeconds=0);
    void requestWaterAfter(int minutes);
    void pause();
    void systemPause(const QString &reason);
    void resume();
    void skip();
    void end();
    void reset();
    void confirmWater();
    void snoozeWater();
    void muteWaterToday();
    void setSettings(const WellnessSettings &settings);
    const WellnessSettings &settings() const {return settings_;}
    bool running() const{return running_;}
    bool paused() const{return paused_;}
    bool focusing() const{return running_ && !paused_ && phase_==Phase::Focus;}
    bool inSession() const{return running_;}
    Phase phase() const{return phase_;}
    QString phaseName() const;
    QString task() const{return task_;}
    qint64 remainingMs() const{return remaining_;}
    qint64 actualMs() const{return actual_;}
    qint64 sessionActiveMs() const{return sessionActive_;}
    bool waterPending() const{return waterPending_;}
    QString waterStatus() const;
    QString recoveryStatus() const{return recovery_;}
    bool quiet() const;
    bool phaseReminderQuiet() const;
    bool takeNotice(CompanionNotice &notice);
    void shown(const CompanionNotice &notice);
    QJsonObject todayStats() const;
    QString weekTrend() const;
    void checkpoint();
signals:
    void changed();
    void sessionSaved();
    void sessionEnded();
private:
    MemoryStore &store_;
    WellnessSettings settings_;
    WellnessTime now_,previous_;
    ActivitySample activity_;
    bool externalQuiet_=false;
    bool running_=false,paused_=false,waterPending_=false,userBusy_=false,wasAway_=false;
    Phase phase_=Phase::Focus;
    qint64 remaining_=25*60000,planned_=25*60000,actual_=0,sessionActive_=0,activeSinceDrink_=0;
    int completed_=0,lastWaterLine_=-1;
    QString id_,task_,recovery_;
    QDate mutedDay_;
    QDateTime started_,lastDrink_,lastReminder_,snoozeUntil_,quietUntil_,lastSave_;
    QJsonArray segments_;
    QQueue<CompanionNotice> notices_;
    QJsonObject state() const;
    int duration(Phase phase) const;
    void advance(bool completed);
    void finish(const QString &status);
    void offer(QString text,int priority,bool water=false,bool silent=false,QString lineId={});
    bool waterDue() const;
    QString waterLine(QString *key);
    void addSegment(qint64 ms);
};
}
