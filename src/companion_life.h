#pragma once
#include "types.h"
#include <QJsonObject>
#include <QStringList>

namespace dragon {
struct LifeState {
    int experience=0, activeDays=0, dailyExperience=0;
    int legacyStage=0;
    QDate growthDate, proactiveDate;
    QDateTime lastInteraction, lastProactive, moodUntil, moodSince;
    QStringList dailyMessages;
    Emotion mood=Emotion::Calm;
    int proactiveCount=0, topic=0;
    bool awaitingReply=false;
    int ignoredOpenings=0;
    QDateTime lastClickGrowth,lastChatGrowth,lastFocusGrowth;
    QStringList awardedEvents;
    QJsonObject json() const;
    static LifeState fromJson(const QJsonObject &json);
    void interact(const QString &text,QDateTime now);
    bool award(const QString &kind,const QString &text,QDateTime now,const QString &eventId={});
    QString stage() const;
    QString growthHint() const;
    int growthPercent() const;
    int stageIndex() const;
};
struct LifeSettings {
    bool enabled=true, memorySuggestions=true, focusMode=false;
    bool memoryFollowups=false;
    QString frequency="normal";
    bool focusProtection=true, proactiveSpeech=true, hideUnheard=false;
    int idleMinutes=15, cooldownMinutes=90, dailyLimit=3;
    int quietStart=22, quietEnd=8;
    QDateTime pausedUntil;
    static LifeSettings load(const QString &path);
    void save(const QString &path) const;
    bool quiet(QDateTime now) const;
    QPair<int,int> intervalSeconds() const;
};
class CompanionLife {
public:
    static bool eligible(const LifeState &state,const LifeSettings &settings,QDateTime now,
                         QDateTime lastActivity,bool blocked,QDateTime snoozedUntil={});
    static QString candidate(const QString &text);
    static QString tags(const QString &text);
};
}
