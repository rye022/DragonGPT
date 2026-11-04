#include "companion_life.h"
#include "emotion_engine.h"
#include <QCryptographicHash>
#include <QJsonArray>
#include <QRegularExpression>
#include <QSettings>
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace dragon {
QJsonObject LifeState::json() const {
    return {{"experience",experience},{"active_days",activeDays},{"daily_experience",dailyExperience},{"legacy_stage",legacyStage},
        {"growth_date",growthDate.toString(Qt::ISODate)},{"last_interaction",lastInteraction.toString(Qt::ISODateWithMs)},
        {"daily_messages",QJsonArray::fromStringList(dailyMessages)},{"mood",int(mood)},
        {"mood_until",moodUntil.toString(Qt::ISODateWithMs)},{"mood_since",moodSince.toString(Qt::ISODateWithMs)},{"last_proactive",lastProactive.toString(Qt::ISODateWithMs)},
        {"proactive_date",proactiveDate.toString(Qt::ISODate)},{"proactive_count",proactiveCount},
        {"topic",topic},{"awaiting_reply",awaitingReply},{"ignored_openings",ignoredOpenings},
        {"last_click_growth",lastClickGrowth.toString(Qt::ISODateWithMs)},
        {"last_chat_growth",lastChatGrowth.toString(Qt::ISODateWithMs)},
        {"last_focus_growth",lastFocusGrowth.toString(Qt::ISODateWithMs)},
        {"last_task_growth",lastTaskGrowth.toString(Qt::ISODateWithMs)},
        {"awarded_events",QJsonArray::fromStringList(awardedEvents)},{"growth_v5",growth.json()}};
}
LifeState LifeState::fromJson(const QJsonObject &j) {
    LifeState s;
    s.growth=GrowthProfile::fromJson(j["growth_v5"].toObject());
    s.experience=std::clamp(j.value("experience").toInt(),0,1000000);
    s.activeDays=std::clamp(j.value("active_days").toInt(),0,100000);
    s.legacyStage=std::clamp(j.value("legacy_stage").toInt(),0,2);
    s.dailyExperience=std::clamp(j.value("daily_experience").toInt(),0,20);
    s.growthDate=QDate::fromString(j.value("growth_date").toString(),Qt::ISODate);
    s.lastInteraction=QDateTime::fromString(j.value("last_interaction").toString(),Qt::ISODateWithMs);
    for(const auto &v:j.value("daily_messages").toArray()) if(s.dailyMessages.size()<32)s.dailyMessages.append(v.toString());
    const int moodValue=j.value("mood").toInt();s.mood=moodValue>=0 && moodValue<EmotionCount?Emotion(moodValue):Emotion::Calm;
    s.moodUntil=QDateTime::fromString(j.value("mood_until").toString(),Qt::ISODateWithMs);
    s.moodSince=QDateTime::fromString(j.value("mood_since").toString(),Qt::ISODateWithMs);
    s.lastProactive=QDateTime::fromString(j.value("last_proactive").toString(),Qt::ISODateWithMs);
    s.proactiveDate=QDate::fromString(j.value("proactive_date").toString(),Qt::ISODate);
    s.proactiveCount=std::clamp(j.value("proactive_count").toInt(),0,100);
    s.topic=std::clamp(j.value("topic").toInt(),0,1000000);
    s.awaitingReply=j.value("awaiting_reply").toBool();
    s.ignoredOpenings=std::clamp(j.value("ignored_openings").toInt(),0,10);
    s.lastClickGrowth=QDateTime::fromString(j["last_click_growth"].toString(),Qt::ISODateWithMs);
    s.lastChatGrowth=QDateTime::fromString(j["last_chat_growth"].toString(),Qt::ISODateWithMs);
    s.lastFocusGrowth=QDateTime::fromString(j["last_focus_growth"].toString(),Qt::ISODateWithMs);
    s.lastTaskGrowth=QDateTime::fromString(j["last_task_growth"].toString(),Qt::ISODateWithMs);
    for(const auto &v:j["awarded_events"].toArray())if(s.awardedEvents.size()<64)s.awardedEvents.append(v.toString());
    return s;
}
bool LifeState::award(const QString &kind,const QString &text,QDateTime now,const QString &eventId){
    if(!growth.enabled || !now.isValid())return false;
    if(kind!="click" && kind!="chat" && kind!="focus" && kind!="task")return false;
    if(kind=="chat" && text.trimmed().size()<2)return false;
    if((kind=="focus" || kind=="task") && (eventId.isEmpty() || awardedEvents.contains(eventId)))return false;
    const auto date=now.toLocalTime().date();
    if(!growthDate.isValid() || date>growthDate){growthDate=date;dailyExperience=0;dailyMessages.clear();++activeDays;growth.dailyClicks=growth.dailyChats=growth.dailyTasks=growth.dailyFocus=0;}
    auto &last=kind=="click"?lastClickGrowth:kind=="focus"?lastFocusGrowth:kind=="task"?lastTaskGrowth:lastChatGrowth;
    const int cooldown=kind=="click"?90:kind=="focus" || kind=="task"?1500:30;
    lastInteraction=now.toUTC();if(awaitingReply)ignoredOpenings=qMax(0,ignoredOpenings-1);awaitingReply=false;
    if(date!=growthDate || (last.isValid() && last.secsTo(now)<cooldown) || dailyExperience>=20)return false;
    // Reaching full affinity must not freeze personality or learned hours.
    // Keep a separate bounded stream of real non-click interactions, worth 0 XP.
    if(experience>=600 && (kind=="click" || growth.dailyChats+growth.dailyTasks+growth.dailyFocus>=10))return false;
    if(kind=="click" && growth.dailyClicks>=2)return false;
    const auto hash=QString::fromLatin1(QCryptographicHash::hash(text.simplified().toUtf8(),QCryptographicHash::Sha256).toHex());
    if(kind=="chat" && dailyMessages.contains(hash))return false;
    if(kind=="chat")dailyMessages.append(hash);
    if(kind=="focus" || kind=="task"){awardedEvents.append(eventId);while(awardedEvents.size()>64)awardedEvents.removeFirst();}
    const int earned=std::min({kind=="click"?1:kind=="focus"?4:2,20-dailyExperience,std::max(0,600-experience)});
    dailyExperience+=earned;experience+=earned;last=now.toUTC();growth.observe(kind,text,earned,now);return earned>0;
}
void LifeState::interact(const QString &text,QDateTime now) {
    if(text.trimmed().isEmpty())return;
    const auto previousInteraction=lastInteraction;
    award("chat",text,now);lastInteraction=now.toUTC();if(awaitingReply)ignoredOpenings=qMax(0,ignoredOpenings-1);awaitingReply=false;
    const auto emotion=EmotionEngine::classify(text,growthPercent());
    const bool distressed=EmotionEngine::distressed(mood);
    const bool weak=emotion==Emotion::Smile || emotion==Emotion::Shy || emotion==Emotion::Happy || emotion==Emotion::Tsundere || emotion==Emotion::Relaxed;
    const bool freshDistress=distressed && weak && moodUntil>now && previousInteraction.isValid() && previousInteraction.secsTo(now)<60;
    const bool freshGreeting=emotion==Emotion::Smile && mood!=Emotion::Calm && moodUntil>now && moodSince.isValid() && moodSince.secsTo(now)<30;
    if(!freshDistress && !freshGreeting && emotion!=Emotion::Calm){
        if(emotion!=mood || !moodSince.isValid() || now>=moodUntil)moodSince=now.toUTC();
        mood=emotion;moodUntil=now.toUTC().addSecs(EmotionEngine::holdSeconds(emotion));
    }
    if(moodUntil.isValid() && now>=moodUntil){mood=Emotion::Calm;moodUntil={};moodSince={};}
}
int LifeState::growthPercent() const{
    // Preserve all old XP/day gates and legacy floors. Map their continuous
    // progress into a global percentage; crossing a stage never resets to 0.
    const int xp[]={0,60,200,600},days[]={0,3,10,30},base[]={0,20,50,100};
    int level=std::clamp(legacyStage,0,2);
    for(int i=1;i<=3;++i)if(experience>=xp[i] && activeDays>=days[i])level=std::max(level,i);
    if(level==3)return 100;
    const int baselineDays=legacyStage>=level && legacyStage>0?0:days[level];
    const double ep=double(experience-xp[level])/(xp[level+1]-xp[level]);
    const double dp=double(activeDays-baselineDays)/(days[level+1]-baselineDays);
    return base[level]+int(std::floor(std::clamp(std::min(ep,dp),0.0,1.0)*(base[level+1]-base[level])));
}
int LifeState::stageIndex() const{const int n=growthPercent();int stage=0;for(const auto &gate:growth.thresholds)if(n>=gate.toInt())++stage;return stage;}
int LifeState::dialoguePercent() const{
    const int p=growthPercent(),stage=stageIndex(),canonical[]={0,20,50,80,100};
    const int low=stage?growth.thresholds[stage-1].toInt():0,high=stage==3?100:growth.thresholds[stage].toInt();
    return canonical[stage]+(p-low)*(canonical[stage+1]-canonical[stage])/(high-low);
}
int LifeState::affinityForDialogue(int required) const{
    const int canonical[]={0,20,50,80,100};const int custom[]={0,growth.thresholds[0].toInt(),growth.thresholds[1].toInt(),growth.thresholds[2].toInt(),100};
    required=std::clamp(required,0,100);int i=0;while(i<3 && required>=canonical[i+1])++i;
    return custom[i]+int(std::ceil(double(required-canonical[i])*(custom[i+1]-custom[i])/(canonical[i+1]-canonical[i])));
}
QString LifeState::stage() const {return QStringList{"初识期","熟悉期","亲近期","深度羁绊期"}[stageIndex()];}
QString LifeState::growthHint() const {
    return QString("亲密度 %1% · %2 · 真实互动累计成长，离开不会倒退").arg(growthPercent()).arg(stage());
}
LifeSettings LifeSettings::load(const QString &path) {
    QSettings q(path,QSettings::IniFormat);LifeSettings s;
    s.focusMode=q.value("focusMode",false).toBool();
    s.frequency=q.value("frequency","normal").toString();
    if(s.frequency!="quiet" && s.frequency!="active" && s.frequency!="off")s.frequency="normal";
    s.focusProtection=q.value("focusProtection",true).toBool();s.proactiveSpeech=q.value("proactiveSpeech",true).toBool();s.hideUnheard=q.value("hideUnheard",false).toBool();
    s.enabled=q.value("enabled",true).toBool();s.memorySuggestions=q.value("memorySuggestions",true).toBool();
    s.memoryFollowups=q.value("memoryFollowups",false).toBool();
    s.idleMinutes=std::clamp(q.value("idleMinutes",15).toInt(),1,240);
    s.cooldownMinutes=std::clamp(q.value("cooldownMinutes",90).toInt(),5,1440);
    s.dailyLimit=std::clamp(q.value("dailyLimit",3).toInt(),1,10);
    s.hourlyLimit=std::clamp(q.value("hourlyLimit",2).toInt(),1,10);
    s.autonomyEnabled=q.value("autonomyEnabled",true).toBool();s.taskSuggestions=q.value("taskSuggestions",false).toBool();
    s.experienceFollowups=q.value("experienceFollowups",false).toBool();s.explorationEnabled=q.value("explorationEnabled",false).toBool();
    s.quietStart=std::clamp(q.value("quietStart",22).toInt(),0,23);s.quietEnd=std::clamp(q.value("quietEnd",8).toInt(),0,23);
    s.pausedUntil=QDateTime::fromString(q.value("pausedUntil").toString(),Qt::ISODateWithMs);return s;
}
void LifeSettings::save(const QString &path) const {
    if(frequency!="quiet" && frequency!="normal" && frequency!="active" && frequency!="off")throw std::runtime_error("Invalid proactive frequency");
    if(idleMinutes<1 || idleMinutes>240 || cooldownMinutes<5 || cooldownMinutes>1440 || dailyLimit<1 || dailyLimit>10 || quietStart<0 || quietStart>23 || quietEnd<0 || quietEnd>23)
        throw std::runtime_error("Invalid companionship settings");
    if(hourlyLimit<1 || hourlyLimit>10)throw std::runtime_error("Invalid hourly limit");
    QSettings q(path,QSettings::IniFormat);q.setValue("enabled",enabled);q.setValue("memorySuggestions",memorySuggestions);
    q.setValue("memoryFollowups",memoryFollowups);
    q.setValue("focusMode",focusMode);
    q.setValue("frequency",frequency);q.setValue("focusProtection",focusProtection);q.setValue("proactiveSpeech",proactiveSpeech);q.setValue("hideUnheard",hideUnheard);
    q.setValue("idleMinutes",idleMinutes);q.setValue("cooldownMinutes",cooldownMinutes);q.setValue("dailyLimit",dailyLimit);
    q.setValue("quietStart",quietStart);q.setValue("quietEnd",quietEnd);q.sync();
    q.setValue("hourlyLimit",hourlyLimit);q.setValue("autonomyEnabled",autonomyEnabled);q.setValue("taskSuggestions",taskSuggestions);
    q.setValue("experienceFollowups",experienceFollowups);q.setValue("explorationEnabled",explorationEnabled);
    q.setValue("pausedUntil",pausedUntil.toUTC().toString(Qt::ISODateWithMs));q.sync();
    if(q.status()!=QSettings::NoError)throw std::runtime_error("Cannot save companionship settings");
}
bool LifeSettings::quiet(QDateTime now) const {
    const int hour=now.toLocalTime().time().hour();
    if(quietStart==quietEnd)return false;
    return quietStart<quietEnd?(hour>=quietStart && hour<quietEnd):(hour>=quietStart || hour<quietEnd);
}
QPair<int,int> LifeSettings::intervalSeconds() const{
    if(frequency=="quiet")return {900,1500};if(frequency=="active")return {180,360};return {300,720};
}
QString CompanionLife::blockedReason(const LifeState &s,const LifeSettings &p,QDateTime now,QDateTime activity,bool blocked,QDateTime snoozed) {
    if(!now.isValid())return "时钟不可用";
    if(!p.enabled || p.frequency=="off")return "主动互动已关闭";
    if(p.focusMode)return "手动勿扰";
    if(blocked)return "忙碌、语音、全屏、会议或专注保护";
    if(p.quiet(now))return "免打扰时段";
    if(snoozed.isValid() && now<snoozed)return "暂时安静陪伴";
    const auto date=now.toLocalTime().date();
    if(s.proactiveDate.isValid() && date<=s.proactiveDate && s.proactiveCount>=p.dailyLimit)return "今日主动次数已达上限";
    int count=0;for(const auto &v:s.growth.proactiveTimes){const auto t=QDateTime::fromString(v.toString(),Qt::ISODateWithMs);if(t.isValid() && t.secsTo(now)<3600)++count;}
    if(count>=p.hourlyLimit)return "每小时主动次数已达上限";
    if(s.lastProactive.isValid() && s.lastProactive.secsTo(now)<qint64(p.cooldownMinutes)*60*(1+std::min(3,s.ignoredOpenings)))return "冷却中；未回应时会延长间隔";
    // Short anti-overlap guard; actual randomized cadence belongs to the one-shot scheduler.
    if(activity.isValid() && activity.secsTo(now)<30)return "刚刚互动，稍后再来";
    return {};
}
bool CompanionLife::eligible(const LifeState &s,const LifeSettings &p,QDateTime now,QDateTime activity,bool blocked,QDateTime snoozed){
    return blockedReason(s,p,now,activity,blocked,snoozed).isEmpty();
}
QString CompanionLife::candidate(const QString &text) {
    const auto value=text.trimmed();
    // Conservative, local extraction: only short, direct first-person statements.
    if(value.size()<4 || value.size()>150 || value.contains(QRegularExpression("[?？\\n]")))return {};
    if(value.contains(QRegularExpression("不记|别记|不要记|忘记|例如|假设|如果|他说|她说|密码|密钥|API|身份证|银行卡|住址|电话|生日|病史|诊断|收入|sk-|[0-9]{7,}",QRegularExpression::CaseInsensitiveOption)))return {};
    // Keep the expression anchored to direct user statements.
    static const QRegularExpression fact("^我(?:喜欢|不喜欢|偏好|对.{1,40}感兴趣|(?:最近|目前|这段时间)?(?:刚开始|开始|正在|在)(?:开发|学习|制作|研究)|(?:最近)?(?:计划|打算|准备|完成了|遇到了)|是.{1,40}(?:开发者|工程师|学生)|的生日是).{0,100}[。！!]?$" );
    return fact.match(value).hasMatch()?value:QString{};
}
QString CompanionLife::tags(const QString &text) {
    if(text.contains("生日") || text.contains("开发者") || text.contains("工程师") || text.contains("人格") || text.contains("星座"))return "用户资料";
    if(text.contains("说话") || text.contains("语气") || text.contains("称呼") || text.contains("回复"))return "交流偏好";
    if(text.contains("喜欢") || text.contains("感兴趣") || text.contains("偏好"))return "偏好，话题";
    return "话题";
}
}
