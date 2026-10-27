#include "companion.h"
#include <QTest>
#include <QSignalSpy>
#include <QTemporaryDir>
using namespace dragon;
class HealthTests:public QObject{
    Q_OBJECT
private slots:
    void sustainedAndBounded(){
        HealthMonitor monitor;QSignalSpy alert(&monitor,&HealthMonitor::sustainedLoad),active(&monitor,&HealthMonitor::activeSeconds);
        HealthSample sample;sample.time=QDateTime::currentDateTimeUtc();sample.cpu=95;sample.memory=40;sample.idleSeconds=2;
        monitor.ingest(sample);QCOMPARE(alert.size(),0);
        for(int i=1;i<=24;++i){sample.time=sample.time.addSecs(5);monitor.ingest(sample);}QCOMPARE(alert.size(),1);
        for(int i=0;i<100;++i){sample.time=sample.time.addSecs(5);monitor.ingest(sample);}QCOMPARE(alert.size(),1);QCOMPARE(monitor.trend().size(),120);QVERIFY(!active.isEmpty());
        sample.time=sample.time.addSecs(3600);monitor.ingest(sample);QCOMPARE(alert.size(),1); // suspended clock gap is not continuous high load
        sample.cpu=20;sample.time=sample.time.addSecs(5);monitor.ingest(sample);QVERIFY(monitor.overview().contains("未发现持续高负载"));
    }
    void realHardware(){
        HealthMonitor monitor;monitor.start();QTRY_VERIFY_WITH_TIMEOUT(monitor.latest().memoryTotal>0,8000);QVERIFY(monitor.latest().memory>=0 && monitor.latest().memory<=100);
        const auto first=monitor.latest().time;monitor.sample();QTRY_VERIFY_WITH_TIMEOUT(monitor.latest().time>first,8000);QVERIFY(monitor.latest().cpu>=0 && monitor.latest().cpu<=100);qInfo().noquote()<<monitor.overview();monitor.stop();
    }
    void statistics(){
        QTemporaryDir dir;Companion c(dir.path()+"/m.sqlite");for(int i=0;i<13;++i)c.tools().observeActivity(5,QDate::currentDate());QCOMPARE(c.tools().dailyStats()["active_seconds"].toInt(),65);
        c.tools().observeActivity(3600,QDate::currentDate());QCOMPARE(c.tools().dailyStats()["active_seconds"].toInt(),65);c.respond("你好");QCOMPARE(c.tools().dailyStats()["interactions"].toInt(),1);QVERIFY(c.tools().dailySummary().contains("仅统计"));
    }
};
QTEST_GUILESS_MAIN(HealthTests)
#include "test_health.moc"
