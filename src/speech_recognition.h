#pragma once
#include "audio_duplex.h"
#include "local_speech.h"
#include <QJsonObject>
#include <QProcess>
#include <QElapsedTimer>
#include <QAudioDevice>
namespace dragon {
struct AsrSettings {
    QString engine="sapi",language="zh",microphone,python,modelPath,device="cpu",endpoint,cloudModel;
    QStringList hotwords;
    int silenceMs=1100,sensitivity=2;
    bool autoSend=false;
    static AsrSettings load(const QString &path);void save(const QString &path) const;
};
class ASRContextCorrector {
public:
    static QJsonObject correct(const QString &raw,const QStringList &hotwords,bool uncertain=false);
};
class SpeechRecognitionService final:public QObject {
    Q_OBJECT
public:
    SpeechRecognitionService(LocalSpeech &legacy,const QString &directory,QObject *parent=nullptr);
    ~SpeechRecognitionService() override;
    QString start();void stop();void finishInput();bool listening() const{return listening_;}
    const AsrSettings &settings() const{return settings_;}
    void configure(const AsrSettings &settings,const QByteArray &key={});
    void setProjectName(const QString &name){projectWords_=name.isEmpty()?QStringList{}:QStringList{name.left(80)};}
    void setOutputActive(bool active);
signals:
    void partial(const QString &text);
    void finalResult(const QJsonObject &result);
    void status(const QString &text);
    void level(double value);
    void failed(const QString &text);
private:
    LocalSpeech &legacy_;QString directory_;AsrSettings settings_;
    QStringList projectWords_;
    QProcess worker_;QByteArray output_;
    std::unique_ptr<QAudioSource> source_;std::unique_ptr<PcmResampler> resampler_;
    QTimer deadline_;QElapsedTimer outputTail_,captureClock_;
    bool listening_=false,outputActive_=false,ready_=false;
    quint64 epoch_=0;
    void capture();void consume();void publish(const QString &text,bool uncertain,const QStringList &reliability={},int latencyMs=-1);
};
}
