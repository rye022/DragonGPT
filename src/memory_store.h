#pragma once
#include "types.h"
#include "dialogue_engine.h"
#include "companion_life.h"
#include <QSqlDatabase>
#include <QJsonObject>
#include <QJsonArray>
#include <QHash>
#include <optional>

namespace dragon {
class MemoryStore final {
public:
    explicit MemoryStore(const QString &filePath);
    ~MemoryStore();
    MemoryStore(const MemoryStore &) = delete;
    MemoryStore &operator=(const MemoryStore &) = delete;
    QString nickname() const;
    void setNickname(const QString &name);
    qint64 remember(const QString &content);
    QVector<Memory> memories() const;
    QVector<Memory> interactionMemories() const;
    QVector<Memory> proactiveMemories(QDateTime now=QDateTime::currentDateTimeUtc()) const;
    DialogueSelection memoryFollowup(QDateTime now);
    QVector<Memory> relevant(const QString &query, int limit = 3) const;
    void forget(qint64 id);
    void updateMemory(qint64 id,const QString &content,const QString &tags,double importance,QDateTime expiresAt = {});
    QByteArray exportMemories() const;
    int importMemories(const QByteArray &json);
    int pruneExpired(QDateTime now=QDateTime::currentDateTimeUtc());
    void recordApiUsage(const QString &source,const QJsonObject &usage);
    QJsonObject usageSummary() const;
    void appendExchange(const QString &user, const QString &assistant, const QString &source = "offline",QDateTime now=QDateTime::currentDateTimeUtc());
    QVector<Message> recentMessages(int limit = 20) const;
    int exchanges() const;
    QString relationship() const;
    LifeState life() const;
    void appendProactive(const QString &text,QDateTime now=QDateTime::currentDateTimeUtc());
    void appendNotice(const QString &text);
    QVector<Memory> suggestions() const;
    void suggest(const QString &content);
    void resolveSuggestion(qint64 id,bool accept,bool replaceConflict=false);
    QVector<Memory> conflicts(const QString &content) const;
    DialogueSelection selectDialogue(DialogueContext context,quint32 seed=0);
    QJsonObject dialogueHistory() const;
    int syncCollection(QDateTime now=QDateTime::currentDateTimeUtc(),bool inferred=false);
    QJsonArray collection() const;
    void recordPresented(const QString &id,QDateTime now=QDateTime::currentDateTimeUtc());
    void recordHeard(const QString &id,QDateTime now=QDateTime::currentDateTimeUtc());
    bool awardInteraction(const QString &kind,const QString &text,QDateTime now=QDateTime::currentDateTimeUtc(),const QString &eventId={});
    int unlockedEvents() const;
    void clearAll();
    void clearMemories();
    QString databasePath() const{return db_.databaseName();}
    void backupTo(const QString &path) const;
    static void restoreBackup(const QString &backup,const QString &destination);
    QString integrity() const;
    void setMemoryMetadata(qint64 id,const QString &layer,const QString &source,bool proactiveAllowed=true);
    QVector<Memory> byIds(const QVector<qint64> &ids,const QString &layer={}) const;
    QJsonObject wellnessState() const;
    void saveWellness(const QJsonObject &state, const QJsonObject &session = {});
    QJsonArray focusSessions() const;
    QJsonObject focusStatistics(QDate day) const;
    void collectCompanionLine(const QString &id,const QString &text);
private:
    mutable QHash<QString,QJsonObject> focusStatsCache_;
    QString connectionName_;
    QSqlDatabase db_;
    void exec(const QString &sql) const;
    void saveLife(const LifeState &state);
};
}
