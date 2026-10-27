#include "companion_window.h"
#include "qwen_speech.h"
#include "clipboard_panel.h"
#include "focus_page.h"
#include <QApplication>
#include <QComboBox>
#include <QPushButton>
#include <QCheckBox>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QSpinBox>
#include <QSet>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QMouseEvent>
#include <QMediaDevices>
#include <QScreen>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTabWidget>
#include <QTest>
#include <QTextStream>
#include <functional>
#include <stdexcept>
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
namespace {
QStringList allDialogueTexts(){QStringList lines;for(const auto &v:DialogueEngine::catalog())lines.append(v.toObject()["text"].toString());return lines;}

int assertions=0;
void expect(bool condition,const char *reason){++assertions;if(!condition)throw std::runtime_error(reason);}
bool until(const std::function<bool()> &condition,int ms=2500){QElapsedTimer clock;clock.start();while(!condition() && clock.elapsed()<ms){QCoreApplication::processEvents();QTest::qWait(1);}return condition();}
QByteArray audio(const QByteArray &pcm,bool done=true){
    const QJsonObject output{{"audio",QJsonObject{{"data",QString::fromLatin1(pcm.toBase64())}}}};
    QByteArray body="data: "+QJsonDocument(QJsonObject{{"output",output}}).toJson(QJsonDocument::Compact)+"\r\n\r\n";
    if(done)body+="data: {\"output\":{\"finish_reason\":\"stop\",\"audio\":{\"data\":\"\"}}}\r\n\r\n";
    return body;
}
struct Server {
    QTcpServer server;
    QByteArray body=audio(QByteArray(800,0)),lastRequest;
    int code=200,delay=0,tailDelay=5,requests=0;
    Server(){
        expect(server.listen(QHostAddress::LocalHost,0),"Cannot start Qwen fixture");
        QObject::connect(&server,&QTcpServer::newConnection,&server,[this]{
            while(server.hasPendingConnections()){
                auto *socket=server.nextPendingConnection();auto data=std::make_shared<QByteArray>();
                QObject::connect(socket,&QTcpSocket::disconnected,socket,&QObject::deleteLater);
                QObject::connect(socket,&QTcpSocket::readyRead,socket,[this,socket,data]{
                    *data+=socket->readAll();if(socket->property("done").toBool())return;
                    const auto boundary=data->indexOf("\r\n\r\n");if(boundary<0)return;
                    int bytes=0;for(const auto &line:data->left(boundary).split('\n'))if(line.toLower().startsWith("content-length:"))bytes=line.mid(15).trimmed().toInt();
                    if(data->size()<boundary+4+bytes)return;
                    socket->setProperty("done",true);lastRequest=*data;++requests;
                    QByteArray response="HTTP/1.1 "+QByteArray::number(code)+" Test\r\nContent-Type: text/event-stream\r\nConnection: close\r\n";
                    if(code==302)response+="Location: http://127.0.0.1:9/leak\r\n";
                    response+="Content-Length: "+QByteArray::number(body.size())+"\r\n\r\n"+body;
                    QTimer::singleShot(delay,socket,[this,socket,response]{
                        if(socket->state()!=QAbstractSocket::ConnectedState)return;
                        // Deliberately split a CRLF boundary in the stream.
                        const auto bodyStart=response.indexOf("\r\n\r\n")+4;
                        const auto firstPacket=response.indexOf("\r\n\r\n",bodyStart);
                        const auto split=tailDelay>5 && firstPacket>0?firstPacket+4:response.indexOf("data:")+7;
                        socket->write(response.left(split));socket->flush();
                        QTimer::singleShot(tailDelay,socket,[socket,response,split]{if(socket->state()==QAbstractSocket::ConnectedState){socket->write(response.mid(split));socket->disconnectFromHost();}});
                    });
                });
            }
        });
    }
    QUrl url()const{return QUrl(QString("http://127.0.0.1:%1/tts").arg(server.serverPort()));}
};
void voices(const QString &directory){
    const auto file=directory+"/voice.ini";const auto all=voiceChoices();expect(all.size()==6,"Five requested voices plus offline choice missing");
    VoicePreferences preferences;
    for(const auto &choice:all){preferences.voice=choice.id;preferences.systemProxy=true;preferences.save(file);const auto restored=VoicePreferences::load(file);expect(restored.voice==choice.id && restored.systemProxy,"Voice selection lost on restart");}
    Server server;QwenSpeech client(nullptr,server.url());QSignalSpy ready(&client,&QwenSpeech::ready),failed(&client,&QwenSpeech::failed);
    expect(!client.start("你好",preferences,{}).isEmpty(),"Missing cloud key accepted");
    preferences.systemProxy=false;
    int completed=0;
    for(const auto &choice:all){
        if(choice.id=="System")continue;
        preferences.voice=choice.id;
        expect(client.start("你好，霜翎",preferences,"fixture-secret").isEmpty(),"Qwen voice rejected");
        expect(until([&]{return ready.size()==completed+1 || !failed.empty();}),"Qwen synthesis timed out");
        expect(failed.empty() && ready.size()==++completed,"Qwen PCM completion failed");
        expect(ready.last().first().toByteArray()==QByteArray(800,0),"Qwen PCM changed");
        const auto body=server.lastRequest.mid(server.lastRequest.indexOf("\r\n\r\n")+4);
        const auto json=QJsonDocument::fromJson(body).object();
        expect(json.value("input").toObject().value("voice").toString()==choice.id && json.value("model")=="qwen3-tts-flash","Wrong requested voice/model");
        const auto headers=server.lastRequest.left(server.lastRequest.indexOf("\r\n\r\n")).toLower();
        expect(headers.contains("x-dashscope-sse: enable") && headers.contains("authorization: bearer fixture-secret"),"Qwen headers incorrect");
    }
    expect(!client.start(QString(601,QChar(u'中')),preferences,"fixture-secret").isEmpty(),"Oversized TTS accepted");
    const auto start=server.requests;server.delay=80;
    expect(client.start("取消测试",preferences,"fixture-secret").isEmpty(),"Cancel fixture failed");
    expect(until([&]{return server.requests>start;}),"Cancelled request not received");client.cancel();QTest::qWait(120);
    expect(ready.size()==completed && failed.empty() && !client.busy(),"Cancelled speech leaked");
    server.delay=0;
    for(const int code:{401,302}){
        server.code=code;const auto count=failed.size();const auto requests=server.requests;
        expect(client.start("错误测试",preferences,"fixture-secret").isEmpty(),"HTTP fixture failed");
        expect(until([&]{return failed.size()==count+1;}),"Qwen HTTP error unreported");QTest::qWait(20);
        expect(ready.size()==completed && server.requests==requests+1,"Qwen redirect/retry/audio leak");
    }
    server.code=200;
    for(const auto &body:{QByteArray("data: {broken}\n\n"),audio(QByteArray(800,0),false),
                         QByteArray("data: {\"output\":{\"audio\":{\"data\":\"invalid!\"}}}\n\n"),
                         QByteArray("data: {\"code\":\"InvalidApiKey\"}\n\n")}){
        server.body=body;const auto count=failed.size();
        expect(client.start("坏流测试",preferences,"fixture-secret").isEmpty(),"Bad stream fixture failed");
        expect(until([&]{return failed.size()==count+1;}),"Qwen invalid stream not rejected");
        expect(ready.size()==completed,"Incomplete audio leaked");
    }
}
void playback(){
    SpeechPlayer player;QSignalSpy finished(&player,&SpeechPlayer::finished),failed(&player,&SpeechPlayer::failed);
    expect(!player.play(QByteArray(1,0)).isEmpty(),"Odd PCM accepted by playback");
    const auto error=player.play(QByteArray(4800,0)); // 100 ms of silence, output only.
    if(QMediaDevices::defaultAudioOutput().isNull()){expect(!error.isEmpty(),"Missing speaker was ignored");return;}
    expect(error.isEmpty(),"Silent playback failed to start");
    expect(until([&]{return !finished.empty() || !failed.empty();}),"Silent playback did not finish");
    expect(finished.size()==1 && failed.empty() && !player.active(),"Playback completion failed");
    expect(player.play(QByteArray(48000,0)).isEmpty(),"Playback restart failed");player.stop();QTest::qWait(50);
    expect(finished.size()==1 && !player.active(),"Stopped playback emitted completion");
}
void cache(const QString &directory){
    VoiceCache cache(directory+"/voice-cache");
    const QByteArray pcm(4800,0);
    cache.write("Momo","你好",pcm);
    expect(cache.read("Momo","你好")==pcm,"Cached voice not replayable offline");
    expect(cache.read("Serena","你好").isEmpty(),"Cached voice crossed speaker identity");
    expect(cache.read("Momo","新句子").isEmpty(),"Cached phrase reused for different text");
    cache.write("Bella","invalid",QByteArray(1,0));
    expect(cache.read("Bella","invalid").isEmpty(),"Invalid PCM cached");
    cache.write("offline:Serena","你好",QByteArray(4800,1));
    expect(cache.read("offline:Serena","你好")!=cache.read("Momo","你好"),"Offline model and cloud voices share cache key");
    QFile userFile(directory+"/voice-cache/user-note.txt");expect(userFile.open(QIODevice::WriteOnly),"Cache fixture failed");userFile.write("keep");userFile.close();
    cache.clear();expect(cache.read("Momo","你好").isEmpty() && QFile::exists(userFile.fileName()),"Cache clear removed unrelated files or kept audio");
    OfflineSpeech unavailable(nullptr,directory+"/not-installed");
    expect(!unavailable.available() && !unavailable.start("测试").isEmpty(),"Unprepared offline engine started");
}
void typedSpeech(const QString &directory){
    const auto path=directory+"/typed";QDir().mkpath(path);
    VoicePreferences preferences;preferences.voice="Momo";preferences.save(path+"/voice.ini");
    Companion companion(path+"/memory.sqlite");
    VoiceCache cache(path+"/voice-cache");
    const QString reply="这是完整的文字回复。第二句也要读出来。";
    const QByteArray silence(4800,0);cache.write("Momo",reply,silence);
    for(const auto &line:allDialogueTexts())cache.write("Momo",line,silence);
    QString longReply(4200,QChar(u'中'));longReply+=QString::fromUtf8("🙂结尾");
    const auto points=longReply.toStdU32String();
    for(size_t i=0;i<points.size();i+=600)cache.write("Momo",QString::fromUcs4(points.data()+i,qMin(size_t(600),points.size()-i)),silence);
    {
        VoiceController voice(companion);QSignalSpy spoken(&voice,&VoiceController::speechRequested);
        auto *player=voice.findChild<SpeechPlayer*>();expect(player,"Output player missing");
        QSignalSpy finished(player,&SpeechPlayer::finished);
        expect(!voice.readAloud(),"New profiles unexpectedly speak typed replies");
        companion.replyReady(reply,"fixture");expect(spoken.empty(),"Disabled typed reply was spoken");
        voice.setReadAloud(true);expect(VoicePreferences::load(path+"/voice.ini").readAloud,"Typed speech switch was not persisted");
        expect(voice.voicePreferences().voice=="Momo","Typed speech switch reset voice selection");
        companion.turnStarted();
        companion.replyReady(reply,"fixture");
        expect(spoken.size()==1 && spoken.last().first().toString()==reply,"Spoken text differed from the complete reply");
        expect(!voice.active() && companion.voice().state()==VoiceSession::State::Idle,"Typed speech activated microphone session");
        voice.setReadAloud(false);QTest::qWait(150);
        expect(!player->active() && finished.empty(),"Turning off switch did not stop current typed speech");
        companion.replyReady(reply,"fixture");expect(spoken.size()==1,"Text-only mode leaked speech");
        voice.setReadAloud(true);companion.turnStarted();companion.replyReady(longReply,"fixture");
        expect(spoken.size()==2 && spoken.last().first().toString()==longReply,"Long text or Unicode was changed");
        expect(until([&]{return finished.size()==8;},6000),"Long reply was truncated or segments were skipped");
        expect(voice.status()=="朗读完成 · 麦克风未开启","Long reply did not finish normally");
        voice.setReadAloud(false);
        voice.speakRandomLine();expect(spoken.size()==3,"Click speech incorrectly depended on typed speech switch");
        const auto first=spoken.last().first().toString();
        expect(allDialogueTexts().contains(first),"Selected click phrase unknown");
        voice.speakRandomLine();expect(spoken.size()==3,"Rapid click bypassed single-utterance cooldown");
        expect(until([&]{return finished.size()==9;}),"Selected click phrase did not finish");
        QTest::qWait(1150);voice.speakRandomLine();expect(spoken.size()==4 && spoken.last().first().toString()!=first,"New eligible click repeated the last phrase");
        expect(!voice.active() && companion.voice().state()==VoiceSession::State::Idle,"Click speech activated microphone session");
        voice.endCall();voice.setReadAloud(true);
    }
    VoiceController restored(companion);expect(restored.readAloud(),"Typed speech switch lost after restart");
    restored.setReadAloud(false);
}
void streaming(const QString &directory){
    const auto path=directory+"/streaming";QDir().mkpath(path);
    VoicePreferences p;p.voice="Momo";p.preferOffline=false;p.cacheAudio=false;p.readAloud=true;p.save(path+"/voice.ini");
    CredentialStore key(path+"/dashscope-key.dpapi");key.save("fixture-only-key");
    Companion companion(path+"/memory.sqlite");Server server;server.tailDelay=900;
    server.body=audio(QByteArray(4800,0),false)+audio(QByteArray(4800,0));
    VoiceController voice(companion,nullptr,server.url());
    auto *qwen=voice.findChild<QwenSpeech*>();auto *player=voice.findChild<SpeechPlayer*>();
    expect(qwen && player,"Streaming components missing");
    QSignalSpy ready(qwen,&QwenSpeech::ready),begun(&voice,&VoiceController::outputStarted),finished(player,&SpeechPlayer::finished);
    QElapsedTimer clock;clock.start();companion.replyReady("流式测试。","fixture");
    expect(until([&]{return !begun.empty();},600),"First audio waited for complete response");
    expect(clock.elapsed()<700 && begun.last().first().toString().contains("百炼"),"Streaming audio did not start promptly");
    expect(ready.empty() && finished.empty(),"Player ended before network stream completed");
    QTest::qWait(200);expect(finished.empty(),"Audio underrun incorrectly completed the stream");
    expect(until([&]{return finished.size()==1;},2200),"Late chunk did not resume/finish streaming playback");
    expect(ready.size()==1 && begun.size()==1,"Streaming completion duplicated playback");
    server.delay=500;server.tailDelay=5;
    VoiceController slow(companion,nullptr,server.url(),80);QSignalSpy fallback(&slow,&VoiceController::outputStarted);
    // Keep the first controller from also consuming this synthetic final reply.
    voice.setReadAloud(false);slow.setReadAloud(true);
    clock.restart();companion.replyReady("测试。","fixture");
    const bool fellBack=until([&]{return !fallback.empty();},2000);
    QTextStream(stdout)<<"Fallback state: "<<slow.status()<<"; elapsed "<<clock.elapsed()<<" ms\n";
    expect(fellBack,"Slow synthesis left the reply silent");
    expect(clock.elapsed()<2100 && fallback.last().first().toString().contains("系统中文"),"Slow voice did not use bounded fallback");
    slow.endCall();QTest::qWait(600);
    expect(fallback.size()==1,"Cancelled slow cloud response later spoke stale audio");
    expect(!slow.active(),"Streaming/fallback opened microphone");
    QTextStream(stdout)<<"Streaming fixture first audio <700 ms; native SAPI fallback <2100 ms.\n";
}
void textPipeline(const QString &directory){
    const auto path=directory+"/text-pipeline";QDir().mkpath(path);
    VoicePreferences preferences;preferences.voice="Momo";preferences.cacheAudio=true;preferences.readAloud=true;preferences.save(path+"/voice.ini");
    VoiceCache cache(path+"/voice-cache");cache.write("Momo","第一句话。",QByteArray(4800,0));cache.write("Momo","第二句话。",QByteArray(4800,0));
    Companion companion(path+"/memory.sqlite");VoiceController voice(companion);
    QSignalSpy spoken(&voice,&VoiceController::speechRequested);
    auto *player=voice.findChild<SpeechPlayer*>();QSignalSpy finished(player,&SpeechPlayer::finished);
    companion.turnStarted();companion.partialReply("第一句话。第");
    expect(spoken.size()==1 && spoken.first().first().toString()=="第一句话。","Complete streaming sentence not queued");
    companion.partialReply("第一句话。第二句话。");companion.replyReady("第一句话。第二句话。","fixture");
    expect(spoken.size()==2 && spoken.last().first().toString()=="第二句话。","Final streaming reply duplicated or lost text");
    expect(until([&]{return finished.size()==1;}),"Text pipeline did not finish its queued cached audio");
    expect(!voice.active() && !player->active(),"Text pipeline opened microphone or left output active");
    companion.turnStarted();companion.partialReply("第一句话。");voice.setReadAloud(false);
    const auto count=spoken.size();companion.partialReply("第一句话。第二句话。");companion.replyReady("第一句话。第二句话。","fixture");
    expect(spoken.size()==count && !player->active(),"Turning off streamed speech did not cancel remaining output");
}
void bundled(){
    const auto lines=VoiceController::interactionLines();expect(lines.size()==12,"Bundled phrase count changed");
    for(const auto &line:lines){const auto pcm=VoiceController::interactionAudio(line);
        expect(pcm.size()>24000 && pcm.size()<12*48000 && pcm.size()%2==0,"Bundled voice is missing/truncated");
        expect(pcmEnergy(pcm.mid(pcm.size()/4/2*2,qMin(24000,pcm.size()/2)))>.001,"Bundled click voice is silent");
    }
    QSet<QString> all;
    for(int i=1;i<PetActions::Count;++i){const auto action=static_cast<PetAction>(i);const auto group=VoiceController::actionLines(action);
        expect(group.size()==3,"Each pose needs three dedicated phrases");
        for(const auto &line:group){expect(!line.isEmpty() && !all.contains(line),"Action phrases are empty or shared across poses");all.insert(line);
            const auto pcm=VoiceController::interactionAudio(line);expect(pcm.size()>24000 && pcm.size()<15*48000 && pcm.size()%2==0,"Action voice clip missing");
            expect(pcmEnergy(pcm.mid(pcm.size()/4/2*2,qMin(24000,pcm.size()/2)))>.001,"Action clip is silent");
            if(action==PetAction::Work)expect(!line.contains("休息") && !line.contains("睡") && !line.contains("枕头"),"Work phrase contradicts displayed activity");
        }
    }
}
void interaction(const QString &directory){
    Companion companion(directory+"/ui.sqlite");PetWidget pet(companion);CompanionWindow window(companion,pet);
    QObject::connect(&pet,&PetWidget::openCompanion,&window,&CompanionWindow::openConversation);
    QSignalSpy opened(&pet,&PetWidget::openCompanion);
    auto *tabs=window.findChild<QTabWidget*>("companionTabs");expect(tabs,"Tabs missing");
    auto *choice=window.findChild<QComboBox*>("voiceChoice");expect(choice && choice->count()==10,"Supported Audio TTS voices and two offline choices missing");
    for(const auto &retired:{"Momo","Chelsie","Bella","Katerina"})expect(choice->findData(retired)<0,"Retired 403 voice remains selectable");
    choice->setCurrentIndex(choice->findData("Serena"));expect(VoicePreferences::load(directory+"/voice.ini").voice=="Serena","UI voice selection not saved");
    auto *typed=window.findChild<QCheckBox*>("typedReplyVoice"),*voiceRead=window.findChild<QCheckBox*>("voicePageReadAloud");
    expect(typed && voiceRead,"Typed speech switches missing");typed->setChecked(true);
    auto *voiceStatus=window.findChild<QLabel*>("chatVoiceStatus");
    expect(voiceStatus && voiceStatus->text().contains("苏瑶"),"Chat does not display selected speech voice");
    window.voiceController().changed();expect(voiceStatus->text().contains(window.voiceController().status()),"Chat speech error/status is hidden");
    expect(voiceRead->isChecked() && VoicePreferences::load(directory+"/voice.ini").readAloud,"Chat switch did not sync/save");
    voiceRead->setChecked(false);expect(!typed->isChecked() && !VoicePreferences::load(directory+"/voice.ini").readAloud,"Voice switch did not sync/save");
    auto *family=window.findChild<QComboBox*>("ttsApiFamily");auto *workspace=window.findChild<QLineEdit*>("ttsWorkspace");auto *audioUrl=window.findChild<QLineEdit*>("audioTtsUrl");
    expect(family && workspace && audioUrl,"Audio TTS configuration controls missing");
    family->setCurrentIndex(family->findData("qwen-audio-tts"));workspace->setText("llm-fixture");
    expect(choice->isEnabled() && choice->currentData()=="audio:longanhuan_v3.1" && audioUrl->isReadOnly() && audioUrl->text().contains("llm-fixture.cn-beijing.maas.aliyuncs.com/api/v1/services/audio/tts/SpeechSynthesizer"),"Audio TTS endpoint or accessible voice selector missing");
    family->setCurrentIndex(family->findData("qwen-tts"));expect(choice->isEnabled(),"Legacy voice selector did not restore");
    // Start with a saved Audio TTS profile, as on the user's installed app.
    auto audio=window.voiceController().voicePreferences();audio.apiFamily="qwen-audio-tts";audio.audioTransport="websocket-public";audio.audioWorkspace="llm-fixture";
    audio.audioVoice="longanhuan_v3.1";audio.fastResponse=false;window.voiceController().saveVoicePreferences(audio,{});
    // Reopening restores the active Audio TTS preset and only the usable legacy choices.
    {
        CompanionWindow reopened(companion,pet);auto *restored=reopened.findChild<QComboBox*>("voiceChoice");
        expect(restored && restored->isEnabled() && restored->currentData()=="audio:longanhuan_v3.1","Audio TTS startup hides original voice choices or shows an inactive voice");
        for(const auto &original:selectableLegacyVoices()){
            restored->setCurrentIndex(restored->findData(original.id));auto saved=VoicePreferences::load(directory+"/voice.ini");
            expect(saved.legacyVoice(original.id),"Choosing an original voice did not restore its protocol and ID");
            expect(saved.audioTransport==audio.audioTransport && saved.audioWorkspace==audio.audioWorkspace && saved.audioModel==audio.audioModel && saved.audioVoice==audio.audioVoice && !saved.fastResponse,"Original voice selection changed saved Audio TTS or fallback settings");
            restored->setCurrentIndex(restored->findData("audio:longanhuan_v3.1"));saved=VoicePreferences::load(directory+"/voice.ini");
            expect(saved.audioTts() && saved.voice==original.id && saved.audioVoice==audio.audioVoice,"Returning to Audio TTS lost the original voice or native Audio TTS ID");
            expect(reopened.voiceController().voiceLabel().contains("龙安欢"),"Current voice label did not follow Audio TTS selection");
        }
        for(const auto &preset:audioVoiceChoices(audio.audioModel,audio.region)){
            restored->setCurrentIndex(restored->findData("audio:"+preset.id));const auto saved=VoicePreferences::load(directory+"/voice.ini");
            expect(saved.audioTts() && saved.audioVoice==preset.id && saved.voice=="Serena","Preset selector did not save native Audio TTS voice");
            expect(saved.audioModel==audio.audioModel && saved.audioTransport==audio.audioTransport && saved.audioWorkspace==audio.audioWorkspace && !saved.fastResponse,"Preset selector changed model, route or fallback settings");
            auto *field=reopened.findChild<QLineEdit*>("audioTtsVoice");expect(field && field->text()==preset.id,"Advanced voice field is stale after preset switch");
            expect(reopened.voiceController().voiceLabel().contains(preset.name),"Friendly voice label did not follow preset");
        }
        restored->setCurrentIndex(restored->findData("audio:longanhuan_v3.1"));
        auto *save=reopened.findChild<QPushButton*>("saveVoice");expect(save,"Voice configuration save button missing");save->click();
        const auto saved=VoicePreferences::load(directory+"/voice.ini");
        expect(saved.audioTts() && saved.voice=="Serena" && saved.audioVoice==audio.audioVoice,"Saving active Audio TTS replaced the original voice ID with the selector's protocol marker");
    }
    QSignalSpy clicked(&pet,&PetWidget::clicked);
    pet.setMotionEnabled(false);pet.show();window.show();QTest::qWait(50);
    tabs->setCurrentIndex(2);window.showMinimized();QTest::qWait(30);
    QTest::mouseDClick(&pet,Qt::LeftButton,Qt::NoModifier,QPoint(pet.width()/2,pet.height()/2));QCoreApplication::processEvents();
    expect(opened.size()==1 && window.isVisible() && !window.isMinimized(),"Double click failed to restore minimized chat");
    expect(tabs->currentIndex()==0,"Double click did not select chat");
    QTest::mouseRelease(&pet,Qt::LeftButton);
    QTest::qWait(QApplication::doubleClickInterval()+50);expect(clicked.empty(),"Native double click also triggered random speech");
    expect(opened.size()==1,"Double-click release opened chat twice");
    window.hide();QTest::mouseClick(&pet,Qt::LeftButton,Qt::NoModifier,QPoint(200,200));QTest::mouseClick(&pet,Qt::LeftButton,Qt::NoModifier,QPoint(200,200));QCoreApplication::processEvents();
    expect(opened.size()==2 && window.isVisible(),"Two ordinary clicks failed to open hidden chat");
    QTest::qWait(QApplication::doubleClickInterval()+50);expect(clicked.empty(),"Fallback double click triggered random speech");
    window.hide();pet.move(10,10);QTest::mousePress(&pet,Qt::LeftButton,Qt::NoModifier,QPoint(100,100));
    const auto global=pet.mapToGlobal(QPoint(160,160));
    QMouseEvent move(QEvent::MouseMove,QPointF(160,160),QPointF(global),Qt::NoButton,Qt::LeftButton,Qt::NoModifier);QCoreApplication::sendEvent(&pet,&move);
    QTest::mouseRelease(&pet,Qt::LeftButton,Qt::NoModifier,QPoint(160,160));
    expect(opened.size()==2 && !window.isVisible(),"Dragging incorrectly opened chat");
    QTest::qWait(QApplication::doubleClickInterval()+50);expect(clicked.empty(),"Dragging triggered random speech");
    window.move(-20000,-20000);QTest::mouseDClick(&pet,Qt::LeftButton,Qt::NoModifier,QPoint(200,200));QCoreApplication::processEvents();
    bool onScreen=false;for(const auto *screen:QGuiApplication::screens())if(screen->availableGeometry().contains(window.frameGeometry().center()))onScreen=true;
    expect(onScreen && !window.isMinimized(),"Chat was restored off-screen");
    QTest::mouseRelease(&pet,Qt::LeftButton);QTest::qWait(QApplication::doubleClickInterval()+50);
    VoiceCache clickCache(directory+"/voice-cache");for(const auto &line:allDialogueTexts())clickCache.write("Momo",line,QByteArray(4800,0));
    QSignalSpy spoken(&window.voiceController(),&VoiceController::speechRequested);
    window.hide();QTest::mouseClick(&pet,Qt::LeftButton,Qt::NoModifier,QPoint(200,200));
    expect(clicked.empty() && spoken.empty(),"Single click did not wait for double-click classification");
    expect(until([&]{return clicked.size()==1 && spoken.size()==1;},QApplication::doubleClickInterval()+1000),"Single click did not speak");
    expect(!window.isVisible() && !window.voiceController().active(),"Single click opened chat or microphone");
    window.voiceController().endCall();
    QTest::mouseClick(&pet,Qt::LeftButton,Qt::NoModifier,QPoint(200,200));
    QTest::mouseClick(&pet,Qt::RightButton,Qt::NoModifier,QPoint(200,200));QTest::qWait(QApplication::doubleClickInterval()+50);
    expect(clicked.size()==1,"Right click failed to cancel pending single-click speech");
#ifdef Q_OS_WIN
    // Reproduce a hidden-launcher window: Qt says visible, Windows says hidden.
    window.openConversation();QTest::qWait(20);
    ShowWindow(reinterpret_cast<HWND>(window.winId()),SW_HIDE);
    expect(window.isVisible() && !IsWindowVisible(reinterpret_cast<HWND>(window.winId())),"Native-hidden regression fixture invalid");
    const auto hiddenOpenCount=opened.size(),hiddenClickCount=clicked.size();
    QTest::mouseDClick(&pet,Qt::LeftButton,Qt::NoModifier,QPoint(200,200));QCoreApplication::processEvents();
    expect(opened.size()==hiddenOpenCount+1 && IsWindowVisible(reinterpret_cast<HWND>(window.winId())),"Double click left native chat hidden after hidden launch");
    QTest::mouseRelease(&pet,Qt::LeftButton);QTest::qWait(QApplication::doubleClickInterval()+50);
    expect(clicked.size()==hiddenClickCount,"Hidden-chat double click emitted single-click speech");
#endif
    window.hide();pet.hide();
}
void actions(const QString &directory){
    Companion companion(directory+"/actions.sqlite");PetWidget pet(companion);
    expect(pet.actionAtlasReady(),"Six action poses were not packaged");
    pet.setAutomaticRest(false);pet.show();
    for(int i=1;i<PetActions::Count;++i){
        const auto action=static_cast<PetAction>(i);pet.previewAction(action,1800);QTest::qWait(560);
        expect(pet.currentAction()==action,"Preview was overwritten by automatic state");
        expect(!pet.grab().isNull(),"Action did not render");
    }
    pet.setMotionEnabled(false);expect(pet.currentAction()==PetAction::Idle,"Disabled motion failed to restore master");
    pet.setMotionEnabled(true);pet.resumeAutomaticActions();
    companion.emotions().request(Emotion::Worried,.7,4000,10);QTest::qWait(35);
    expect(until([&]{return pet.currentAction()==PetAction::Comfort;}),"Concern did not trigger comfort pose");
    companion.emotions().reset();companion.emotions().request(Emotion::Sleepy,.7,4000,10);QTest::qWait(35);
    expect(until([&]{return pet.currentAction()==PetAction::Sleep;}),"Goodnight did not trigger sleep pose");
    companion.voice().begin();QTest::qWait(35);
    expect(until([&]{return pet.currentAction()==PetAction::Idle;}),"Listening failed to wake sleepy pose");
    companion.voice().stop();pet.previewAction(PetAction::Sleep);pet.setMouthOpen(.7);QTest::qWait(35);
    expect(until([&]{return pet.currentAction()==PetAction::Idle;}),"Speech did not wake manual sleep");
    pet.hide();
}
void actionClicks(const QString &directory){
    const auto path=directory+"/action-clicks";QDir().mkpath(path);
    VoicePreferences preferences;preferences.voice="Momo";preferences.cacheAudio=true;preferences.save(path+"/voice.ini");
    VoiceCache cache(path+"/voice-cache");
    for(const auto &line:allDialogueTexts())cache.write("Momo",line,QByteArray(12000,0));
    Companion companion(path+"/memory.sqlite");PetWidget pet(companion);CompanionWindow window(companion,pet);pet.show();
    auto &voice=window.voiceController();QSignalSpy spoken(&voice,&VoiceController::speechRequested),captured(&pet,&PetWidget::actionClicked),finished(&voice,&VoiceController::interactionFinished);
    for(int i=1;i<PetActions::Count;++i){
        const auto action=static_cast<PetAction>(i);pet.previewAction(action,4000);QTest::qWait(580);
        const auto before=spoken.size();QTest::mouseClick(&pet,Qt::LeftButton,Qt::NoModifier,QPoint(200,250));
        expect(until([&]{return spoken.size()==before+1;}),"Action click produced no speech");
        expect(captured.last().first().value<PetAction>()==action,"Click selected another action");
        expect(allDialogueTexts().contains(spoken.last().first().toString()),"Click phrase not from displayed pose");
        expect(pet.actionInteractionActive() && pet.currentAction()==action,"Speaking replaced clicked pose");
        pet.setMouthOpen(.8);QTest::qWait(50);expect(pet.currentAction()==action,"Mouth animation overrode action context");
        expect(until([&]{return !pet.actionInteractionActive();}),"Completed speech left action frozen");
    }
    // A transition has started, but the screen still mostly shows the old work pose.
    pet.previewAction(PetAction::Work);QTest::qWait(580);pet.previewAction(PetAction::Sleep);
    const auto before=spoken.size();QTest::mouseClick(&pet,Qt::LeftButton,Qt::NoModifier,QPoint(200,250));
    expect(until([&]{return spoken.size()==before+1;}),"Transition click missing");
    expect(captured.last().first().value<PetAction>()==PetAction::Work && allDialogueTexts().contains(spoken.last().first().toString()),"Transition click used future sleep instead of visible work");
    voice.endCall();expect(!pet.actionInteractionActive(),"Cancel failed to release pose");
    // One accepted click, then all immediate duplicates are suppressed.
    QTest::qWait(1150);VoiceController fresh(companion);QSignalSpy freshSpoken(&fresh,&VoiceController::speechRequested);
    QObject::connect(&fresh,&VoiceController::interactionStarted,&pet,&PetWidget::beginActionInteraction);
    QObject::connect(&fresh,&VoiceController::interactionFinished,&pet,&PetWidget::finishActionInteraction);
    fresh.speakActionLine(PetAction::Work);expect(freshSpoken.size()==1,"Eligible click missing");
    for(int i=0;i<20;++i)fresh.speakActionLine(PetAction::Work);
    expect(freshSpoken.size()==1 && pet.currentAction()==PetAction::Work,"Burst clicks overlapped speech or lost pose");
    fresh.endCall();expect(!pet.actionInteractionActive() && !fresh.active(),"End output left pose or microphone active");
    pet.hide();
}
void lifeUi(const QString &directory){
    Companion c(directory+"/life-ui/memory.sqlite");PetWidget pet(c);CompanionWindow window(c,pet);
    auto *tabs=window.findChild<QTabWidget*>("companionTabs");expect(tabs && tabs->tabText(5)=="陪伴","Companionship page missing");
    auto *enabled=window.findChild<QCheckBox*>("proactiveEnabled");auto *frequency=window.findChild<QComboBox*>("proactiveFrequency");
    auto *focus=window.findChild<QCheckBox*>("proactiveFocusMode");
    expect(enabled && frequency && focus,"Companionship settings missing");enabled->setChecked(false);frequency->setCurrentIndex(frequency->findData("quiet"));focus->setChecked(true);
    window.findChild<QPushButton*>("saveLifeSettings")->click();
    expect(!c.lifeSettings().enabled && c.lifeSettings().frequency=="quiet" && c.lifeSettings().focusMode,"Companionship settings not wired to storage");
    window.findChild<QPushButton*>("resumeProactive")->click();expect(!c.lifeSettings().focusMode && !focus->isChecked(),"Resume left focus mode blocking dialogue");
    window.findChild<QPushButton*>("quietToday")->click();expect(c.lifeSettings().pausedUntil>QDateTime::currentDateTimeUtc(),"Quiet today did not persist pause");
    window.findChild<QPushButton*>("resumeProactive")->click();expect(c.lifeSettings().enabled && !c.lifeSettings().pausedUntil.isValid(),"Resume did not clear pause");
    QSignalSpy speech(&window.voiceController(),&VoiceController::speechRequested);
    const auto count=c.memory().exchanges();const auto xp=c.memory().life().experience;window.findChild<QPushButton*>("previewProactive")->click();
    expect(speech.count()==1 && c.memory().exchanges()==count && c.memory().life().experience==xp,"Preview must request current-voice audio without chat or growth");
    window.voiceController().endCall();
    c.respond("我喜欢十九世纪文学");auto *suggestions=window.findChild<QListWidget*>("memorySuggestions");
    expect(suggestions && suggestions->count()==1,"Memory suggestion not visible");
    window.findChild<QPushButton*>("acceptMemorySuggestion")->click();
    expect(suggestions->count()==0 && c.memory().memories().size()==1,"Confirm UI did not store memory");
    c.respond("我喜欢星空");window.findChild<QPushButton*>("dismissMemorySuggestion")->click();
    expect(suggestions->count()==0 && c.memory().memories().size()==1,"Dismiss UI stored rejected memory");
}
void integratedToolsUi(const QString &directory){
    Companion c(directory+"/tools-ui/memory.sqlite");PetWidget pet(c);CompanionWindow window(c,pet);pet.setRandomActionsEnabled(false);pet.setAutomaticRest(false);window.resize(560,760);window.show();pet.show();
    auto *tabs=window.findChild<QTabWidget*>("companionTabs");expect(tabs && tabs->tabText(8)=="工具","Integrated tools page missing");
    auto *sections=window.findChild<QTabWidget*>("toolSections");expect(sections && sections->count()==4,"Tool sections missing");
    expect(window.findChild<QWidget*>("healthTrend") && window.findChild<QWidget*>("dailySummary") && window.findChild<QWidget*>("filePreview"),"Health, summary or file controls missing");
    window.sendText("进入认真工作状态");expect(until([&]{return pet.currentAction()==PetAction::Work;}),"Local work action did not use original controller");
    window.sendText("招招手");expect(until([&]{return pet.currentAction()==PetAction::Wave;}),"Local wave action missing");
    window.sendText("我今天不想工作了");expect(until([&]{return !c.busy() && pet.currentAction()!=PetAction::Work;}),"Emotional statement triggered work action");
    window.sendText("开始番茄钟");expect(window.focusPage()->service().focusing(),"Local command did not reuse FocusService");
    window.sendText("暂停番茄钟");expect(window.focusPage()->service().paused(),"Local pause did not reuse FocusService");
    window.focusPage()->service().end();
    window.sendText("添加待办：写一段测试");auto *todos=window.findChild<QListWidget*>("todoList");expect(todos && todos->count()==1,"Local task did not refresh UI");
    window.sendText("打开电脑状态页面");expect(tabs->currentIndex()==8 && sections->currentIndex()==1,"Health command did not open existing tools page");
    c.remember("学习并发编程");c.remember("喜欢草莓");auto *search=window.findChild<QLineEdit*>("memorySearch");expect(search,"Memory search control missing");search->setText("并发");auto *memories=window.findChild<QListWidget*>();Q_UNUSED(memories);
    auto *shelf=pet.findChild<ClipboardPanel*>("clipboardShelf");expect(shelf,"Pet clipboard shelf missing");shelf->toggle();
    expect(until([&]{return shelf->height()>34;}),"Clipboard expansion animation failed");
    // The window includes transparent speech-bubble space. The head shelf may
    // use that space, but must remain clear of the visible character itself.
    const auto character=pet.characterGeometry().translated(pet.pos()).toAlignedRect();
    expect(!shelf->geometry().intersects(character),"Clipboard covers the visible character");
    c.clipboard().setEnabled(true);c.clipboard().capture("这一条仅用于本地界面测试");auto *entries=shelf->findChild<QListWidget*>("clipboardEntries");
    expect(entries && until([&]{return entries->count()==1;}),"Clipboard model did not refresh shelf");
    expect(pet.avatarIdentity()=="shuangling-chibi-v1" && pet.actionAtlasReady(),"Tools replaced avatar assets");pet.hide();expect(!shelf->isVisible(),"Hidden pet left clipboard visible");window.hide();
}
}
int main(int argc,char **argv){
    qputenv("QT_MEDIA_BACKEND","windows");QApplication app(argc,argv);app.setQuitOnLastWindowClosed(false);QTemporaryDir directory;
    qunsetenv("DASHSCOPE_API_KEY");
    try{expect(directory.isValid(),"Temporary directory failed");voices(directory.path());cache(directory.path());playback();bundled();typedSpeech(directory.path());streaming(directory.path());textPipeline(directory.path());interaction(directory.path());actions(directory.path());actionClicks(directory.path());lifeUi(directory.path());integratedToolsUi(directory.path());
        QTextStream(stdout)<<"PASS: "<<assertions<<" assertions (five voices, persistence, Qwen SSE/error/cancel, double click/drag/restore). No live API or microphone.\n";return 0;
    }catch(const std::exception &e){QTextStream(stderr)<<"FAIL: "<<e.what()<<"\n";return 1;}
}
