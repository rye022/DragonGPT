#pragma once
#include "types.h"
#include "pet_actions.h"
#include <QJsonObject>
#include <QJsonArray>
namespace dragon {
struct DialogueContext {
    int percent=0;Emotion emotion=Emotion::Calm;PetAction action=PetAction::Idle;
    QDateTime now=QDateTime::currentDateTimeUtc();bool proactive=false,rapid=false;
    int sessionMinutes=0;QVector<Memory> memories;
    QJsonObject traits;
    QString behaviorReason;
    int stage() const{return percent>=80?3:percent>=50?2:percent>=20?1:0;}
};
struct DialogueSelection {QString id,text,topic;bool hidden=false;int minPercent=0;qint64 memoryId=0;explicit operator bool() const{return !text.isEmpty();}};
class DialogueEngine {
public:
    static const QJsonArray &catalog();
    static DialogueSelection select(const DialogueContext &context,QJsonObject &history,quint32 seed=0);
    static bool eligible(const QJsonObject &line,const DialogueContext &context,int clickCount);
    static QString relationStyle(int percent);
    static int requiredAffinity(const QJsonObject &line);
    static QString category(const QJsonObject &line);
    static QStringList categories();
    static QJsonObject find(const QString &id);
};
}
