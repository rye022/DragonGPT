#pragma once
#include "chat_settings.h"
#include "response_stream.h"
#include "types.h"
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPointer>
#include <QTimer>

namespace dragon {
class ChatService final : public QObject {
    Q_OBJECT
public:
    explicit ChatService(QObject *parent=nullptr,QUrl endpoint=QUrl("https://api.deepseek.com/responses"));
    ~ChatService() override;
    QString start(const ChatSettings &settings,const QByteArray &key,const QString &persona,
                  const QString &context,const QVector<Message> &history,const QString &message,
                  const QByteArray &png={},bool complex=false);
    void cancel();
    bool busy() const { return !reply_.isNull(); }
    void setTimeoutMs(int timeout) {timeoutMs_=qBound(50,timeout,120000);}
    QUrl endpoint() const {return endpoint_;}
signals:
    void timing(const QString &event);
    void partial(const QString &text);
    void succeeded(const QString &text,const QJsonObject &usage);
    void failed(const QString &reason);
    void limitsReceived(const QJsonObject &limits);
private:
    QUrl endpoint_;
    QNetworkAccessManager network_;
    QPointer<QNetworkReply> reply_;
    QTimer deadline_;
    ResponseStream stream_;
    QByteArray errorBody_;
    quint64 epoch_=0;
    int timeoutMs_=45000;
    bool firstResponse_=true;
    void consume();
    void fail(const QString &reason);
    void release();
};
}
