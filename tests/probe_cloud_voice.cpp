// Manual diagnostic only. Not part of CTest; requires an explicit authorized invocation.
#include "qwen_speech.h"
#include "chat_settings.h"
#include <QCoreApplication>
#include <QJsonDocument>
#include <QSaveFile>
#include <QDataStream>
#include <QElapsedTimer>
#include <QWebSocket>
#include <QFile>
#include <QJsonArray>
#include <QFileInfo>
#include <functional>
#include <algorithm>
using namespace dragon;
int main(int argc,char **argv){
    qputenv("QT_MEDIA_BACKEND","windows");QCoreApplication app(argc,argv);const auto args=app.arguments();
    if(args.size()==5 && args[1]=="--check-audio-voices"){
        QFile catalog(args[3]);if(!catalog.open(QIODevice::ReadOnly))return 2;
        const auto voices=QJsonDocument::fromJson(catalog.readAll()).array();if(voices.isEmpty() || voices.size()>8)return 2;
        auto p=VoicePreferences::load(args[2]+"/voice.ini");if(!p.audioTts())return 2;
        QByteArray key;try{key=CredentialStore(args[2]+"/dashscope-key.dpapi").load();}catch(...){return 3;}if(key.isEmpty())return 3;
        QwenSpeech speech;speech.setTimeoutMs(45000);QJsonArray results;int cursor=0;QElapsedTimer clock;
        std::function<void()> next;
        auto finishRow=[&](QJsonObject row){row["voice"]=p.audioVoice;row["elapsed_ms"]=clock.elapsed();row["emotion"]=emotionName(p.expression.emotion);row["expression"]=p.expression.parameters();results.append(row);++cursor;QTimer::singleShot(0,&app,next);};
        QObject::connect(&speech,&QwenSpeech::failed,&app,[&](const QString &reason){finishRow({{"success",false},{"error",reason}});});
        QObject::connect(&speech,&QwenSpeech::ready,&app,[&](const QByteArray &pcm){
            const auto path=QFileInfo(args[4]).absolutePath()+"/"+p.audioVoice+(voices[cursor].isObject()?"-"+QString::number(int(p.expression.emotion)):QString{})+".wav";QSaveFile f(path);
            bool written=f.open(QIODevice::WriteOnly);if(written){QDataStream w(&f);w.setByteOrder(QDataStream::LittleEndian);w.writeRawData("RIFF",4);w<<quint32(36+pcm.size());w.writeRawData("WAVEfmt ",8);w<<quint32(16)<<quint16(1)<<quint16(1)<<quint32(24000)<<quint32(48000)<<quint16(2)<<quint16(16);w.writeRawData("data",4);w<<quint32(pcm.size());w.writeRawData(pcm.constData(),pcm.size());written=f.commit();}
            finishRow({{"success",written && !pcm.isEmpty() && pcmEnergy(pcm)>0.001},{"pcm_bytes",pcm.size()},{"rms",pcmEnergy(pcm)},{"seconds",pcm.size()/48000.0},{"wav_written",written}});
        });
        next=[&]{
            if(cursor>=voices.size()){
                QSaveFile f(args[4]);if(!f.open(QIODevice::WriteOnly)){app.exit(4);return;}
                f.write(QJsonDocument(QJsonObject{{"model",p.audioModel},{"region",p.region},{"attempts",voices.size()},{"results",results}}).toJson());
                const bool saved=f.commit();key.fill('\0');app.exit(saved?0:4);return;
            }
            if(voices[cursor].isObject()){
                const auto item=voices[cursor].toObject();p.audioVoice=item.value("voice").toString();p.expression=SpeechExpression::forVoice(p.audioVoice,Emotion(std::clamp(item.value("emotion").toInt(),0,9)));
            }else p.audioVoice=voices[cursor].toString();
            clock.start();const auto error=speech.start("今天也一起慢慢来吧，我会在这里陪着你哦。",p,key);
            if(!error.isEmpty())finishRow({{"success",false},{"error",error}});
        };QTimer::singleShot(0,&app,next);return app.exec();
    }
    if(args.size()==4 && args[1]=="--configure-audio-tts-public"){
        auto p=VoicePreferences::load(args[2]+"/voice.ini");p.apiFamily="qwen-audio-tts";p.audioWorkspace=args[3];p.audioTransport="websocket-public";
        p.audioModel="qwen-audio-3.1-tts-flash";p.audioVoice="longanhuan_v3.1";
        if(p.audioEndpoint().isEmpty())return 3;
        try{p.save(args[2]+"/voice.ini");}catch(const std::exception&){return 3;}return 0;
    }
    if(args.size()==6 && args[1]=="--handshake-only"){
        auto p=VoicePreferences::load(args[2]+"/voice.ini");p.audioWorkspace=args[5];
        if(p.audioEndpoint().isEmpty() || (args[4]!="public" && args[4]!="workspace"))return 2;
        auto url=p.audioEndpoint();url.setScheme("wss");url.setPath("/api-ws/v1/inference");
        if(args[4]=="public")url.setHost(p.region=="beijing"?"dashscope.aliyuncs.com":"dashscope-intl.aliyuncs.com");
        QWebSocket socket;QTimer guard;guard.setSingleShot(true);QElapsedTimer clock;bool done=false;
        QJsonObject report{{"endpoint",url.toString()},{"generation_requests",0},{"text_sent",false}};
        auto finish=[&](int code){if(done)return;done=true;guard.stop();socket.abort();report["elapsed_ms"]=clock.elapsed();QSaveFile f(args[3]);if(f.open(QIODevice::WriteOnly)){f.write(QJsonDocument(report).toJson());f.commit();}app.exit(code);};
        QObject::connect(&socket,&QWebSocket::connected,&app,[&]{report["connected"]=true;report["handshake_status"]=101;finish(0);});
        QObject::connect(&socket,&QWebSocket::errorOccurred,&app,[&](QAbstractSocket::SocketError e){report["connected"]=false;report["socket_error"]=int(e);if(socket.errorString().contains("403"))report["handshake_status"]=403;else if(socket.errorString().contains("401"))report["handshake_status"]=401;finish(1);});
        QObject::connect(&guard,&QTimer::timeout,&app,[&]{report["timeout"]=true;finish(1);});
        QTimer::singleShot(0,&app,[&]{clock.start();guard.start(10000);QNetworkProxy proxy(QNetworkProxy::NoProxy);if(p.systemProxy){const auto error=resolveSystemProxy(proxy,url);if(!error.isEmpty()){report["error"]=error;finish(1);return;}}socket.setProxy(proxy);
            QByteArray key=qgetenv("DASHSCOPE_API_KEY").trimmed();try{if(key.isEmpty())key=CredentialStore(args[2]+"/dashscope-key.dpapi").load();}catch(const std::exception&){report["error"]="Cannot decrypt stored key";finish(1);return;}
            QNetworkRequest request(url);request.setRawHeader("Authorization","Bearer "+key);socket.open(request);key.fill('\0');
        });return app.exec();
    }
    const bool audio=args.size()==6 && (args[1]=="--one-authorized-audio-tts" || args[1]=="--one-authorized-audio-tts-ws");
    if(!audio && (args.size()!=5 || args[1]!="--one-authorized-request"))return 2;
    auto p=VoicePreferences::load(args[2]+"/voice.ini");if(audio){p.apiFamily="qwen-audio-tts";p.audioWorkspace=args[3];}
    if(audio && args[1]=="--one-authorized-audio-tts-ws")p.audioTransport="websocket-public";
    const auto reportFile=args[audio?4:3],wavFile=args[audio?5:4];
    QwenSpeech speech;SpeechPlayer player;QElapsedTimer clock;QTimer guard;guard.setSingleShot(true);
    QJsonObject report{{"model",p.audioTts()?p.audioModel:"qwen3-tts-flash"},{"voice",p.audioTts()?p.audioVoice:p.voice},{"region",p.region},{"api_family",p.apiFamily},{"system_proxy",p.systemProxy},{"attempts",0}};
    if(p.audioTts())report["endpoint"]=(p.audioTransport=="http"?p.audioEndpoint():p.audioWebSocketEndpoint()).toString();
    auto finish=[&](int code){guard.stop();report["elapsed_ms"]=clock.elapsed();QSaveFile file(reportFile);if(!file.open(QIODevice::WriteOnly))return app.exit(4);file.write(QJsonDocument(report).toJson());file.commit();app.exit(code);};
    QObject::connect(&guard,&QTimer::timeout,&app,[&]{speech.cancel();player.stop();report["error"]="Diagnostic timeout; no retry";finish(5);});
    QObject::connect(&player,&SpeechPlayer::finished,&app,[&]{report["playback_completed"]=true;finish(0);});
    QObject::connect(&player,&SpeechPlayer::failed,&app,[&](const QString &reason){report["playback_completed"]=false;report["playback_error"]=reason;finish(5);});
    QObject::connect(&speech,&QwenSpeech::failed,&app,[&](const QString &reason){report["success"]=false;report["error"]=reason;finish(1);});
    QObject::connect(&speech,&QwenSpeech::ready,&app,[&](const QByteArray &pcm){
        report["success"]=true;report["pcm_bytes"]=pcm.size();report["seconds"]=pcm.size()/48000.0;report["rms"]=pcmEnergy(pcm);
        QSaveFile f(wavFile);if(!f.open(QIODevice::WriteOnly)){report["wav_written"]=false;finish(4);return;}
        QDataStream w(&f);w.setByteOrder(QDataStream::LittleEndian);w.writeRawData("RIFF",4);w<<quint32(36+pcm.size());w.writeRawData("WAVEfmt ",8);w<<quint32(16)<<quint16(1)<<quint16(1)<<quint32(24000)<<quint32(48000)<<quint16(2)<<quint16(16);w.writeRawData("data",4);w<<quint32(pcm.size());w.writeRawData(pcm.constData(),pcm.size());report["wav_written"]=f.commit();
        if(!audio){finish(0);return;}const auto error=player.play(pcm);if(!error.isEmpty()){report["playback_completed"]=false;report["playback_error"]=error;finish(5);}
    });
    QTimer::singleShot(0,&app,[&]{clock.start();guard.start(90000);QByteArray key=qgetenv("DASHSCOPE_API_KEY").trimmed();
        try{if(key.isEmpty())key=CredentialStore(args[2]+"/dashscope-key.dpapi").load();}
        catch(const std::exception&){report["error"]="Stored credential cannot be decrypted";finish(3);return;}
        report["attempts"]=1;const auto error=speech.start("你好，我是霜翎。",p,key);key.fill('\0');if(!error.isEmpty()){report["success"]=false;report["error"]=error;finish(3);}
    });return app.exec();
}
