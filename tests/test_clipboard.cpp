#include "companion.h"
#include <QTest>
#include <QTemporaryDir>
using namespace dragon;
class ClipboardTests:public QObject{
    Q_OBJECT
private slots:
    void privateContent(){
        for(const auto &s:{"sk-this-is-a-fake-secret","password=example","验证码 123456","Authorization: Bearer test","https://example.com/?session=123","abCDefghijklMn987654","C:\\secret\\.env"})QVERIFY2(ClipboardHistory::sensitive(QString::fromUtf8(s)),s);
        QVERIFY(!ClipboardHistory::sensitive("今天记得复习高等数学"));QVERIFY(!ClipboardHistory::sensitive("https://example.com"));
        QCOMPARE(ClipboardHistory::classify("https://example.com"),QString("URL"));QCOMPARE(ClipboardHistory::classify("C:\\Work\\notes.txt"),QString("文件路径"));QCOMPARE(ClipboardHistory::classify("error: undefined reference"),QString("报错"));QCOMPARE(ClipboardHistory::classify("int main() { return 0; }"),QString("代码"));
    }
    void persistenceAndPrune(){
        QTemporaryDir dir;const auto path=dir.path()+"/m.sqlite";qint64 pinned=0;
        {Companion c(path);auto &history=c.clipboard();QVERIFY(!history.enabled());QVERIFY(!history.capture("普通文字"));history.setEnabled(true);QVERIFY(!history.capture("我的密码是example"));QVERIFY(history.capture("普通文字"));pinned=history.entries().first().id;history.pin(pinned,true);
         for(int i=0;i<70;++i)QVERIFY(history.capture(QString("正常的文字 %1").arg(i)));QCOMPARE(history.entries().size(),51);history.prune(QDateTime::currentDateTimeUtc().addDays(2));QCOMPARE(history.entries().size(),1);}
        {Companion c(path);auto &history=c.clipboard();QVERIFY(history.enabled());QCOMPARE(history.entries().first().id,pinned);history.remove(pinned);QVERIFY(history.entries().isEmpty());history.capture("第二条文字");history.clear();QVERIFY(history.entries().isEmpty());}
    }
};
QTEST_GUILESS_MAIN(ClipboardTests)
#include "test_clipboard.moc"
