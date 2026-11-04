#include "growth_profile.h"
#include "privacy.h"
#include <QJsonDocument>
#include <QRegularExpression>
#include <QUuid>
#include <algorithm>
#include <cmath>

namespace dragon {
QStringList GrowthProfile::keys(){return {"playfulness","tsundere","curiosity","care","initiative","humor"};}
QStringList GrowthProfile::labels(){return {"活泼","傲娇","好奇","关怀","主动","幽默"};}
GrowthProfile::GrowthProfile(){
    const int defaults[]={38,22,70,82,55,35};const auto names=keys();
    for(int i=0;i<names.size();++i){baseline[names[i]]=defaults[i];drift[names[i]]=0;locked[names[i]]=false;}
    for(int i=0;i<24;++i)habits.append(0);
}
bool GrowthProfile::validThresholds(const QJsonArray &a){
    if(a.size()!=3)return false;int last=0;
    for(const auto &v:a){const double x=v.toDouble(-1);if(!v.isDouble() || !std::isfinite(x) || std::floor(x)!=x || x<=last || x>=100)return false;last=int(x);}return true;
}
QJsonObject GrowthProfile::json() const{
    return {{"version",5},{"enabled",enabled},{"personality_enabled",personalityEnabled},{"habits_enabled",habitsEnabled},
        {"shared_enabled",sharedEnabled},{"idle_enabled",idleEnabled},{"thresholds",thresholds},{"trust",trust},
        {"daily_clicks",dailyClicks},{"daily_chats",dailyChats},{"daily_tasks",dailyTasks},{"daily_focus",dailyFocus},
        {"baseline",baseline},{"drift",drift},{"locked",locked},{"habits",habits},{"events",events},{"shared",shared},
        {"proactive_times",proactiveTimes},{"last_reason",lastReason}};
}
GrowthProfile GrowthProfile::fromJson(const QJsonObject &j){
    GrowthProfile p;if(j.isEmpty())return p;
    p.enabled=j["enabled"].toBool(true);p.personalityEnabled=j["personality_enabled"].toBool(true);
    p.habitsEnabled=j["habits_enabled"].toBool(true);p.sharedEnabled=j["shared_enabled"].toBool(true);p.idleEnabled=j["idle_enabled"].toBool(true);
    if(validThresholds(j["thresholds"].toArray()))p.thresholds=j["thresholds"].toArray();
    p.trust=std::clamp(j["trust"].toInt(),0,600);
    p.dailyClicks=std::clamp(j["daily_clicks"].toInt(),0,20);p.dailyChats=std::clamp(j["daily_chats"].toInt(),0,20);
    p.dailyTasks=std::clamp(j["daily_tasks"].toInt(),0,20);p.dailyFocus=std::clamp(j["daily_focus"].toInt(),0,20);
    for(const auto &k:keys()){
        p.baseline[k]=std::clamp(j["baseline"].toObject()[k].toInt(p.baseline[k].toInt()),0,100);
        p.drift[k]=std::clamp(j["drift"].toObject()[k].toInt(),-100,100);p.locked[k]=j["locked"].toObject()[k].toBool();
    }
    const auto hours=j["habits"].toArray();for(int i=0;i<std::min(24,int(hours.size()));++i)p.habits[i]=std::clamp(hours[i].toInt(),0,10000);
    auto bounded=[](QJsonArray a,int max){while(a.size()>max)a.removeAt(0);return a;};
    p.events=bounded(j["events"].toArray(),200);p.shared=bounded(j["shared"].toArray(),100);
    p.proactiveTimes=bounded(j["proactive_times"].toArray(),100);p.lastReason=j["last_reason"].toString().left(200);return p;
}
QJsonObject GrowthProfile::traits() const{
    QJsonObject result;for(const auto &k:keys())result[k]=std::clamp(baseline[k].toInt()+(enabled && personalityEnabled && !locked[k].toBool()?drift[k].toInt()/10.0:0),0.0,100.0);return result;
}
void GrowthProfile::event(const QString &kind,const QString &reason,QDateTime now,int earned){
    events.append(QJsonObject{{"kind",kind},{"reason",reason.left(200)},{"at",now.toUTC().toString(Qt::ISODateWithMs)},{"earned",earned}});
    while(events.size()>200)events.removeAt(0);
}
void GrowthProfile::observe(const QString &kind,const QString &text,int earned,QDateTime now){
    if(!enabled || earned<0)return;
    if(kind=="click")++dailyClicks;else if(kind=="chat")++dailyChats;else if(kind=="focus")++dailyFocus;else ++dailyTasks;
    if(kind!="click")trust=std::min(600,trust+std::max(1,earned));
    if(habitsEnabled){const int h=now.toLocalTime().time().hour();habits[h]=std::min(10000,habits[h].toInt()+1);}
    QStringList influenced;
    if(personalityEnabled && kind!="click"){
        auto change=[&](const QString &key){if(locked[key].toBool())return;drift[key]=std::min(100,drift[key].toInt()+1);influenced.append(labels()[keys().indexOf(key)]);};
        if(kind=="focus" || kind=="task")change("initiative");
        if(text.contains(QRegularExpression("谢谢|辛苦|休息|关心|照顾")))change("care");
        if(text.contains(QRegularExpression("学习|研究|为什么|探索|如何")))change("curiosity");
        if(text.contains(QRegularExpression("开心|一起玩|好玩")))change("playfulness");
        if(text.contains(QRegularExpression("哈哈|笑话|幽默")))change("humor");
        if(text.contains(QRegularExpression("傲娇|逗你")))change("tsundere");
    }
    const QString source=kind=="click"?"单击互动":kind=="chat"?"有效交流":kind=="focus"?"完成专注":"共同完成任务";
    event(kind,source+(influenced.isEmpty()?QString{}:"；缓慢影响："+influenced.join("、")),now,earned);
}
void GrowthProfile::recordProactive(const QString &kind,const QString &reason,QDateTime now){
    QJsonArray recent;for(const auto &v:proactiveTimes){const auto t=QDateTime::fromString(v.toString(),Qt::ISODateWithMs);if(t.isValid() && t.secsTo(now)<86400)recent.append(v);}
    recent.append(now.toUTC().toString(Qt::ISODateWithMs));while(recent.size()>100)recent.removeAt(0);proactiveTimes=recent;
    lastReason=reason;event("自主行为·"+kind,reason,now);
}
bool GrowthProfile::addShared(const QString &value,const QString &source,QDateTime now){
    const auto title=value.trimmed();
    if(!enabled || !sharedEnabled || title.size()<2 || title.size()>200 || looksLikeCredential(title))return false;
    if(title.contains(QRegularExpression("密码|密钥|身份证|银行卡|住址|电话|病史|诊断|[0-9]{7,}")))return false;
    const auto canonical=[](QString s){return s.normalized(QString::NormalizationForm_KC).simplified().toCaseFolded().remove(QRegularExpression("[。！! ]"));};
    for(const auto &v:shared)if(canonical(v.toObject()["title"].toString())==canonical(title))return false;
    shared.append(QJsonObject{{"id",QUuid::createUuid().toString(QUuid::WithoutBraces)},{"title",title},{"source",source.left(40)},
        {"at",now.toUTC().toString(Qt::ISODateWithMs)},{"confidence",1.0},{"confirmed",true}});
    while(shared.size()>100)shared.removeAt(0);event("共同经历","保存了一条已确认的共同经历",now);return true;
}
QString GrowthProfile::sharedContext(const QString &query) const{
    if(!enabled || !sharedEnabled)return {};QJsonArray result;
    for(int i=int(shared.size())-1;i>=0 && result.size()<3;--i){const auto e=shared[i].toObject();const auto title=e["title"].toString();
        bool match=query.contains("共同经历") || query.contains("回忆");for(int n=0;n+1<query.size() && !match;++n)if(title.contains(query.mid(n,2),Qt::CaseInsensitive))match=true;
        if(match && e["confirmed"].toBool() && !looksLikeCredential(title))result.append(e);
    }
    return QString::fromUtf8(QJsonDocument(result).toJson(QJsonDocument::Compact));
}
bool GrowthProfile::preferredHour(QDateTime now) const{
    if(!enabled || !habitsEnabled)return true;int sum=0,peak=0;for(const auto &v:habits){sum+=v.toInt();peak=std::max(peak,v.toInt());}
    if(sum<20)return true;const int h=now.toLocalTime().time().hour();return habits[h].toInt()+habits[(h+23)%24].toInt()+habits[(h+1)%24].toInt()>=std::max(1,peak/3);
}
}
