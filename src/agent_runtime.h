#pragma once
#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
namespace dragon {
inline QJsonObject preparedAgentRuntime(const QString &kind){
    for(const auto &base:{QCoreApplication::applicationDirPath(),QString(DRAGON_SOURCE_DIR)}){
        QFile f(base+"/"+kind+"/ready.json");if(f.open(QIODevice::ReadOnly) && f.size()<4096){const auto r=QJsonDocument::fromJson(f.readAll()).object();if(QFileInfo::exists(r["python"].toString()))return r;}
    }return {{"python",QStandardPaths::findExecutable("python")}};
}
}
