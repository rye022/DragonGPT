#pragma once
#include "chat_settings.h"
#include <QJsonObject>
#include <QSet>
#include <QTimer>
#include <QWebSocket>
#include <memory>

namespace dragon {
class RealtimeClient final:public QObject {
    Q_OBJECT
public:
    explicit RealtimeClient(QObject *parent=nullptr,QUrl endpoint=QUrl("wss://api.openai.com/v1/realtime"));
    QString start(const ChatSettings &settings,const QByteArray &key,const QString &model,const QString &instructions);
    void stop();
    void appendAudio(const QByteArray &pcm);
    void interrupt(int playedMs);
    void clearInput();
    bool ready() const{return ready_;}
signals:
    void sessionReady();
    void audio(const QByteArray &pcm);
    void interrupted();
    void userSpeechStarted();
    void userText(const QString &text);
    void assistantText(const QString &text);
    void completed(const QString &input,const QString &output,const QJsonObject &usage);
    void failed(const QString &reason);
    void rateLimits(const QJsonArray &limits);
private:
    QUrl endpoint_;
    std::unique_ptr<QWebSocket> socket_;
    QTimer timeout_;
    bool ready_=false;
    QString responseId_,itemId_,inputText_,outputText_;
    QSet<QString> cancelled_;
    quint64 generation_=0;
    void send(const QJsonObject &event);
    void event(const QJsonObject &event);
    void fail(const QString &reason);
};
}
