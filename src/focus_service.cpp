#include "focus_service.h"
#include <QJsonArray>
#include <QRandomGenerator>
#include <QUuid>
#include <algorithm>
#ifdef Q_OS_WIN
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <wtsapi32.h>
#endif

namespace dragon {
namespace {
QString iso(QDateTime time){return time.toUTC().toString(Qt::ISODateWithMs);}
QDateTime date(const QJsonObject &o,const char *key){return QDateTime::fromString(o.value(key).toString(),Qt::ISODateWithMs);}
}
QJsonObject WellnessSettings::json() const {
    return {{"focus",focusSeconds},{"short",shortSeconds},{"long",longSeconds},{"every",longEvery},
        {"auto",autoNext},{"focus_tts",focusTts},{"water",waterEnabled},{"activity",useActivity},{"defer",deferFocus},
        {"water_tts",waterTts},{"bubble",waterBubble},{"quiet",quietEnabled},{"prefer_break",preferBreak},
        {"silent_focus",silentDuringLongFocus},{"interval",waterMinutes},{"snooze",snoozeMinutes},
        {"quiet_start",quietStart.toString("HH:mm")},{"quiet_end",quietEnd.toString("HH:mm")}};
}
WellnessSettings WellnessSettings::fromJson(const QJsonObject &o){
    WellnessSettings s;
    s.focusSeconds=qBound(1,o.value("focus").toInt(s.focusSeconds),180*60);
    s.shortSeconds=qBound(1,o.value("short").toInt(s.shortSeconds),60*60);
    s.longSeconds=qBound(1,o.value("long").toInt(s.longSeconds),120*60);
    s.longEvery=qBound(1,o.value("every").toInt(s.longEvery),12);
    s.autoNext=o.value("auto").toBool(s.autoNext);s.focusTts=o.value("focus_tts").toBool(s.focusTts);
    s.waterEnabled=o.value("water").toBool(s.waterEnabled);s.useActivity=o.value("activity").toBool(s.useActivity);
    s.deferFocus=o.value("defer").toBool(s.deferFocus);s.waterTts=o.value("water_tts").toBool(s.waterTts);
    s.waterBubble=o.value("bubble").toBool(s.waterBubble);s.quietEnabled=o.value("quiet").toBool(s.quietEnabled);
    s.preferBreak=o.value("prefer_break").toBool(s.preferBreak);s.silentDuringLongFocus=o.value("silent_focus").toBool(false);
    s.waterMinutes=qBound(1,o.value("interval").toInt(s.waterMinutes),240);s.snoozeMinutes=qBound(1,o.value("snooze").toInt(15),120);
    auto a=QTime::fromString(o.value("quiet_start").toString(),"HH:mm"),b=QTime::fromString(o.value("quiet_end").toString(),"HH:mm");
    if(a.isValid())s.quietStart=a;
    if(b.isValid())s.quietEnd=b;
    return s;
}
ActivitySample systemActivity(){
    ActivitySample s;
#ifdef Q_OS_WIN
    s.uptimeMs=qint64(GetTickCount64());
    LASTINPUTINFO input{sizeof(LASTINPUTINFO),0};
    if(GetLastInputInfo(&input)){s.idleAvailable=true;s.idleMs=DWORD(GetTickCount()-input.dwTime);}
    auto desktop=OpenInputDesktop(0,FALSE,DESKTOP_READOBJECTS);
    if(desktop){wchar_t name[256]{};DWORD size=0;
        if(GetUserObjectInformationW(desktop,UOI_NAME,name,sizeof(name),&size)){s.lockAvailable=true;s.locked=QString::fromWCharArray(name).compare("Default",Qt::CaseInsensitive)!=0;}
        CloseDesktop(desktop);
    }else {s.lockAvailable=true;s.locked=true;}
    LPWSTR buffer=nullptr;DWORD bytes=0;
    if(WTSQuerySessionInformationW(WTS_CURRENT_SERVER_HANDLE,WTS_CURRENT_SESSION,WTSSessionInfo,&buffer,&bytes)){
        if(bytes>=sizeof(WTSINFOW)){auto info=reinterpret_cast<WTSINFOW*>(buffer);if(info->LogonTime.QuadPart>0)s.sessionMs=qMax<qint64>(0,(info->CurrentTime.QuadPart-info->LogonTime.QuadPart)/10000);}
        WTSFreeMemory(buffer);
    }
    s.source="Windows GetLastInputInfo / GetTickCount64 / 输入桌面 / WTS";
#else
    s.source="系统活跃 API 不可用；活跃计时暂停，可在设置中选择经过时间模式";
#endif
    return s;
}
FocusService::FocusService(MemoryStore &store,WellnessTime now,QObject *parent):QObject(parent),store_(store),now_(now),previous_(now){
    auto s=store_.wellnessState();settings_=WellnessSettings::fromJson(s.value("settings").toObject());
    running_=s.value("running").toBool();paused_=running_;phase_=Phase(qBound(0,s.value("phase").toInt(),2));
    planned_=s.value("planned").toInteger(duration(phase_));remaining_=s.value("remaining").toInteger(planned_);
    actual_=s.value("actual").toInteger();completed_=s.value("completed").toInt();id_=s.value("id").toString();task_=s.value("task").toString();
    started_=date(s,"started");lastDrink_=date(s,"drink");lastReminder_=date(s,"reminder");snoozeUntil_=date(s,"snooze");
    mutedDay_=QDate::fromString(s.value("muted_day").toString(),Qt::ISODate);waterPending_=settings_.waterEnabled && s.value("water_pending").toBool();
    segments_=s.value("segments").toArray();activeSinceDrink_=s.value("active_since_drink").toInteger();
    quietUntil_=now.utc.addSecs(120);lastSave_=now.utc;
    if(running_)recovery_="已恢复为暂停；关闭、锁屏与休眠期间不计入专注，请点击继续。";
    checkpoint();
}
QJsonObject FocusService::state() const {
    return {{"version",1},{"settings",settings_.json()},{"running",running_},{"phase",int(phase_)},{"paused",paused_},
        {"planned",planned_},{"remaining",remaining_},{"actual",actual_},{"completed",completed_},{"id",id_},{"task",task_},
        {"started",iso(started_)},{"drink",iso(lastDrink_)},{"reminder",iso(lastReminder_)},{"snooze",iso(snoozeUntil_)},
        {"muted_day",mutedDay_.toString(Qt::ISODate)},{"water_pending",waterPending_},{"active_since_drink",activeSinceDrink_},
        {"session_active_ms",sessionActive_},{"segments",segments_}};
}
void FocusService::checkpoint(){store_.saveWellness(state());lastSave_=now_.utc;}
int FocusService::duration(Phase p) const {return (p==Phase::Focus?settings_.focusSeconds:p==Phase::ShortBreak?settings_.shortSeconds:settings_.longSeconds)*1000;}
QString FocusService::phaseName() const{return phase_==Phase::Focus?"专注":phase_==Phase::ShortBreak?"短休息":"长休息";}
bool FocusService::quiet() const {
    return externalQuiet_ || phaseReminderQuiet();
}
bool FocusService::phaseReminderQuiet() const {
    if(!settings_.quietEnabled)return false;
    const auto t=now_.utc.toLocalTime().time();
    return settings_.quietStart==settings_.quietEnd || (settings_.quietStart<settings_.quietEnd?(t>=settings_.quietStart && t<settings_.quietEnd):(t>=settings_.quietStart || t<settings_.quietEnd));
}
void FocusService::addSegment(qint64 ms){
    if(ms<=0)return;
    const qint64 at=now_.utc.toMSecsSinceEpoch()-ms;
    if(!segments_.isEmpty()){
        auto last=segments_.last().toObject();
        if(qAbs(last.value("at").toInteger()+last.value("ms").toInteger()-at)<=20){last["ms"]=last.value("ms").toInteger()+ms;segments_.replace(segments_.size()-1,last);return;}
    }
    segments_.append(QJsonObject{{"at",at},{"ms",ms}});
}
void FocusService::tick(WellnessTime now,const ActivitySample &activity,bool userBusy,bool doNotDisturb){
    const auto delta=now.monotonicMs-previous_.monotonicMs;now_=now;activity_=activity;userBusy_=userBusy;externalQuiet_=doNotDisturb;
    const bool gap=delta<0 || delta>15000;
    const bool away=activity.locked || (activity.idleAvailable && activity.idleMs>=60000);
    if(gap || activity.locked){
        if(running_ && !paused_){paused_=true;recovery_="检测到锁屏或休眠/调度中断，已暂停，请点击继续。";}
        quietUntil_=now.utc.addSecs(120);notices_.clear();
    }
    if(away || wasAway_)quietUntil_=now.utc.addSecs(120);
    const bool active=activity.idleAvailable && !away && !wasAway_ && !gap;
    if(active && delta>0){sessionActive_+=delta;activeSinceDrink_+=delta;}
    wasAway_=away;
    if(running_ && !paused_ && !gap){
        const auto consumed=qMin(remaining_,qMax<qint64>(0,delta));remaining_-=consumed;
        if(phase_==Phase::Focus){actual_+=consumed;addSegment(consumed);}
        if(remaining_<=0)advance(true);
    }
    previous_=now;
    if(!gap && !away && !userBusy && waterDue()){
        const bool hasWater=std::any_of(notices_.cbegin(),notices_.cend(),[](const auto &n){return n.water;});
        const bool defer=focusing() && (settings_.deferFocus || (settings_.preferBreak && remaining_<=5*60000));
        if(!hasWater && (!defer || (settings_.silentDuringLongFocus && remaining_>15*60000))){QString key;offer(waterLine(&key),2,true,defer,key);}
    }
    if(gap || now.utc<lastSave_ || lastSave_.secsTo(now.utc)>=5)checkpoint();
    emit changed();
}
void FocusService::offer(QString text,int priority,bool water,bool silent,QString lineId){
    CompanionNotice n{QUuid::createUuid().toString(QUuid::WithoutBraces),std::move(text),std::move(lineId),now_.utc.addSecs(120),priority,water,silent};
    if(priority>=3){for(int i=notices_.size()-1;i>=0;--i)if(notices_[i].priority>=3 || (water && notices_[i].water))notices_.removeAt(i);notices_.prepend(n);}
    else notices_.enqueue(n);
    while(notices_.size()>4)notices_.removeLast();
}
bool FocusService::waterDue() const {
    if(!settings_.waterEnabled || waterPending_ || quiet() || mutedDay_==now_.utc.toLocalTime().date() || now_.utc<quietUntil_)return false;
    if(snoozeUntil_.isValid())return now_.utc>=snoozeUntil_;
    if(lastReminder_.isValid() && lastReminder_.secsTo(now_.utc)<15*60)return false;
    const auto since=lastDrink_.isValid()?lastDrink_.secsTo(now_.utc):previous_.utc.secsTo(now_.utc);
    // Before first confirmation, the monitored active total supplies the reliable baseline.
    const bool elapsed=lastDrink_.isValid()?since>=settings_.waterMinutes*60:sessionActive_>=settings_.waterMinutes*60000;
    if(settings_.useActivity)return activeSinceDrink_>=settings_.waterMinutes*60000 && elapsed;
    const auto baseline=lastDrink_.isValid()?lastDrink_:quietUntil_.addSecs(-120);
    return baseline.secsTo(now_.utc)>=settings_.waterMinutes*60;
}
QString FocusService::waterLine(QString *key){
    const int stage=store_.life().stageIndex();
    int index=QRandomGenerator::global()->bounded(3);if(index==lastWaterLine_)index=(index+1)%3;lastWaterLine_=index;
    *key=QString("water.%1.%2").arg(stage).arg(index);return DialogueEngine::find(*key).value("text").toString();
}

void FocusService::start(const QString &task,int requestedSeconds){
    if(running_)return;
    task_=task.trimmed().left(120);phase_=Phase::Focus;planned_=remaining_=(requestedSeconds>=60 && requestedSeconds<=10800)?requestedSeconds*1000:duration(phase_);actual_=0;segments_={};
    id_=QUuid::createUuid().toString(QUuid::WithoutBraces);started_=now_.utc;running_=true;paused_=false;recovery_.clear();
    const QStringList lines={"好啦，接下来一起认真工作吧！","今天也认真一点哦，我会陪着你的～","哼，我才不是特意陪你学习的呢！"};
    offer(lines[QRandomGenerator::global()->bounded(lines.size())],3);checkpoint();emit changed();
}
void FocusService::systemPause(const QString &reason){
    if(running_){paused_=true;recovery_=reason+"，已暂停，请点击继续。";}
    quietUntil_=now_.utc.addSecs(120);notices_.clear();checkpoint();emit changed();
}
void FocusService::pause(){if(!running_ || paused_)return;paused_=true;checkpoint();emit changed();}
void FocusService::resume(){if(!running_ || !paused_ || activity_.locked)return;paused_=false;previous_=now_;recovery_.clear();checkpoint();emit changed();}
void FocusService::finish(const QString &status){
    if(phase_!=Phase::Focus || id_.isEmpty()){checkpoint();return;}
    const QJsonObject record{{"id",id_},{"task",task_},{"started",iso(started_)},{"ended",iso(now_.utc)},
        {"planned_ms",planned_},{"actual_ms",actual_},{"status",status},{"segments",segments_},{"reward_day",now_.utc.toLocalTime().date().toString(Qt::ISODate)}};
    // Save the resulting phase and settle the unique cycle reward in one transaction.
    store_.saveWellness(state(),record);lastSave_=now_.utc;
}
void FocusService::advance(bool complete){
    const auto oldPhase=phase_;const auto oldId=id_;const auto oldPlanned=planned_;const auto oldActual=actual_;const auto oldSegments=segments_;const auto oldStarted=started_;
    if(oldPhase==Phase::Focus && complete)++completed_;
    const bool water=complete && oldPhase==Phase::Focus && waterDue();
    const Phase next=oldPhase==Phase::Focus?(complete && completed_>0 && completed_%settings_.longEvery==0?Phase::LongBreak:Phase::ShortBreak):Phase::Focus;
    phase_=next;planned_=remaining_=duration(next);actual_=0;segments_={};paused_=!settings_.autoNext;
    id_=QUuid::createUuid().toString(QUuid::WithoutBraces);started_=now_.utc;
    if(oldPhase==Phase::Focus){
        QJsonObject record{{"id",oldId},{"task",task_},{"started",iso(oldStarted)},{"ended",iso(now_.utc)},
            {"planned_ms",oldPlanned},{"actual_ms",oldActual},{"status",complete?"completed":"skipped"},{"segments",oldSegments},
            {"reward_day",now_.utc.toLocalTime().date().toString(Qt::ISODate)}};
        store_.saveWellness(state(),record);
    }else checkpoint();
    if(complete)offer(oldPhase==Phase::Focus?(water?"完成啦！辛苦了，休息一下，顺便喝点水吧～":"完成啦！已经很努力啦，休息也是很重要的哦！"):"休息结束啦，准备好后我们再一起专注吧～",3,water,false,water?"water.focus_complete":"");
    else offer(oldPhase==Phase::Focus?"已切换到休息阶段，先放松一下吧，我会陪着你的。":"已切换到专注阶段，准备好后我们一起认真工作吧。",3);
    emit sessionSaved();emit changed();
}
void FocusService::skip(){if(running_)advance(false);}
void FocusService::end(){
    if(!running_)return;
    running_=false;paused_=false;finish("ended");notices_.clear();
    offer("本轮专注已结束，先放松一下吧，我会陪着你的。",3);
    emit sessionSaved();emit changed();emit sessionEnded();
}
void FocusService::reset(){if(running_){running_=false;paused_=false;finish("reset");}phase_=Phase::Focus;remaining_=planned_=duration(phase_);actual_=0;segments_={};id_.clear();notices_.clear();checkpoint();emit changed();}
void FocusService::confirmWater(){lastDrink_=now_.utc;activeSinceDrink_=0;waterPending_=false;snoozeUntil_={};for(int i=notices_.size()-1;i>=0;--i)if(notices_[i].water)notices_.removeAt(i);checkpoint();emit changed();}
void FocusService::snoozeWater(){waterPending_=false;snoozeUntil_=now_.utc.addSecs(settings_.snoozeMinutes*60);for(int i=notices_.size()-1;i>=0;--i)if(notices_[i].water)notices_.removeAt(i);checkpoint();emit changed();}
void FocusService::requestWaterAfter(int minutes){
    if(minutes<1 || minutes>1440)throw std::runtime_error("Water reminder must be 1–1440 minutes");
    if(!settings_.waterEnabled)return;
    waterPending_=false;snoozeUntil_=now_.utc.addSecs(minutes*60);
    for(int i=notices_.size()-1;i>=0;--i)if(notices_[i].water)notices_.removeAt(i);
    checkpoint();emit changed();
}
void FocusService::muteWaterToday(){mutedDay_=now_.utc.toLocalTime().date();waterPending_=false;snoozeUntil_={};for(int i=notices_.size()-1;i>=0;--i)if(notices_[i].water)notices_.removeAt(i);checkpoint();emit changed();}
void FocusService::setSettings(const WellnessSettings &s){
    settings_=WellnessSettings::fromJson(s.json());
    if(!running_)planned_=remaining_=duration(phase_);
    if(!settings_.waterEnabled){waterPending_=false;snoozeUntil_={};for(int i=notices_.size()-1;i>=0;--i)if(notices_[i].water)notices_.removeAt(i);}
    checkpoint();emit changed();
}
bool FocusService::takeNotice(CompanionNotice &notice,bool deferPassive){
    if(userBusy_ || activity_.locked)return false;
    while(!notices_.isEmpty()){
        if(notices_.head().expires>=now_.utc && notices_.head().priority<3 && deferPassive)return false;
        auto n=notices_.dequeue();if(n.expires<now_.utc)continue;
        if(n.water && !waterDue()){if(n.priority<3)continue;n.water=false;n.lineId.clear();n.text="完成啦！辛苦了，稍微休息一下吧～";}
        if(n.water){waterPending_=true;lastReminder_=now_.utc;snoozeUntil_={};checkpoint();}
        // User-requested timer milestones obey timer settings; passive hydration
        // reminders also obey the companion's proactive quiet hours.
        n.silent=n.silent || (n.priority>=3?phaseReminderQuiet():quiet());notice=n;emit changed();return true;
    }
    return false;
}
void FocusService::shown(const CompanionNotice &n){if(n.water && !n.lineId.isEmpty())store_.collectCompanionLine(n.lineId,n.text);}
QString FocusService::waterStatus() const {
    if(!settings_.waterEnabled)return "喝水提醒已关闭";
    if(mutedDay_==now_.utc.toLocalTime().date())return "今天暂停，明天自动恢复";
    if(waterPending_)return "有一条待确认的喝水提醒";
    if(snoozeUntil_.isValid())return "已延后至 "+snoozeUntil_.toLocalTime().toString("HH:mm");
    if(quiet())return "勿扰时段 · 不播放提醒语音";
    if(settings_.useActivity && !activity_.idleAvailable)return "活跃检测不可用 · 暂停活跃累计（可改经过时间模式）";
    if(focusing() && settings_.deferFocus)return "专注中 · 优先等到休息再提醒";
    return QString("每累计 %1 分钟%2后评估提醒").arg(settings_.waterMinutes).arg(settings_.useActivity?"活跃使用":"经过时间");
}
QJsonObject FocusService::todayStats() const {
    const auto day=now_.utc.toLocalTime().date();auto stats=store_.focusStatistics(day);qint64 ms=stats.value("ms").toInteger();
    const qint64 a=QDateTime(day,QTime(0,0)).toUTC().toMSecsSinceEpoch(),b=QDateTime(day.addDays(1),QTime(0,0)).toUTC().toMSecsSinceEpoch();
    if(running_ && phase_==Phase::Focus)for(const auto &v:segments_){auto s=v.toObject();auto at=s.value("at").toInteger();ms+=qMax<qint64>(0,qMin(at+s.value("ms").toInteger(),b)-qMax(at,a));}
    stats["ms"]=ms;return stats;
}
QString FocusService::weekTrend() const {
    QStringList rows;const auto today=now_.utc.toLocalTime().date();
    for(int i=6;i>=0;--i){const auto day=today.addDays(-i);auto s=i==0?todayStats():store_.focusStatistics(day);rows<<QString("%1  %2 分钟  %3").arg(day.toString("MM-dd")).arg(s.value("ms").toInteger()/60000).arg(QString(qMin(20,int(s.value("ms").toInteger()/600000)),QChar(0x2587)));}
    return rows.join('\n');
}
}
