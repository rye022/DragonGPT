#include "companion.h"
#include <QTest>
#include <QTemporaryDir>
#include <QDir>
#include <QJsonArray>
#include <QSignalSpy>
#include <QSqlQuery>
using namespace dragon;
class SemanticTests:public QObject{
    Q_OBJECT
private slots:
    void migration(){
        QTemporaryDir dir;const auto path=dir.path()+"/memory.sqlite";
        {MemoryStore initial(path);initial.setNickname("旧称呼");initial.remember("我在学习 C++ 多线程");initial.appendExchange("难过","陪你");initial.selectDialogue({});}
        {auto db=QSqlDatabase::addDatabase("QSQLITE","v5-fixture");db.setDatabaseName(path);QVERIFY(db.open());QSqlQuery q(db);QVERIFY(q.exec("PRAGMA user_version=5"));db.close();}QSqlDatabase::removeDatabase("v5-fixture");
        {MemoryStore upgraded(path);QCOMPARE(upgraded.nickname(),QString("旧称呼"));QCOMPARE(upgraded.exchanges(),1);QCOMPARE(upgraded.memories().size(),1);QCOMPARE(upgraded.life().mood,Emotion::Sad);QCOMPARE(upgraded.integrity(),QString("ok"));
         QVERIFY(QDir(dir.path()).entryList({"*.pre-v7-*.sqlite"},QDir::Files).size()==1);
         upgraded.backupTo(dir.path()+"/backup.sqlite");upgraded.clearMemories();QCOMPARE(upgraded.memories().size(),0);QCOMPARE(upgraded.exchanges(),1);QVERIFY(upgraded.life().experience>0);}
        {MemoryStore backup(dir.path()+"/backup.sqlite");QCOMPARE(backup.memories().size(),1);QCOMPARE(backup.nickname(),QString("旧称呼"));}
    }
    void vectorLifecycle(){
        QTemporaryDir dir;MemoryStore store(dir.path()+"/memory.sqlite");const auto id=store.remember("学习并发编程");
        auto db=QSqlDatabase::addDatabase("QSQLITE","vectors");db.setDatabaseName(store.databasePath());QVERIFY(db.open());
        auto insert=[&]{QSqlQuery q(db);q.prepare("INSERT INTO memory_vectors VALUES(?,'test','hash',X'00000000',1,'now')");q.addBindValue(id);QVERIFY(q.exec());};
        auto count=[&]{QSqlQuery q(db);q.exec("SELECT COUNT(*) FROM memory_vectors");q.next();return q.value(0).toInt();};
        insert();QCOMPARE(count(),1);store.updateMemory(id,"学习绘画","兴趣",.8);QCOMPARE(count(),0);
        insert();store.forget(id);QCOMPARE(count(),0);QVERIFY(store.byIds({id}).isEmpty());db.close();db={};QSqlDatabase::removeDatabase("vectors");
    }
    void restoreAndFailedRestore(){
        QTemporaryDir dir;const auto path=dir.path()+"/current.sqlite",backup=dir.path()+"/backup.sqlite";
        {MemoryStore store(path);store.setNickname("备份前称呼");store.remember("真正保存的经历");store.backupTo(backup);store.setNickname("后来修改的称呼");}
        MemoryStore::restoreBackup(backup,path);{MemoryStore restored(path);QCOMPARE(restored.nickname(),QString("备份前称呼"));QCOMPARE(restored.memories().size(),1);QCOMPARE(restored.integrity(),QString("ok"));}
        QFile invalid(dir.path()+"/invalid.sqlite");QVERIFY(invalid.open(QIODevice::WriteOnly));invalid.write("not a database");invalid.close();bool rejected=false;try{MemoryStore::restoreBackup(invalid.fileName(),path);}catch(const std::exception &){rejected=true;}QVERIFY(rejected);
        {MemoryStore preserved(path);QCOMPARE(preserved.nickname(),QString("备份前称呼"));QVERIFY(QDir(dir.path()).entryList({"*.before-restore-*.sqlite"},QDir::Files).size()==1);}
    }
    void realLegacyColumns(){
        QTemporaryDir dir;const auto path=dir.path()+"/legacy.sqlite";
        {auto db=QSqlDatabase::addDatabase("QSQLITE","real-v5");db.setDatabaseName(path);QVERIFY(db.open());QSqlQuery q(db);
         for(const auto &sql:{"CREATE TABLE memories(id INTEGER PRIMARY KEY,content TEXT NOT NULL UNIQUE,created_at TEXT NOT NULL,tags TEXT NOT NULL DEFAULT '',importance REAL NOT NULL DEFAULT 0.5,expires_at TEXT,last_used_at TEXT)","CREATE TABLE relation(id INTEGER PRIMARY KEY,exchanges INTEGER NOT NULL)","INSERT INTO relation VALUES(1,35)","INSERT INTO memories VALUES(9,'old memory','2026-10-20T01:00:00.000Z','project',0.8,NULL,NULL)","PRAGMA user_version=5"})QVERIFY(q.exec(QString::fromUtf8(sql)));db.close();}QSqlDatabase::removeDatabase("real-v5");
        {MemoryStore migrated(path);QCOMPARE(migrated.memories().first().id,qint64(9));QCOMPARE(migrated.memories().first().content,QString("old memory"));QCOMPARE(migrated.memories().first().importance,.8);QCOMPARE(migrated.exchanges(),35);QCOMPARE(migrated.life().legacyStage,2);QCOMPARE(migrated.integrity(),QString("ok"));}
    }
    void realChineseEmbeddings(){
        if(qEnvironmentVariableIsEmpty("DRAGONGPT_EMBEDDING_HOME"))QSKIP("Local embedding runtime not configured");
        QTemporaryDir dir;Companion c(dir.path()+"/memory.sqlite");
        c.remember("我最近开始学习 C++ 多线程，准备掌握互斥锁和线程池。");c.remember("我喜欢吃草莓蛋糕。");c.remember("我在周末给猫咪梳毛。");
        SemanticMemory engine(c.memory().databasePath());QSignalSpy spy(&engine,&SemanticMemory::searched);
        QTimer heartbeat;heartbeat.setInterval(10);QElapsedTimer elapsed;elapsed.start();int beats=0;qint64 lastBeat=0,maxGap=0;
        connect(&heartbeat,&QTimer::timeout,this,[&]{const auto now=elapsed.elapsed();maxGap=qMax(maxGap,now-lastBeat);lastBeat=now;++beats;});heartbeat.start();
        engine.search(1,"你觉得我最近的学习进度怎么样？");QTRY_COMPARE_WITH_TIMEOUT(spy.size(),1,20000);
        QVERIFY(beats>=3);qInfo()<<"Embedding event-loop heartbeats:"<<beats<<"longest interval ms:"<<maxGap;heartbeat.stop();
        const auto first=spy.takeFirst();QVERIFY2(first[2].toString().contains("ONNX"),qPrintable(first[2].toString()));const auto hits=first[1].toJsonArray();QVERIFY(!hits.isEmpty());
        const auto best=c.memory().byIds({hits.first().toObject()["id"].toInteger()});QVERIFY(best.first().content.contains("多线程"));
        engine.search(3,"甜点与蛋糕","relationship");QTRY_COMPARE_WITH_TIMEOUT(spy.size(),1,10000);QVERIFY(spy.takeFirst()[1].toJsonArray().isEmpty());
        c.forget(best.first().id);engine.search(2,"线程同步与并发学习");QTRY_COMPARE_WITH_TIMEOUT(spy.size(),1,10000);
        for(const auto &hit:spy.takeFirst()[1].toJsonArray())QVERIFY(hit.toObject()["id"].toInteger()!=best.first().id);
        QVERIFY(engine.workerProcessId()>0);engine.cancel();QTRY_COMPARE_WITH_TIMEOUT(engine.workerProcessId(),qint64(0),2000);
        QSignalSpy reply(&c,&Companion::replyReady);QVERIFY(c.sendMessage("还记得我的甜点喜好吗？"));QTRY_COMPARE_WITH_TIMEOUT(reply.size(),1,20000);QVERIFY(reply.first()[0].toString().contains("草莓"));
        QVERIFY(c.sendMessage("我最近在做什么？"));c.cancelReply();QVERIFY(!c.busy());
    }
    void followupConsentAndPrivacy(){
        QTemporaryDir dir;Companion c(dir.path()+"/memory.sqlite");QVERIFY(!c.lifeSettings().memoryFollowups);
        c.remember("我正在学习 C++ 多线程，PRIVATE_CONTEXT_MUST_NOT_BE_QUOTED");
        const auto now=QDateTime::currentDateTimeUtc().addDays(2);const auto selected=c.memory().memoryFollowup(now);QVERIFY(selected);QVERIFY(selected.text.contains("多线程"));QVERIFY(!selected.text.contains("PRIVATE_CONTEXT"));QVERIFY(!c.memory().memoryFollowup(now.addSecs(60)));
        c.respond("别再提多线程了");QVERIFY(c.memory().interactionMemories().isEmpty());QVERIFY(!c.memory().memoryFollowup(now.addDays(5)));
    }
    void unavailableModelFallback(){
        QTemporaryDir home;const auto original=qgetenv("DRAGONGPT_EMBEDDING_HOME");
        QFile ready(home.path()+"/ready.json"),worker(home.path()+"/worker.py");QVERIFY(ready.open(QIODevice::WriteOnly));ready.write("{\"python\":\"missing-python.exe\",\"model\":\"missing\"}");ready.close();QVERIFY(worker.open(QIODevice::WriteOnly));worker.close();
        qputenv("DRAGONGPT_EMBEDDING_HOME",home.path().toUtf8());
        {QTemporaryDir data;Companion c(data.path()+"/m.sqlite");c.remember("我喜欢草莓");QSignalSpy reply(&c,&Companion::replyReady);QVERIFY(c.sendMessage("还记得草莓吗？"));QTRY_COMPARE_WITH_TIMEOUT(reply.size(),1,2000);QVERIFY(c.semanticStatus().contains("降级"));QVERIFY(reply.first()[0].toString().contains("草莓"));QVERIFY(!c.busy());}
        if(original.isNull())qunsetenv("DRAGONGPT_EMBEDDING_HOME");else qputenv("DRAGONGPT_EMBEDDING_HOME",original);
    }
    void extractionRequiresConfirmation(){
        QTemporaryDir dir;Companion c(dir.path()+"/m.sqlite");c.respond("我最近开始学习 C++ 多线程");QVERIFY(c.memory().memories().isEmpty());QCOMPARE(c.memory().suggestions().size(),1);
        c.resolveSuggestion(c.memory().suggestions().first().id,true);QCOMPARE(c.memory().memories().first().source,QString("confirmed-conversation"));
        c.respond("我最近完成了课程项目");QCOMPARE(c.memory().suggestions().size(),1);c.resolveSuggestion(c.memory().suggestions().first().id,true);QCOMPARE(c.memory().memories().first().layer,QString("episodic"));
        c.respond("如果我最近开始学习绘画呢？");QVERIFY(c.memory().suggestions().isEmpty());
    }
};
QTEST_GUILESS_MAIN(SemanticTests)
#include "test_semantic.moc"
