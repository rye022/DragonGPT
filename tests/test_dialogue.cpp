#include "companion.h"
#include "voice_controller.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QSignalSpy>
#include <QTest>
#include <QFile>
#include <QDir>
#include <algorithm>
#include <QJsonDocument>
#include <QJsonArray>
#include <QTextStream>
#include <QSet>
#include <stdexcept>
using namespace dragon;
namespace {
int checks=0;
void check(bool v,const char *m){++checks;if(!v)throw std::runtime_error(m);}
QJsonArray simulations;
void catalog(){
    QSet<QString> ids,texts;int counts[4]={};
    check(DialogueEngine::catalog().size()==405,"Wrong authored catalog size");
    for(const auto &v:DialogueEngine::catalog()){
        const auto l=v.toObject();const auto id=l["id"].toString(),text=l["text"].toString();
        check(!id.isEmpty() && !text.isEmpty() && !ids.contains(id) && !texts.contains(text),"Duplicate or empty authored line");ids.insert(id);texts.insert(text);
        if(l["kind"]=="ordinary")++counts[l["stage"].toInt()];
    }
    for(int n:counts)check(n>=60,"Stage has fewer than sixty distinct ordinary lines");
}
void simulate(const QString &root){
    const auto start=QDateTime(QDate(2026,11,2),QTime(18,0));
    for(int percent:{10,40,70,95}){
        const auto path=root+QString("/p%1.sqlite").arg(percent);QJsonArray output;QSet<QString> unique;QStringList recent;
        {
            MemoryStore store(path);DialogueContext c;c.percent=percent;c.emotion=Emotion::Calm;
            for(int i=0;i<100;++i){
                c.now=start.addSecs(i*2);c.rapid=i>=2;
                if(i==25 || i==60){
                    c.proactive=true;check(bool(store.selectDialogue(c,123+i)),"Interleaved proactive missing");c.proactive=false;
                }
                const auto line=store.selectDialogue(c,quint32(3200+i*17+percent));
                check(bool(line),"Click did not return exactly one line");check(!line.text.contains('\n'),"Multiple bubble lines returned");
                check(line.minPercent<=percent,"Hidden unlock leaked below intimacy gate");check(!recent.contains(line.id),"Last thirty click replies repeated");
                const auto entry=std::find_if(DialogueEngine::catalog().cbegin(),DialogueEngine::catalog().cend(),[&](const QJsonValue &v){return v.toObject()["id"]==line.id;});
                check(entry!=DialogueEngine::catalog().cend() && DialogueEngine::eligible(entry->toObject(),c,i+1),"Reply does not match its stage/context");
                output.append(QJsonObject{{"id",line.id},{"text",line.text},{"hidden",line.hidden}});unique.insert(line.id);recent.append(line.id);if(recent.size()>30)recent.removeFirst();
                check(!store.selectDialogue(c,123),"Same instant produced overlapping click replies");
            }
            check(store.dialogueHistory()["recent"].toArray().size()==60,"History is unbounded or prematurely lost");
            check(store.dialogueHistory()["proactive_recent"].toArray().size()==2,"Proactive contaminated click-only history");
            check(store.life().experience==0 && store.exchanges()==0,"Simulation farmed real growth/chat exchanges");
        }
        MemoryStore reopened(path);check(reopened.dialogueHistory()["recent"].toArray().size()==60,"Recent history did not survive restart");
        check(unique.size()>=50,"Insufficient selection diversity");
        simulations.append(QJsonObject{{"intimacy",percent},{"unique",unique.size()},{"clicks",output}});
    }
}
void unlocks(){
    DialogueContext c;const auto now=c.now;
    for(const auto &v:DialogueEngine::catalog()){
        auto l=v.toObject();if(l["kind"]!="hidden")continue;
        c.percent=l["minPercent"].toInt()-1;c.emotion=Emotion(l["emotion"].toInt());
        for(int h=0;h<24;++h){c.now=QDateTime(now.date(),QTime(h,0));check(!DialogueEngine::eligible(l,c,10000),"Hidden line unlocked early");}
        c.percent=l["minPercent"].toInt();c.now=QDateTime(now.date(),QTime(19,0));check(DialogueEngine::eligible(l,c,10000),"Eligible hidden line is unreachable");
        check(!DialogueEngine::eligible(l,c,0),"Hidden interaction-count gate bypassed");
    }
    c.percent=70;c.emotion=Emotion::Sad;QJsonObject state;
    for(int i=0;i<100;++i){c.now=now.addSecs(i*2);const auto line=DialogueEngine::select(c,state,100+i);check(line.topic!="happy" && line.topic!="tease","Distress picked cheerful teasing");}
    c.memories.clear();for(const auto &v:DialogueEngine::catalog())if(v.toObject()["kind"]=="memory")check(!DialogueEngine::eligible(v.toObject(),c,100),"Deleted/unconfirmed memory leaked");
}
void confirmedTopics(const QString &root){
    MemoryStore store(root+"/topics.sqlite");
    auto id=store.remember("我喜欢古典文学");store.updateMemory(id,"我喜欢古典文学","兴趣，话题",.7);
    check(store.interactionMemories().size()==1,"Confirmed interest not available to dialogue");
    auto privateId=store.remember("密码不应被主动朗读");store.updateMemory(privateId,"密码不应被主动朗读","偏好",.7);
    auto profileId=store.remember("一条个人资料");store.updateMemory(profileId,"一条个人资料","用户资料",.7);
    auto rejectedId=store.remember("不要引用我的这个兴趣");store.updateMemory(rejectedId,"不要引用我的这个兴趣","兴趣",.7);
    check(store.interactionMemories().size()==1,"Private/profile/opt-out memory became an unsolicited topic");
    store.forget(id);check(store.interactionMemories().isEmpty(),"Deleted interest remained available");
}
void noAmbientMemory(const QString &root){
    const QString marker="PRIVATE_MEMORY_SENTINEL_DO_NOT_SPEAK";
    MemoryStore store(root+"/privacy.sqlite");const auto id=store.remember(marker);store.updateMemory(id,marker,"兴趣，话题",.9);
    for(int percent:{0,20,50,80})for(bool proactive:{false,true}){
        DialogueContext c;c.percent=percent;c.proactive=proactive;c.now=QDateTime::currentDateTimeUtc();c.memories=store.memories();QJsonObject history;
        for(const auto &v:DialogueEngine::catalog())if(v.toObject()["kind"]=="memory")check(!DialogueEngine::eligible(v.toObject(),c,100),"Memory template eligible for unsolicited speech");
        for(int seed=1;seed<=100;++seed){c.now=c.now.addSecs(3600);auto line=DialogueEngine::select(c,history,seed);check(bool(line),"Safe static dialogue pool lost");check(!line.text.contains(marker) && DialogueEngine::find(line.id)["kind"]!="memory","Memory content leaked into click/proactive dialogue");}
    }
    check(store.memories().size()==1 && store.relevant("PRIVATE_MEMORY").size()==1,"Memory removed or explicit recall broken");
    Companion c(root+"/privacy-companion.sqlite");c.remember(marker);
    check(c.respond("你记得 PRIVATE_MEMORY 吗").text.contains(marker),"User-requested recall no longer works");
    check(!c.previewProactive().contains(marker),"Proactive preview leaked memory");
}
void emotionAndVoice(const QString &root){
    Companion c(root+"/emotion.sqlite");c.respond("我好难过");check(c.effectiveMood()==Emotion::Sad,"Sad emotion was not persisted");
    c.respond("你好");check(c.effectiveMood()==Emotion::Sad,"Greeting erased fresh distress");
    c.respond("这是一个普通的陈述");check(c.emotions().state().emotion==Emotion::Sad,"Neutral chat erased held mood");
    Companion reopened(root+"/emotion.sqlite");check(reopened.effectiveMood()==Emotion::Sad,"Emotion lost after restart");
    check(EmotionEngine::classify("不累，也不生气，任务没完成")==Emotion::Calm,"Negation produced wrong affect");
    const auto sad=SpeechExpression::forVoice("longanyuanfei_v3.1",Emotion::Sad),happy=SpeechExpression::forVoice("longanyuanfei_v3.1",Emotion::Happy);
    check(sad.rate<happy.rate && sad.cacheKey()!=happy.cacheKey(),"Emotion did not affect real voice controls/cache");
    for(int mood=0;mood<10;++mood){auto p=SpeechExpression::forVoice("longanyuanfei_v3.1",Emotion(mood));check(p.rate>=.90 && p.rate<=1.18,"Tsundere rate escaped natural bounds");}
    EmotionEngine e;const auto now=QDateTime::currentDateTimeUtc();e.request(Emotion::Shy,.9,10000,10,now);e.tick(now.addSecs(5));check(e.state().intensity<.9 && e.state().intensity>0,"Emotion did not gradually decay");
}
void scheduler(const QString &root){
    const auto folder=root+"/proactive";QDir().mkpath(folder);VoicePreferences p;p.voice="Momo";p.readAloud=false;p.save(folder+"/voice.ini");
    auto clock=QDateTime::currentDateTimeUtc();Companion c(folder+"/memory.sqlite",nullptr,QUrl("http://127.0.0.1:9/responses"),100,[&]{return clock;});VoiceController voice(c);QObject owner;
    auto settings=c.lifeSettings();settings.quietStart=settings.quietEnd=0;settings.proactiveSpeech=false;c.saveLifeSettings(settings);
    const auto observed=QDateTime::currentDateTimeUtc();
    for(int n=0;n<=240;++n)c.observeDesktopIdle(20,observed.addSecs(n*15));
    check(c.desktopActiveMinutes()==60,"Actual recent desktop activity did not accumulate");
    c.observeDesktopIdle(300,observed.addSecs(3615));check(c.desktopActiveMinutes()==0,"Idle desktop fabricated prolonged work");
    c.noteActivity(QDateTime::currentDateTimeUtc().addSecs(-200));bool blocked=true;
    c.attachProactiveScheduler(&owner,[&]{return blocked;});check(c.proactiveSchedulerRunning(),"Proactive scheduler is not running");
    QSignalSpy announced(&c,&Companion::proactiveReady),bubbles(&voice,&VoiceController::textOnlyBubble),spoken(&voice,&VoiceController::speechRequested);
    QObject::connect(&c,&Companion::proactiveReady,&voice,&VoiceController::deliverProactive);
    check(!c.tryProactive(true),"Typing/fullscreen/voice blocker ignored");blocked=false;
    clock=c.nextProactiveAt();c.findChild<QTimer*>("proactiveScheduler")->start(10);QTest::qWait(100);check(announced.size()==1 && bubbles.size()==1 && spoken.empty(),"Real timer failed to deliver exactly one text-only bubble");
    check(!c.tryProactive(false,QDateTime::currentDateTimeUtc().addDays(1)),"Unanswered proactive opening repeated");
    voice.endCall();settings.proactiveSpeech=true;c.saveLifeSettings(settings);QSignalSpy requests(&voice,&VoiceController::speechRequested);voice.deliverProactive("真实朗读开场测试。");check(requests.size()==1,"Enabled proactive TTS did not dispatch selected voice");voice.endCall();
    check(!voice.active(),"Proactive TTS opened microphone");
}
}
int main(int argc,char **argv){
    QCoreApplication app(argc,argv);QTemporaryDir dir;qunsetenv("DASHSCOPE_API_KEY");qunsetenv("DEEPSEEK_API_KEY");
    try{catalog();simulate(dir.path());unlocks();confirmedTopics(dir.path());noAmbientMemory(dir.path());emotionAndVoice(dir.path());scheduler(dir.path());
        if(app.arguments().size()==3 && app.arguments()[1]=="--report"){QFile out(app.arguments()[2]);check(out.open(QIODevice::WriteOnly),"Cannot save simulation report");out.write(QJsonDocument(simulations).toJson());}
        QTextStream(stdout)<<"PASS: "<<checks<<" checks; four stages x 100 isolated clicks; real scheduler, bounded restart history, unlocks, emotion and TTS dispatch. No cloud calls.\n";return 0;
    }catch(const std::exception &e){QTextStream(stderr)<<"FAIL: "<<e.what()<<"\n";return 1;}
}
