#include "tool_hub.h"
#include "context_engine.h"
#include "desktop_hotkey.h"
#include "screen_capture.h"
#include "screen_ocr.h"
#include "companion.h"
#include <QtTest>
#include <QTemporaryDir>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QJsonDocument>
#include <QSqlQuery>
#include <QPainter>
using namespace dragon;
namespace {
QJsonObject step(QString id,QJsonObject args={}){return {{"tool",id},{"input",args}};}
QJsonObject emptySchema(){return {{"type","object"},{"properties",QJsonObject{}}};}
}
class IntelligenceTests:public QObject {
    Q_OBJECT
private slots:
    void permissionsAndSchema(){
        ToolHub hub;int calls=0;hub.add({"write","mutation",emptySchema(),true,false},[&](auto,auto done){++calls;done(true,{});});
        QVERIFY(!hub.propose({step("shell")}).isEmpty());QVERIFY(!hub.propose({step("write",{{"command","delete"}})}).isEmpty());
        QVERIFY(!hub.propose({step("write"),step("write")}).isEmpty());QVERIFY(hub.propose({step("write")}).isEmpty());
        QSignalSpy permission(&hub,&ToolHub::confirmationRequired);hub.run();QCOMPARE(permission.size(),1);QCOMPARE(calls,0);QVERIFY(!hub.busy());
        hub.run(true);QTRY_COMPARE(hub.state(),QString("completed"));QCOMPARE(calls,1);hub.run(true);QCOMPARE(calls,1);
        QVERIFY(!QString::fromUtf8(QJsonDocument(hub.audit()).toJson()).contains("input"));
    }
    void orderedRetryAndFailure(){
        ToolHub hub;QStringList order;int attempts=0;
        hub.add({"read","safe",emptySchema(),false,true},[&](auto,auto done){order<<"read";done(++attempts>1,{});});
        hub.add({"write","mutation",emptySchema(),true,false},[&](auto,auto done){order<<"write";done(false,{});});
        hub.add({"after","unreachable",emptySchema()},[&](auto,auto done){order<<"after";done(true,{});});
        QVERIFY(hub.propose({step("read"),step("write"),step("after")}).isEmpty());hub.run(true);QTRY_COMPARE(hub.state(),QString("failed"));QCOMPARE(order,QStringList({"read","read","write"}));QCOMPARE(hub.audit().size(),3);
    }
    void cancellationAndTimeout(){
        ToolHub hub;ToolHub::Completion late;int cancelled=0;
        hub.add({"wait","async",emptySchema(),false,false,100},[&](auto,auto done){late=done;},[&]{++cancelled;});
        QVERIFY(hub.propose({step("wait")}).isEmpty());hub.run();hub.cancel();late(true,{});QCOMPARE(hub.state(),QString("cancelled"));QVERIFY(hub.audit().isEmpty());
        QVERIFY(hub.propose({step("wait")}).isEmpty());hub.run();QTRY_COMPARE(hub.state(),QString("failed"));QCOMPARE(cancelled,2);late(true,{});QCOMPARE(hub.audit().size(),1);
    }
    void numericValidationAndLoopLimit(){
        ToolHub hub;hub.add({"focus","focus",QJsonObject{{"properties",QJsonObject{{"minutes",QJsonObject{{"type","integer"},{"minimum",1},{"maximum",180}}}}},{"required",QJsonArray{"minutes"}}}},[](auto,auto d){d(true,{});});
        for(double value:{-1.,0.,1.5,181.})QVERIFY(!hub.propose({step("focus",{{"minutes",value}})}).isEmpty());
        QJsonArray tooMany;for(int i=1;i<10;++i)tooMany.append(step("focus",{{"minutes",i}}));QVERIFY(!hub.propose(tooMany).isEmpty());
        QVERIFY(hub.propose({step("focus",{{"minutes",25}})}).isEmpty());
    }
    void hotkeyDoesNotTreatTypingAsControl(){
        DesktopHotkey hotkey;QSignalSpy activated(&hotkey,&DesktopHotkey::activated);
        hotkey.observeQ(true,false,10);hotkey.observeQ(false,false,30);hotkey.observeQ(true,false,110);hotkey.observeQ(false,false,130);QTest::qWait(1);QCOMPARE(activated.size(),0);
        hotkey.observeQ(true,true,300);hotkey.observeQ(true,true,350);hotkey.observeQ(false,true,400);hotkey.observeQ(true,true,450);hotkey.observeQ(false,true,500);QTRY_COMPARE(activated.size(),1);
        hotkey.observeQ(true,true,1200);hotkey.observeQ(false,true,1250);hotkey.observeQ(true,true,1800);hotkey.observeQ(false,true,1850);QTest::qWait(1);QCOMPARE(activated.size(),1);
        QVERIFY(!hotkey.configure("Q;run-command").isEmpty());
    }
    void contextHysteresisAndPrivacy(){
        ContextEngine engine;auto now=QDateTime::currentDateTimeUtc();engine.observe("C++ documentation",{},false,now);QCOMPARE(engine.current().kind,QString("unknown"));engine.setEnabled(true);
        engine.observe("C++ documentation",{},false,now);engine.observe("C++ documentation",{},false,now.addSecs(11));QCOMPARE(engine.current().kind,QString("programming"));
        engine.observe("error: compiler failed",{},false,now.addSecs(12));QCOMPARE(engine.current().kind,QString("programming"));engine.observe("error: compiler failed",{},false,now.addSecs(23));QCOMPARE(engine.current().kind,QString("troubleshooting"));
        engine.observe("game",{},true,now.addSecs(24));QVERIFY(engine.current().lowInterruption);QCOMPARE(engine.current().kind,QString("fullscreen"));engine.setEnabled(false);QCOMPARE(engine.current().confidence,0.);
        QTemporaryDir dir;ScreenCapture capture(dir.filePath("intelligence.ini"));QSignalSpy failed(&capture,&ScreenCapture::failed);capture.capture();QCOMPARE(failed.size(),1);QVERIFY(!capture.busy());
        CaptureTarget target{1,"支付确认","browser.exe",QRect(0,0,100,100),false};QVERIFY(!ScreenCapture::denied(target,{}).isEmpty());target.title="C++ reference";QVERIFY(ScreenCapture::denied(target,{}).isEmpty());QVERIFY(!ScreenCapture::denied(target,{"browser"}).isEmpty());
    }
    void memoryDedupeConflictAndRelation(){
        QTemporaryDir dir;MemoryStore store(dir.filePath("memory.sqlite"));store.appendExchange("hello","hi");const auto relation=store.life().json();
        const auto id=store.remember("我喜欢安静。");QCOMPARE(store.remember(" 我喜欢安静！ "),id);QCOMPARE(store.memories().size(),1);
        store.suggest("我不喜欢安静");QCOMPARE(store.suggestions().size(),1);const auto suggestion=store.suggestions().first().id;QCOMPARE(store.conflicts("我不喜欢安静").size(),1);
        QVERIFY_EXCEPTION_THROWN(store.resolveSuggestion(suggestion,true),std::runtime_error);QCOMPARE(store.memories().first().content,QString("我喜欢安静。"));
        store.resolveSuggestion(suggestion,true,true);QCOMPARE(store.memories().size(),1);QCOMPARE(store.memories().first().id,id);QCOMPARE(store.memories().first().content,QString("我不喜欢安静"));QCOMPARE(store.life().json(),relation);QCOMPARE(store.integrity(),QString("ok"));
        store.suggest("我喜欢我的密码是secret123");QVERIFY(store.suggestions().isEmpty());
    }
    void proactiveQuotasAndIgnoredFeedback(){
        LifeSettings p;p.quietStart=p.quietEnd=0;LifeState s;auto now=QDateTime(QDate(2026,10,25),QTime(12,0));
        s.lastProactive=now.addSecs(-60);QVERIFY(!CompanionLife::eligible(s,p,now,{},false));s.lastProactive=now.addSecs(-5401);QVERIFY(CompanionLife::eligible(s,p,now,{},false));
        s.ignoredOpenings=2;QVERIFY(!CompanionLife::eligible(s,p,now,{},false));s.lastProactive=now.addSecs(-18000);s.proactiveDate=now.date();s.proactiveCount=p.dailyLimit;QVERIFY(!CompanionLife::eligible(s,p,now,{},false));
        QVERIFY(CompanionLife::eligible(s,p,now.addDays(1),{},false));QVERIFY(!CompanionLife::eligible(s,p,now.addDays(-1),{},false));
    }
    void sharedRequestGateAndVisionCapabilities(){
        QTcpServer server;QVERIFY(server.listen(QHostAddress::LocalHost));int count=0;QByteArray body;
        connect(&server,&QTcpServer::newConnection,&server,[&]{while(server.hasPendingConnections()){auto *s=server.nextPendingConnection();connect(s,&QTcpSocket::readyRead,s,[&,s]{body+=s->readAll();++count;});connect(s,&QTcpSocket::disconnected,s,&QObject::deleteLater);}});
        const QUrl url(QString("http://127.0.0.1:%1/responses").arg(server.serverPort()));ChatService one(nullptr,url),two(nullptr,url);ChatSettings settings;settings.onlineEnabled=true;
        QVERIFY(one.start(settings,"fixture","persona","{}",{},"hello").isEmpty());QVERIFY(!two.start(settings,"fixture","persona","{}",{},"hello").isEmpty());one.cancel();
        settings.visionModel="deepseek-v4-pro";settings.model=settings.visionModel;QVERIFY(!two.start(settings,"fixture","persona","{}",{},"screen","fake-png").isEmpty());
        settings.visionModel="deepseek-flash";settings.model=settings.visionModel;QVERIFY(two.start(settings,"fixture","persona","{}",{},"screen","fake-png").isEmpty());QTRY_VERIFY(body.contains("input_image"));two.cancel();QVERIFY(count<=2);
    }
    void localOcrRealWindowsEngine(){
        QImage image(1400,300,QImage::Format_RGB32);image.fill(Qt::white);QPainter painter(&image);painter.setPen(Qt::black);painter.setFont(QFont("Arial",50));painter.drawText(40,150,"DragonGPT SCREEN TEST 42");painter.end();
        ScreenOcr ocr;QSignalSpy ready(&ocr,&ScreenOcr::ready),failed(&ocr,&ScreenOcr::failed);ocr.start(image);QTRY_VERIFY_WITH_TIMEOUT(!ready.isEmpty() || !failed.isEmpty(),30000);
        QVERIFY2(failed.isEmpty(),failed.isEmpty()?"":qPrintable(failed.first().first().toString()));QVERIFY(ready.first().first().toString().contains("42"));
    }
};
QTEST_MAIN(IntelligenceTests)
#include "test_intelligence.moc"
