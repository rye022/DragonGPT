#include "tool_hub.h"
#include "memory_store.h"
#include "local_speech.h"
#include "focus_service.h"
#include <QAbstractEventDispatcher>
#include <algorithm>
#include <QtTest>
#include <QTemporaryDir>
#include <QSqlQuery>
#include <QJsonDocument>
using namespace dragon;
namespace {
QJsonObject step(QString id){return {{"tool",id},{"input",QJsonObject{}}};}
QJsonObject schema(){return {{"type","object"},{"properties",QJsonObject{}}};}
}
class FinalAuditTests:public QObject {
    Q_OBJECT
private slots:
    void idleSpeechEventLoopWakeups(){
        LocalSpeech speech;int wakes=0;QEventLoop loop;
        const auto connection=connect(QAbstractEventDispatcher::instance(),&QAbstractEventDispatcher::awake,&loop,[&]{++wakes;});
        QTimer::singleShot(2000,&loop,&QEventLoop::quit);loop.exec();disconnect(connection);
        qInfo().noquote()<<"AUDIT_IDLE_SPEECH_2_SECONDS_EVENT_LOOP_WAKES"<<wakes;
        QVERIFY(!speech.speaking());QVERIFY(!speech.listening());
    }
    void idleSpeechDoesNotPoll(){LocalSpeech speech;auto *timer=speech.findChild<QTimer*>("localSpeechPoll");QVERIFY(timer);QVERIFY(!timer->isActive());speech.stopSpeaking();speech.stopListening();QVERIFY(!timer->isActive());}
    void passiveHydrationWaitsButTimerMilestoneWins(){
        QTemporaryDir dir;MemoryStore store(dir.filePath("memory.sqlite"));WellnessTime time{QDateTime(QDate(2026,10,26),QTime(12,0)).toUTC(),0};FocusService service(store,time);
        auto settings=service.settings();settings.waterMinutes=1;settings.deferFocus=false;service.setSettings(settings);
        ActivitySample active{true,false,true,0};for(int i=0;i<121;++i){time.utc=time.utc.addSecs(1);time.monotonicMs+=1000;service.tick(time,active);}
        CompanionNotice notice;QVERIFY(!service.takeNotice(notice,true));QVERIFY(service.takeNotice(notice));QVERIFY(notice.water);
        service.confirmWater();service.start("explicit timer");QVERIFY(service.takeNotice(notice,true));QVERIFY(notice.priority>=3);
    }
    void synchronousCancelCallbackIsIgnored(){
        ToolHub hub;ToolHub::Completion completion;int after=0;
        hub.add({"wait","async",schema(),false,false,1000},[&](auto,auto done){completion=done;},[&]{completion(true,{{"status","cancel callback"}});});
        hub.add({"after","must not run",schema()},[&](auto,auto done){++after;done(true,{});});
        QVERIFY(hub.propose({step("wait"),step("after")}).isEmpty());hub.run();hub.cancel();
        QTest::qWait(10);QCOMPARE(hub.state(),QString("cancelled"));QCOMPARE(after,0);QVERIFY(hub.audit().isEmpty());
    }
    void timeoutCannotBeReportedAsSuccess(){
        ToolHub hub;ToolHub::Completion completion;int after=0;
        hub.add({"wait","async",schema(),false,false,100},[&](auto,auto done){completion=done;},[&]{completion(true,{});});
        hub.add({"after","must not run",schema()},[&](auto,auto done){++after;done(true,{});});
        QVERIFY(hub.propose({step("wait"),step("after")}).isEmpty());hub.run();
        QTRY_VERIFY(!hub.busy());QCOMPARE(hub.state(),QString("failed"));QCOMPARE(after,0);
        QCOMPARE(hub.audit().size(),1);QVERIFY(!hub.audit().first().toObject()["ok"].toBool());
    }
    void normalizedImportPreservesExistingMetadata(){
        QTemporaryDir dir;MemoryStore store(dir.filePath("memory.sqlite"));
        const auto id=store.remember("我喜欢安静。");store.updateMemory(id,"我喜欢安静。","用户资料",.9);store.setMemoryMetadata(id,"relationship","manual",false);
        const auto before=store.memories().first();
        QJsonObject row{{"content","我喜欢安静！"},{"tags","话题"},{"importance",.1},{"layer","semantic"},{"source","imported"},{"proactive_allowed",true}};
        const auto data=QJsonDocument(QJsonObject{{"format","DragonGPT memories"},{"version",2},{"memories",QJsonArray{row}}}).toJson();
        QCOMPARE(store.importMemories(data),0);QCOMPARE(store.memories().size(),1);
        const auto after=store.memories().first();QCOMPARE(after.id,id);QCOMPARE(after.content,before.content);QCOMPARE(after.tags,before.tags);QCOMPARE(after.layer,before.layer);QCOMPARE(after.source,before.source);QCOMPARE(after.proactiveAllowed,before.proactiveAllowed);QCOMPARE(after.importance,before.importance);
    }
    void primaryKeyLookupUsesLiveRowsAndPreservesOrder(){
        QTemporaryDir dir;MemoryStore store(dir.filePath("memory.sqlite"));const auto a=store.remember("第一条记录"),b=store.remember("第二条记录");
        store.setMemoryMetadata(a,"episodic","test",true);auto rows=store.byIds({b,a,b,99999});QCOMPARE(rows.size(),3);QCOMPARE(rows[0].id,b);QCOMPARE(rows[1].id,a);QCOMPARE(rows[2].id,b);
        QCOMPARE(store.byIds({b,a},"episodic").size(),1);store.updateMemory(a,"更新后的记录","",.5);QCOMPARE(store.byIds({a}).first().content,QString("更新后的记录"));store.forget(a);QVERIFY(store.byIds({a}).isEmpty());
        store.updateMemory(b,"第二条记录","",.5,QDateTime::currentDateTimeUtc().addSecs(-1));QVERIFY(store.byIds({b}).isEmpty());QCOMPARE(store.integrity(),QString("ok"));
    }
    void lookupPerformance(){
        QTemporaryDir dir;const auto path=dir.filePath("memory.sqlite");MemoryStore store(path);
        const auto name=QUuid::createUuid().toString();{
            auto db=QSqlDatabase::addDatabase("QSQLITE",name);db.setDatabaseName(path);QVERIFY(db.open());QVERIFY(db.transaction());QSqlQuery q(db);
            QVERIFY(q.prepare("INSERT INTO memories(content,created_at,updated_at) VALUES(?,?,?)"));
            const auto now=QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
            for(int i=0;i<5000;++i){q.bindValue(0,QString("性能样本 %1").arg(i));q.bindValue(1,now);q.bindValue(2,now);QVERIFY(q.exec());}QVERIFY(db.commit());
        }QSqlDatabase::removeDatabase(name);
        QVector<double> timings;for(int trial=0;trial<7;++trial){QElapsedTimer t;t.start();for(int i=0;i<100;++i)QCOMPARE(store.byIds({1,2500,5000}).size(),3);timings<<t.nsecsElapsed()/1000000.;}
        std::sort(timings.begin(),timings.end());qInfo().noquote()<<"AUDIT_LOOKUP_5000_ROWS_100_CALLS_MEDIAN_MS"<<timings[3];
    }
};
QTEST_MAIN(FinalAuditTests)
#include "test_final_audit.moc"
