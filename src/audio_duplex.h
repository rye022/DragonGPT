#pragma once
#include <QAudioFormat>
#include <QAudioSink>
#include <QAudioSource>
#include <QObject>
#include <QTimer>
#include <memory>

namespace dragon {
class PcmResampler final {
public:
    PcmResampler(QAudioFormat input,QAudioFormat output):input_(input),output_(output){}
    QByteArray feed(const QByteArray &data);
private:
    QAudioFormat input_,output_;
    QByteArray carry_;
    QVector<double> samples_;
    double phase_=0;
};
QAudioFormat voicePcmFormat();
double pcmEnergy(const QByteArray &pcm16);
QString decodePcm24kWav(const QByteArray &wav,QByteArray &pcm);
class AudioDuplex final:public QObject {
    Q_OBJECT
public:
    explicit AudioDuplex(QObject *parent=nullptr);
    QString start();
    void stop();
    void mute(bool muted);
    void play(const QByteArray &pcm);
    void clearPlayback();
    int playedMilliseconds() const;
    bool active() const{return active_;}
signals:
    void captured(const QByteArray &pcm);
    void mouthChanged(double value);
    void failed(const QString &message);
private:
    std::unique_ptr<QAudioSource> source_;
    std::unique_ptr<QAudioSink> sink_;
    std::unique_ptr<PcmResampler> input_,output_;
    QIODevice *capture_=nullptr,*playback_=nullptr;
    QByteArray queue_;
    QTimer pump_;
    bool active_=false;
    qint64 turnStartUs_=0;
    void flush();
};
}
