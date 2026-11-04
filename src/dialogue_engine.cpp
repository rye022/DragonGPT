#include "dialogue_engine.h"
#include "emotion_engine.h"
#include "dialogue_catalog.h"
#include <QJsonDocument>
#include <QRandomGenerator>
#include <QSet>
#include <QRegularExpression>
#include <algorithm>
#include <cmath>
namespace dragon {
namespace {
int period(QDateTime now){const int h=now.toLocalTime().time().hour();return h>=6 && h<11?0:h>=11 && h<17?1:2;}
QSet<QString> pairs(const QString &s){QSet<QString> out;for(int i=0;i+1<s.size();++i)if(s[i].isLetter() && s[i+1].isLetter())out.insert(s.mid(i,2));return out;}
double similarity(const QString &a,const QString &b){auto x=pairs(a),y=pairs(b);const int n=x.size()+y.size();return n?2.0*x.intersect(y).size()/n:0;}
}
const QJsonArray &DialogueEngine::catalog(){static const auto data=QJsonDocument::fromJson(dialogueCatalogJson).array();return data;}
int DialogueEngine::requiredAffinity(const QJsonObject &l){
    static const int gates[]={0,20,50,80};const int stage=l["stage"].toInt(-1);
    return std::max(l["minPercent"].toInt(),stage>=0 && stage<4?gates[stage]:0);
}
QStringList DialogueEngine::categories(){return {"单击互动","主动互动","待机陪伴","工作陪伴","休息提醒","开心","害羞","傲娇","特殊亲密度台词"};}
QString DialogueEngine::category(const QJsonObject &l){
    const auto kind=l["kind"].toString(),topic=l["topic"].toString();
    if(kind=="hidden")return "特殊亲密度台词";
    if(l["emotion"].toInt(-1)==int(Emotion::Shy) || topic=="shy")return "害羞";
    if(topic=="happy" || l["emotion"].toInt(-1)==int(Emotion::Happy))return "开心";
    if(topic=="tease" || topic=="tsundere")return "傲娇";
    if(topic=="work" || l["text"].toString().contains(QRegularExpression("工作|代码|任务|专注|你忙|忙你的")))return "工作陪伴";
    if(topic=="rest" || l["emotion"].toInt(-1)==int(Emotion::Sleepy))return "休息提醒";
    if(kind=="proactive" || kind=="memory")return "主动互动";
    if(topic=="company" || topic=="care" || topic=="trust" || kind=="time")return "待机陪伴";
    return "单击互动";
}
QJsonObject DialogueEngine::find(const QString &id){for(const auto &v:catalog())if(v.toObject()["id"].toString()==id)return v.toObject();return {};}
QString DialogueEngine::relationStyle(int p){
    if(p>=80)return "深度羁绊期：信任、默契、亲密自然，尊重独处，不索取承诺，不捏造经历";
    if(p>=50)return "亲近期：自然关心、偶尔含蓄撒娇，保留轻微傲娇";
    if(p>=20)return "熟悉期：放松俏皮、偶尔轻轻吐槽，避免过分亲密";
    return "初识期：略拘谨、好奇、轻微傲娇，关心但保持礼貌距离";
}
bool DialogueEngine::eligible(const QJsonObject &line,const DialogueContext &c,int clicks){
    if(c.percent<requiredAffinity(line))return false;
    const auto kind=line["kind"].toString();
    if(kind=="memory")return false; // Private memory is chat context, never an ambient script.
    if(kind=="hydration")return false; // Scheduled only by the hydration service.
    if(c.proactive){if(kind!="proactive" && kind!="memory")return false;}
    else if(kind=="proactive")return false;
    const int stage=line["stage"].toInt(-1);
    // A small transition pool is allowed only for ordinary text, never unlocks.
    if(stage>=0 && stage!=c.stage()){
        const int boundaries[]={20,50,80};
        if(kind!="ordinary" || stage!=c.stage()+1 || c.stage()>=3 || c.percent<boundaries[c.stage()]-2)return false;
    }
    if(c.percent<line["minPercent"].toInt() || clicks<line["minClicks"].toInt())return false;
    if(line.contains("emotion") && !EmotionEngine::matches(Emotion(line["emotion"].toInt()),c.emotion))return false;
    if(line.contains("period") && line["period"].toInt()!=period(c.now))return false;
    const auto topic=line["topic"].toString();
    if((c.emotion==Emotion::Sleepy || c.emotion==Emotion::Relaxed) && topic=="work")return false;
    if(c.emotion==Emotion::Focused && (topic=="rest" || topic=="tease"))return false;
    if(c.sessionMinutes<line["sessionMinutes"].toInt())return false;
    if(kind=="rapid" && !c.rapid)return false;
    if(kind=="memory" && c.memories.isEmpty())return false;
    // Never offer cheerful teasing while the user is distressed or sleepy.
    if((c.emotion==Emotion::Worried || c.emotion==Emotion::Sad || c.emotion==Emotion::Angry || c.emotion==Emotion::Sleepy)
        && (line["topic"]=="happy" || line["topic"]=="tease"))return false;
    return true;
}
DialogueSelection DialogueEngine::select(const DialogueContext &c,QJsonObject &state,quint32 seed){
    QRandomGenerator rng(seed?seed:QRandomGenerator::global()->generate());
    const int clicks=std::min(1000000,state["clicks"].toInt()+(!c.proactive));
    const QString historyKey=c.proactive?"proactive_recent":"recent";
    const auto recent=state[historyKey].toArray();auto used=state["used"].toObject();
    struct Candidate{QJsonObject line;double weight;};QVector<Candidate> candidates;double sum=0;
    // Hard exclusion for the latest 30, then progressively relax only recency
    // if a genuinely small eligible pool is exhausted. Unlock/meaning guards stay.
    for(int exclude:{30,15,1,0}){
        candidates.clear();sum=0;QSet<QString> banned;
        for(int i=std::max(0,int(recent.size())-exclude);i<recent.size();++i)banned.insert(recent[i].toObject()["id"].toString());
        for(const auto &value:catalog()){
            const auto line=value.toObject();const auto id=line["id"].toString();
            if(!eligible(line,c,clicks) || banned.contains(id))continue;
            const auto last=QDateTime::fromString(used.value(id).toString(),Qt::ISODateWithMs);
            const int cooldown=exclude==0 && line["kind"]!="hidden"?std::min(300,line["cooldown"].toInt()):line["cooldown"].toInt();
            if(last.isValid() && last.secsTo(c.now)<cooldown)continue;
            double weight=line["weight"].toDouble(1)*EmotionEngine::topicWeight(c.emotion,line["topic"].toString());
            if(!c.traits.isEmpty()){
                const auto topic=line["topic"].toString();
                const QString trait=topic=="care" || topic=="rest"?"care":topic=="tease" || topic=="tsundere"?"tsundere":topic=="work"?"initiative":topic=="happy"?"playfulness":topic=="curious"?"curiosity":"humor";
                weight*=.5+c.traits[trait].toDouble(50)/100.0;
            }
            if(line["stage"].toInt(c.stage())!=c.stage())weight*=.12;
            for(int i=std::max(0,int(recent.size())-8);i<recent.size();++i){
                const auto previous=recent[i].toObject();
                const double near=similarity(line["text"].toString(),previous["text"].toString());
                weight*=std::max(.1,1-near*.9);
                if(i==recent.size()-1 && line["topic"]==previous["topic"])weight*=.25;
            }
            if(c.action==PetAction::Sleep && line["topic"]=="rest")weight*=1.6;
            if(c.action==PetAction::Comfort && line["topic"]=="care")weight*=1.6;
            candidates.append({line,weight});sum+=weight;
        }
        if(candidates.size()>1 && !recent.isEmpty()){
            const auto latest=recent.last().toObject()["id"].toString();
            candidates.erase(std::remove_if(candidates.begin(),candidates.end(),[&](const Candidate &v){if(v.line["id"]==latest){sum-=v.weight;return true;}return false;}),candidates.end());
        }
        if(!candidates.isEmpty())break;
    }
    if(candidates.isEmpty())return {};
    double draw=rng.generateDouble()*sum;auto chosen=candidates.last().line;
    for(const auto &item:candidates){draw-=item.weight;if(draw<=0){chosen=item.line;break;}}
    QString text=chosen["text"].toString();
    QJsonArray next=recent;next.append(QJsonObject{{"id",chosen["id"]},{"text",text},{"topic",chosen["topic"]}});
    while(next.size()>60)next.removeAt(0);
    used[chosen["id"].toString()]=c.now.toUTC().toString(Qt::ISODateWithMs);
    // Bounded by the fixed catalog IDs; discard stale/foreign IDs on every save.
    QJsonObject bounded;for(const auto &v:catalog()){const auto id=v.toObject()["id"].toString();if(QDateTime::fromString(used.value(id).toString(),Qt::ISODateWithMs).isValid())bounded[id]=used.value(id);}
    state[historyKey]=next;state["used"]=bounded;state["clicks"]=clicks;
    return {chosen["id"].toString(),text,chosen["topic"].toString(),chosen["kind"]=="hidden",chosen["minPercent"].toInt(),0};
}
}
