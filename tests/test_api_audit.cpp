#include "qwen_speech.h"
#include "chat_service.h"
#include "realtime_client.h"
#include "voice_controller.h"
#include "api_errors.h"
#include <QtTest>
#include <QTcpServer>
#include <QTcpSocket>
#include <QWebSocketServer>
#include <QWebSocket>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonArray>
#include <QSettings>
#include <QTemporaryDir>
#include <QtEndian>
#include <functional>
using namespace dragon;
namespace {
QStringList allDialogueTexts(){QStringList lines;for(const auto &v:DialogueEngine::catalog())lines.append(v.toObject()["text"].toString());return lines;}

QByteArray json(const QJsonObject &o){return QJsonDocument(o).toJson(QJsonDocument::Compact);}
QByteArray audio(const QByteArray &pcm,bool last=true){return "data: "+json({{"output",QJsonObject{{"audio",QJsonObject{{"data",QString::fromLatin1(pcm.toBase64())}}},{"finish_reason",last?"stop":"null"}}}})+"\r\n\r\n";}
QByteArray wav(const QByteArray &pcm){
    QByteArray result("RIFF");auto u32=[&](quint32 v){char b[4];qToLittleEndian(v,b);result.append(b,4);};
    auto u16=[&](quint16 v){char b[2];qToLittleEndian(v,b);result.append(b,2);};
    u32(36+pcm.size());result+="WAVEfmt ";u32(16);u16(1);u16(1);u32(24000);u32(48000);u16(2);u16(16);result+="data";u32(pcm.size());result+=pcm;return result;
}
struct Http {
    QTcpServer server;QList<QByteArray> requests;QByteArray body=audio(QByteArray(4800,0)),type="text/event-stream";
    int status=200,delay=0;bool byteSplit=false;
    std::function<QByteArray(const QByteArray&)> route;
    Http(){
        if(!server.listen(QHostAddress::LocalHost))qFatal("fixture listen");
        QObject::connect(&server,&QTcpServer::newConnection,&server,[this]{while(server.hasPendingConnections()){
            auto *s=server.nextPendingConnection();auto buffer=std::make_shared<QByteArray>();
            QObject::connect(s,&QTcpSocket::disconnected,s,&QObject::deleteLater);
            QObject::connect(s,&QTcpSocket::readyRead,s,[this,s,buffer]{
                *buffer+=s->readAll();if(s->property("served").toBool())return;
                const auto end=buffer->indexOf("\r\n\r\n");if(end<0)return;int length=0;
                for(const auto &line:buffer->left(end).split('\n'))if(line.toLower().startsWith("content-length:"))length=line.mid(15).trimmed().toInt();
                if(buffer->size()<end+4+length)return;s->setProperty("served",true);requests<<*buffer;
                const auto b=route?route(*buffer):body;const auto t=type;const auto code=status;const bool split=byteSplit;
                QTimer::singleShot(delay,s,[s,b,t,code,split]{
                    if(s->state()!=QAbstractSocket::ConnectedState)return;
                    s->write("HTTP/1.1 "+QByteArray::number(code)+" Test\r\nContent-Type: "+t+"\r\nConnection: close\r\nContent-Length: "+QByteArray::number(b.size())+"\r\n\r\n");
                    if(split){auto pos=std::make_shared<int>(0);auto *timer=new QTimer(s);timer->setInterval(1);QObject::connect(timer,&QTimer::timeout,s,[s,b,pos,timer]{if(*pos<b.size())s->write(b.mid((*pos)++,1));else{timer->stop();s->disconnectFromHost();}});timer->start();}
                    else{s->write(b);s->disconnectFromHost();}
                });
            });
        }});
    }
    QUrl url()const{return QUrl(QString("http://127.0.0.1:%1/tts").arg(server.serverPort()));}
};
ChatSettings settings(){ChatSettings s;s.onlineEnabled=true;s.proxyMode=ProxyMode::Direct;return s;}
}
class ApiAudit:public QObject{
    Q_OBJECT
    QByteArray savedDashscope_;
private slots:
    void initTestCase(){savedDashscope_=qgetenv("DASHSCOPE_API_KEY");qunsetenv("DASHSCOPE_API_KEY");}
    void cleanupTestCase(){if(savedDashscope_.isNull())qunsetenv("DASHSCOPE_API_KEY");else qputenv("DASHSCOPE_API_KEY",savedDashscope_);}
    void audioTtsOfficialConfiguration(){
        VoicePreferences p;p.apiFamily="qwen-audio-tts";
        QVERIFY(QwenSpeech::configurationError(p).contains("Workspace"));
        p.audioWorkspace="llm-fixture";QVERIFY(QwenSpeech::configurationError(p).isEmpty());
        QCOMPARE(p.audioEndpoint().toString(),QString("https://llm-fixture.cn-beijing.maas.aliyuncs.com/api/v1/services/audio/tts/SpeechSynthesizer"));
        p.region="singapore";QCOMPARE(p.audioEndpoint().host(),QString("llm-fixture.ap-southeast-1.maas.aliyuncs.com"));
        const auto id=p.cacheVoiceId();p.audioModel="qwen-audio-3.0-tts-plus";QVERIFY(id!=p.cacheVoiceId());QVERIFY(QwenSpeech::configurationError(p).isEmpty());
        p.audioVoice="Chelsie";QVERIFY(!QwenSpeech::configurationError(p).isEmpty());p.audioVoice="longanhuan_v3.1";
        p.audioModel="qwen3-tts-flash";QVERIFY(!QwenSpeech::configurationError(p).isEmpty());p.audioModel="qwen-audio-3.1-tts-next";QVERIFY(!QwenSpeech::configurationError(p).isEmpty());
        p.audioModel="qwen-audio-3.1-tts-flash";p.audioWorkspace="llm-fixture.evil";QVERIFY(p.audioEndpoint().isEmpty());
        p.audioWorkspace="llm-fixture";QTemporaryDir d;p.voice="Chelsie";p.save(d.filePath("voice.ini"));auto restored=VoicePreferences::load(d.filePath("voice.ini"));QCOMPARE(restored.cacheVoiceId(),p.cacheVoiceId());QCOMPARE(restored.voice,QString("Chelsie"));
    }
    void audioTtsCuratedPresets(){
        const auto choices=audioVoiceChoices("qwen-audio-3.1-tts-flash","beijing");QCOMPARE(choices.size(),8);
        QCOMPARE(audioVoiceChoices("qwen-audio-3.0-tts-flash","beijing").size(),0);
        QCOMPARE(audioVoiceChoices("qwen-audio-3.1-tts-flash","singapore").size(),1);
        const auto legacy=selectableLegacyVoices();QCOMPARE(legacy.size(),2);QCOMPARE(legacy.last().id,QString("Serena"));
        Http h;h.type="application/json";h.route=[&](const QByteArray &r){return r.startsWith("GET ")?wav(QByteArray(4800,0)):json({{"output",QJsonObject{{"finish_reason","stop"},{"audio",QJsonObject{{"url",h.url().resolved(QUrl("/audio.wav")).toString()}}}}}});};
        QwenSpeech speech(nullptr,h.url());VoicePreferences p;p.apiFamily="qwen-audio-tts";p.audioWorkspace="llm-fixture";
        QSignalSpy ready(&speech,&QwenSpeech::ready),failed(&speech,&QwenSpeech::failed);QSet<QString> keys;
        for(const auto &v:choices){
            p.audioVoice=v.id;keys.insert(p.cacheVoiceId());const auto count=ready.size();
            QVERIFY(speech.start("新的女声。",p,"fixture-key").isEmpty());QTRY_COMPARE(ready.size(),count+1);QVERIFY(failed.isEmpty());
            const auto request=h.requests[h.requests.size()-2];const auto payload=QJsonDocument::fromJson(request.mid(request.indexOf("\r\n\r\n")+4)).object();
            QCOMPARE(payload.value("model").toString(),p.audioModel);QCOMPARE(payload.value("input").toObject().value("voice").toString(),v.id);
        }
        QCOMPARE(keys.size(),8);
    }
    void audioTtsNativeRequestAndWav(){
        Http h;h.type="application/json";const QByteArray pcm(4800,0);
        h.route=[&](const QByteArray &r){return r.startsWith("GET ")?wav(pcm):json({{"output",QJsonObject{{"finish_reason","stop"},{"audio",QJsonObject{{"url",h.url().resolved(QUrl("/audio.wav")).toString()}}}}}});};
        auto url=h.url();url.setPath("/api/v1/services/audio/tts/SpeechSynthesizer");QwenSpeech speech(nullptr,url);VoicePreferences p;p.apiFamily="qwen-audio-tts";p.audioWorkspace="llm-fixture";
        QSignalSpy ready(&speech,&QwenSpeech::ready),failed(&speech,&QwenSpeech::failed);
        QVERIFY(speech.start("官方 Audio TTS 格式。",p,"fixture-key").isEmpty());QTRY_VERIFY(!ready.empty() || !failed.empty());QVERIFY(failed.empty());QCOMPARE(ready.first().first().toByteArray(),pcm);QCOMPARE(h.requests.size(),2);
        const auto request=h.requests.first();QVERIFY(request.startsWith("POST /api/v1/services/audio/tts/SpeechSynthesizer "));QVERIFY(!request.toLower().contains("x-dashscope-sse:"));QVERIFY(request.toLower().contains("authorization: bearer fixture-key"));
        const auto body=QJsonDocument::fromJson(request.mid(request.indexOf("\r\n\r\n")+4)).object();QCOMPARE(body.value("model").toString(),QString("qwen-audio-3.1-tts-flash"));const auto input=body.value("input").toObject();
        QCOMPARE(input.value("voice").toString(),QString("longanhuan_v3.1"));QCOMPARE(input.value("format").toString(),QString("wav"));QCOMPARE(input.value("sample_rate").toInt(),24000);QVERIFY(!input.contains("language_type"));QVERIFY(!body.contains("messages"));QVERIFY(!h.requests.last().toLower().contains("authorization:"));
    }
    void audioTtsClickAndChatShareAdapter(){
        Http h;h.type="application/json";h.route=[&](const QByteArray &r){return r.startsWith("GET ")?wav(QByteArray(4800,0)):json({{"output",QJsonObject{{"finish_reason","stop"},{"audio",QJsonObject{{"url",h.url().resolved(QUrl("/audio.wav")).toString()}}}}}});};
        QTemporaryDir d;VoicePreferences p;p.apiFamily="qwen-audio-tts";p.audioWorkspace="llm-fixture";p.voice="Serena";p.preferOffline=true;p.fastResponse=false;p.readAloud=true;p.save(d.filePath("voice.ini"));
        CredentialStore(d.filePath("dashscope-key.dpapi")).save("fixture-key");VoiceCache cache(d.filePath("voice-cache"));cache.write("Serena","聊天使用 Audio TTS。",QByteArray(4800,0));
        Companion companion(d.filePath("memory.sqlite"));VoiceController voice(companion,nullptr,h.url());auto *speech=voice.findChild<QwenSpeech*>();QSignalSpy ready(speech,&QwenSpeech::ready),begun(&voice,&VoiceController::outputStarted);
        voice.speakActionLine(PetAction::Work);QTRY_COMPARE(ready.size(),1);QTRY_COMPARE_WITH_TIMEOUT(begun.size(),1,10000);QVERIFY(begun.last().first().toString().contains("龙安欢"));QCOMPARE(h.requests.size(),2);voice.endCall();
        companion.turnStarted();companion.partialReply("聊天使用 Audio TTS。");companion.replyReady("聊天使用 Audio TTS。","fixture");QTRY_COMPARE(ready.size(),2);QTRY_COMPARE(begun.size(),2);QCOMPARE(h.requests.size(),4);QVERIFY(h.requests[2].contains("qwen-audio-3.1-tts-flash"));QVERIFY(h.requests[2].contains("longanhuan_v3.1"));QVERIFY(!voice.active());voice.endCall();
        p.expression=voice.speechExpression();QVERIFY(p.expression.instruction.contains("关系初识"));
        QVERIFY(!cache.read(p.expressiveCacheId(),"聊天使用 Audio TTS。").isEmpty());QVERIFY(p.expressiveCacheId()!=p.cacheVoiceId());
    }
    void audioTtsRejectsContainerAsRawPcm(){
        Http h;QwenSpeech speech(nullptr,h.url());VoicePreferences p;p.apiFamily="qwen-audio-tts";p.audioWorkspace="llm-fixture";QSignalSpy failed(&speech,&QwenSpeech::failed),chunks(&speech,&QwenSpeech::chunkReady);
        h.body=audio(wav(QByteArray(4800,0)));QVERIFY(speech.start("测试",p,"fixture-key").isEmpty());QTRY_COMPARE(failed.size(),1);QVERIFY(chunks.empty());
    }
    void streamingHttpErrorReasonIsRetained(){
        Http h;h.status=403;h.body="event: error\r\ndata: {\"code\":\"Model.AccessDenied\",\"message\":\"private sk-do-not-print\"}\r\n\r\n";
        QwenSpeech speech(nullptr,h.url());QSignalSpy failed(&speech,&QwenSpeech::failed);QVERIFY(speech.start("测试",{},"fixture").isEmpty());QTRY_COMPARE(failed.size(),1);
        const auto reason=failed.first().first().toString();QVERIFY(reason.contains("Model.AccessDenied"));QVERIFY(reason.contains("业务空间"));QVERIFY(!reason.contains("private"));QVERIFY(!reason.contains("sk-"));
        QVERIFY(apiError("test",403,QJsonObject{{"code","AllocationQuota.FreeTierOnly"}}).contains("用完即停"));
        QVERIFY(apiError("test",403,QJsonObject{{"code","Endpoint.AccessDenied"}}).contains("业务空间入口"));
        const auto restricted=apiError("Qwen-TTS / qwen3-tts-flash",403,QJsonObject{{"code","AccessDenied"},{"message","Access denied by API-Key restrictions."}});
        QVERIFY(restricted.contains("访问限制"));QVERIFY(restricted.contains("可访问模型范围"));QVERIFY(restricted.contains("IP 白名单"));
        QVERIFY(!apiError("test",403,QJsonObject{{"code","AccessDenied"},{"message","Unknown private message sk-never-print"}}).contains("sk-never-print"));
    }
    void audioTtsWebSocketTaskProtocol(){
        QWebSocketServer server("audio-tts",QWebSocketServer::NonSecureMode);QVERIFY(server.listen(QHostAddress::LocalHost));QVector<QJsonObject> received;QByteArray auth;QString id;const QByteArray pcm(4800,0);
        connect(&server,&QWebSocketServer::newConnection,&server,[&]{auto *ws=server.nextPendingConnection();auth=ws->request().rawHeader("Authorization");connect(ws,&QWebSocket::disconnected,ws,&QObject::deleteLater);
            connect(ws,&QWebSocket::textMessageReceived,ws,[&,ws](const QString &s){const auto o=QJsonDocument::fromJson(s.toUtf8()).object();received<<o;const auto h=o.value("header").toObject();
                if(h.value("action")=="run-task"){
                    id=h.value("task_id").toString();ws->sendTextMessage(QString::fromUtf8(json({{"header",QJsonObject{{"event","task-started"},{"task_id","stale"}}}})));
                    const auto started=QString::fromUtf8(json({{"header",QJsonObject{{"event","task-started"},{"task_id",id}}}}));ws->sendTextMessage(started);ws->sendTextMessage(started);
                }
                if(h.value("action")=="finish-task"){
                    ws->sendBinaryMessage(pcm.left(1));ws->sendBinaryMessage(pcm.mid(1));ws->sendTextMessage(QString::fromUtf8(json({{"header",QJsonObject{{"event","task-finished"},{"task_id",id}}}})));
                }
            });
        });
        VoicePreferences p;p.apiFamily="qwen-audio-tts";p.audioTransport="websocket-public";p.expression=SpeechExpression::forVoice(p.audioVoice,Emotion::Worried);QVERIFY(QwenSpeech::configurationError(p).isEmpty());
        QCOMPARE(p.audioWebSocketEndpoint(),QUrl("wss://dashscope.aliyuncs.com/api-ws/v1/inference"));
        QwenSpeech speech(nullptr,QUrl(QString("ws://127.0.0.1:%1/api-ws/v1/inference").arg(server.serverPort())));QSignalSpy ready(&speech,&QwenSpeech::ready),failed(&speech,&QwenSpeech::failed),chunks(&speech,&QwenSpeech::chunkReady);
        QVERIFY(speech.start("同一段测试。",p,"fixture-key").isEmpty());QTRY_VERIFY(!ready.empty() || !failed.empty());QVERIFY(failed.empty());QCOMPARE(ready.first().first().toByteArray(),pcm);QVERIFY(!speech.busy());QCOMPARE(auth,QByteArray("Bearer fixture-key"));
        QCOMPARE(received.size(),3);const auto payload=received[0].value("payload").toObject();QCOMPARE(payload.value("function").toString(),QString("SpeechSynthesizer"));QCOMPARE(payload.value("model").toString(),p.audioModel);
        const auto params=payload.value("parameters").toObject();QCOMPARE(params.value("voice").toString(),p.audioVoice);QCOMPARE(params.value("format").toString(),QString("pcm"));QCOMPARE(params.value("sample_rate").toInt(),24000);
        QCOMPARE(params.value("rate").toDouble(),p.expression.rate);QCOMPARE(params.value("pitch").toDouble(),p.expression.pitch);QCOMPARE(params.value("instruction").toString(),p.expression.instruction);
        QCOMPARE(received[1].value("payload").toObject().value("input").toObject().value("text").toString(),QString("同一段测试。"));
        for(const auto &o:received)QCOMPARE(o.value("header").toObject().value("task_id").toString(),id);
        QByteArray joined;for(const auto &c:chunks){QCOMPARE(c.first().toByteArray().size()%2,0);joined+=c.first().toByteArray();}QCOMPARE(joined,pcm);
    }
    void audioTtsWebSocketFailureCancelAndTimeout(){
        QWebSocketServer server("audio-tts-errors",QWebSocketServer::NonSecureMode);QVERIFY(server.listen(QHostAddress::LocalHost));bool sendError=true;int requests=0;
        connect(&server,&QWebSocketServer::newConnection,&server,[&]{auto *ws=server.nextPendingConnection();connect(ws,&QWebSocket::disconnected,ws,&QObject::deleteLater);connect(ws,&QWebSocket::textMessageReceived,ws,[&,ws](const QString &s){++requests;const auto id=QJsonDocument::fromJson(s.toUtf8()).object().value("header").toObject().value("task_id").toString();if(sendError)ws->sendTextMessage(QString::fromUtf8(json({{"header",QJsonObject{{"event","task-failed"},{"task_id",id},{"error_code","Model.AccessDenied"},{"error_message","private sk-fixture"}}}})));});});
        VoicePreferences p;p.apiFamily="qwen-audio-tts";p.audioTransport="websocket-public";QwenSpeech speech(nullptr,QUrl(QString("ws://127.0.0.1:%1/api-ws/v1/inference").arg(server.serverPort())));QSignalSpy failed(&speech,&QwenSpeech::failed),ready(&speech,&QwenSpeech::ready);
        QVERIFY(speech.start("测试",p,"fixture-key").isEmpty());QTRY_COMPARE(failed.size(),1);QVERIFY(failed.first().first().toString().contains("Model.AccessDenied"));QVERIFY(!failed.first().first().toString().contains("private"));QVERIFY(ready.empty());
        sendError=false;speech.setTimeoutMs(80);QVERIFY(speech.start("测试",p,"fixture-key").isEmpty());QTRY_COMPARE(failed.size(),2);QVERIFY(!speech.busy());QCOMPARE(requests,2);
        speech.setTimeoutMs(500);QVERIFY(speech.start("取消",p,"fixture-key").isEmpty());QTRY_COMPARE(requests,3);speech.cancel();QTest::qWait(150);QCOMPARE(failed.size(),2);QVERIFY(ready.empty());QVERIFY(!speech.busy());
    }
    void typedChatUsesSameSelectedVoice(){
        Http h;QTemporaryDir d;VoicePreferences p;p.voice="Chelsie";p.readAloud=true;p.preferOffline=false;p.fastResponse=false;p.save(d.filePath("voice.ini"));
        CredentialStore(d.filePath("dashscope-key.dpapi")).save("fixture-only-key");VoiceCache cache(d.filePath("voice-cache"));
        for(const auto &line:allDialogueTexts())cache.write("Chelsie",line,QByteArray(4800,0));
        Companion companion(d.filePath("memory.sqlite"));VoiceController voice(companion,nullptr,h.url());
        QSignalSpy begun(&voice,&VoiceController::outputStarted);auto *t=voice.findChild<QwenSpeech*>();auto *player=voice.findChild<SpeechPlayer*>();QVERIFY(t && player);
        QSignalSpy ready(t,&QwenSpeech::ready),finished(player,&SpeechPlayer::finished);
        voice.speakActionLine(PetAction::Work);QTRY_COMPARE(begun.size(),1);QVERIFY(begun.first().first().toString().contains("千雪"));QCOMPARE(h.requests.size(),0);voice.endCall();
        companion.turnStarted();companion.partialReply("聊天同一个音色。");companion.replyReady("聊天同一个音色。","fixture");
        QTRY_COMPARE(ready.size(),1);QTRY_COMPARE(begun.size(),2);QTRY_COMPARE(finished.size(),1);
        QVERIFY(h.requests.last().contains("Chelsie"));QVERIFY(begun.last().first().toString().contains("百炼 · 千雪"));QVERIFY(!voice.active());
    }
    void typedChatDoesNotChangeVoiceOnFailure(){
        Http h;h.status=403;h.body="{\"code\":\"AccessDenied\"}";QTemporaryDir d;
        VoicePreferences p;p.voice="Chelsie";p.readAloud=true;p.preferOffline=false;p.fastResponse=false;p.cacheAudio=false;p.save(d.filePath("voice.ini"));
        CredentialStore(d.filePath("dashscope-key.dpapi")).save("fixture-only-key");Companion companion(d.filePath("memory.sqlite"));VoiceController voice(companion,nullptr,h.url());
        QSignalSpy begun(&voice,&VoiceController::outputStarted);auto *t=voice.findChild<QwenSpeech*>();QSignalSpy failed(t,&QwenSpeech::failed);
        companion.turnStarted();companion.partialReply("保留千雪原声。");QTRY_COMPARE(failed.size(),1);QTest::qWait(600);
        QVERIFY2(begun.empty(),"Failed cloud voice silently changed to system/another speaker");
        QVERIFY(voice.status().contains("403"));QVERIFY(voice.status().contains("千雪"));QVERIFY(voice.status().contains("qwen3-tts-flash"));
        companion.partialReply("保留千雪原声。第二句也不要变声。");companion.replyReady("保留千雪原声。第二句也不要变声。","fixture");QTest::qWait(150);QCOMPARE(h.requests.size(),1);
        QVERIFY(begun.empty());QVERIFY(!voice.active());
    }
    void clickFailureDoesNotSubstituteBundledSpeaker(){
        Http h;h.status=403;h.body="{\"code\":\"AccessDenied\"}";QTemporaryDir d;
        VoicePreferences p;p.voice="Chelsie";p.fastResponse=false;p.cacheAudio=false;p.preferOffline=false;p.save(d.filePath("voice.ini"));
        CredentialStore(d.filePath("dashscope-key.dpapi")).save("fixture-only-key");Companion companion(d.filePath("memory.sqlite"));VoiceController voice(companion,nullptr,h.url());
        QSignalSpy begun(&voice,&VoiceController::outputStarted),ended(&voice,&VoiceController::interactionFinished);auto *t=voice.findChild<QwenSpeech*>();QSignalSpy failed(t,&QwenSpeech::failed);
        voice.speakActionLine(PetAction::Work);QTRY_COMPARE(failed.size(),1);QTest::qWait(100);QVERIFY(begun.empty());QCOMPARE(ended.size(),1);QVERIFY(voice.status().contains("千雪"));QVERIFY(voice.status().contains("403"));
    }
    void nativeRequestAndVoiceSwitch(){
        Http h;QwenSpeech t(nullptr,h.url());QSignalSpy ready(&t,&QwenSpeech::ready),failed(&t,&QwenSpeech::failed);
        VoicePreferences p;p.voice="Momo";
        QVERIFY(t.start("工作中的测试。",p,"fixture-key").isEmpty());QTRY_COMPARE_WITH_TIMEOUT(ready.size(),1,2500);QVERIFY(failed.empty());
        auto request=h.requests.last();QVERIFY(request.toLower().contains("authorization: bearer fixture-key"));QVERIFY(request.toLower().contains("x-dashscope-sse: enable"));
        auto body=QJsonDocument::fromJson(request.mid(request.indexOf("\r\n\r\n")+4)).object();
        QCOMPARE(body.value("model").toString(),QString("qwen3-tts-flash"));QVERIFY(!body.contains("messages"));QVERIFY(!body.contains("response_format"));
        QCOMPARE(body.value("input").toObject().value("voice").toString(),QString("Momo"));
        p.voice="Chelsie";QVERIFY(t.start("另一音色。",p,"fixture-key").isEmpty());QTRY_COMPARE(ready.size(),2);
        QVERIFY(h.requests.last().contains("Chelsie"));QVERIFY(!t.busy());
    }
    void byteSplitCrlfAndTail(){
        Http h;h.byteSplit=true;h.body=audio(QByteArray::fromHex("010203"),false)+audio(QByteArray::fromHex("040506"));h.body.chop(4);
        QwenSpeech t(nullptr,h.url());QSignalSpy ready(&t,&QwenSpeech::ready),failed(&t,&QwenSpeech::failed),chunks(&t,&QwenSpeech::chunkReady);
        QVERIFY(t.start("测试",{},"fixture").isEmpty());QTRY_VERIFY_WITH_TIMEOUT(!ready.empty() || !failed.empty(),4000);
        QVERIFY2(failed.empty(),qPrintable(failed.empty()?QString():failed.first().first().toString()));QCOMPARE(ready.first().first().toByteArray(),QByteArray::fromHex("010203040506"));
        QByteArray joined;for(const auto &chunk:chunks){const auto b=chunk.first().toByteArray();QCOMPARE(b.size()%2,0);joined+=b;}QCOMPARE(joined,ready.first().first().toByteArray());
    }
    void urlOnlyDownloadsWavWithoutKey(){
        Http h;h.type="application/json";const QByteArray pcm(4800,0);
        h.route=[&](const QByteArray &r){if(r.startsWith("GET /audio.wav"))return wav(pcm);
            return json({{"output",QJsonObject{{"finish_reason","stop"},{"audio",QJsonObject{{"url",h.url().resolved(QUrl("/audio.wav")).toString()}}}}}});};
        QwenSpeech t(nullptr,h.url());QSignalSpy ready(&t,&QwenSpeech::ready),failed(&t,&QwenSpeech::failed);
        QVERIFY(t.start("测试",{},"fixture-secret").isEmpty());QTRY_VERIFY(!ready.empty() || !failed.empty());
        QVERIFY2(failed.empty(),qPrintable(failed.empty()?QString():failed.first().first().toString()));QCOMPARE(ready.first().first().toByteArray(),pcm);
        QCOMPARE(h.requests.size(),2);QVERIFY(!h.requests[1].toLower().contains("authorization:"));QVERIFY(!h.requests[1].contains("fixture-secret"));
    }
    void malformedResponses_data(){
        QTest::addColumn<QByteArray>("body");
        QTest::newRow("bad-base64")<<QByteArray("data: {\"output\":{\"audio\":{\"data\":\"!!AQID\"},\"finish_reason\":\"stop\"}}\n\n");
        QTest::newRow("odd-terminal-pcm")<<audio(QByteArray(3,0));
        QTest::newRow("truncated")<<audio(QByteArray(4,0),false);
        QTest::newRow("empty")<<audio({});
        QTest::newRow("invalid-json")<<QByteArray("data: nope\n\n");
        QTest::newRow("oversized-event")<<QByteArray(2*1024*1024+1,'x');
        QTest::newRow("untrusted-url")<<json({{"output",QJsonObject{{"finish_reason","stop"},{"audio",QJsonObject{{"url","https://example.org/audio.wav"}}}}}});
    }
    void downloadFailureDoesNotResynthesize(){
        Http h;h.type="application/json";h.route=[&](const QByteArray &r){
            if(r.startsWith("GET ")){h.status=404;return json({{"code","NoSuchKey"}});}
            return json({{"output",QJsonObject{{"finish_reason","stop"},{"audio",QJsonObject{{"url",h.url().resolved(QUrl("/expired.wav")).toString()}}}}}});
        };
        QwenSpeech t(nullptr,h.url());QSignalSpy ready(&t,&QwenSpeech::ready),failed(&t,&QwenSpeech::failed);
        QVERIFY(t.start("测试",{},"fixture").isEmpty());QTRY_COMPARE(failed.size(),1);QVERIFY(ready.empty());QCOMPARE(h.requests.size(),2);
        QVERIFY(failed.first().first().toString().contains("404"));QTest::qWait(80);QCOMPARE(h.requests.size(),2);
    }
    void connectionRefused(){
        QTcpServer reserve;QVERIFY(reserve.listen(QHostAddress::LocalHost));const auto port=reserve.serverPort();reserve.close();
        QwenSpeech t(nullptr,QUrl(QString("http://127.0.0.1:%1/tts").arg(port)));t.setTimeoutMs(500);QSignalSpy failed(&t,&QwenSpeech::failed),ready(&t,&QwenSpeech::ready);
        QVERIFY(t.start("测试",{},"fixture").isEmpty());QTRY_COMPARE(failed.size(),1);QVERIFY(ready.empty());QVERIFY(!t.busy());
    }
    void malformedResponses(){
        QFETCH(QByteArray,body);Http h;h.body=body;QwenSpeech t(nullptr,h.url());QSignalSpy ready(&t,&QwenSpeech::ready),failed(&t,&QwenSpeech::failed);
        QVERIFY(t.start("测试",{},"fixture").isEmpty());QTRY_COMPARE(failed.size(),1);QVERIFY(ready.empty());QVERIFY(!t.busy());QCOMPARE(h.requests.size(),1);
    }
    void httpErrors_data(){QTest::addColumn<int>("status");for(int s:{400,401,403,407,429,500,302})QTest::newRow(qPrintable(QString::number(s)))<<s;}
    void httpErrors(){
        QFETCH(int,status);Http h;h.status=status;h.body=json({{"code","AccessDenied"},{"message","private-text sk-fixture-sensitive"},{"request_id","12345678-abcd"}});
        QwenSpeech t(nullptr,h.url());QSignalSpy failed(&t,&QwenSpeech::failed);QVERIFY(t.start("测试",{},"fixture").isEmpty());QTRY_COMPARE(failed.size(),1);
        const auto reason=failed.first().first().toString();QVERIFY(reason.contains(QString::number(status)));QVERIFY(reason.contains("AccessDenied"));QVERIFY(!reason.contains("private-text"));QVERIFY(!reason.contains("sensitive"));QCOMPARE(h.requests.size(),1);
    }
    void timeoutCancelAndReentrantChunk(){
        Http h;h.delay=200;QwenSpeech t(nullptr,h.url());t.setTimeoutMs(50);QSignalSpy ready(&t,&QwenSpeech::ready),failed(&t,&QwenSpeech::failed);
        QVERIFY(t.start("测试",{},"fixture").isEmpty());QTRY_COMPARE(failed.size(),1);QTest::qWait(250);QVERIFY(ready.empty());QCOMPARE(h.requests.size(),1);
        t.setTimeoutMs(500);h.delay=0;auto c=connect(&t,&QwenSpeech::chunkReady,&t,&QwenSpeech::cancel);
        QVERIFY(t.start("取消",{},"fixture").isEmpty());QTRY_COMPARE(h.requests.size(),2);QTest::qWait(50);QVERIFY(ready.empty());QCOMPARE(failed.size(),1);disconnect(c);
        QVERIFY(t.start("恢复",{},"fixture").isEmpty());QTRY_COMPARE(ready.size(),1);
    }
    void wavValidation(){
        const QByteArray pcm(480,0);QByteArray out;auto good=wav(pcm);QVERIFY(decodePcm24kWav(good,out).isEmpty());QCOMPARE(out,pcm);
        for(int cut:{1,8,40,43}){QVERIFY(!decodePcm24kWav(good.left(cut),out).isEmpty());}
        auto stereo=good;stereo[22]=2;QVERIFY(!decodePcm24kWav(stereo,out).isEmpty());
        auto rate=good;rate[24]=0;QVERIFY(!decodePcm24kWav(rate,out).isEmpty());
        QVERIFY(!decodePcm24kWav(wav(QByteArray(3,0)),out).isEmpty());
        auto extra=good;extra.insert(12,QByteArray::fromHex("4a554e4b020000000000"));qToLittleEndian(quint32(extra.size()-8),extra.data()+4);
        QVERIFY(decodePcm24kWav(extra,out).isEmpty());QCOMPARE(out,pcm);
    }
    void configurationAndProxy(){
        QTemporaryDir d;VoicePreferences p;p.voice="Bella";p.region="singapore";p.save(d.filePath("voice.ini"));auto loaded=VoicePreferences::load(d.filePath("voice.ini"));QCOMPARE(loaded.region,p.region);QCOMPARE(loaded.voice,p.voice);
        QCOMPARE(QwenSpeech::endpointForRegion("beijing").host(),QString("dashscope.aliyuncs.com"));QCOMPARE(QwenSpeech::endpointForRegion("singapore").host(),QString("dashscope-intl.aliyuncs.com"));QVERIFY(QwenSpeech::endpointForRegion("invalid").isEmpty());
        QwenSpeech t;loaded.region="invalid";QVERIFY(!t.start("测试",loaded,"fixture").isEmpty());QVERIFY(!t.start("测试",p,"bad\nkey").isEmpty());
        QNetworkProxy proxy;QVERIFY(selectSystemProxy({QNetworkProxy(QNetworkProxy::NoProxy)},proxy).isEmpty());QCOMPARE(proxy.type(),QNetworkProxy::NoProxy);
        QVERIFY(!selectSystemProxy({QNetworkProxy(QNetworkProxy::DefaultProxy)},proxy).isEmpty());
        QVERIFY(selectSystemProxy({QNetworkProxy(QNetworkProxy::HttpProxy,"127.0.0.1",7890)},proxy).isEmpty());QCOMPARE(proxy.port(),quint16(7890));
        QSettings ini(d.filePath("online.ini"),QSettings::IniFormat);ini.setValue("online/provider","deepseek");ini.sync();QCOMPARE(ChatSettings::load(ini.fileName()).model,QString("deepseek-flash"));
    }
    void deepSeekModelSwitchTailAndError(){
        Http h;const QJsonObject response{{"status","completed"},{"output",QJsonArray{QJsonObject{{"type","message"},{"content",QJsonArray{QJsonObject{{"type","output_text"},{"text","完成"}}}}}}}};
        h.body="data: "+json({{"type","response.completed"},{"response",response}});
        ChatService c(nullptr,h.url());QSignalSpy ready(&c,&ChatService::succeeded),failed(&c,&ChatService::failed);auto s=settings();
        for(const auto &model:{QString("deepseek-flash"),QString("deepseek-v4-pro")}){const auto expected=ready.size()+1;s.model=model;QVERIFY(c.start(s,"fixture","角色","{}",{},"测试").isEmpty());QTRY_COMPARE(ready.size(),expected);QVERIFY(h.requests.last().contains(model.toUtf8()));}
        QCOMPARE(ready.size(),2);QVERIFY(failed.empty());h.status=403;h.body="{\"error\":{\"code\":\"permission_denied\",\"message\":\"secret-prompt\"}}";
        QVERIFY(c.start(s,"fixture","角色","{}",{},"测试").isEmpty());QTRY_COMPARE(failed.size(),1);QVERIFY(failed.first().first().toString().contains("permission_denied"));QVERIFY(!failed.first().first().toString().contains("secret-prompt"));
    }
    void realtimeInvalidAudioAndFailedResponse(){
        for(bool malformed:{true,false}){
            QWebSocketServer server("fixture",QWebSocketServer::NonSecureMode);QVERIFY(server.listen(QHostAddress::LocalHost));
            connect(&server,&QWebSocketServer::newConnection,&server,[&]{auto *ws=server.nextPendingConnection();connect(ws,&QWebSocket::disconnected,ws,&QObject::deleteLater);connect(ws,&QWebSocket::textMessageReceived,ws,[ws,malformed](const QString&){
                ws->sendTextMessage(QString::fromUtf8(json({{"type","response.created"},{"response",QJsonObject{{"id","r1"}}}})));
                QJsonObject e=malformed?QJsonObject{{"type","response.output_audio.delta"},{"response_id","r1"},{"delta","!!AQIDBA=="}}:QJsonObject{{"type","response.done"},{"response",QJsonObject{{"id","r1"},{"status","failed"},{"status_details",QJsonObject{{"error",QJsonObject{{"code","server_error"}}}}}}}};
                ws->sendTextMessage(QString::fromUtf8(json(e)));
            });});
            RealtimeClient c(nullptr,QUrl(QString("ws://127.0.0.1:%1/v1/realtime").arg(server.serverPort())));QSignalSpy failed(&c,&RealtimeClient::failed),played(&c,&RealtimeClient::audio);
            QVERIFY(c.start(settings(),"fixture","fixture-model","角色").isEmpty());QTRY_COMPARE(failed.size(),1);QVERIFY(played.empty());
        }
    }
    void playbackAndCancellationRace(){
        SpeechPlayer player;QSignalSpy finished(&player,&SpeechPlayer::finished),failed(&player,&SpeechPlayer::failed);
        QVERIFY2(player.beginStream().isEmpty(),"No usable output device on test machine");QVERIFY(player.appendPcm(QByteArray(4800,0)).isEmpty());player.finishStream();QTRY_COMPARE(finished.size(),1);QVERIFY(failed.empty());
        connect(&player,&SpeechPlayer::started,&player,&SpeechPlayer::stop);QVERIFY(player.play(QByteArray(4800,0)).isEmpty());QTest::qWait(100);QVERIFY(!player.active());QCOMPARE(finished.size(),1);
        QTemporaryDir d;VoicePreferences p;p.readAloud=true;p.voice="Momo";p.save(d.filePath("voice.ini"));VoiceCache cache(d.filePath("voice-cache"));
        for(const auto &line:allDialogueTexts())cache.write("Momo",line,QByteArray(48000,0));cache.write("Momo","回退文本。",QByteArray(4800,0));
        Companion companion(d.filePath("memory.sqlite"));VoiceController voice(companion);QSignalSpy spoken(&voice,&VoiceController::speechRequested),ended(&voice,&VoiceController::interactionFinished);
        companion.turnStarted();companion.partialReply("尚未结束");voice.speakActionLine(PetAction::Work);const auto n=spoken.size();QVERIFY(n>0);
        companion.replyAborted();companion.replyReady("回退文本。","offline");QCOMPARE(spoken.size(),n);QVERIFY(ended.empty());
        voice.endCall();companion.turnStarted();companion.replyAborted();companion.replyReady("回退文本。","offline");QCOMPARE(spoken.size(),n+1);voice.endCall();
    }
};
QTEST_GUILESS_MAIN(ApiAudit)
#include "test_api_audit.moc"
