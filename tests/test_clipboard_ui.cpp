#include "clipboard_panel.h"
#include <QApplication>
#include <QClipboard>
#include <QCryptographicHash>
#include <QFile>
#include <QLabel>
#include <QListWidget>
#include <QMimeData>
#include <QPainter>
#include <QProcess>
#include <QScrollArea>
#include <QPushButton>
#include <QScreen>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <memory>
#ifdef Q_OS_WIN
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
using namespace dragon;
class ClipboardUiTests:public QObject {
    Q_OBJECT
private slots:
    void basicCopy(){
        QTemporaryDir dir;Companion c(dir.filePath("memory.sqlite"));
        c.clipboard().setEnabled(true);c.clipboard().capture("普通的复制验证文字");c.clipboard().setEnabled(false);
        PetWidget pet(c);pet.setMotionEnabled(false);pet.move(400,360);
        ClipboardPanel panel(c.clipboard(),pet);pet.show();panel.toggle();QTest::qWait(250);
        QPushButton *copy=nullptr;for(auto *b:panel.findChildren<QPushButton*>())if(b->text()=="复制")copy=b;
        QVERIFY(copy);QVERIFY(copy->isVisible());
        QTRY_VERIFY(copy->visibleRegion().contains(copy->rect().center()));
        QGuiApplication::clipboard()->setText("用于覆盖的测试文字");
        QTest::mouseClick(copy,Qt::LeftButton);
        QTRY_COMPARE(QGuiApplication::clipboard()->text(),QString("普通的复制验证文字"));
    }
    void copyAndCapture(){
        QTemporaryDir dir; Companion c(dir.filePath("memory.sqlite"));
        PetWidget pet(c); pet.setMotionEnabled(false);
        ClipboardPanel panel(c.clipboard(),pet); pet.move(400,360); pet.show();
        panel.toggle(); QTest::qWait(250);
        auto *list=panel.findChild<QListWidget*>("clipboardEntries"); QVERIFY(list);
        QPushButton *copy=nullptr; for(auto *b:panel.findChildren<QPushButton*>())if(b->text()=="复制")copy=b;
        QVERIFY(copy);
        c.clipboard().setEnabled(true);
        const QString original="    缩进的中文和 emoji 🐉\n\t第二行\n";
        QGuiApplication::clipboard()->setText(original);
        QTRY_COMPARE(list->count(),1);
        QCOMPARE(c.clipboard().entries().first().text,original);
        QGuiApplication::clipboard()->setText("另一个用于验证的内容");
        QTRY_COMPARE(list->count(),2);
        list->setCurrentRow(1);
        QTest::mouseClick(copy,Qt::LeftButton);
        QTRY_COMPARE(QGuiApplication::clipboard()->text(),original);
        QProcess reader; reader.start(QCoreApplication::applicationFilePath(),{"--clipboard-digest"});
        // Deliberately stop pumping the owner's GUI events. Windows must hold
        // materialized data, rather than requesting it back from a busy pet.
        QVERIFY(reader.waitForFinished(5000)); QCOMPARE(reader.exitCode(),0);
        QCOMPARE(reader.readAllStandardOutput().trimmed(),QCryptographicHash::hash(original.toUtf8(),QCryptographicHash::Sha256).toHex());
        QTest::qWait(120);
        QCOMPARE(list->count(),2);
        c.clipboard().setEnabled(false);
        QGuiApplication::clipboard()->setText("未记录的测试内容");
        QTest::mouseClick(copy,Qt::LeftButton);
        QTRY_COMPARE(QGuiApplication::clipboard()->text(),original);
        const auto preview=qEnvironmentVariable("DRAGON_CLIPBOARD_PREVIEW");
        if(!preview.isEmpty()){
            QTRY_VERIFY_WITH_TIMEOUT(!pet.bubbleActive(),10000);
            QVERIFY(panel.grab().save(preview));
            const auto saveScene=[&](const QString &path){
                const QRect scene=pet.geometry().united(panel.geometry()).adjusted(-12,-12,12,12);
                const auto dpr=pet.devicePixelRatioF();
                QImage joined(QSize(qRound(scene.width()*dpr),qRound(scene.height()*dpr)),QImage::Format_ARGB32_Premultiplied);
                joined.setDevicePixelRatio(dpr);joined.fill(QColor("#191622"));QPainter painter(&joined);
                painter.drawPixmap(pet.pos()-scene.topLeft(),pet.grab());painter.drawPixmap(panel.pos()-scene.topLeft(),panel.grab());painter.end();return joined.save(path);
            };
            QVERIFY(saveScene(preview+".scene.png"));panel.toggle();
            QTRY_VERIFY(panel.width()<=120);QVERIFY(saveScene(preview+".collapsed.png"));
        }
    }
    void headerAboveHead(){
        QTemporaryDir dir; Companion c(dir.filePath("memory.sqlite")); PetWidget pet(c);
        pet.setMotionEnabled(false); ClipboardPanel panel(c.clipboard(),pet);
        auto *header=panel.findChild<QPushButton*>("toggleClipboardShelf"); QVERIFY(header);
        const auto area=pet.screen()->availableGeometry();
        for(int height:{260,480,680})for(bool half:{false,true}){
            pet.setPetHeight(height);pet.setHalfView(half);pet.move(area.center()-QPoint(pet.width()/2,150));pet.show();
            QTest::qWait(30);
            const QImage sprite(":/assets/character.png");
            const QSizeF source=half?QSizeF(sprite.width()*.70,sprite.height()*.61):QSizeF(sprite.size());
            const QRectF available(13,102,pet.width()-26,height-16);
            const double scale=qMin(available.width()/source.width(),available.height()/source.height());
            int top=0;bool found=false;
            for(int y=0;y<sprite.height() && !found;++y)for(int x=0;x<sprite.width();++x)if(qAlpha(sprite.pixel(x,y))>16){top=y;found=true;break;}
            const int headY=pet.y()+qRound(available.bottom()-(source.height()-top)*scale);
            const int headerBottom=header->mapToGlobal(QPoint(0,header->height())).y();
            QVERIFY2(headY-headerBottom>=4 && headY-headerBottom<=16,"Clipboard header must remain directly above the character at every size and view");
        }
        pet.setPetHeight(480);pet.setHalfView(false);
        const int idleHeader=header->mapToGlobal(QPoint(0,header->height())).y();
        pet.setMotionEnabled(true);pet.setRandomActionsEnabled(false);pet.previewAction(PetAction::Work,5000);
        QTRY_VERIFY(header->mapToGlobal(QPoint(0,header->height())).y()>idleHeader+35);
        pet.setMotionEnabled(false);pet.move(area.topLeft());QTest::qWait(30);
        panel.toggle();QTest::qWait(250);QVERIFY(area.contains(panel.geometry()));
        auto *scroll=panel.findChild<QScrollArea*>();if(scroll)QVERIFY(scroll->height()>0);
        panel.toggle();QTest::qWait(250);QVERIFY(panel.width()<=120);
        pet.hide();QVERIFY(!panel.isVisible());pet.show();QTRY_VERIFY(panel.isVisible());
    }
    void clipboardBusyRetry(){
#ifdef Q_OS_WIN
        QTemporaryDir dir;Companion c(dir.filePath("memory.sqlite"));c.clipboard().setEnabled(true);c.clipboard().capture("占用重试验证文字");
        c.clipboard().setEnabled(false);PetWidget pet(c);pet.move(400,360);pet.setMotionEnabled(false);
        ClipboardPanel panel(c.clipboard(),pet);pet.show();panel.toggle();QTest::qWait(250);
        auto *copy=panel.findChild<QPushButton*>("copyClipboardEntry");QVERIFY(copy);
        QProcess locker;locker.start(QCoreApplication::applicationFilePath(),{"--hold-clipboard","250"});
        QVERIFY(locker.waitForReadyRead(3000));QCOMPARE(locker.readAllStandardOutput().trimmed(),QByteArray("locked"));
        QTest::mouseClick(copy,Qt::LeftButton);
        QTRY_COMPARE_WITH_TIMEOUT(QGuiApplication::clipboard()->text(),QString("占用重试验证文字"),5000);
        QVERIFY(locker.waitForFinished(3000) || locker.state()==QProcess::NotRunning);
        auto *status=panel.findChild<QLabel*>("clipboardStatus");QVERIFY(status);QCOMPARE(status->text(),QString("已复制，可以粘贴了。"));
#endif
    }
};
int main(int argc,char **argv){
    QApplication app(argc,argv);
#ifdef Q_OS_WIN
    if(app.arguments().contains("--hold-clipboard")){
        if(!OpenClipboard(nullptr))return 3;QFile output;output.open(stdout,QIODevice::WriteOnly);output.write("locked\n");output.flush();
        QTimer::singleShot(app.arguments().last().toInt(),&app,[&]{CloseClipboard();app.quit();});return app.exec();
    }
#endif
    if(app.arguments().contains("--clipboard-digest")){
        QFile output;output.open(stdout,QIODevice::WriteOnly);
        output.write(QCryptographicHash::hash(QGuiApplication::clipboard()->text().toUtf8(),QCryptographicHash::Sha256).toHex());return 0;
    }
    // Preserve every existing clipboard format after the isolated GUI tests.
    auto saved=std::make_unique<QMimeData>();const auto *current=QGuiApplication::clipboard()->mimeData();
    if(current)for(const auto &format:current->formats())saved->setData(format,current->data(format));
    if(app.arguments().contains("--clipboard-demo")){
        QTemporaryDir dir;Companion c(dir.filePath("memory.sqlite"));
        c.clipboard().setEnabled(true);c.clipboard().capture("用于真实窗口验证的普通文字 🐉");c.clipboard().setEnabled(false);
        PetWidget pet(c);pet.setMotionEnabled(false);pet.setWindowFlag(Qt::Tool,false);pet.setWindowFlag(Qt::Window,true);
        pet.setWindowTitle("DragonGPT 剪贴板验证桌宠");
        ClipboardPanel panel(c.clipboard(),pet);panel.setWindowFlag(Qt::Tool,false);panel.setWindowFlag(Qt::Window,true);
        panel.setWindowTitle("DragonGPT 剪贴板验证");
        const auto area=pet.screen()->availableGeometry();pet.move(area.center()-QPoint(pet.width()/2,0));pet.show();panel.toggle();
        QTimer::singleShot(90000,&app,&QApplication::quit);const int result=app.exec();
        QGuiApplication::clipboard()->setMimeData(saved.release());return result;
    }
    ClipboardUiTests test;const int result=QTest::qExec(&test,argc,argv);
    QGuiApplication::clipboard()->setMimeData(saved.release());return result;
}
#include "test_clipboard_ui.moc"
