#pragma once
#include <QDateTime>
#include <QDebug>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QUuid>
namespace dragon {
// Metadata only: never record prompts, memory contents, audio, keys or URLs.
class LatencyTrace {
public:
    void begin(const QString &parent={}){id_=QUuid::createUuid().toString(QUuid::Id128);parent_=parent;once_.clear();clock_.start();}
    QString id() const{return id_;}
    void mark(const QString &event,QJsonObject fields={}) const{
        if(!clock_.isValid())return;
        fields["component"]="latency";fields["trace_id"]=id_;fields["parent_trace_id"]=parent_;
        fields["event"]=event;fields["elapsed_ms"]=clock_.elapsed();fields["at"]=QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
        qInfo().noquote()<<QString::fromUtf8(QJsonDocument(fields).toJson(QJsonDocument::Compact));
    }
    void markOnce(const QString &event){if(!once_.contains(event)){once_.insert(event);mark(event);}}
private:
    QString id_,parent_;QElapsedTimer clock_;QSet<QString> once_;
};
}
