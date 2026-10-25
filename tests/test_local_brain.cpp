#include "companion.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QSignalSpy>
#include <QTest>
using namespace dragon;
class BrainTests:public QObject{
    Q_OBJECT
private slots:
    void routing(){
        for(const auto &s:{"现在几点了？","几点啦？","告诉我时间。","请告诉我时间"})QCOMPARE(LocalBrain::recognize(QString::fromUtf8(s)).intent,LocalIntent::Time);
        for(const auto &s:{"我今天不想工作了。","如何实现招手？","如果我说休息一下呢？","不要开始番茄钟","他说招招手","我想知道时间管理的意义","现在几点然后招手","开始番茄钟并且提醒喝水"})QVERIFY2(!LocalBrain::recognize(QString::fromUtf8(s)),s);
        QCOMPARE(LocalBrain::recognize("进入认真工作状态").action,PetAction::Work);
        QCOMPARE(LocalBrain::recognize("打开电脑状态页面").intent,LocalIntent::HealthPage);
        QCOMPARE(LocalBrain::recognize("开始45分钟番茄钟").minutes,45);
        QCOMPARE(LocalBrain::recognize("15分钟后提醒我喝水").minutes,15);
        QCOMPARE(LocalBrain::recognize("我的 CPU 占用率是多少？").intent,LocalIntent::Cpu);
        QCOMPARE(LocalBrain::recognize("内存使用情况怎么样？").intent,LocalIntent::Memory);
        QCOMPARE(LocalBrain::recognize("开始番茄钟（45分钟）").minutes,45);
        QCOMPARE(LocalBrain::recognize("开始0分钟番茄钟").minutes,-1);
        QCOMPARE(LocalBrain::recognize("明天下午三点提醒我复习高数").intent,LocalIntent::TodoAdd);
    }
    void unifiedResponse(){
        QTemporaryDir dir;Companion c(dir.path()+"/m.sqlite");
        auto settings=c.chatSettings();settings.onlineEnabled=true;c.saveChatSettings(settings);
        QSignalSpy replies(&c,&Companion::replyReady),actions(&c,&Companion::actionRequested),started(&c,&Companion::turnStarted);
        QVERIFY(c.sendMessage("现在几点了？"));QCOMPARE(replies.size(),1);QCOMPARE(replies.first().at(1).toString(),QString("local-brain"));QVERIFY(!c.busy());
        QVERIFY(c.sendMessage("招招手"));QCOMPARE(actions.size(),1);QCOMPARE(started.size(),2);QCOMPARE(c.memory().exchanges(),2);
        QCOMPARE(c.memory().recentMessages(1).first().source,QString("local-brain"));QVERIFY(c.memory().life().experience>0);
        const QString fake="sk-fake-key-only-for-privacy-test";QVERIFY(c.sendMessage("记住："+fake));QVERIFY(!c.memory().exportMemories().contains(fake.toUtf8()));
        for(const auto &m:c.memory().recentMessages())QVERIFY(!m.content.contains(fake));
    }
};
QTEST_GUILESS_MAIN(BrainTests)
#include "test_local_brain.moc"
