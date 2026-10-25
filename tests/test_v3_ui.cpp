#include "companion_window.h"
#include "smart_page.h"
#include <QtTest>
#include <QApplication>
#include <QTemporaryDir>
#include <QProcess>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTabWidget>
#include <QTextBrowser>
#include <QSettings>
#include <QPainter>
#include <QTimer>
#include <QTextStream>
#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif
using namespace dragon;
class CaptureFixture:public QWidget{
protected:
    void paintEvent(QPaintEvent *) override{QPainter painter(this);painter.fillRect(rect(),QColor(36,172,84));painter.setPen(Qt::white);painter.setFont(QFont("Arial",24));painter.drawText(30,100,"SCREEN TEST 42");}
};
class V3UiTests:public QObject {
    Q_OBJECT
private slots:
    void hideShowPreservesStateAndStopsAnimation(){
        QTextStream(stdout)<<"V3 UI: visibility preservation"<<Qt::endl;
        QTemporaryDir dir;Companion c(dir.filePath("memory.sqlite"));c.remember("保留的记忆");c.respond("你好");PetWidget pet(c);pet.move(210,140);pet.show();QTest::qWait(20);const auto position=pet.pos();const auto life=c.memory().life().json();const auto count=c.memory().memories().size();
        auto *timer=pet.findChild<QTimer*>("petAnimationTimer");QTextStream(stdout)<<"Animation timer found="<<bool(timer)<<Qt::endl;QVERIFY(timer);QVERIFY(timer->isActive());pet.hide();QVERIFY(!timer->isActive());QTest::qWait(30);pet.showOnDesktop();QVERIFY(timer->isActive());QCOMPARE(pet.pos(),position);QCOMPARE(c.memory().life().json(),life);QCOMPARE(c.memory().memories().size(),count);
    }
    void naturalLanguagePlanUsesExistingServices(){
        QTextStream(stdout)<<"V3 UI: natural language tool integration"<<Qt::endl;
        QTemporaryDir dir;Companion c(dir.filePath("memory.sqlite"));PetWidget pet(c);CompanionWindow window(c,pet);auto *smart=window.findChild<SmartPage*>();QVERIFY(smart);
        const int before=c.memory().exchanges();QVERIFY(c.sendMessage("请检查电脑为什么变卡，再总结今天的使用情况"));QCOMPARE(smart->hub().state(),QString("preview"));QCOMPARE(smart->hub().plan().size(),2);QCOMPARE(c.memory().exchanges(),before);
        QSignalSpy results(&smart->hub(),&ToolHub::result);smart->hub().run(true);QTRY_COMPARE(smart->hub().state(),QString("completed"));QCOMPARE(results.size(),2);QCOMPARE(results.first()[0].toString(),QString("system.health"));QCOMPARE(results.last()[0].toString(),QString("daily.summary"));QCOMPARE(c.memory().exchanges(),before);QCOMPARE(c.memory().integrity(),QString("ok"));
    }
    void originalTargetIsCapturedAndOwnWindowsRestored(){
        QTextStream(stdout)<<"V3 UI: external synthetic window capture"<<Qt::endl;
#ifdef Q_OS_WIN
        QProcess fixture;fixture.setProgram(QCoreApplication::applicationFilePath());fixture.setArguments({"--capture-fixture"});fixture.start();QVERIFY(fixture.waitForStarted());QVERIFY(fixture.waitForReadyRead(5000));const auto handle=fixture.readLine().trimmed().toULongLong();QVERIFY(handle);
        const auto target=ScreenCapture::inspect(handle);QTextStream(stdout)<<"Fixture handle received; target="<<target.title<<" process="<<target.process<<Qt::endl;QCOMPARE(target.title,QString("DragonGPT synthetic capture fixture"));QVERIFY(ScreenCapture::denied(target,{}).isEmpty());
        QTemporaryDir dir;ScreenCapture capture(dir.filePath("intelligence.ini"));capture.configure(true,false,{});QWidget overlay;overlay.resize(300,200);overlay.move(200,200);overlay.show();QSignalSpy ready(&capture,&ScreenCapture::ready),failed(&capture,&ScreenCapture::failed);
        capture.capture(handle);capture.capture(handle);QElapsedTimer wait;wait.start();while(ready.isEmpty() && failed.isEmpty() && wait.elapsed()<6000)QTest::qWait(10);QTextStream(stdout)<<"Capture ready="<<ready.size()<<" failed="<<failed.size()<<" overlayVisible="<<overlay.isVisible()<<Qt::endl;QVERIFY(!ready.isEmpty() || !failed.isEmpty());if(!failed.isEmpty())QTextStream(stdout)<<"Capture failure: "<<failed.first()[0].toString()<<Qt::endl;QVERIFY2(failed.isEmpty(),failed.isEmpty()?"":qPrintable(failed.first()[0].toString()));QCOMPARE(ready.size(),1);QVERIFY(overlay.isVisible());const auto image=qvariant_cast<QImage>(ready.first()[0]);QVERIFY(!image.isNull());const auto color=image.pixelColor(image.width()/2,image.height()*3/4);QTextStream(stdout)<<"Captured test pixel="<<color.name()<<Qt::endl;QVERIFY(color.green()>color.red()*2);QVERIFY(QDir(dir.path()).entryList({"*.png","*.jpg"},QDir::Files).isEmpty());
        capture.capture(handle);capture.cancel();QTest::qWait(200);QCOMPARE(ready.size(),1);QVERIFY(overlay.isVisible());fixture.terminate();if(!fixture.waitForFinished(2000)){fixture.kill();fixture.waitForFinished();}
#else
        QSKIP("Windows native capture test");
#endif
    }
};
int main(int argc,char **argv){
    QApplication app(argc,argv);qputenv("DRAGONGPT_DISABLE_HOTKEY","1");
    if(app.arguments().contains("--capture-fixture")){CaptureFixture widget;widget.setWindowTitle("DragonGPT synthetic capture fixture");widget.resize(600,350);widget.move(200,200);widget.show();QTimer::singleShot(100,&app,[&]{QTextStream(stdout)<<qulonglong(widget.winId())<<Qt::endl;});QTimer::singleShot(15000,&app,&QCoreApplication::quit);return app.exec();}
    V3UiTests tests;return QTest::qExec(&tests,argc,argv);
}
#include "test_v3_ui.moc"
