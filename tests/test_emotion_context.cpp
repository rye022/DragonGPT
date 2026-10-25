#include "companion.h"
#include "speech_expression.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QTextStream>
#include <QJsonDocument>
#include <stdexcept>
using namespace dragon;
namespace {
int checks=0;void expect(bool b,const char *m){++checks;if(!b)throw std::runtime_error(m);}
QDateTime noon(){return QDateTime(QDate(2026,10,23),QTime(12,0)).toUTC();}
void classification(){
    static_assert(int(Emotion::Angry)==9 && int(Emotion::Tsundere)==10);
    const QList<QPair<QString,Emotion>> cases={{"你好",Emotion::Smile},{"我完成了",Emotion::Happy},{"我有点害羞",Emotion::Shy},{"小傲娇，又嘴硬啦",Emotion::Tsundere},{"我困了",Emotion::Sleepy},{"放松一下，休息一下",Emotion::Relaxed},{"一起认真工作",Emotion::Focused},{"我很好奇？",Emotion::Curious},{"好耶，太棒了",Emotion::Excited},{"我好难过",Emotion::Sad},{"我生气了",Emotion::Angry},{"我有点累",Emotion::Worried},{"为什么会这样",Emotion::Thinking},{"普通的陈述",Emotion::Calm}};
    for(const auto &test:cases)expect(EmotionEngine::classify(test.first)==test.second,"classification matrix");
    expect(EmotionEngine::classify("不困、不累，也不生气，任务没完成")==Emotion::Calm,"negation");
    expect(EmotionEngine::classify("兴奋但压力很大")==Emotion::Worried,"distress wins celebration");
    expect(EmotionEngine::classify("害羞状态机怎么实现")==Emotion::Thinking,"technical question is not personal affect");
    expect(EmotionEngine::classify("你好可爱",0)==Emotion::Happy && EmotionEngine::classify("你好可爱",60)==Emotion::Shy,"affinity changes praise response");
    expect(EmotionEngine::classify("并不兴奋，也不放松")==Emotion::Calm,"new negation guards");
}
void timing(){
    auto now=noon();EmotionEngine e;e.setBaseline(Emotion::Shy,.7,now.addSecs(180),now);
    expect(e.state().emotion==Emotion::Shy,"past injected clock baseline accepted");
    EmotionContext c;c.now=now;c.lastInteraction=now;c.affinity=60;e.updateContext(c);
    e.request(Emotion::Shy,.9,10000,10,now);e.tick(now.addSecs(5));expect(e.state().intensity>0 && e.state().intensity<.9,"transient gradual fade");
    expect(!e.request(Emotion::Smile,.9,1000,3,now.addSecs(5)),"low priority cannot erase reaction");
    e.tick(now.addSecs(11));expect(e.state().emotion==Emotion::Shy,"persistent mood survives short reaction");
    const double before=e.state().intensity;c.now=now.addSecs(130);c.lastInteraction=c.now;e.updateContext(c);expect(e.state().emotion==Emotion::Shy && e.state().intensity<before,"held mood gradually softens");
    e.tick(now.addSecs(181));expect(e.state().emotion==Emotion::Calm,"held mood eventually expires");
    e.reset(now.addSecs(182));expect(e.state().emotion==Emotion::Calm,"injected reset time respected");
}
void context(){
    const auto night=QDateTime(QDate(2026,10,23),QTime(23,30)).toUTC();
    EmotionEngine e;EmotionContext c;c.now=night;c.lastInteraction=night;c.idleSeconds=0;e.updateContext(c);expect(e.state().emotion==Emotion::Calm,"night alone does not make active user sleepy");
    c.now=night.addSecs(100);c.idleSeconds=100;e.updateContext(c);expect(e.state().emotion==Emotion::Calm,"ambient debounce");
    c.now=night.addSecs(114);e.updateContext(c);expect(e.state().emotion==Emotion::Calm,"no early ambient switch");
    c.now=night.addSecs(115);e.updateContext(c);expect(e.state().emotion==Emotion::Sleepy,"night plus idle");
    c.focusing=true;c.busy=true;e.updateContext(c);expect(e.state().emotion==Emotion::Focused,"focus overrides night");
    c.focusing=false;c.resting=true;e.updateContext(c);expect(e.state().emotion==Emotion::Relaxed,"break relaxes");
    e.setBaseline(Emotion::Sad,.6,c.now.addSecs(1200),c.now);expect(e.state().emotion==Emotion::Sad,"break respects distress");
    c.focusing=true;c.resting=false;e.updateContext(c);expect(e.state().emotion==Emotion::Focused,"focus owns posture while persistent sadness retained");
    c.focusing=false;e.updateContext(c);expect(e.state().emotion==Emotion::Sad,"focus did not delete real mood");
    EmotionEngine day;c={};c.now=noon();c.lastInteraction=c.now.addSecs(-300);c.idleSeconds=300;day.updateContext(c);expect(day.state().emotion==Emotion::Relaxed,"daytime idle relaxes");
    c.returnedAt=c.now;c.idleSeconds=0;c.lastInteraction=c.now;c.now=c.now.addSecs(46);day.updateContext(c);c.now=c.now.addSecs(15);day.updateContext(c);expect(day.state().emotion==Emotion::Curious,"return curiosity for early relation");
    c.affinity=80;c.resting=true;day.updateContext(c);expect(day.state().emotion==Emotion::Relaxed,"explicit rest immediate");
}
void actions(){
    for(int i=0;i<EmotionCount;++i){const auto mood=Emotion(i);const auto allowed=EmotionEngine::actionsFor(mood);expect(!allowed.isEmpty(),"safe action fallback exists");
        ActionCycle cycle(42);cycle.tick(0,false,allowed);expect(cycle.current()==allowed.first(),"emotion begins in primary action");
        for(qint64 time=1000;time<=1200000;time+=1000){cycle.tick(time,false,allowed);expect(allowed.contains(cycle.current()),"idle cycle stays within mood actions");}
    }
    ActionCycle hold(11);const auto shy=EmotionEngine::actionsFor(Emotion::Shy);hold.tick(0,false,shy);hold.tick(10000,true,shy);expect(hold.current()==PetAction::Shy,"interaction freezes mood idle choice");
    hold.tick(10001,false,EmotionEngine::actionsFor(Emotion::Sleepy));expect(hold.current()==PetAction::Sleep,"mood change selects safe primary");
}
void integration(const QString &path){
    auto now=noon();Companion c(path,nullptr,QUrl("http://127.0.0.1:9/responses"),100,[&]{return now;});
    auto settings=c.chatSettings();settings.onlineEnabled=false;c.saveChatSettings(settings);
    c.respond("我很好奇？");expect(c.memory().life().mood==Emotion::Curious && c.effectiveMood()==Emotion::Curious,"curiosity no longer discarded");
    now=now.addSecs(31);c.respond("状态机怎么实现");expect(c.memory().life().mood==Emotion::Thinking,"thinking persisted");
    now=now.addSecs(31);c.respond("好耶，太棒了");expect(c.effectiveMood()==Emotion::Excited,"excited reply survives final resolution");
    c.respond("你好");expect(c.effectiveMood()==Emotion::Excited,"greeting does not erase fresh strong mood");
    c.setFocusContext(true,false);expect(c.effectiveMood()==Emotion::Focused,"focus integration");
    c.setFocusContext(false,true);expect(c.effectiveMood()==Emotion::Relaxed,"rest integration");
    const auto xp=c.memory().life().experience,exchanges=c.memory().exchanges();
    for(int i=0;i<600;++i){now=now.addSecs(5);c.updateEmotionContext(now);c.tickEmotions();}
    expect(c.memory().life().experience==xp && c.memory().exchanges()==exchanges,"autonomy cannot farm XP or chat");
    c.setFocusContext(false,false);now=now.addSecs(1);c.respond("我好难过");c.respond("你好");expect(c.effectiveMood()==Emotion::Sad,"fresh distress retained");
    const auto encoded=c.memory().life().json();expect(LifeState::fromJson(encoded).mood==Emotion::Sad,"persistence round trip");
    for(int i=0;i<EmotionCount;++i){LifeState life;life.mood=Emotion(i);expect(LifeState::fromJson(life.json()).mood==Emotion(i),"all emotion IDs deserialize");}
    expect(LifeState::fromJson({{"mood",999}}).mood==Emotion::Calm,"unknown future value safely falls back");
}
void speechAndTopics(){
    for(const auto &id:QStringList{"Momo","longanyuanfei_v3.1","longanhuan_v3.1"}){
        for(int i=0;i<EmotionCount;++i){const auto p=SpeechExpression::forVoice(id,Emotion(i),80);expect(p.rate>=.9 && p.rate<=1.18 && p.pitch>=.9 && p.pitch<=1.08,"prosody remains natural");expect(!p.instruction.isEmpty(),"emotion instruction present");}
        auto calm=SpeechExpression::forVoice(id,Emotion::Calm,0),close=SpeechExpression::forVoice(id,Emotion::Calm,80);expect(calm.cacheKey()!=close.cacheKey(),"relationship-specific expression cache");
        expect(SpeechExpression::forVoice(id,Emotion::Focused,80).cacheKey()!=SpeechExpression::forVoice(id,Emotion::Excited,80).cacheKey(),"new emotion expression cache separated");
    }
    expect(EmotionEngine::topicWeight(Emotion::Focused,"work")>EmotionEngine::topicWeight(Emotion::Focused,"tease"),"focus dialogue is relevant");
    expect(EmotionEngine::topicWeight(Emotion::Relaxed,"rest")>EmotionEngine::topicWeight(Emotion::Relaxed,"work"),"relax dialogue is relevant");
    expect(EmotionEngine::matches(Emotion::Happy,Emotion::Excited) && !EmotionEngine::matches(Emotion::Angry,Emotion::Tsundere),"compatible catalog mood mapping");
}
}
int main(int argc,char **argv){QCoreApplication app(argc,argv);QTemporaryDir dir;try{expect(dir.isValid(),"temp fixture");classification();timing();context();actions();integration(dir.filePath("memory.sqlite"));speechAndTopics();QTextStream(stdout)<<"PASS "<<checks<<" emotion/context/action/prosody checks; injected clock; no network\n";return 0;}catch(const std::exception &e){QTextStream(stderr)<<"FAIL after "<<checks<<": "<<e.what()<<'\n';return 1;}}
