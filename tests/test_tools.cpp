#include "companion.h"
#include <QTest>
#include <QTemporaryDir>
#include <QSignalSpy>
#include <QTimeZone>
using namespace dragon;
class ToolTests:public QObject{
    Q_OBJECT
private slots:
    void dates(){
        const QDateTime now(QDate(2026,10,24),QTime(12,30),QTimeZone("Asia/Shanghai"));
        const auto task=TaskParser::parse("明天下午三点提醒我复习高数",now);QVERIFY(!task.needsConfirmation);QCOMPARE(task.title,QString("复习高数"));QCOMPARE(task.due.date(),QDate(2026,10,25));QCOMPARE(task.due.time(),QTime(15,0));
        QCOMPARE(TaskParser::parse("明天下午三点半提醒我看书",now).due.time(),QTime(15,30));
        QCOMPARE(TaskParser::parse("45分钟后提醒我交作业",now).due,now.addSecs(2700));
        for(const auto &s:{"明天三点提醒我学习","下午三点提醒我学习","明天下午提醒我学习","今天上午八点提醒我学习","2026-13-40 99:99提醒我学习"})QVERIFY2(TaskParser::parse(QString::fromUtf8(s),now).needsConfirmation,s);
        const auto plain=TaskParser::parse("添加待办：高优先级：完成数学练习",now);QCOMPARE(plain.title,QString("完成数学练习"));QCOMPARE(plain.priority,2);QVERIFY(!plain.needsConfirmation && !plain.due.isValid());
    }
    void persistentReminders(){
        QTemporaryDir dir;const auto path=dir.path()+"/m.sqlite";qint64 id=0;const auto due=QDateTime::currentDateTimeUtc().addSecs(20);
        {Companion c(path);TaskDraft draft;draft.title="测试任务";draft.due=due;draft.priority=2;id=c.tools().addTask(draft);}
        {Companion c(path);QCOMPARE(c.tools().tasks().first().id,id);QSignalSpy notifications(&c.tools(),&LocalTools::notification);
         c.tools().tick(due.addSecs(1));QCOMPARE(notifications.size(),1);c.tools().tick(due.addSecs(2));QCOMPARE(notifications.size(),1);
         c.tools().completeTask(id,true);QCOMPARE(c.tools().dailyStats()["todos_completed"].toInt(),1);
         c.tools().tick(due.addSecs(240));QCOMPARE(notifications.size(),1);
        }
    }
    void routeAndAmbiguity(){
        QTemporaryDir dir;Companion c(dir.path()+"/m.sqlite");QSignalSpy confirm(&c.tools(),&LocalTools::confirmationNeeded),replies(&c,&Companion::replyReady);
        QVERIFY(c.sendMessage("明天三点提醒我学习"));QCOMPARE(confirm.size(),1);QCOMPARE(c.tools().tasks().size(),0);QCOMPARE(replies.size(),1);
        QVERIFY(c.sendMessage("添加待办：完成练习"));QCOMPARE(c.tools().tasks().size(),1);QCOMPARE(replies.last()[1].toString(),QString("local-brain"));
        QVERIFY(c.sendMessage("开始番茄钟"));QVERIFY(replies.last()[0].toString().contains("专注陪伴"));
    }
};
QTEST_GUILESS_MAIN(ToolTests)
#include "test_tools.moc"
