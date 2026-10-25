#pragma once
#include <QByteArray>
#include <QJsonObject>
#include <QString>

namespace dragon {
// Parses SSE bytes before decoding JSON, so UTF-8 split between packets is safe.
class ResponseStream final {
public:
    void feed(const QByteArray &bytes);
    void finish();
    QString text() const { return text_; }
    QString error() const { return error_; }
    bool completed() const { return completed_; }
    QJsonObject usage() const { return usage_; }
private:
    QByteArray buffer_,data_;
    QString text_,error_;
    QJsonObject usage_;
    qsizetype received_=0;
    bool completed_=false;
    void dispatch();
};
}
