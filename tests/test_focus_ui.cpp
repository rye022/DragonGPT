#include "companion_window.h"
#include "focus_page.h"
#include <QApplication>
#include <QDir>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTextStream>
#include <QTest>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QJsonDocument>
#include <stdexcept>
using namespace dragon;
namespace {int checks=0;void expect(bool b,const char *m){++checks;if(!b)throw std::runtime_error(m);}}
int main(int argc,char **argv){
    qputenv("QT_MEDIA_BACKEND","windows");QApplication app(argc,argv);QTemporaryDir dir;
    try{
        Companion companion(dir.filePath("memory.sqlite"));auto online=companion.chatSettings();online.onlineEnabled=false;companion.saveChatSettings(online);
        PetWidget pet(companion);CompanionWindow window(companion,pet);window.show();pet.show();
        auto *tabs=window.findChild<QTabWidget*>("companionTabs");expect(tabs && tabs->count()==10 && tabs->tabText(7)=="专注陪伴" && tabs->tabText(8)=="工具" && tabs->tabText(9)=="智能","existing tabs plus focus/tools and appended intelligence");
        tabs->setCurrentIndex(7);auto *page=window.focusPage();expect(page!=nullptr,"focus page present");
        auto *task=page->findChild<QLineEdit*>("focusTask");task->setText("验证学习陪伴");
        auto click=[&](const char *name){auto *button=page->findChild<QPushButton*>(name);expect(button!=nullptr,"focus button exists");button->click();};
        click("focusStart");expect(page->service().focusing() && !task->isEnabled(),"UI starts task");
        click("focusPause");expect(page->service().paused(),"UI pauses");click("focusResume");expect(page->service().focusing(),"UI resumes");
        app.processEvents();QTest::qWait(40);expect(pet.currentAction()==PetAction::Work,"focus uses existing Work action");
        QDir().mkpath("outputs/previews");expect(window.grab().save("outputs/previews/focus-page.png"),"focus preview saved");
        click("focusEnd");expect(!page->service().running(),"UI ends");
        auto s=page->service().settings();s.waterMinutes=1;s.waterTts=false;page->service().setSettings(s);
        ActivitySample active{true,false,true,0,12*3600000,3*3600000,"injected active sample"};
        WellnessTime time{QDateTime::currentDateTimeUtc(),0};for(int i=1;i<=121;++i){time.utc=time.utc.addSecs(1);time.monotonicMs+=1000;page->service().tick(time,active);}
        CompanionNotice notice;expect(page->service().takeNotice(notice) && notice.water,"UI service hydration candidate");pet.say(notice.text);page->service().shown(notice);app.processEvents();
        auto *petConfirm=pet.findChild<QPushButton*>("petWaterAction0");expect(petConfirm && petConfirm->isVisible(),"desktop hydration actions visible");
        expect(pet.grab().save("outputs/previews/water-pet.png"),"hydration desktop preview saved");
        expect(window.grab().save("outputs/previews/water-page.png"),"hydration focus preview saved");
        auto before=companion.memory().exchanges();petConfirm->click();expect(!page->service().waterPending() && companion.memory().exchanges()==before,"desktop confirm clears without reward");
        auto stats=systemActivity();QTextStream(stdout)<<"Native activity: idle="<<stats.idleAvailable<<" lock="<<stats.lockAvailable<<" uptimeMs="<<stats.uptimeMs<<" sessionMs="<<stats.sessionMs<<'\n';
#ifdef Q_OS_WIN
        expect(stats.idleAvailable && stats.uptimeMs>0,"native Windows idle/uptime working");
#endif
        // Exercise the real selected-voice controller against a localhost failure fixture.
        QTcpServer server;expect(server.listen(QHostAddress::LocalHost,0),"voice fixture listen");QByteArray request;bool failAudio=true;
        QObject::connect(&server,&QTcpServer::newConnection,&server,[&]{
            auto *socket=server.nextPendingConnection();auto buffer=std::make_shared<QByteArray>();
            QObject::connect(socket,&QTcpSocket::disconnected,socket,&QObject::deleteLater);
            QObject::connect(socket,&QTcpSocket::readyRead,socket,[&,socket,buffer]{
                *buffer+=socket->readAll();const int head=buffer->indexOf("\r\n\r\n");if(head<0 || socket->property("handled").toBool())return;
                int size=0;for(const auto &h:buffer->left(head).split('\n'))if(h.toLower().startsWith("content-length:"))size=h.mid(15).trimmed().toInt();if(buffer->size()<head+4+size)return;
                socket->setProperty("handled",true);request=*buffer;
                const QByteArray body=failAudio?QByteArray("{\"code\":\"AccessDenied\",\"message\":\"fixture\"}"):
                    QByteArray("data: {\"output\":{\"audio\":{\"data\":\"")+QByteArray(24000,0).toBase64()+"\"},\"finish_reason\":\"stop\"}}\r\n\r\n";
                socket->write(QByteArray("HTTP/1.1 ")+(failAudio?"403 Forbidden":"200 OK")+"\r\nContent-Type: text/event-stream\r\nContent-Length: "+QByteArray::number(body.size())+"\r\nConnection: close\r\n\r\n"+body);socket->disconnectFromHost();
            });
        });
        VoiceController voice(companion,nullptr,QUrl(QString("http://127.0.0.1:%1/").arg(server.serverPort())),100);
        auto prefs=voice.voicePreferences();prefs.voice="Momo";prefs.preferOffline=false;prefs.fastResponse=false;prefs.cacheAudio=false;voice.saveVoicePreferences(prefs,"test-fixture-only");
        QSignalSpy bubbleSpy(&voice,&VoiceController::speechRequested);
        QString noticeId;for(const auto &v:companion.memory().collection()){auto r=v.toObject();if(r["id"].toString().startsWith("water.0.") && !r["discovered"].toBool()){noticeId=r["id"].toString();break;}}
        auto collected=[&](){for(const auto &v:companion.memory().collection())if(v.toObject()["id"]==noticeId)return v.toObject();return QJsonObject{};};
        expect(voice.speakCompanionNotice("喝点水吧。",false,noticeId),"notice uses existing controller");
        expect(!voice.speakCompanionNotice("不得重叠。"),"second speech rejected when busy");
        QElapsedTimer wait;wait.start();while(voice.speechBusy() && wait.elapsed()<3000){app.processEvents();QTest::qWait(1);}
        expect(!voice.speechBusy() && voice.status().contains("原声不可用"),"403 clears busy with honest status");
        expect(request.contains("Momo") && !request.contains("验证学习陪伴"),"selected voice, no task telemetry");expect(bubbleSpy.count()==0,"bubble off honored during TTS");
        expect(!collected()["discovered"].toBool(),"TTS-only failure does not fabricate display");
        failAudio=false;expect(voice.speakCompanionNotice("先喝点水吧。",false,noticeId),"success fixture accepted");
        wait.restart();while(voice.engaged() && wait.elapsed()<20000){app.processEvents();QTest::qWait(1);}
        if(voice.engaged() || !collected()["discovered"].toBool() || collected()["first_heard_at"].toString().isEmpty())QTextStream(stdout)<<"Notice audio diagnostic: "<<voice.status()<<" busy="<<voice.engaged()<<" id="<<noticeId<<" row="<<QJsonDocument(collected()).toJson(QJsonDocument::Compact)<<'\n';
        expect(!voice.engaged() && collected()["discovered"].toBool() && !collected()["first_heard_at"].toString().isEmpty(),"actual audio collects in existing gallery");
        expect(collected()["play_count"].toInt()==1 && companion.memory().life().experience==0,"one discovery, hydration gives no XP");
        QTextStream(stdout)<<"PASS "<<checks<<" focus UI/native/TTS fixture assertions; no provider calls\n";
        return 0;
    }catch(const std::exception &e){QTextStream(stderr)<<"FAIL after "<<checks<<": "<<e.what()<<'\n';return 1;}
}
