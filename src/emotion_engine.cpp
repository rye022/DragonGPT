#include "emotion_engine.h"
#include <QRegularExpression>
#include <algorithm>
#include <cmath>
namespace dragon {
int EmotionEngine::holdSeconds(Emotion e){
    switch(e){
    case Emotion::Smile:return 90;case Emotion::Happy:return 360;case Emotion::Shy:return 180;
    case Emotion::Tsundere:return 180;case Emotion::Relaxed:return 300;case Emotion::Focused:return 180;
    case Emotion::Curious:case Emotion::Thinking:return 120;case Emotion::Excited:return 300;
    case Emotion::Sleepy:return 900;case Emotion::Worried:return 2700;case Emotion::Sad:return 1200;
    case Emotion::Angry:return 480;default:return 0;
    }
}
EmotionState EmotionEngine::desired(QDateTime now) const{
    if(contextReady_ && context_.focusing)return {Emotion::Focused,.68,0,now.addSecs(30),"番茄钟专注中"};
    if(persistent_.expiresAt>now && distressed(persistent_.emotion)){
        auto p=persistent_;const auto ratio=std::clamp(double(now.secsTo(p.expiresAt))/qMax(1,holdSeconds(p.emotion)),0.0,1.0);
        p.intensity*=.2+.8*ratio;p.reason="最近聊天中的情绪仍在持续";return p;
    }
    if(contextReady_ && context_.resting)return {Emotion::Relaxed,.58,0,now.addSecs(30),"番茄钟休息中"};
    if(persistent_.expiresAt>now){
        auto p=persistent_;const auto ratio=std::clamp(double(now.secsTo(p.expiresAt))/qMax(1,holdSeconds(p.emotion)),0.0,1.0);
        p.intensity*=.2+.8*ratio;p.reason="最近聊天中的情绪仍在持续";return p;
    }
    if(contextReady_ && !context_.busy){
        const qint64 since=context_.lastInteraction.isValid()?qMax<qint64>(0,context_.lastInteraction.secsTo(now)):0;
        const int hour=now.toLocalTime().time().hour();
        const bool idle=(context_.idleSeconds>=90) || (context_.idleSeconds<0 && since>=180);
        if((hour>=23 || hour<7) && idle)return {Emotion::Sleepy,.55,0,now.addSecs(30),"夜间且已有一段时间没有互动"};
        if(context_.returnedAt.isValid() && context_.returnedAt.secsTo(now)>=0 && context_.returnedAt.secsTo(now)<90)
            return {context_.affinity>=50?Emotion::Smile:Emotion::Curious,.42,0,context_.returnedAt.addSecs(90),"离开一段时间后重新回来"};
        if(since>=180 || context_.idleSeconds>=180)
            return {Emotion::Relaxed,context_.affinity>=50?.5:.35,0,now.addSecs(30),"安静待机，放松陪伴"};
    }
    return {Emotion::Calm,0,0,{},"平静陪伴"};
}
void EmotionEngine::rebuildBaseline(QDateTime now,bool immediate){
    const auto wanted=desired(now);
    if(wanted.emotion!=baseline_.emotion && !immediate){
        if(!pendingAt_.isValid() || pending_!=wanted.emotion){pending_=wanted.emotion;pendingAt_=now;}
        // Ambient causes must persist; explicit chat/focus transitions use immediate=true.
        if(pendingAt_.secsTo(now)<15 || (baselineChangedAt_.isValid() && baselineChangedAt_.secsTo(now)<45))return;
    }
    const bool different=wanted.emotion!=baseline_.emotion;
    baseline_=wanted;pendingAt_={};
    if(different || !baselineChangedAt_.isValid())baselineChangedAt_=now;
    if(state_.priority==0){
        const bool changedState=state_.emotion!=baseline_.emotion || std::abs(state_.intensity-baseline_.intensity)>.025 || state_.reason!=baseline_.reason;
        state_=baseline_;if(changedState)emit changed();
    }
}
bool EmotionEngine::request(Emotion emotion,double intensity,int durationMs,int priority,QDateTime now){
    tick(now);if(priority<state_.priority)return false;
    state_={emotion,std::clamp(intensity,0.0,1.0),priority,now.addMSecs(std::clamp(durationMs,1,300000)),"当前聊天或操作的即时反应"};
    requestedAt_=now;requestedIntensity_=state_.intensity;emit changed();return true;
}
void EmotionEngine::tick(QDateTime now){
    if(persistent_.expiresAt.isValid() && now>=persistent_.expiresAt){persistent_={};rebuildBaseline(now,true);}
    else if(baseline_.expiresAt.isValid() && now>=baseline_.expiresAt)rebuildBaseline(now,false);
    if(state_.priority>0 && state_.expiresAt.isValid() && now>=state_.expiresAt){state_=baseline_;requestedAt_={};emit changed();}
    else if(requestedAt_.isValid() && state_.priority>0){
        const double total=qMax<qint64>(1,requestedAt_.msecsTo(state_.expiresAt));
        const double remaining=std::clamp(now.msecsTo(state_.expiresAt)/total,0.0,1.0);
        const double target=baseline_.emotion==state_.emotion?baseline_.intensity:0;
        const double strength=target+(requestedIntensity_-target)*remaining;
        if(std::abs(strength-state_.intensity)>.025){state_.intensity=strength;emit changed();}
    }
}
void EmotionEngine::reset(QDateTime now){
    if(persistent_.expiresAt.isValid() && now>=persistent_.expiresAt)persistent_={};
    rebuildBaseline(now,true);state_=baseline_;requestedAt_={};emit changed();
}
void EmotionEngine::setBaseline(Emotion emotion,double intensity,QDateTime expiresAt,QDateTime now){
    persistent_=expiresAt>now?EmotionState{emotion,std::clamp(intensity,0.0,1.0),0,expiresAt,"最近聊天中的情绪仍在持续"}:EmotionState{};
    rebuildBaseline(now,true);
}
void EmotionEngine::updateContext(const EmotionContext &c){
    const bool immediate=!contextReady_ || c.focusing!=context_.focusing || c.resting!=context_.resting;
    context_=c;contextReady_=true;tick(c.now);rebuildBaseline(c.now,immediate);
}
Emotion EmotionEngine::moodAt(QDateTime now) const{
    if(state_.expiresAt.isValid() && now>=state_.expiresAt)return pendingAt_.isValid()?baseline_.emotion:desired(now).emotion;
    return state_.emotion;
}
AvatarParameters EmotionEngine::avatarParameters() const{
    AvatarParameters p;
    switch(state_.emotion){
    case Emotion::Shy:case Emotion::Tsundere:p.blush=state_.intensity;p.headAngle=-5*state_.intensity;break;
    case Emotion::Thinking:case Emotion::Curious:case Emotion::Focused:p.headAngle=6*state_.intensity;break;
    case Emotion::Sleepy:p.eyeOpen=.35;break;
    case Emotion::Happy:case Emotion::Excited:p.eyeOpen=.8;break;
    default:break;
    }return p;
}
Emotion EmotionEngine::classify(const QString &text,int affinity){
    // A direct expression of reluctance must replace an earlier work mood,
    // while leaving the actual focus timer and its explicit priority intact.
    if(QRegularExpression("^我(?:今天|现在|最近|有点|真的)?(?:不想|不太想|不愿意|没心情)(?:再)?(?:工作|学习|写代码)").match(text.trimmed()).hasMatch())return Emotion::Worried;
    QString value=text;
    for(const auto &negated:QStringList{"不累","不困","不生气","不难过","没有压力","不焦虑","没完成","未完成","没成功","不成功","不可爱","不想休息","不想睡","不想工作","不想学习","并不兴奋","不兴奋","不害羞","不放松"})value.remove(negated);
    const bool technical=value.contains(QRegularExpression("怎么实现|如何实现|状态机|情绪系统|枚举|编译|报错|动画代码"));
    if(technical && !value.contains(QRegularExpression("我(很|好|有点|真的).*(累|难过|焦虑|生气)")))return Emotion::Thinking;
    if(value.contains("不开心") || value.contains("不高兴"))return Emotion::Worried;
    if(value.contains(QRegularExpression("生气|气死|烦死|愤怒")))return Emotion::Angry;
    if(value.contains(QRegularExpression("难过|失落|伤心|想哭|低落")))return Emotion::Sad;
    if(value.contains(QRegularExpression("难受|压力|累|焦虑")))return Emotion::Worried;
    if(value.contains(QRegularExpression("晚安|困了|想睡|好困")))return Emotion::Sleepy;
    if(value.contains(QRegularExpression("害羞|不好意思")))return Emotion::Shy;
    if(value.contains(QRegularExpression("傲娇|嘴硬|才不是|哼")))return Emotion::Tsundere;
    if(value.contains(QRegularExpression("好看|可爱|漂亮|你真棒|你太棒|你好棒|你真厉害")))return affinity>=20?Emotion::Shy:Emotion::Happy;
    if(value.contains(QRegularExpression("兴奋|激动|好耶|太棒了|终于通关")))return Emotion::Excited;
    if(value.contains(QRegularExpression("放松|休息一下|歇会|发会儿呆|慢慢陪")))return Emotion::Relaxed;
    if(value.contains(QRegularExpression("认真工作|专心|专注|开始工作|开始学习|一起学习|写代码")))return Emotion::Focused;
    if(value.contains(QRegularExpression("完成|成功|开心|高兴|谢谢你")))return Emotion::Happy;
    if(value.contains("为什么") || value.contains("怎么"))return Emotion::Thinking;
    if(value.contains("好奇") || value.contains('?') || value.contains("？"))return Emotion::Curious;
    if(value.contains("你好") || value.contains("早安") || value.contains("回来"))return Emotion::Smile;
    return Emotion::Calm;
}
QList<PetAction> EmotionEngine::actionsFor(Emotion e){
    switch(e){
    case Emotion::Smile:return {PetAction::Wave,PetAction::Idle,PetAction::Comfort};
    case Emotion::Happy:return {PetAction::Wave,PetAction::Comfort,PetAction::Idle};
    case Emotion::Shy:return {PetAction::Shy,PetAction::Comfort};
    case Emotion::Tsundere:return {PetAction::Jealous,PetAction::Shy,PetAction::Idle};
    case Emotion::Sleepy:return {PetAction::Sleep,PetAction::Comfort};
    case Emotion::Relaxed:return {PetAction::Comfort,PetAction::Idle};
    case Emotion::Focused:return {PetAction::Work};
    case Emotion::Thinking:return {PetAction::Work,PetAction::Idle};
    case Emotion::Curious:return {PetAction::Idle,PetAction::Work};
    case Emotion::Excited:return {PetAction::Wave,PetAction::Shy,PetAction::Comfort};
    case Emotion::Worried:case Emotion::Sad:return {PetAction::Comfort,PetAction::Idle};
    case Emotion::Angry:return {PetAction::Jealous,PetAction::Comfort};
    default:return {PetAction::Idle,PetAction::Comfort};
    }
}
bool EmotionEngine::matches(Emotion required,Emotion current){
    if(required==current)return true;
    if(current==Emotion::Focused)return required==Emotion::Thinking;
    if(current==Emotion::Excited)return required==Emotion::Happy;
    if(current==Emotion::Tsundere)return required==Emotion::Shy;
    if(current==Emotion::Relaxed)return required==Emotion::Calm || required==Emotion::Smile;
    return false;
}
double EmotionEngine::topicWeight(Emotion e,const QString &topic){
    const bool rest=topic=="rest" || topic=="company" || topic=="care" || topic=="trust";
    switch(e){
    case Emotion::Focused:case Emotion::Thinking:return topic=="work"?4:topic=="tease" || topic=="happy"?.15:1;
    case Emotion::Sleepy:case Emotion::Relaxed:return rest?4:topic=="work" || topic=="tease" || topic=="happy"?.1:1;
    case Emotion::Shy:return topic=="shy"?4:1;
    case Emotion::Tsundere:return topic=="tease" || topic=="tsundere" || topic=="shy"?4:1;
    case Emotion::Happy:case Emotion::Excited:return topic=="happy"?4:rest?.7:1;
    case Emotion::Worried:case Emotion::Sad:return rest?4:1;
    case Emotion::Curious:return topic=="question" || topic=="curious"?4:1;
    default:return 1;
    }
}
}
