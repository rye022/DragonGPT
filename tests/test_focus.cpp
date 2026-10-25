#include "focus_service.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QTextStream>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <stdexcept>
using namespace dragon;
namespace {
int assertions=0;
void expect(bool condition,const char *text){++assertions;if(!condition)throw std::runtime_error(text);}
struct Rig {
    QTemporaryDir dir;MemoryStore store{dir.filePath("memory.sqlite")};
    WellnessTime time{QDateTime(QDate(2026,10,23),QTime(10,0)).toUTC(),0};
    ActivitySample activity{true,false,true,0,8*3600000,2*3600000,"simulated Windows"};
    FocusService service{store,time};
    Rig(){
        // Fast disposable fixture; diskRestore below keeps SQLite's durable defaults.
        for(const auto &name:QSqlDatabase::connectionNames()){
            auto db=QSqlDatabase::database(name);if(db.databaseName()==dir.filePath("memory.sqlite")){QSqlQuery q(db);q.exec("PRAGMA synchronous=OFF");q.exec("PRAGMA journal_mode=MEMORY");}
        }
    }
    void advance(int seconds,bool busy=false){for(int i=0;i<seconds;++i){time.utc=time.utc.addSecs(1);time.monotonicMs+=1000;service.tick(time,activity,busy);}}
    void jump(int seconds){time.utc=time.utc.addSecs(seconds);time.monotonicMs+=seconds*1000;service.tick(time,activity);}
    void drain(){CompanionNotice n;while(service.takeNotice(n)){};}
    void waterSettings(int minutes=1){auto s=service.settings();s.waterMinutes=minutes;s.deferFocus=true;s.preferBreak=true;s.waterTts=false;service.setSettings(s);}
};
void timer(){
    {Rig q;auto options=q.service.settings();options.focusSeconds=1;options.quietEnabled=false;q.service.setSettings(options);
        q.service.tick(q.time,q.activity,false,true);q.service.start("explicit timer");CompanionNotice n;
        expect(q.service.takeNotice(n) && !n.silent,"proactive quiet does not mute explicit timer start");q.advance(1);
        expect(q.service.takeNotice(n) && !n.silent && n.text.contains("休息"),"completion announces rest");
        options.quietEnabled=true;options.quietStart=options.quietEnd=QTime(0,0);q.service.setSettings(options);q.service.end();q.service.start("quiet timer");
        expect(q.service.takeNotice(n) && n.silent,"explicit timer quiet setting remains honored");}
    Rig r;auto s=r.service.settings();s.focusSeconds=3;s.shortSeconds=2;s.longSeconds=4;s.longEvery=2;r.service.setSettings(s);
    r.service.start("学习 C++");r.service.start("duplicate");r.drain();r.advance(1);expect(r.service.remainingMs()==2000,"countdown");
    r.service.pause();r.advance(20);expect(r.service.remainingMs()==2000 && r.store.focusSessions().isEmpty(),"pause no finish");
    r.service.resume();r.advance(2);expect(r.service.phase()==FocusService::Phase::ShortBreak && r.service.paused(),"short break waits by default");
    expect(r.store.focusSessions().size()==1,"one persisted cycle");expect(r.store.focusSessions()[0].toObject().value("actual_ms").toInteger()==3000,"actual excludes pause");
    r.advance(10);expect(r.store.focusSessions().size()==1,"no duplicate completion");
    r.service.resume();r.advance(2);expect(r.service.phase()==FocusService::Phase::Focus && r.service.paused(),"break to focus");
    r.service.resume();r.advance(3);expect(r.service.phase()==FocusService::Phase::LongBreak && r.service.remainingMs()==4000,"long cadence");
    expect(r.store.exchanges()==0,"short sessions no reward");r.service.end();r.service.end();expect(r.store.focusSessions().size()==2,"end break idempotent");
    s.autoNext=true;r.service.setSettings(s);r.service.start("auto");r.advance(3);expect(!r.service.paused(),"automatic next");
    r.service.reset();expect(!r.service.running() && r.service.remainingMs()==3000,"reset defaults");
}
void recovery(){
    Rig r;r.service.start("restore");r.drain();r.advance(7);r.service.checkpoint();
    FocusService restored(r.store,r.time);expect(restored.paused() && restored.remainingMs()==r.service.remainingMs(),"restart conservatively paused");
    expect(restored.sessionActiveMs()==0,"restart no invented activity history");CompanionNotice n;expect(!restored.takeNotice(n),"restart no old notice");
    auto actual=r.service.actualMs();r.jump(3600);expect(r.service.paused() && r.service.actualMs()==actual,"sleep pauses no retrospective focus");
    r.service.resume();r.activity.locked=true;r.advance(1);expect(r.service.paused(),"lock auto pause");
    r.service.resume();expect(r.service.paused(),"cannot resume locked");r.activity.locked=false;r.advance(1);r.service.resume();r.advance(2);expect(r.service.actualMs()==actual+2000,"unlock explicit resume");
    auto left=r.service.remainingMs();r.time.utc=r.time.utc.addSecs(-7200);r.advance(1);expect(r.service.remainingMs()==left-1000,"wall clock does not drive timer");
    r.service.skip();r.service.skip();r.service.reset();expect(r.store.exchanges()==0,"skip reset no reward");
}
void water(){
    Rig r;r.waterSettings();expect(r.service.sessionActiveMs()==0,"eight hour uptime is not activity");
    r.advance(119);CompanionNotice n;expect(!r.service.takeNotice(n),"initial quiet period");r.advance(1);expect(r.service.takeNotice(n) && n.water,"active use water due");
    r.service.shown(n);expect(r.service.waterPending(),"one pending notification");r.advance(300);expect(!r.service.takeNotice(n),"no repeat pending");
    auto active=r.service.sessionActiveMs();r.service.snoozeWater();r.advance(899);expect(!r.service.takeNotice(n),"snooze deadline");r.advance(1);expect(r.service.takeNotice(n) && n.water,"snooze reuses candidate");expect(r.service.sessionActiveMs()>active,"snooze retains accumulated activity");
    r.service.confirmWater();expect(!r.service.waterPending(),"confirmed clears pending");r.advance(59);expect(!r.service.takeNotice(n),"confirmation resets interval");
    r.service.muteWaterToday();r.advance(3600);expect(!r.service.takeNotice(n),"muted today");
    r.time.utc=QDateTime(r.time.utc.toLocalTime().date().addDays(1),QTime(10,0)).toUTC();r.advance(1);expect(r.service.takeNotice(n),"next local day resumes");
    auto s=r.service.settings();s.waterEnabled=false;r.service.setSettings(s);r.advance(1000);expect(!r.service.waterPending() && !r.service.takeNotice(n),"disabled clears candidate");expect(r.store.exchanges()==0,"water cannot farm relation");
    FocusService restart(r.store,r.time);expect(!restart.settings().waterEnabled,"settings persistence");
}
void awayAndUnavailable(){
    Rig r;r.waterSettings();r.activity.idleMs=10*60000;r.advance(4000);expect(r.service.sessionActiveMs()==0,"away no activity");
    r.activity.idleMs=0;r.advance(1);CompanionNotice n;expect(!r.service.takeNotice(n),"return no backlog");r.advance(120);expect(r.service.takeNotice(n),"return grace and reliable use");
    Rig u;u.waterSettings();u.activity.idleAvailable=false;u.advance(4000);expect(u.service.sessionActiveMs()==0 && !u.service.takeNotice(n),"unavailable API does not invent activity");
    auto s=u.service.settings();s.useActivity=false;u.service.setSettings(s);u.advance(1);expect(u.service.takeNotice(n),"explicit elapsed fallback");
    Rig q;q.waterSettings();s=q.service.settings();s.quietEnabled=true;s.quietStart=QTime(0,0);s.quietEnd=QTime(0,0);q.service.setSettings(s);q.advance(4000);expect(!q.service.takeNotice(n),"quiet blocks hydration");
}
void mergeAndPriority(){
    Rig r;r.waterSettings();auto s=r.service.settings();s.focusSeconds=130;r.service.setSettings(s);r.service.start("merge");r.drain();r.advance(120);CompanionNotice n;expect(!r.service.takeNotice(n),"focus defers hydration");
    r.advance(10);expect(r.service.takeNotice(n) && n.water && n.priority==3 && n.text.contains("完成"),"focus completion merges hydration");expect(!r.service.takeNotice(n),"no second water notice");
    Rig busy;busy.waterSettings();busy.advance(200,true);expect(!busy.service.takeNotice(n),"chat owns priority");busy.advance(1);expect(busy.service.takeNotice(n),"reminder after busy");
    // Simulate a permanently busy or failed TTS output: candidates expire and the scheduler still progresses.
    Rig fail;fail.waterSettings();s=fail.service.settings();s.focusSeconds=2;fail.service.setSettings(s);fail.service.start("tts failure");fail.advance(130,true);expect(fail.store.focusSessions().size()==1,"TTS does not block settlement");fail.advance(1);expect(fail.service.takeNotice(n) && n.water && n.priority==2,"expired phase dropped, fresh hydration remains");
    Rig silence;silence.waterSettings();s=silence.service.settings();s.focusSeconds=3600;s.silentDuringLongFocus=true;silence.service.setSettings(s);silence.service.start("long");silence.drain();silence.advance(120);expect(silence.service.takeNotice(n) && n.silent,"optional lightweight silent focus reminder");
}
void existingV6Migration(){
    QTemporaryDir dir;const auto path=dir.filePath("old-v6.sqlite");
    {MemoryStore store(path);store.setNickname("preserved");store.appendExchange("old message","old answer");
        for(const auto &name:QSqlDatabase::connectionNames()){
            auto db=QSqlDatabase::database(name);if(db.databaseName()==path){QSqlQuery q(db);q.exec("DELETE FROM dialogue_collection WHERE id LIKE 'water.%'");q.exec("DROP TABLE wellness_state");q.exec("DROP TABLE focus_sessions");}
        }
    }
    {MemoryStore store(path);expect(store.nickname()=="preserved" && store.recentMessages().size()==2,"v6 migration preserves old records");
        store.recordPresented("water.0.0");bool discovered=false;for(const auto &v:store.collection())if(v.toObject()["id"]=="water.0.0")discovered=v.toObject()["discovered"].toBool();
        expect(discovered,"existing v6 can collect new hydration lines");
        expect(QDir(dir.path()).entryList({"*.pre-v7-*.sqlite"},QDir::Files).size()==1,"feature migration consistent backup");
    }
}
void diskRestore(){
    QTemporaryDir dir;WellnessTime time{QDateTime::currentDateTimeUtc(),0};
    {MemoryStore store(dir.filePath("real.sqlite"));FocusService engine(store,time);engine.start("disk checkpoint");
        time.utc=time.utc.addSecs(5);time.monotonicMs+=5000;engine.tick(time,{true,false,true,0,999999,999999,"test"});engine.checkpoint();}
    {MemoryStore reopened(dir.filePath("real.sqlite"));FocusService engine(reopened,time);
        expect(engine.paused() && engine.actualMs()==5000 && engine.task()=="disk checkpoint","actual database close and reopen");
        engine.systemPause("native suspend");expect(engine.paused(),"native event pauses explicitly");}
}
void rewardsAndStats(){
    Rig r;auto s=r.service.settings();s.focusSeconds=25*60;s.waterEnabled=false;r.service.setSettings(s);
    for(int j=0;j<5;++j){r.service.start("valid");r.advance(1500);r.service.end();}
    expect(r.store.focusSessions().size()==5 && r.store.life().experience==16,"daily reward cap");
    auto row=r.store.focusSessions()[0].toObject();r.store.saveWellness(r.store.wellnessState(),row);expect(r.store.life().experience==16 && r.store.focusSessions().size()==5,"atomic idempotent reward");
    expect(r.service.todayStats().value("count").toInt()==5,"today completed count");expect(r.service.todayStats().value("ms").toInteger()==5*1500000,"focus persisted duration");
    Rig midnight;midnight.time.utc=QDateTime(QDate(2026,10,23),QTime(23,59,58)).toUTC();midnight.time.monotonicMs+=1000;midnight.service.tick(midnight.time,midnight.activity);
    s=midnight.service.settings();s.focusSeconds=4;midnight.service.setSettings(s);midnight.service.start("midnight");midnight.advance(4);
    expect(midnight.store.focusStatistics(QDate(2026,10,23)).value("ms").toInteger()==2000,"midnight previous day slice");expect(midnight.store.focusStatistics(QDate(2026,10,24)).value("ms").toInteger()==2000,"midnight next day slice");
    expect(midnight.store.focusStatistics(QDate(2026,10,24)).value("count").toInt()==1,"completion local day");
}
}
int main(int argc,char **argv){QCoreApplication app(argc,argv);try{timer();recovery();water();awayAndUnavailable();mergeAndPriority();rewardsAndStats();diskRestore();existingV6Migration();QTextStream(stdout)<<"PASS "<<assertions<<" focus/water assertions; simulated clock; no network\n";return 0;}catch(const std::exception &e){QTextStream(stderr)<<"FAIL after "<<assertions<<": "<<e.what()<<'\n';return 1;}}
