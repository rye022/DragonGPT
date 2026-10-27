#include "audio_duplex.h"
#include "local_speech.h"
#include "memory_store.h"
#include "realtime_client.h"
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QTextStream>
#include <QThread>
#include <QWebSocketServer>
#include <cmath>
#include <functional>
#include <algorithm>
#include <stdexcept>
using namespace dragon;
namespace {
int assertions=0;
void expect(bool condition,const char *message){++assertions;if(!condition)throw std::runtime_error(message);}
bool until(const std::function<bool()> &predicate,int timeout=2500){QElapsedTimer clock;clock.start();while(!predicate() && clock.elapsed()<timeout){QCoreApplication::processEvents(QEventLoop::AllEvents,10);QThread::msleep(1);}return predicate();}
void memories(const QString &root){
    MemoryStore store(root+"/memory.sqlite");
    const auto first=store.remember("学习 C++ 做桌宠");store.updateMemory(first,"学习 C++ 做桌宠","项目，技术",0.2);
    const auto second=store.remember("学习 C++ 做语音");store.updateMemory(second,"学习 C++ 做语音","语音",0.9,QDateTime::currentDateTimeUtc().addDays(7));
    expect(store.relevant("学习 C++").first().id==second,"importance tie-break failed");
    expect(store.relevant("项目").first().id==first,"tag retrieval failed");
    store.updateMemory(first,"改成学习 Qt","项目",0.7);
    expect(store.memories().last().content=="改成学习 Qt","editing failed");
    store.setNickname("测试称呼");const auto exported=store.exportMemories();
    expect(!exported.contains("api-key") && !exported.contains("messages"),"export included private credentials or chat");
    MemoryStore imported(root+"/imported.sqlite");
    expect(imported.importMemories(exported)==2 && imported.nickname()=="测试称呼","memory import failed");
    expect(imported.importMemories(exported)==0 && imported.memories().size()==2,"duplicate import changed rows");
    const auto restored=imported.memories();expect(std::any_of(restored.begin(),restored.end(),[](const Memory &memory){return memory.content.contains("语音") && memory.expiresAt.isValid();}),"expiry lost on export/import");
    const auto expiring=store.remember("稍后应该过期");store.updateMemory(expiring,"稍后应该过期","",0.5,QDateTime::currentDateTimeUtc().addSecs(-1));
    expect(store.memories().size()==2 && store.relevant("应该过期").isEmpty(),"expired memory leaked into context");
    expect(store.pruneExpired()==1,"expired rows not pruned");
    const QJsonArray badRows{QJsonObject{{"content","一条新记忆"}},QJsonObject{{"content",""}}};
    const auto bad=QJsonDocument(QJsonObject{{"format","DragonGPT memories"},{"version",1},{"memories",badRows}}).toJson();
    bool rejected=false;try{imported.importMemories(bad);}catch(...){rejected=true;}
    expect(rejected && imported.memories().size()==2,"invalid import not atomic");
    rejected=false;try{imported.importMemories(QByteArray(2*1024*1024+1,'x'));}catch(...){rejected=true;}expect(rejected,"oversized import accepted");
    store.recordApiUsage("responses",QJsonObject{{"input_tokens",10},{"output_tokens",20}});
    store.recordApiUsage("responses",QJsonObject{});
    expect(store.usageSummary().value("requests").toInt()==1 && store.usageSummary().value("output_tokens").toInteger()==20,"usage accounting invented missing usage");
    imported.clearAll();expect(imported.memories().isEmpty() && imported.usageSummary().value("requests").toInt()==0,"clearAll failed");
}
void pcm(){
    QAudioFormat input;input.setSampleRate(48000);input.setChannelCount(2);input.setSampleFormat(QAudioFormat::Float);
    QByteArray source;
    for(int i=0;i<480;++i){const float value=float(0.3*std::sin(i*0.13));source.append(reinterpret_cast<const char*>(&value),4);source.append(reinterpret_cast<const char*>(&value),4);}
    PcmResampler whole(input,voicePcmFormat()),split(input,voicePcmFormat());const auto expected=whole.feed(source);QByteArray actual;
    for(char byte:source)actual+=split.feed(QByteArray(1,byte));
    expect(expected==actual,"resampling depended on packet boundaries");
    expect(actual.size()>=476 && actual.size()<=480 && actual.size()%2==0,"resampler output rate or sample alignment incorrect");
    expect(pcmEnergy(actual)>0.1,"valid audio became silent");
    expect(pcmEnergy(QByteArray(100,0))==0,"silence drives mouth");
    PcmResampler reverse(voicePcmFormat(),input);expect(reverse.feed(actual).size()>3000,"output device conversion failed");
}
void realtime(){
    QWebSocketServer server("voice-fixture",QWebSocketServer::NonSecureMode);
    expect(server.listen(QHostAddress::LocalHost,0),"WebSocket fixture failed");
    QWebSocket *peer=nullptr;QVector<QJsonObject> received;
    QObject::connect(&server,&QWebSocketServer::newConnection,&server,[&]{
        peer=server.nextPendingConnection();QObject::connect(peer,&QWebSocket::disconnected,peer,&QWebSocket::deleteLater);
        QObject::connect(peer,&QWebSocket::textMessageReceived,&server,[&](const QString &text){const auto event=QJsonDocument::fromJson(text.toUtf8()).object();received.append(event);
            if(event.value("type")=="session.update")peer->sendTextMessage("{\"type\":\"session.updated\"}");
        });
    });
    RealtimeClient client(nullptr,QUrl(QString("ws://127.0.0.1:%1/v1/realtime").arg(server.serverPort())));
    auto settings=ChatSettings{};settings.onlineEnabled=true;settings.proxyMode=ProxyMode::Tun;
    int ready=0,audio=0,completed=0,failures=0;QString output;
    QObject::connect(&client,&RealtimeClient::sessionReady,[&]{++ready;});
    QObject::connect(&client,&RealtimeClient::audio,[&](const QByteArray &bytes){audio+=bytes.size();});
    QObject::connect(&client,&RealtimeClient::completed,[&](const QString &,const QString &text,const QJsonObject &){++completed;output=text;});
    QObject::connect(&client,&RealtimeClient::failed,[&](const QString &){++failures;});
    expect(client.start(settings,"fixture-key","fixture-realtime","persona").isEmpty(),"Realtime start failed");
    expect(until([&]{return ready==1;}),"Realtime session initialization failed");
    expect(received.first().value("session").toObject().value("audio").toObject().value("input").toObject().value("format").toObject().value("rate").toInt()==24000,"Realtime audio contract incorrect");
    client.appendAudio(QByteArray(480,0));expect(until([&]{return received.size()>=2;}),"microphone PCM not sent");
    expect(received.last().value("type")=="input_audio_buffer.append","incorrect input event");
    auto send=[&](QJsonObject event){peer->sendTextMessage(QString::fromUtf8(QJsonDocument(event).toJson(QJsonDocument::Compact)));};
    send({{"type","response.created"},{"response",QJsonObject{{"id","response-1"}}}});
    send({{"type","response.output_audio.delta"},{"response_id","response-1"},{"item_id","item-1"},{"delta",QString::fromLatin1(QByteArray(480,0).toBase64())}});
    send({{"type","response.output_audio_transcript.delta"},{"response_id","response-1"},{"delta","你好，我在。"}});
    expect(until([&]{return audio==480;}),"audio output missing");
    client.interrupt(100);expect(until([&]{return received.size()>=4;}),"interrupt did not send cancellation and truncation");
    const auto before=audio;
    send({{"type","response.output_audio.delta"},{"response_id","response-1"},{"delta",QString::fromLatin1(QByteArray(480,0).toBase64())}});
    send({{"type","response.done"},{"response",QJsonObject{{"id","response-1"},{"status","completed"}}}});
    until([]{return false;},60);expect(audio==before && completed==0,"cancelled voice callbacks leaked");
    send({{"type","response.created"},{"response",QJsonObject{{"id","response-2"}}}});
    send({{"type","response.output_audio_transcript.delta"},{"response_id","response-2"},{"delta","新回复"}});
    send({{"type","response.done"},{"response",QJsonObject{{"id","response-2"},{"status","completed"},{"usage",QJsonObject{{"input_tokens",2},{"output_tokens",3}}}}}});
    expect(until([&]{return completed==1;}) && output=="新回复","new voice turn failed after interruption");
    client.stop();expect(!client.ready() && failures==0,"stopping call emitted spurious failure");
}
void synthesis(const QString &root){
    LocalSpeech speech;const auto path=root+"/voice.wav";
    expect(speech.renderWav("你好，我是霜翎。",path).isEmpty(),"local Chinese TTS could not render WAV");
    QFile file(path);expect(file.open(QIODevice::ReadOnly),"TTS output missing");const auto data=file.readAll();
    if(!data.startsWith("RIFF") || data.mid(8,4)!="WAVE" || data.size()<=10000)
        QTextStream(stderr)<<"TTS diagnostic: "<<data.size()<<" bytes, prefix "<<data.left(32).toHex()<<"\n";
    expect(data.startsWith("RIFF") && data.mid(8,4)=="WAVE" && data.size()>10000,"TTS output not a valid nonempty WAV");
    const auto index=data.indexOf("data");expect(index>0 && pcmEnergy(data.mid(index+8))>0.05,"TTS produced silence");
    expect(!speech.listening(),"TTS synthesis unexpectedly opened microphone");
    QString transcript;
    const auto error=speech.recognizeWav(path,transcript);
    if(!error.isEmpty())QTextStream(stderr)<<"ASR diagnostic: "<<error<<"\n";
    expect(error.isEmpty() && !transcript.trimmed().isEmpty(),"local Chinese file recognition failed");
}
}
int main(int argc,char **argv){
    QCoreApplication app(argc,argv);QTemporaryDir dir(QDir::currentPath()+"/product-tests-XXXXXX");
    try{expect(dir.isValid(),"fixture directory failed");memories(dir.path());pcm();realtime();synthesis(dir.path());
        QTextStream(stdout)<<"PASS: "<<assertions<<" assertions (memory lifecycle, PCM conversion, realtime protocol, actual Chinese TTS). No microphone capture or live API calls.\n";return 0;
    }catch(const std::exception &error){QTextStream(stderr)<<"FAIL: "<<error.what()<<"\n";return 1;}
}
