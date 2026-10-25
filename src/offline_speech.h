#pragma once
#include <QObject>
#include <QProcess>
#include <QTimer>
#include <QJsonObject>

namespace dragon {
class VoiceCache final {
public:
    explicit VoiceCache(QString directory):directory_(std::move(directory)){}
    QByteArray read(const QString &voice,const QString &text) const;
    void write(const QString &voice,const QString &text,const QByteArray &pcm);
    void clear();
private:
    QString directory_;
    QString path(const QString &voice,const QString &text) const;
};
class OfflineSpeech final:public QObject {
    Q_OBJECT
public:
    explicit OfflineSpeech(QObject *parent=nullptr,QString applicationDirectory={});
    ~OfflineSpeech() override;
    bool available() const;
    bool busy() const{return pending_;}
    QString start(const QString &text);
    void cancel();
signals:
    void ready(const QByteArray &pcm);
    void failed(const QString &reason);
    void progress(const QString &message);
private:
    QString directory_,pendingText_;
    QProcess process_;
    QTimer timeout_,idle_;
    QByteArray buffer_;
    quint64 generation_=0;
    bool loaded_=false,pending_=false;
    QString interpreter() const;
    void receive();
    void send();
    void fail(const QString &message);
};
}
