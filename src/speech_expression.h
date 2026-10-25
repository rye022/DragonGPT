#pragma once
#include "types.h"
#include <QJsonObject>

namespace dragon {
struct SpeechExpression {
    double rate=1.0,pitch=1.0;
    QString instruction,style="自然";
    Emotion emotion=Emotion::Calm;
    QString cacheKey() const;
    QJsonObject parameters() const;
    static SpeechExpression forVoice(const QString &voice,Emotion emotion,int affinity=-1);
};
}
