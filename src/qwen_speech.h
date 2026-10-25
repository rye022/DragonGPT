#pragma once
#include "chat_settings.h"
#include <QNetworkAccessManager>
#include <QPointer>
#include <QJsonObject>
#include <QTimer>
#include <QAudioSink>
#include <QWebSocket>
#include <QFutureWatcher>
#include <QMediaDevices>
#include "latency_trace.h"
#include "audio_duplex.h"
#include "speech_expression.h"
#include <memory>
#include <functional>

namespace dragon {
struct VoiceChoice {QString id,name;};
QVector<VoiceChoice> voiceChoices();
QVector<VoiceChoice> selectableLegacyVoices();
QVector<VoiceChoice> audioVoiceChoices(const QString &model,const QString &region);
struct VoicePreferences {
    QString voice="Serena";
    QString region="beijing";
    QString apiFamily="qwen-tts";
    QString audioWorkspace;
    QString audioTransport="http";
    QString audioModel="qwen-audio-3.1-tts-flash";
    QString audioVoice="longanhuan_v3.1";
    // Per-utterance expression; deliberately not written into voice.ini.
    SpeechExpression expression;
    bool systemProxy=false;
    bool preferOffline=true;
    bool cacheAudio=true;
    bool readAloud=false;
    bool fastResponse=true;
    static VoicePreferences load(const QString &file);
    void save(const QString &file) const;
    bool audioTts() const{return apiFamily=="qwen-audio-tts";}
    bool legacyVoice(const QString &id) const{return apiFamily=="qwen-tts" && voice==id;}
    QString cacheVoiceId() const;
    QString expressiveCacheId() const;
    QUrl audioEndpoint() const;
    QUrl audioWebSocketEndpoint() const;
};
class QwenSpeech final:public QObject {
    Q_OBJECT
public:
    explicit QwenSpeech(QObject *parent=nullptr,QUrl endpoint=QUrl("https://dashscope.aliyuncs.com/api/v1/services/aigc/multimodal-generation/generation"));
    QString start(const QString &text,const VoicePreferences &preferences,const QByteArray &key);
    void cancel();
    bool busy() const{return !reply_.isNull() || bool(socket_);}
    void setTimeoutMs(int value){timeoutMs_=qBound(50,value,180000);}
    static QUrl endpointForRegion(const QString &region);
    static QString configurationError(const VoicePreferences &preferences);
    static constexpr int maxCharacters=600;
signals:
    void chunkReady(const QByteArray &pcm);
    void timing(const QString &event);
    void ready(const QByteArray &pcm);
    void failed(const QString &reason);
private:
    QNetworkAccessManager manager_;
    QPointer<QNetworkReply> reply_;
    std::unique_ptr<QWebSocket> socket_;
    QString socketTaskId_;
    bool socketTaskStarted_=false;
    QUrl endpoint_,apiUrl_,audioUrl_;
    QTimer timeout_;
    QByteArray events_,pcm_,pendingPcm_,errorBody_;
    quint64 generation_=0;
    bool complete_=false;
    bool downloading_=false;
    bool audioTts_=false;
    int timeoutMs_=60000;
    qsizetype received_=0;
    void read();
    void event(const QByteArray &packet);
    void payload(const QJsonObject &value);
    void attach(quint64 generation);
    void finished(quint64 generation);
    void download();
    void fail(const QString &reason);
    QString startSocket(const QString &text,const VoicePreferences &preferences,const QByteArray &key);
    void socketEvent(const QJsonObject &value,const QString &text);
};
// Playback only: constructing/starting this class never opens an input device.
class SpeechPlayer final:public QObject {
    Q_OBJECT
public:
    explicit SpeechPlayer(QObject *parent=nullptr,std::function<QAudioDevice()> outputResolver={});
    ~SpeechPlayer() override;
    QString play(const QByteArray &pcm24k);
    QString beginStream();
    QString appendPcm(const QByteArray &pcm24k);
    void finishStream();
    void stop();
    bool active() const{return waitingDevice_ || bool(sink_);}
    bool preparingOutput() const{return waitingDevice_;}
    int bufferedMilliseconds() const{return qMax(0,int(wire_.size()/48-(sink_?sink_->processedUSecs()/1000:0)));}
signals:
    void finished();
    void timing(const QString &event);
    void failed(const QString &reason);
    void mouthChanged(double value);
    void started();
private:
    std::unique_ptr<QAudioSink> sink_;
    QIODevice *device_=nullptr;
    std::unique_ptr<PcmResampler> converter_;
    QByteArray wire_,queue_;
    QTimer timer_;
    bool streamOpen_=false,started_=false;
    quint64 generation_=0;
    QFutureWatcher<QAudioDevice> deviceQuery_;
    QAudioDevice outputDevice_;
    std::unique_ptr<QMediaDevices> deviceMonitor_;
    std::function<QAudioDevice()> outputResolver_;
    LatencyTrace deviceTrace_;
    bool deviceReady_=false,waitingDevice_=false,deviceLoading_=false;
    void prepareDevice();
    QString openDevice();
    void flush();
};
}
