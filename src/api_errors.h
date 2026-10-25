#pragma once
#include <QByteArray>
#include <QJsonObject>
#include <QString>
namespace dragon {
// Only whitelisted code/request-id tokens are surfaced, never provider free text.
QString apiError(const QString &service,int status,const QJsonObject &body={},const QByteArray &requestId={});
QString apiError(const QString &service,int status,const QByteArray &body,const QByteArray &requestId={});
}
