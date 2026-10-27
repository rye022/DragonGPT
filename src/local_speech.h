#pragma once
#include <QObject>
#include <QTimer>
#include <memory>

namespace dragon {
class LocalSpeech final : public QObject {
    Q_OBJECT
public:
    explicit LocalSpeech(QObject *parent=nullptr);
    ~LocalSpeech() override;
    QString speak(const QString &text);
    void synthesizePcm(const QString &text);
    void stopSpeaking();
    QString listen();
    void configureRecognition(int silenceMs,const QStringList &hotwords);
    void stopListening();
    void finishListening(){if(implListening())recognitionEnd_.start(qMax(300,silenceMs_));}
    bool speaking() const;
    bool listening() const;
    QString capabilities() const;
    static QString renderWav(const QString &text,const QString &path);
    QString recognizeWav(const QString &path,QString &transcript,int timeoutMs=5000);
signals:
    void recognized(const QString &text);
    void hypothesis(const QString &text);
    void recognitionDetail(const QString &text,bool uncertain);
    void speechStarted();
    void speechFinished();
    void mouthChanged(double value);
    void failed(const QString &message);
    void pcmReady(const QByteArray &pcm);
    void synthesisFailed(const QString &message);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    QTimer timer_;
    QTimer recognitionEnd_,recognitionDeadline_;
    QString recognitionText_;
    QStringList hotwords_;
    int silenceMs_=1100;
    bool recognitionUncertain_=false;
    quint64 generation_=0;
    bool rendering_=false;
    void poll();
    bool implListening() const{return listening();}
};
}
