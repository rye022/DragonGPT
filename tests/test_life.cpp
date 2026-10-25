#include "companion.h"
#include <QCoreApplication>
#include <QDir>
#include <QJsonDocument>
#include <QSignalSpy>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTextStream>
#include <stdexcept>

using namespace dragon;
namespace {
int assertions=0;
void expect(bool value,const char *message){++assertions;if(!value)throw std::runtime_error(message);}
QDateTime midday(){return QDateTime(QDate(2026,11,1),QTime(12,0));}
void growth(){
    LifeState s;const auto now=midday();s.interact("你好",now);
    expect(s.experience==2 && s.activeDays==1,"first real interaction did not grow");
    s.interact("  你好  ",now.addSecs(10));expect(s.experience==2,"same phrase farmed growth");
    for(int i=0;i<100;++i)s.interact(QString("聊天 %1").arg(i),now.addSecs((i+1)*31));
    expect(s.experience==20 && s.dailyExperience==20 && s.stage()=="初识期","daily cap or day gate failed");
    s=LifeState::fromJson(s.json());expect(s.dailyMessages.size()<=10 && s.dailyExperience==20,"growth state did not round trip");
    s.interact("昨天",now.addDays(-1));expect(s.experience==20 && s.activeDays==1,"clock rollback reopened allowance");
    for(int day=1;day<3;++day)for(int i=0;i<10;++i)s.interact(QString("新的交流 %1").arg(i),now.addDays(day).addSecs(i*31));
    expect(s.stage()=="熟悉期" && s.experience==60 && s.activeDays==3,"multi-day familiar stage unavailable");
    for(int day=3;day<10;++day)for(int i=0;i<10;++i)s.interact(QString("新的交流 %1").arg(i),now.addDays(day).addSecs(i*31));
    expect(s.stage()=="亲近期" && s.experience==200,"tacit stage unavailable");
    const int before=s.experience;s.interact("很久以后",now.addDays(100));
    expect(s.experience==before+2 && s.activeDays==11,"absence penalized growth or fabricated active days");
    expect(!s.growthHint().isEmpty(),"growth explanation missing");
}
void scheduling(const QString &root){
    LifeSettings p;LifeState s;const auto now=midday();
    expect(CompanionLife::eligible(s,p,now,now.addSecs(-301),false),"eligible opening blocked");
    p.focusMode=true;expect(!CompanionLife::eligible(s,p,now,now.addSecs(-301),false),"DND ignored");p.focusMode=false;
    expect(!CompanionLife::eligible(s,p,now,now.addSecs(-20),false),"recent manual interaction guard bypassed");
    expect(!CompanionLife::eligible(s,p,now,now.addSecs(-301),true),"busy guard ignored");
    expect(!CompanionLife::eligible(s,p,now,now.addSecs(-301),false,now.addSecs(60)),"pause ignored");
    s.awaitingReply=true;expect(CompanionLife::eligible(s,p,now,now.addSecs(-301),false),"legacy awaiting reply permanently blocked companionship");
    p.frequency="off";expect(!CompanionLife::eligible(s,p,now,now.addSecs(-301),false),"off frequency ignored");
    p.frequency="quiet";expect(p.intervalSeconds()==qMakePair(900,1500),"quiet interval wrong");
    p.frequency="normal";expect(p.intervalSeconds()==qMakePair(300,720),"normal interval wrong");
    p.frequency="active";expect(p.intervalSeconds()==qMakePair(180,360),"active interval wrong");
    expect(p.quiet(QDateTime(now.date(),QTime(23,59))) && !p.quiet(now),"quiet hours incorrect");
    p.quietStart=p.quietEnd=9;expect(!p.quiet(now),"equal quiet bounds should disable quiet hours");
    p.enabled=false;p.focusMode=true;p.focusProtection=false;p.proactiveSpeech=false;p.hideUnheard=true;p.pausedUntil=now.addDays(1).toUTC();p.save(root+"/life.ini");const auto saved=LifeSettings::load(root+"/life.ini");
    expect(saved.focusMode && !saved.enabled && !saved.focusProtection && !saved.proactiveSpeech && saved.hideUnheard && saved.frequency=="active" && saved.pausedUntil==p.pausedUntil,"new settings did not persist");
}
void memoryAndProactive(const QString &file){
    qint64 candidate=0;
    {
        Companion c(file);auto prefs=c.lifeSettings();prefs.quietStart=prefs.quietEnd=0;prefs.idleMinutes=1;c.saveLifeSettings(prefs);
        c.respond("我喜欢十九世纪西方文学");
        expect(c.memory().memories().isEmpty(),"candidate became confirmed memory without approval");
        auto rows=c.memory().suggestions();expect(rows.size()==1,"direct preference candidate missing");candidate=rows.first().id;
        expect(!c.promptContext("文学").contains("十九世纪"),"unconfirmed suggestion leaked into prompt context");
        c.resolveSuggestion(candidate,true);expect(c.memory().memories().size()==1 && c.memory().suggestions().isEmpty(),"confirmation failed");
        c.resolveSuggestion(candidate,true);expect(c.memory().memories().size()==1,"repeat confirmation duplicated memory");
        expect(c.promptContext("文学").contains("十九世纪"),"confirmed memory not retrievable");
        c.respond("我正在学习 C++");candidate=c.memory().suggestions().first().id;c.resolveSuggestion(candidate,false);
        c.respond("我正在学习 C++");expect(c.memory().suggestions().isEmpty(),"dismissed memory suggested again");
        c.respond("记住：我喜欢安静的陪伴");expect(c.memory().memories().size()==2,"explicit memory command regressed");
        c.respond("我有点累");expect(c.memory().life().mood==Emotion::Worried,"user emotion not persisted");
        const int before=c.memory().exchanges(),xp=c.memory().life().experience;
        const auto now=QDateTime::currentDateTimeUtc().addSecs(125);c.noteActivity(now.addSecs(-125));
        QSignalSpy spy(&c,&Companion::proactiveReady);expect(c.tryProactive(false,now),"companion did not initiate");
        expect(spy.size()==1 && c.memory().exchanges()==before && c.memory().life().experience==xp,"proactive opening grew relation or missed signal");
        const auto history=c.memory().recentMessages(1);expect(history.size()==1 && history.first().role=="assistant" && history.first().source=="proactive-local","proactive opening fabricated a user turn or source");
        expect(!c.tryProactive(false,now.addSecs(10000)),"unanswered opening repeated");
    }
    {
        Companion c(file);expect(c.memory().life().awaitingReply && c.emotions().state().emotion==Emotion::Worried,"restart lost proactive or emotion state");
        c.respond("我正在学习 C++");expect(c.memory().suggestions().isEmpty(),"dismissal did not survive restart");
        expect(!c.memory().life().awaitingReply,"real reply did not acknowledge opening");
        auto settings=c.lifeSettings();settings.memorySuggestions=false;c.saveLifeSettings(settings);c.respond("我喜欢星空");expect(c.memory().suggestions().isEmpty(),"suggestion switch ignored");
        c.clearAll();expect(c.memory().life().experience==0 && c.memory().suggestions().isEmpty() && c.emotions().state().emotion==Emotion::Calm,"clear all retained life state");
    }
}
void emotionBaseline(){
    EmotionEngine e;const auto now=QDateTime::currentDateTimeUtc();e.setBaseline(Emotion::Worried,0.4,now.addSecs(60));
    e.request(Emotion::Thinking,0.8,1000,10,now);e.tick(now.addSecs(2));expect(e.state().emotion==Emotion::Worried,"temporary expression lost persistent mood");
    e.tick(now.addSecs(61));expect(e.state().emotion==Emotion::Calm,"persistent mood failed to decay");
    expect(CompanionLife::candidate("如果我喜欢读书呢？").isEmpty(),"hypothetical fact extracted");
    expect(CompanionLife::candidate("我喜欢使用 API 密钥 sk-secret").isEmpty(),"sensitive credential candidate extracted");
    expect(CompanionLife::candidate("我喜欢安静，但不要记住").isEmpty(),"explicit no-memory request ignored");
    expect(CompanionLife::candidate("我对19世纪西方文学感兴趣").contains("文学"),"literature interest not recognized");
    expect(EmotionEngine::classify("我不开心")==Emotion::Worried,"negative mood misclassified as happiness");
}
void migration(const QString &file){
    {
        auto db=QSqlDatabase::addDatabase("QSQLITE","legacy-life");db.setDatabaseName(file);expect(db.open(),"legacy fixture open failed");
        QSqlQuery q(db);expect(q.exec("CREATE TABLE relation(id INTEGER PRIMARY KEY CHECK(id=1),exchanges INTEGER NOT NULL DEFAULT 0)"),"legacy schema fixture failed");
        expect(q.exec("INSERT INTO relation VALUES(1,35)"),"legacy count fixture failed");q.finish();db.close();
    }
    QSqlDatabase::removeDatabase("legacy-life");
    MemoryStore store(file);expect(store.relationship()=="亲近期" && store.exchanges()==35 && store.life().activeDays==0,"upgrade demoted relationship or fabricated dated interactions");
    store.appendExchange("你好","欢迎回来");expect(store.relationship()=="亲近期","migrated relationship not retained on write");
}
}
int main(int argc,char **argv){
    QCoreApplication app(argc,argv);QTemporaryDir dir(QDir::currentPath()+"/life-tests-XXXXXX");
    try{expect(dir.isValid(),"temporary directory failed");growth();scheduling(dir.path());memoryAndProactive(dir.path()+"/life.sqlite");emotionBaseline();migration(dir.path()+"/legacy.sqlite");
        QTextStream(stdout)<<"PASS: "<<assertions<<" assertions (growth, scheduling, suggestions, emotion persistence, migration). No live API calls.\n";return 0;
    }catch(const std::exception &e){QTextStream(stderr)<<"FAIL: "<<e.what()<<"\n";return 1;}
}
