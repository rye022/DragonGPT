#include "companion_window.h"
#include <QApplication>
#include <QTcpServer>
#include <QTcpSocket>
#include <QWebSocketServer>
#include <QWebSocket>
#include <QTemporaryDir>
#include <QTextBrowser>
#include <QJsonDocument>
#include <QJsonArray>
#include <QFile>
#include <QTest>
#include <QSignalSpy>
#include <QThread>
#include <stdexcept>
using namespace dragon;
QJsonArray traceEvents;
void capture(QtMsgType,const QMessageLogContext &,const QString &message){auto o=QJsonDocument::fromJson(message.toUtf8()).object();if(o["component"]=="latency")traceEvents.append(o);}
QByteArray json(const QJsonObject &v){return QJsonDocument(v).toJson(QJsonDocument::Compact);}
int main(int argc,char **argv){
    qputenv("QT_MEDIA_BACKEND","windows");QApplication app(argc,argv);app.setQuitOnLastWindowClosed(false);
    const auto saved=qgetenv("DASHSCOPE_API_KEY");qunsetenv("DASHSCOPE_API_KEY");
    const bool optimized=app.arguments().contains("--optimized");QJsonArray samples;
    if(optimized){
        SpeechTextBuffer clause;clause.update("我会陪着你，");if(clause.takeReady()!=QStringList{"我会陪着你，"})return 2;
        for(const QString &text:QStringList{"你好，今天想聊什么？","我会陪着你，我们慢慢来。","Hello there, let's keep every word.","版本 1.25 可用。😀 尾句没有标点"}){
            SpeechTextBuffer buffer;QString joined;
            for(int i=1;i<=text.size();++i){if(!buffer.update(text.left(i)))return 3;joined+=buffer.takeReady().join("");}
            joined+=buffer.takeReady(true).join("");if(joined!=text)return 4;
        }
    }
    auto previousHandler=qInstallMessageHandler(capture);
    const auto savedLlm=qgetenv("DEEPSEEK_API_KEY");qputenv("DEEPSEEK_API_KEY","fixture-only");
    try{for(int run=0;run<3;++run){
        QTemporaryDir dir;QElapsedTimer clock;clock.start();QJsonObject times;
        auto mark=[&](const QString &name){if(!times.contains(name))times[name]=clock.elapsed();};
        QTcpServer llm;int modelRequests=0;if(!llm.listen(QHostAddress::LocalHost))throw std::runtime_error("LLM fixture listen");
        const QString first="我会陪着你，",tail="我们一起慢慢把这件事做好。",full=first+tail;
        QObject::connect(&llm,&QTcpServer::newConnection,&llm,[&]{auto *s=llm.nextPendingConnection();auto bytes=std::make_shared<QByteArray>();
            QObject::connect(s,&QTcpSocket::disconnected,s,&QObject::deleteLater);
            QObject::connect(s,&QTcpSocket::readyRead,s,[&,s,bytes]{*bytes+=s->readAll();const int end=bytes->indexOf("\r\n\r\n");if(end<0 || s->property("sent").toBool())return;int size=0;for(auto h:bytes->left(end).split('\n'))if(h.toLower().startsWith("content-length:"))size=h.mid(15).trimmed().toInt();if(bytes->size()<end+4+size)return;s->setProperty("sent",true);mark("llm_request");
                ++modelRequests;s->write("HTTP/1.1 200 OK\r\nContent-Type: text/event-stream\r\nConnection: close\r\n\r\n");
                QTimer::singleShot(100,s,[s,first]{s->write("data: "+json({{"type","response.output_text.delta"},{"delta",first}})+"\n\n");s->flush();});
                QTimer::singleShot(1000,s,[s,tail,full]{s->write("data: "+json({{"type","response.output_text.delta"},{"delta",tail}})+"\n\n");s->write("data: "+json({{"type","response.completed"},{"response",QJsonObject{{"status","completed"},{"output",QJsonArray{QJsonObject{{"type","message"},{"content",QJsonArray{QJsonObject{{"type","output_text"},{"text",full}}}}}}}}}})+"\n\n");s->disconnectFromHost();});
            });});
        QWebSocketServer tts("latency fixture",QWebSocketServer::NonSecureMode);if(!tts.listen(QHostAddress::LocalHost))throw std::runtime_error("TTS fixture listen");QStringList spoken;int tasks=0;
        QObject::connect(&tts,&QWebSocketServer::newConnection,&tts,[&]{auto *ws=tts.nextPendingConnection();mark("tts_connected");QObject::connect(ws,&QWebSocket::disconnected,ws,&QObject::deleteLater);
            QObject::connect(ws,&QWebSocket::textMessageReceived,ws,[&,ws](const QString &text){const auto o=QJsonDocument::fromJson(text.toUtf8()).object();const auto h=o["header"].toObject();const auto id=h["task_id"].toString(),action=h["action"].toString();
                if(action=="run-task"){++tasks;mark("tts_run");const auto p=o["payload"].toObject()["parameters"].toObject();if(p["voice"]!="anyuqing_v3.1" || p["sample_rate"]!=24000 || p["format"]!="pcm")throw std::runtime_error("selected voice or quality changed");QTimer::singleShot(350,ws,[&,ws,id]{mark("tts_task_started");ws->sendTextMessage(QString::fromUtf8(json({{"header",QJsonObject{{"task_id",id},{"event","task-started"}}}})));});}
                if(action=="continue-task"){mark("tts_text_sent");spoken<<o["payload"].toObject()["input"].toObject()["text"].toString();}
                if(action=="finish-task")QTimer::singleShot(150,ws,[&,ws,id]{mark("fixture_first_pcm");ws->sendBinaryMessage(QByteArray(24000,0));ws->sendTextMessage(QString::fromUtf8(json({{"header",QJsonObject{{"task_id",id},{"event","task-finished"}}}})));});
            });});
        VoicePreferences p;p.apiFamily="qwen-audio-tts";p.audioTransport="websocket-public";p.audioVoice="anyuqing_v3.1";p.fastResponse=false;p.cacheAudio=false;p.readAloud=true;p.save(dir.filePath("voice.ini"));CredentialStore(dir.filePath("dashscope-key.dpapi")).save("fixture-only");
        Companion c(dir.filePath("memory.sqlite"),nullptr,QUrl(QString("http://127.0.0.1:%1/responses").arg(llm.serverPort())));auto cfg=c.chatSettings();cfg.onlineEnabled=true;cfg.proxyMode=ProxyMode::Direct;c.saveChatSettings(cfg);c.saveApiKey("fixture-only");
        PetWidget pet(c);CompanionWindow w(c,pet,QUrl(QString("ws://127.0.0.1:%1/tts").arg(tts.serverPort())));w.show();
        QObject::connect(&c,&Companion::partialReply,&w,[&](const QString &){mark("first_text_visible");});QObject::connect(&c,&Companion::replyReady,&w,[&]{mark("llm_final");});
        QObject::connect(&w.voiceController(),&VoiceController::segmentQueued,&w,[&]{mark("first_segment_ready");});QObject::connect(&w.voiceController(),&VoiceController::outputStarted,&w,[&]{mark("speaker_first_write");});
        if(run==0 && app.arguments().contains("--startup-grace"))QTest::qWait(8000);
        clock.restart();mark("send");c.sendMessage("请简短回应。");
        const auto protocolComplete=[&]{return times.contains("llm_final") && modelRequests==1 && spoken.join("")==full && tasks==(optimized?2:1);};
        // Keep the network/segmentation budget independent of the native device.
        // Cold Windows device enumeration was measured at 7.8 seconds here;
        // waiting for it must not disguise missing text or extra model requests.
        while(!protocolComplete() && clock.elapsed()<9000)QTest::qWait(2);
        const bool protocolOk=protocolComplete();
        while(protocolOk && (!times.contains("speaker_first_write") || w.voiceController().engaged()) && clock.elapsed()<20000)QTest::qWait(2);
        if(!protocolOk || !times.contains("speaker_first_write") || w.voiceController().engaged()){
            const QJsonObject failure{{"run",run},{"model_requests",modelRequests},{"tts_tasks",tasks},{"spoken_characters",spoken.join("").size()},{"expected_characters",full.size()},{"voice_status",w.voiceController().status()},{"engaged",w.voiceController().engaged()},{"times",times},{"traces",traceEvents}};
            QFile diagnostic("outputs/latency-failure.json");if(diagnostic.open(QIODevice::WriteOnly))diagnostic.write(QJsonDocument(failure).toJson());
            fprintf(stderr,"Latency diagnostic: run=%d requests=%d tasks=%d spoken=%lld expected=%lld status=%s\n",run,modelRequests,tasks,qint64(spoken.join("").size()),qint64(full.size()),qPrintable(w.voiceController().status()));
            throw std::runtime_error("speech lost/duplicated, model retried or fixture did not play");
        }
        const auto delta=[&](const QString &a,const QString &b){return times[b].toInteger()-times[a].toInteger();};
        QJsonObject stages{{"llm_first_text_ms",delta("send","first_text_visible")},{"text_to_segment_ms",delta("first_text_visible","first_segment_ready")},{"queue_and_connect_ms",delta("first_segment_ready","tts_run")},{"task_handshake_ms",delta("tts_run","tts_task_started")},{"task_to_first_pcm_ms",delta("tts_task_started","fixture_first_pcm")},{"pcm_to_device_ms",delta("fixture_first_pcm","speaker_first_write")},{"text_voice_gap_ms",delta("first_text_visible","speaker_first_write")}};
        if(optimized && delta("first_text_visible","first_segment_ready")>120)throw std::runtime_error("first natural clause still delayed");
        samples.append(QJsonObject{{"times_ms",times},{"stages",stages},{"model_requests",modelRequests},{"tts_tasks",tasks},{"text_preserved",true},{"voice_and_pcm_quality_preserved",true}});
    }
    #ifdef DRAGON_LATENCY_ASYNC_DEVICE
    if(optimized){
        // Reproduce a slow device query without changing the user's audio setup.
        SpeechPlayer slow(nullptr,[]{QThread::msleep(350);return QMediaDevices::defaultAudioOutput();});
        QSignalSpy started(&slow,&SpeechPlayer::started),finished(&slow,&SpeechPlayer::finished),failed(&slow,&SpeechPlayer::failed);
        QTimer heartbeat;int beats=0;QObject::connect(&heartbeat,&QTimer::timeout,&slow,[&]{++beats;});heartbeat.start(10);
        QElapsedTimer elapsed;elapsed.start();if(!slow.play(QByteArray(4800,0)).isEmpty() || elapsed.elapsed()>100 || slow.bufferedMilliseconds()!=100)throw std::runtime_error("device lookup blocked main thread or lost queued PCM");
        while(finished.isEmpty() && elapsed.elapsed()<3000)QTest::qWait(2);
        if(started.size()!=1 || finished.size()!=1 || !failed.isEmpty() || beats<15)throw std::runtime_error("asynchronous device/audio lifecycle");
        SpeechPlayer cancelled(nullptr,[]{QThread::msleep(250);return QMediaDevices::defaultAudioOutput();});QSignalSpy stale(&cancelled,&SpeechPlayer::started);
        cancelled.play(QByteArray(4800,0));cancelled.stop();QTest::qWait(400);if(!stale.empty() || cancelled.active())throw std::runtime_error("cancelled queued audio later played");
        SpeechPlayer missing(nullptr,[]{QThread::msleep(80);return QAudioDevice{};});QSignalSpy absent(&missing,&SpeechPlayer::failed);
        missing.play(QByteArray(4800,0));elapsed.restart();while(absent.empty() && elapsed.elapsed()<1000)QTest::qWait(2);
        if(absent.size()!=1 || missing.active())throw std::runtime_error("missing audio device not reported honestly");
    }
    #endif
    QJsonObject result{{"mode",optimized?"after":"before"},{"scope","real Qt + localhost LLM/WebSocket TTS + Windows audio device; controlled 100/1000 ms text, 350 ms handshake, 150 ms PCM"},{"live_paid_calls",0},{"protocol_deadline_ms",9000},{"device_settle_deadline_ms",20000},{"samples",samples}};
    const bool grace=app.arguments().contains("--startup-grace");result["startup_grace_ms"]=grace?8000:0;
    QFile file(grace?(optimized?"outputs/latency-after-startup-grace.json":"outputs/latency-before-startup-grace.json"):(optimized?"outputs/latency-after.json":"outputs/latency-before.json"));if(!file.open(QIODevice::WriteOnly))throw std::runtime_error("result file");file.write(QJsonDocument(result).toJson(QJsonDocument::Indented));
    if(optimized){
        const auto bytes=QJsonDocument(traceEvents).toJson(QJsonDocument::Indented);
        if(traceEvents.isEmpty() || bytes.contains("fixture-only") || bytes.contains("我会陪着你"))throw std::runtime_error("telemetry missing or logging private text");
        QFile traces("outputs/latency-trace-after.json");if(!traces.open(QIODevice::WriteOnly))throw std::runtime_error("trace file");traces.write(bytes);
    }
    qInstallMessageHandler(previousHandler);qputenv("DEEPSEEK_API_KEY",savedLlm);qputenv("DASHSCOPE_API_KEY",saved);return 0;
    }catch(const std::exception &e){qInstallMessageHandler(previousHandler);qputenv("DEEPSEEK_API_KEY",savedLlm);qputenv("DASHSCOPE_API_KEY",saved);fprintf(stderr,"FAIL latency fixture: %s\n",e.what());return 1;}
}
