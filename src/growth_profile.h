#pragma once
#include <QDateTime>
#include <QJsonArray>
#include <QJsonObject>
#include <QStringList>

namespace dragon {
// Part of LifeState, not a second relationship or emotion store.
struct GrowthProfile {
    bool enabled=true, personalityEnabled=true, habitsEnabled=true;
    bool sharedEnabled=true, idleEnabled=true;
    int trust=0, dailyClicks=0, dailyChats=0, dailyTasks=0, dailyFocus=0;
    QJsonArray thresholds{20,50,80}; // Keep existing saves/catalog progression.
    QJsonObject baseline, drift, locked;
    QJsonArray habits, events, shared, proactiveTimes;
    QString lastReason;
    GrowthProfile();
    static QStringList keys();
    static QStringList labels();
    QJsonObject json() const;
    static GrowthProfile fromJson(const QJsonObject &json);
    static bool validThresholds(const QJsonArray &values);
    QJsonObject traits() const;
    void observe(const QString &kind,const QString &text,int earned,QDateTime now);
    void event(const QString &kind,const QString &reason,QDateTime now,int earned=0);
    void recordProactive(const QString &kind,const QString &reason,QDateTime now);
    bool addShared(const QString &title,const QString &source,QDateTime now);
    QString sharedContext(const QString &query) const;
    bool preferredHour(QDateTime now) const;
};
}
